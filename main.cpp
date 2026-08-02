#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#include "backend.h"
#include "matcher.h"
#include "session.h"

namespace {

constexpr auto kDefaultTimeout = std::chrono::milliseconds(5000);  // Section 2A

std::atomic<bool> g_interrupted{false};
void handle_sigint(int) { g_interrupted.store(true); }

// Reports exactly which env var(s) detect_backend() checked and whether
// each was set, instead of a flat "no WM detected" -- the difference
// between "none of these are set at all" (wrong shell/session) and "one
// is set but something's still wrong" is itself diagnostic, and this is
// the cheapest place to say so: it's already the failure path.
void log_wm_detection_failure() {
    auto describe = [](const char* var) {
        const char* v = std::getenv(var);
        std::cerr << "  " << var << ": " << (v && v[0] != '\0' ? "set" : "not set") << "\n";
    };
    std::cerr << "error: no supported window manager detected (checked sway, hyprland, i3)\n";
    describe("HYPRLAND_INSTANCE_SIGNATURE");
    describe("SWAYSOCK");
    describe("I3SOCK");
    std::cerr << "is this running inside your Hyprland/sway/i3 session? (not, say, an SSH shell "
                 "or a terminal with a stale/inherited environment)\n";
}

// Same exception, better-targeted advice per failure mode -- the raw
// what() message alone doesn't tell you what to actually go check.
// `context` replaces the default "error: " prefix for call sites that
// need to say what they were doing when it failed (e.g. which workspace).
void log_ipc_failure(const std::exception& e, const std::string& context = "error: ") {
    std::cerr << context << e.what() << "\n";
    if (dynamic_cast<const IpcConnectionError*>(&e)) {
        std::cerr << "couldn't reach the WM's IPC socket -- either the socket file doesn't exist, or "
                      "nothing is listening on it. Is it still running, or did it restart since this shell "
                      "started?\n";
    } else if (dynamic_cast<const IpcTimeoutError*>(&e)) {
        std::cerr << "connected, but got no response in time -- is the WM hung or overloaded?\n";
    } else if (dynamic_cast<const IpcMalformedResponseError*>(&e)) {
        std::cerr << "connected and got a response, but couldn't make sense of it -- wrong WM "
                      "version, or something else is listening on that socket?\n";
    }
}

void print_usage() {
    std::cerr << "usage: tileroot dump [--workspace NAME] [-o FILE] [--pretty] [--verbose]\n"
                 "       tileroot restore [FILE] [--dry-run] [--verbose]\n"
                 "       tileroot doctor [FILE]\n"
                 "       tileroot --version | --help\n";
}

enum class SessionLoadStatus { Ok, NotFound, ParseError, SchemaError };

// Shared by run_restore and run_doctor so "read + parse + validate a session
// file" exists exactly once — doctor needs the same three failure modes
// restore already distinguishes (missing file / bad JSON / schema mismatch),
// just reported instead of acted on.
SessionLoadStatus load_session_file(const std::string& path, SessionFile& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return SessionLoadStatus::NotFound;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        err = e.what();
        return SessionLoadStatus::ParseError;
    }
    try {
        out = session_from_json(j);
    } catch (const SchemaValidationError& e) {
        err = e.what();
        return SessionLoadStatus::SchemaError;
    }
    return SessionLoadStatus::Ok;
}

// Section 1C/2C: write to a temp file then rename() so FILE is never left
// truncated/corrupt by an interrupted write, and both the write and the
// rename are checked rather than assumed to succeed.
bool atomic_write(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "error: could not open " << tmp << " for writing\n";
            return false;
        }
        out << content;
        out.flush();
        if (!out) {
            std::cerr << "error: write failed to " << tmp << "\n";
            return false;
        }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::cerr << "error: could not rename " << tmp << " to " << path << ": " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

