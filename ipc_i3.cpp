#include "ipc_i3.h"

#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>

#include "ipc_socket.h"
#include "x11_pid.h"

using json = nlohmann::json;

namespace {
constexpr uint32_t kRunCommand = 0;
constexpr uint32_t kGetTree = 4;
constexpr char kMagic[] = "i3-ipc";
constexpr size_t kMagicLen = 6;
constexpr size_t kHeaderLen = kMagicLen + 4 + 4;

// Prefers sway's native Wayland "app_id"; falls back to
// window_properties.class for XWayland-backed windows. Empty string means
// "not a window leaf" (an internal split/workspace/output node).
std::string node_wm_class(const json& node) {
    if (node.contains("app_id") && node["app_id"].is_string()) {
        return node["app_id"].get<std::string>();
    }
    auto wp = node.find("window_properties");
    if (wp != node.end() && wp->contains("class") && (*wp)["class"].is_string()) {
        return (*wp)["class"].get<std::string>();
    }
    return "";
}

bool is_window_leaf(const json& node) {
    return !node_wm_class(node).empty();
}

// Reads argv from /proc/<pid>/cmdline (NUL-separated). Best-effort: an
// empty result means the process couldn't be inspected (permission,
// already exited) — such windows are skipped from the dump rather than
// saving a session that can never be restored (schema requires non-empty
// cmdline; see session.cpp window_from_json).
std::vector<std::string> read_proc_cmdline(long pid) {
    std::vector<std::string> argv;
    std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return argv;
    std::string buf;
    char chunk[256];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0) buf.append(chunk, n);
    std::fclose(f);
    size_t start = 0;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] == '\0') {
            if (i > start) argv.push_back(buf.substr(start, i - start));
            start = i + 1;
        }
    }
    return argv;
}

// Mutates `node` in place, attaching a "_cmdline" JSON array wherever a
// /proc lookup succeeds — run once over the whole tree before translation
// so node_to_layout only ever reads a value that's already resolved.
//
// sway includes a "pid" field directly on window nodes; real i3 does not
// (confirmed via live testing — an i3 window leaf has "window" (the X11
// window id) but no "pid" at all). For that case, `x11` resolves the pid
// via the window's _NET_WM_PID property instead — the standard EWMH way
// to map an X11 window to its owning process. `x11` may be an inert
// resolver (no X11 available, e.g. under sway/Hyprland) — pid_for_window
// just returns nullopt in that case, same as any other unrecoverable window.
// A window leaf ending up with no "_cmdline" means it silently vanishes
// from the dump (node_to_layout below skips it) -- that's a real "why
// didn't this restore" surprise if it happens quietly, so it's logged
// here, at the one place that actually knows which of the three reasons
// applies, rather than left for the user to guess at from a shorter
// session.json than they expected.
void warn_unrecoverable_window(const json& node, bool had_native_pid, bool x11_available) {
    std::string wm_class = node_wm_class(node);
    std::cerr << "warning: couldn't recover cmdline for window (" << (wm_class.empty() ? "unknown class" : wm_class)
               << ") -- it will be skipped: ";
    if (had_native_pid) {
        std::cerr << "process already exited, or /proc access denied\n";
    } else if (!node.contains("window")) {
        std::cerr << "no pid field and no X11 window id either\n";
    } else if (!x11_available) {
        std::cerr << "no pid field and X11 is unavailable (no XWayland running?)\n";
    } else {
        std::cerr << "no pid field and the X11 lookup for this window failed (already closed?)\n";
    }
}

void attach_cmdlines(json& node, X11PidResolver& x11) {
    std::optional<long> pid;
    bool had_native_pid = node.contains("pid") && node["pid"].is_number();
    if (had_native_pid) {
        pid = node["pid"].get<long>();
    } else if (node.contains("window") && node["window"].is_number()) {
        pid = x11.pid_for_window(node["window"].get<unsigned long>());
    }

    bool recovered = false;
    if (pid) {
        auto argv = read_proc_cmdline(*pid);
        if (!argv.empty()) {
            node["_cmdline"] = argv;
            recovered = true;
        }
    }
    if (!recovered && is_window_leaf(node)) {
        warn_unrecoverable_window(node, had_native_pid, x11.available());
    }

    if (node.contains("nodes")) {
        for (auto& child : node["nodes"]) attach_cmdlines(child, x11);
    }
    if (node.contains("floating_nodes")) {
        for (auto& child : node["floating_nodes"]) attach_cmdlines(child, x11);
    }
}

