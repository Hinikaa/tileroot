#include "ipc_hyprland.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "ipc_socket.h"

using json = nlohmann::json;

namespace {
// Hyprland versions this backend has been written/checked against. Not
// exhaustive — Hyprland's plaintext/JSON IPC shape has been observed to
// shift between releases (Section 1D), so anything outside this list gets
// a warning, not a hard failure.
const std::vector<std::string> kTestedVersions = {"0.55", "0.56"};
}  // namespace

std::string HyprlandBackend::socket_dir() {
    const char* sig = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) return "";
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    std::string base = runtime_dir ? runtime_dir : "/tmp";
    return base + "/hypr/" + sig;
}

HyprlandBackend::HyprlandBackend(std::chrono::milliseconds ipc_timeout) : timeout_(ipc_timeout) {
    std::string dir = socket_dir();
    if (dir.empty()) {
        throw IpcConnectionError("HYPRLAND_INSTANCE_SIGNATURE is not set — is Hyprland running?");
    }
    command_socket_path_ = dir + "/.socket.sock";
    check_version();
}

std::string HyprlandBackend::send_command(const std::string& cmd) {
    UnixSocket sock(command_socket_path_, timeout_);
    sock.write_all(cmd);
    return sock.read_until_eof();
}

void HyprlandBackend::check_version() {
    std::string raw;
    try {
        raw = send_command("j/version");
    } catch (const std::exception&) {
        return;  // version check is advisory — never block dump/restore on it
    }
    try {
        json v = json::parse(raw);
        std::string version = v.value("version", "");
        bool known = std::any_of(kTestedVersions.begin(), kTestedVersions.end(), [&](const std::string& t) {
            return version.rfind(t, 0) == 0;  // version starts with a tested major.minor
        });
        if (!known && !version.empty()) {
            std::cerr << "warning: running Hyprland " << version
                      << " has not been tested with this tool — dump/restore may misparse. "
                         "Tested versions: 0.55.x, 0.56.x\n";
        }
    } catch (const json::parse_error&) {
        // Advisory only — a malformed version response doesn't block the real command.
    }
}

namespace {

// Builds one WorkspaceSession from the full `j/clients` list, keeping only
// windows whose workspace name matches `target_name`. Shared by both dump()
// paths (explicit --workspace filter and the all-workspaces enumeration) so
// the clients-to-session translation exists exactly once.
WorkspaceSession build_hypr_workspace(const json& clients, const std::string& target_name,
                                       const std::string& output_name) {
    WorkspaceSession ws;
    ws.name = target_name;
    ws.output = output_name;

    std::vector<WindowInfo> tiled;
    for (const auto& c : clients) {
        std::string ws_name = c.value("workspace", json::object()).value("name", "");
        if (ws_name != target_name) continue;

        WindowInfo w;
        w.wm_class = c.value("class", "");
        long pid = c.value("pid", 0L);
        // Same /proc cmdline recovery as the sway/i3 backend — see ipc_i3.cpp
        // read_proc_cmdline for the documented "skip if unrecoverable" policy.
        std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        std::string buf;
        char chunk[256];
        size_t n;
        while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) buf.append(chunk, n);
        std::fclose(f);
        size_t start = 0;
        for (size_t i = 0; i < buf.size(); ++i) {
            if (buf[i] == '\0') {
                if (i > start) w.cmdline.push_back(buf.substr(start, i - start));
                start = i + 1;
            }
        }
        if (w.cmdline.empty()) continue;  // unrecoverable — skip (matches ipc_i3.cpp policy)

        auto at = c.value("at", std::vector<int>{0, 0});
        auto size = c.value("size", std::vector<int>{0, 0});
        w.x = at.size() > 0 ? at[0] : 0;
        w.y = at.size() > 1 ? at[1] : 0;
        w.w = size.size() > 0 ? size[0] : 0;
        w.h = size.size() > 1 ? size[1] : 0;

        if (c.value("floating", false)) {
            ws.floating.push_back(w);
        } else {
            tiled.push_back(w);
        }
    }