int run_dump(const std::string& workspace_filter, const std::string& out_file, bool pretty, bool verbose) {
    auto backend = detect_backend(kDefaultTimeout);
    if (!backend) {
        log_wm_detection_failure();
        return 1;
    }
    if (verbose) std::cerr << "verbose: detected wm=" << backend->wm_name() << "\n";

    std::vector<WorkspaceSession> workspaces;
    try {
        workspaces = backend->dump(workspace_filter);
    } catch (const std::exception& e) {
        log_ipc_failure(e);
        return 1;
    }

    if (pretty) {
        for (const auto& ws : workspaces) std::cout << render_pretty(ws);
    }

    if (!out_file.empty() || !pretty) {
        SessionFile session{backend->wm_name(), workspaces};
        std::string json_str = to_json(session).dump(2);
        if (!out_file.empty()) {
            if (verbose) std::cerr << "verbose: writing " << json_str.size() << " bytes to " << out_file << "\n";
            if (!atomic_write(out_file, json_str)) return 1;
        } else if (!pretty) {
            std::cout << json_str << "\n";
        }
    }
    return 0;
}

// Recursively collects every window leaf from `node`, remembering the
// workspace it belongs to via `slot_workspace` (parallel to the returned
// vector by index) — WindowMatcher::PlaceFn needs this to move a
// relaunched window to the right workspace before positioning it.
// `needs_placement` records whether place_window() should still run for
// this slot: false when prepare_tree_layout() already put the WM's own
// native placeholder-matching in charge of this window's position, true
// when it needs the geometry-based fallback (Hyprland, or tree-layout
// failing for some reason). Every slot collected here is tiled (from
// ws.layout, not ws.floating), so it always needs floating explicitly OFF.
void collect_slots(const LayoutNode& node, const std::string& ws_name, bool needs_placement,
                    std::vector<WindowInfo>& slots, std::vector<std::string>& slot_workspace,
                    std::vector<bool>& slot_needs_placement, std::vector<bool>& slot_is_floating) {
    if (node.type == LayoutNode::Type::Window) {
        slots.push_back(*node.window);
        slot_workspace.push_back(ws_name);
        slot_needs_placement.push_back(needs_placement);
        slot_is_floating.push_back(false);
        return;
    }
    for (const auto& child : node.children) {
        collect_slots(child, ws_name, needs_placement, slots, slot_workspace, slot_needs_placement,
                      slot_is_floating);
    }
}