// Translates one tree node (post attach_cmdlines) into a LayoutNode.
// Returns std::nullopt for a window leaf with no recoverable cmdline
// (documented skip, see read_proc_cmdline) or a split node whose every
// child was skipped for the same reason.
std::optional<LayoutNode> node_to_layout(const json& node) {
    if (is_window_leaf(node)) {
        if (!node.contains("_cmdline")) return std::nullopt;
        LayoutNode leaf;
        leaf.type = LayoutNode::Type::Window;
        WindowInfo w;
        w.wm_class = node_wm_class(node);
        w.cmdline = node["_cmdline"].get<std::vector<std::string>>();
        const json& rect = node.at("rect");
        w.x = rect.value("x", 0);
        w.y = rect.value("y", 0);
        w.w = rect.value("width", 0);
        w.h = rect.value("height", 0);
        leaf.window = w;
        return leaf;
    }

    LayoutNode split;
    std::string layout = node.value("layout", "splith");
    split.type = layout == "splitv" ? LayoutNode::Type::SplitV : LayoutNode::Type::SplitH;
    if (node.contains("nodes")) {
        for (const auto& child : node["nodes"]) {
            if (auto translated = node_to_layout(child)) {
                split.children.push_back(std::move(*translated));
            }
        }
    }
    if (split.children.empty()) return std::nullopt;
    if (split.children.size() == 1) return split.children[0];  // collapse a now-single-child split
    return split;
}