    // No real split tree available (design doc): geometry-order flat
    // split_h, left to right (Section "Data & CLI Surface").
    std::sort(tiled.begin(), tiled.end(), [](const WindowInfo& a, const WindowInfo& b) { return a.x < b.x; });

    if (!tiled.empty()) {
        LayoutNode root;
        root.type = LayoutNode::Type::SplitH;
        for (const auto& w : tiled) {
            LayoutNode leaf;
            leaf.type = LayoutNode::Type::Window;
            leaf.window = w;
            root.children.push_back(std::move(leaf));
        }
        ws.layout = std::move(root);
    }  // else: std::nullopt — Section 1A empty-workspace convention

    return ws;
}

}  // namespace

std::vector<WorkspaceSession> HyprlandBackend::dump(const std::string& workspace_filter) {
    json clients = json::parse(send_command("j/clients"), nullptr, false);
    if (!clients.is_array()) {
        throw IpcMalformedResponseError("clients reply was not a JSON array");
    }

    json workspaces = json::parse(send_command("j/workspaces"), nullptr, false);
    if (!workspaces.is_array()) {
        throw IpcMalformedResponseError("workspaces reply was not a JSON array");
    }

    std::vector<WorkspaceSession> result;

    if (!workspace_filter.empty()) {
        std::string output_name;
        for (const auto& w : workspaces) {
            if (w.value("name", "") == workspace_filter) {
                output_name = w.value("monitor", "");
                break;
            }
        }
        result.push_back(build_hypr_workspace(clients, workspace_filter, output_name));
        return result;
    }

    // No filter: dump every workspace on every monitor — a user with
    // multiple monitors wants their whole session captured, not just
    // whichever one has focus. `j/workspaces` already enumerates every
    // workspace with windows on it, across all monitors.
    for (const auto& w : workspaces) {
        result.push_back(build_hypr_workspace(clients, w.value("name", ""), w.value("monitor", "")));
    }
    std::sort(result.begin(), result.end(), workspace_name_less);
    return result;
}

std::vector<std::string> HyprlandBackend::list_windows(const std::string& wm_class) {
    json clients = json::parse(send_command("j/clients"), nullptr, false);
    std::vector<std::string> ids;
    if (!clients.is_array()) return ids;
    for (const auto& c : clients) {
        if (c.value("class", "") == wm_class) {
            ids.push_back(c.value("address", ""));
        }
    }
    return ids;
}

void HyprlandBackend::place_window(const std::string& window_id, const WindowInfo& slot,
                                    const std::string& target_workspace, bool is_floating) {
    // window_id is a client "address" we parsed ourselves from our own IPC
    // response — safe to interpolate (Section 3B: structured, not
    // string-built from arbitrary/untrusted data). Workspace move (silent —
    // doesn't switch the user's focus) must happen before positioning, same
    // reasoning as the sway/i3 backend.
    send_command("dispatch movetoworkspacesilent name:" + target_workspace + ",address:" + window_id);
    // Applying the same fix confirmed live on i3 (a relaunched window
    // defaults to tiled, floating state must be forced explicitly) by
    // analogy — Hyprland's `setfloating` dispatcher sets floating state on
    // (as opposed to `togglefloating`, which would risk going the wrong way
    // if the window somehow already floated by default). NOT live-verified
    // against a real Hyprland session — see README Status.
    if (is_floating) send_command("dispatch setfloating address:" + window_id);
    send_command("dispatch movewindowpixel exact " + std::to_string(slot.x) + " " + std::to_string(slot.y) +
                 ",address:" + window_id);
    send_command("dispatch resizewindowpixel exact " + std::to_string(slot.w) + " " + std::to_string(slot.h) +
                 ",address:" + window_id);
}
