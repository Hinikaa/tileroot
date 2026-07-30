#include <csignal>
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

void print_usage() {
    std::cerr << "usage: tileroot dump [--workspace NAME] [-o FILE] [--pretty] [--verbose]\n"
                 "       tileroot restore [FILE] [--dry-run] [--verbose]\n"
                 "       tileroot --version | --help\n";
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
        std::cerr << "error: no supported window manager detected (checked sway, hyprland, i3)\n";
        return 1;
    }
    if (verbose) std::cerr << "verbose: detected wm=" << backend->wm_name() << "\n";

    std::vector<WorkspaceSession> workspaces;
    try {
        workspaces = backend->dump(workspace_filter);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
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
void collect_slots(const LayoutNode& node, const std::string& ws_name, std::vector<WindowInfo>& slots,
                    std::vector<std::string>& slot_workspace) {
    if (node.type == LayoutNode::Type::Window) {
        slots.push_back(*node.window);
        slot_workspace.push_back(ws_name);
        return;
    }
    for (const auto& child : node.children) collect_slots(child, ws_name, slots, slot_workspace);
}

int run_restore(const std::string& file_arg, bool dry_run, bool verbose) {
    std::string path = file_arg.empty() ? "session.json" : file_arg;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "error: session file not found: " << path << "\n";
        return 1;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "error: malformed session file: " << e.what() << "\n";
        return 1;
    }

    SessionFile session;
    try {
        session = session_from_json(j);
    } catch (const SchemaValidationError& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    auto current = detect_backend(kDefaultTimeout);
    if (!current) {
        std::cerr << "error: no supported window manager detected (checked sway, hyprland, i3)\n";
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
            std::cerr << "error: could not check target workspace " << ws.name << ": " << e.what() << "\n";
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
    for (const auto& ws : session.workspaces) {
        if (ws.layout) collect_slots(*ws.layout, ws.name, slots, slot_workspace);
        for (const auto& w : ws.floating) {
            slots.push_back(w);
            slot_workspace.push_back(ws.name);
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
            current->place_window(window_id, slot, slot_workspace[slot_index]);
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
        std::cout << "tileroot 0.1.0\n";
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

    std::cerr << "error: unknown command: " << command << "\n";
    print_usage();
    return 1;
}