int run_restore(const std::string& file_arg, bool dry_run, bool verbose) {
    std::string path = file_arg.empty() ? "session.json" : file_arg;
    SessionFile session;
    std::string load_err;
    switch (load_session_file(path, session, load_err)) {
        case SessionLoadStatus::NotFound:
            std::cerr << "error: session file not found: " << path << "\n";
            return 1;
        case SessionLoadStatus::ParseError:
            std::cerr << "error: malformed session file: " << load_err << "\n";
            return 1;
        case SessionLoadStatus::SchemaError:
            std::cerr << "error: " << load_err << "\n";
            return 1;
        case SessionLoadStatus::Ok:
            break;
    }

    auto current = detect_backend(kDefaultTimeout);
    if (!current) {
        log_wm_detection_failure();
        return 1;
    }
    if (current->wm_name() != session.wm) {
        std::cerr << "error: session was saved for " << session.wm << ", but " << current->wm_name()
                   << " is running\n";
        return 1;
    }
    if (verbose) std::cerr << "verbose: wm matches (" << session.wm << "), " << session.workspaces.size()
                            << " workspace(s) in session\n";

    // Refuse up front if ANY target workspace already has windows — no
    // partial application across workspaces, matches the design doc's
    // "refuses if target workspace already has windows."
    for (const auto& ws : session.workspaces) {
        std::vector<WorkspaceSession> live;
        try {
            live = current->dump(ws.name);
        } catch (const std::exception& e) {
            log_ipc_failure(e, "error: could not check target workspace " + ws.name + ": ");
            return 1;
        }
        bool occupied = !live.empty() && (live[0].layout.has_value() || !live[0].floating.empty());
        if (occupied) {
            std::cerr << "error: target workspace already has windows: " << ws.name << "\n";
            return 1;
        }
    }

    if (dry_run) {
        for (const auto& ws : session.workspaces) {
            std::cout << "would restore workspace " << ws.name << ":\n" << render_pretty(ws);
        }
        return 0;
    }

    std::vector<WindowInfo> slots;
    std::vector<std::string> slot_workspace;
    std::vector<bool> slot_needs_placement;
    std::vector<bool> slot_is_floating;
    for (const auto& ws : session.workspaces) {
        if (ws.layout) {
            // Try the WM's native tree-reconstruction mechanism first
            // (i3/sway's append_layout) — if it works, place_window() is
            // skipped for this workspace's tiled slots below, since the WM
            // itself will place relaunched windows correctly via swallow
            // matching. Falls back to per-window geometry placement
            // (place_window) if unsupported (Hyprland) or the attempt fails.
            bool tree_ready = current->prepare_tree_layout(*ws.layout, ws.name);
            if (verbose) {
                std::cerr << "verbose: tree layout for workspace " << ws.name << ": "
                          << (tree_ready ? "prepared" : "unavailable, using geometry placement") << "\n";
            }
            collect_slots(*ws.layout, ws.name, /*needs_placement=*/!tree_ready, slots, slot_workspace,
                          slot_needs_placement, slot_is_floating);
        }
        for (const auto& w : ws.floating) {
            slots.push_back(w);
            slot_workspace.push_back(ws.name);
            slot_needs_placement.push_back(true);  // floating windows are never part of the tiled tree-layout
            slot_is_floating.push_back(true);
        }
    }
    if (slots.empty()) {
        std::cout << "nothing to restore\n";
        return 0;
    }

    std::signal(SIGINT, handle_sigint);  // Section 4A

    WindowMatcher::Config cfg;
    cfg.timeout = kDefaultTimeout;
    WindowMatcher matcher(
        cfg, [&](const std::string& wm_class) { return current->list_windows(wm_class); },
        [&](const std::string& window_id, size_t slot_index, const WindowInfo& slot) {
            if (slot_needs_placement[slot_index]) {
                current->place_window(window_id, slot, slot_workspace[slot_index], slot_is_floating[slot_index]);
            }
        });

    auto results = matcher.restore_all(slots, &g_interrupted);

    int placed = 0, failed = 0;
    for (const auto& r : results) {
        switch (r.status) {
            case WindowMatcher::Status::Placed:
                ++placed;
                if (verbose) std::cerr << "verbose: placed " << r.wm_class << " -> " << r.window_id << "\n";
                break;
            case WindowMatcher::Status::LaunchFailed:
                ++failed;
                std::cerr << "warning: could not launch process for " << r.wm_class << "\n";
                break;
            case WindowMatcher::Status::TimedOut:
                ++failed;
                std::cerr << "warning: window matching " << r.wm_class << " not found, skipping\n";
                break;
            case WindowMatcher::Status::Interrupted:
                ++failed;
                break;
        }
    }

    if (g_interrupted.load()) {
        std::cerr << placed << " of " << results.size() << " windows restored before interrupt\n";
        return 1;
    }
    std::cout << placed << " of " << results.size() << " windows restored\n";
    return failed > 0 ? 1 : 0;  // Section 1B: non-zero on any partial failure
}