// Escapes regex metacharacters in a wm_class before embedding it in an i3
// "swallows" criteria regex. Real class names routinely contain '.'
// (e.g. "org.gnome.Nautilus", "com.mitchellh.ghostty") — '.' is itself a
// regex metachar meaning "any character," so leaving it unescaped risks a
// swallow criterion matching windows it was never meant to match.
std::string regex_escape(const std::string& s) {
    static const std::string special = R"(.^$*+?()[]{}|\)";
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (special.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

// Converts a saved LayoutNode into i3's append_layout JSON format: split
// containers become plain "con" nodes with a "layout" direction, and each
// window leaf becomes a placeholder "con" with a "swallows" criteria array
// — i3 matches newly-launched windows against these criteria and inserts
// them into the exact tree position, which is what makes restore an actual
// tree reconstruction instead of position/resize geometry replay.
json layout_to_append_json(const LayoutNode& node) {
    if (node.type == LayoutNode::Type::Window) {
        json swallow;
        swallow["class"] = "^" + regex_escape(node.window->wm_class) + "$";
        return json{{"type", "con"}, {"swallows", json::array({swallow})}};
    }
    json children = json::array();
    for (const auto& child : node.children) children.push_back(layout_to_append_json(child));
    return json{
        {"type", "con"},
        {"layout", node.type == LayoutNode::Type::SplitV ? "splitv" : "splith"},
        {"nodes", children},
    };
}

// Recursively collects every type=="workspace" node under `node` into
// `out`. Real i3 nests workspaces inside a "content" con wrapper alongside
// dockarea containers (for bars) rather than putting them directly under
// the output — `output["nodes"]` is topdock/content/bottomdock, not
// workspaces themselves. Recursing (instead of assuming a fixed nesting
// depth) is what makes this correct regardless of bar config or future i3
// tree changes. Does not descend into a found workspace's own children —
// workspaces don't nest inside each other.
void collect_workspaces(const json& node, std::vector<const json*>& out) {
    if (node.value("type", "") == "workspace") {
        out.push_back(&node);
        return;
    }
    if (node.contains("nodes")) {
        for (const auto& child : node["nodes"]) collect_workspaces(child, out);
    }
}

// Builds one WorkspaceSession from a GET_TREE workspace node (post
// attach_cmdlines). Shared by both dump() paths (explicit --workspace name
// search and focused-output enumeration) so the tree-to-session translation
// logic exists exactly once.
WorkspaceSession build_workspace_session(const json& ws_node, const std::string& output_name) {
    WorkspaceSession ws;
    ws.name = ws_node.value("name", "");
    ws.output = output_name;

    std::vector<json> tiled_children;
    if (ws_node.contains("nodes")) {
        for (const auto& c : ws_node["nodes"]) tiled_children.push_back(c);
    }
    if (tiled_children.empty()) {
        // Section 1A: empty workspace → std::nullopt, not an empty split node.
        ws.layout = std::nullopt;
    } else if (tiled_children.size() == 1) {
        ws.layout = node_to_layout(tiled_children[0]);  // already optional; nullopt if unrecoverable
    } else {
        LayoutNode root;
        std::string layout = ws_node.value("layout", "splith");
        root.type = layout == "splitv" ? LayoutNode::Type::SplitV : LayoutNode::Type::SplitH;
        for (const auto& c : tiled_children) {
            if (auto translated = node_to_layout(c)) {
                root.children.push_back(std::move(*translated));
            }
        }
        if (root.children.empty()) {
            ws.layout = std::nullopt;
        } else if (root.children.size() == 1) {
            ws.layout = std::move(root.children[0]);
        } else {
            ws.layout = std::move(root);
        }
    }

    if (ws_node.contains("floating_nodes")) {
        for (const auto& fn : ws_node["floating_nodes"]) {
            if (auto leaf = node_to_layout(fn); leaf && leaf->window) {
                ws.floating.push_back(*leaf->window);
            }
        }
    }
    // Scratchpad capture (the "__i3_scratch" workspace's floating_nodes)
    // needs live-sway validation of its exact tree shape before it can
    // be trusted — left empty for now rather than shipping unverified
    // logic. See README "Status".

    // A window without a recoverable /proc cmdline (attach_cmdlines
    // found nothing — permission denied, process already exited) is
    // simply skipped from the dump: documented limitation, matches
    // i3-resurrect's own approach to unrecoverable cmdlines.

    return ws;
}

}  // namespace

std::string I3ProtocolBackend::socket_path(const char* env_var) {
    const char* v = std::getenv(env_var);
    return v ? std::string(v) : std::string();
}

I3ProtocolBackend::I3ProtocolBackend(const char* socket_env_var, std::string wm_name,
                                      std::chrono::milliseconds ipc_timeout)
    : socket_path_(socket_path(socket_env_var)), wm_name_(std::move(wm_name)), timeout_(ipc_timeout) {
    if (socket_path_.empty()) {
        throw IpcConnectionError(std::string(socket_env_var) + " is not set — is " + wm_name_ + " running?");
    }
}

json I3ProtocolBackend::request(uint32_t type, const std::string& payload) {
    UnixSocket sock(socket_path_, timeout_);

    std::string msg;
    msg.reserve(kHeaderLen + payload.size());
    msg.append(kMagic, kMagicLen);
    uint32_t len = static_cast<uint32_t>(payload.size());
    msg.append(reinterpret_cast<const char*>(&len), 4);
    msg.append(reinterpret_cast<const char*>(&type), 4);
    msg.append(payload);
    sock.write_all(msg);

    std::string header = sock.read_exact(kHeaderLen);
    if (std::memcmp(header.data(), kMagic, kMagicLen) != 0) {
        throw IpcMalformedResponseError("reply header magic mismatch — not an i3-ipc socket?");
    }
    uint32_t reply_len;
    std::memcpy(&reply_len, header.data() + kMagicLen, 4);
    std::string payload_bytes = sock.read_exact(reply_len);

    try {
        return json::parse(payload_bytes);
    } catch (const json::parse_error& e) {
        throw IpcMalformedResponseError(std::string("reply was not valid JSON: ") + e.what());
    }
}

std::vector<WorkspaceSession> I3ProtocolBackend::dump(const std::string& workspace_filter) {
    json tree = request(kGetTree, "");
    X11PidResolver x11;  // inert (all lookups return nullopt) if no X11 display is available
    attach_cmdlines(tree, x11);

    std::vector<WorkspaceSession> result;
    if (!tree.contains("nodes")) return result;

    if (!workspace_filter.empty()) {
        // Explicit --workspace NAME: search every real output for a
        // workspace with this name. Never the "__i3" pseudo-output — that's
        // sway/i3's internal scratchpad container, not a real workspace
        // target (see the empty-filter comment below for why it matters
        // here too). Workspaces are found via collect_workspaces(), not by
        // assuming they sit directly under the output — real i3 nests them
        // inside a "content" wrapper alongside dock areas for bars.
        for (const auto& output : tree["nodes"]) {
            if (output.value("type", "") != "output" || output.value("name", "") == "__i3") continue;
            std::vector<const json*> workspaces_in_output;
            collect_workspaces(output, workspaces_in_output);
            for (const json* ws_node : workspaces_in_output) {
                if (ws_node->value("name", "") != workspace_filter) continue;
                result.push_back(build_workspace_session(*ws_node, output.value("name", "")));
            }
        }
        return result;
    }

    // No filter: dump every workspace on every real output (monitor), not
    // just the one the command was run from — a user with multiple monitors
    // wants their whole session captured, not just whichever screen has
    // focus. Never the "__i3" pseudo-output — that's sway/i3's internal
    // scratchpad container, not a real workspace. Sorted by workspace
    // number (see workspace_less) so the output is stable and matches how
    // i3/sway itself orders workspaces, regardless of tree traversal order.
    for (const auto& output : tree["nodes"]) {
        if (output.value("type", "") != "output" || output.value("name", "") == "__i3") continue;
        std::vector<const json*> workspaces_in_output;
        collect_workspaces(output, workspaces_in_output);
        for (const json* ws_node : workspaces_in_output) {
            result.push_back(build_workspace_session(*ws_node, output.value("name", "")));
        }
    }
    std::sort(result.begin(), result.end(), workspace_name_less);
    return result;
}

std::vector<std::string> I3ProtocolBackend::list_windows(const std::string& wm_class) {
    json tree = request(kGetTree, "");
    std::vector<std::string> ids;

    std::function<void(const json&)> walk = [&](const json& node) {
        if (is_window_leaf(node) && node_wm_class(node) == wm_class && node.contains("id")) {
            ids.push_back(std::to_string(node["id"].get<long long>()));
        }
        if (node.contains("nodes")) {
            for (const auto& c : node["nodes"]) walk(c);
        }
        if (node.contains("floating_nodes")) {
            for (const auto& c : node["floating_nodes"]) walk(c);
        }
    };
    walk(tree);
    return ids;
}

void I3ProtocolBackend::place_window(const std::string& window_id, const WindowInfo& slot,
                                      const std::string& target_workspace, bool is_floating) {
    // window_id is a con id we parsed ourselves from our own IPC response
    // (an integer, re-serialized as a string) — safe to interpolate into
    // the criteria selector; this is not untrusted external text (Section
    // 3B: structured criteria, not string-built from arbitrary data).
    // Workspace move MUST happen first — a relaunched window otherwise
    // lands on whatever workspace is currently focused, not the saved one.
    // A relaunched window defaults to tiled regardless of how it was saved
    // (confirmed via live testing), so floating state must be forced
    // explicitly rather than assumed — "move position"/"resize set" are
    // floating-specific commands that silently no-op on a still-tiled window.
    std::string cmd = "[con_id=" + window_id + "] move to workspace \"" + target_workspace + "\", " +
                       (is_floating ? "floating enable, " : "") + "move position " + std::to_string(slot.x) + " " +
                       std::to_string(slot.y) + ", resize set " + std::to_string(slot.w) + " " +
                       std::to_string(slot.h);
    request(kRunCommand, cmd);
}

bool I3ProtocolBackend::prepare_tree_layout(const LayoutNode& layout, const std::string& target_workspace) {
    // Focus the target workspace first — append_layout inserts into
    // whatever's currently focused, and the workspace-empty check in
    // main.cpp's restore() already guarantees this one has nothing in it.
    json focus_reply = request(kRunCommand, "workspace \"" + target_workspace + "\"");
    if (!focus_reply.is_array() || focus_reply.empty() || !focus_reply[0].value("success", false)) {
        return false;
    }

    json append_json = layout_to_append_json(layout);
    std::string tmp_path = "/tmp/tileroot-layout-XXXXXX";
    std::vector<char> tmp_template(tmp_path.begin(), tmp_path.end());
    tmp_template.push_back('\0');
    int fd = mkstemp(tmp_template.data());
    if (fd < 0) return false;

    std::string dumped = append_json.dump();
    bool write_ok = ::write(fd, dumped.data(), dumped.size()) == static_cast<ssize_t>(dumped.size());
    ::close(fd);
    std::string actual_path(tmp_template.data());
    if (!write_ok) {
        std::remove(actual_path.c_str());
        return false;
    }

    json append_reply;
    try {
        append_reply = request(kRunCommand, "append_layout " + actual_path);
    } catch (const std::exception&) {
        std::remove(actual_path.c_str());
        return false;
    }
    std::remove(actual_path.c_str());

    return append_reply.is_array() && !append_reply.empty() && append_reply[0].value("success", false);
}