// Runs every precondition restore itself would check (WM detected, IPC
// reachable, session file parses, wm matches, target workspaces free) and
// reports each one instead of stopping at the first failure like restore
// does — the point is to tell you *why* a restore would fail before you run
// it, not to restore anything. Reuses detect_backend/log_wm_detection_failure
// /log_ipc_failure/load_session_file rather than re-deriving any of it.
int run_doctor(const std::string& file_arg) {
    bool all_ok = true;
    auto check = [&](bool ok, const std::string& label) {
        std::cout << (ok ? "[ok]   " : "[FAIL] ") << label << "\n";
        if (!ok) all_ok = false;
    };

    auto backend = detect_backend(kDefaultTimeout);
    check(backend != nullptr, backend ? ("window manager detected: " + backend->wm_name())
                                       : "window manager detected");
    if (!backend) {
        log_wm_detection_failure();
        return 1;
    }

    try {
        backend->dump("");
        check(true, backend->wm_name() + " IPC socket is reachable");
    } catch (const std::exception& e) {
        check(false, backend->wm_name() + " IPC socket is reachable");
        log_ipc_failure(e, "  ");
    }

    std::string path = file_arg.empty() ? "session.json" : file_arg;
    SessionFile session;
    std::string load_err;
    auto load_status = load_session_file(path, session, load_err);
    if (load_status == SessionLoadStatus::NotFound) {
        if (file_arg.empty()) {
            std::cout << "[skip] no session.json in the current directory (nothing to restore yet)\n";
            return all_ok ? 0 : 1;
        }
        check(false, "session file exists: " + path);
        return 1;
    }
    check(load_status == SessionLoadStatus::Ok, "session file is valid: " + path);
    if (load_status != SessionLoadStatus::Ok) {
        std::cout << "  " << load_err << "\n";
        return 1;
    }

    bool wm_match = backend->wm_name() == session.wm;
    check(wm_match, "session matches the running window manager (" + backend->wm_name() + ")");
    if (!wm_match) std::cout << "  session was saved for " << session.wm << "\n";

    for (const auto& ws : session.workspaces) {
        std::vector<WorkspaceSession> live;
        try {
            live = backend->dump(ws.name);
        } catch (const std::exception& e) {
            check(false, "workspace " + ws.name + " is reachable");
            log_ipc_failure(e, "  ");
            continue;
        }
        bool occupied = !live.empty() && (live[0].layout.has_value() || !live[0].floating.empty());
        check(!occupied, "workspace " + ws.name + " is free to restore into");
        if (occupied) std::cout << "  already has windows -- restore will refuse this workspace\n";
    }

    return all_ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage();
        return 1;
    }
    if (args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return 0;
    }
    if (args[0] == "--version") {
        std::cout << "tileroot 0.4.0\n";
        return 0;
    }

    std::string command = args[0];
    if (command == "dump") {
        std::string workspace_filter, out_file;
        bool pretty = false, verbose = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--workspace" && i + 1 < args.size()) {
                workspace_filter = args[++i];
            } else if (args[i] == "-o" && i + 1 < args.size()) {
                out_file = args[++i];
            } else if (args[i] == "--pretty") {
                pretty = true;
            } else if (args[i] == "--verbose") {
                verbose = true;
            } else {
                std::cerr << "error: unrecognized argument: " << args[i] << "\n";
                print_usage();
                return 1;
            }
        }
        return run_dump(workspace_filter, out_file, pretty, verbose);
    }

    if (command == "restore") {
        std::string file_arg;
        bool dry_run = false, verbose = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--dry-run") {
                dry_run = true;
            } else if (args[i] == "--verbose") {
                verbose = true;
            } else if (args[i][0] != '-') {
                file_arg = args[i];
            } else {
                std::cerr << "error: unrecognized argument: " << args[i] << "\n";
                print_usage();
                return 1;
            }
        }
        return run_restore(file_arg, dry_run, verbose);
    }

    if (command == "doctor") {
        std::string file_arg;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i][0] != '-') {
                file_arg = args[i];
            } else {
                std::cerr << "error: unrecognized argument: " << args[i] << "\n";
                print_usage();
                return 1;
            }
        }
        return run_doctor(file_arg);
    }

    std::cerr << "error: unknown command: " << command << "\n";
    print_usage();
    return 1;
}
