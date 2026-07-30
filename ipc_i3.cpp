#include "ipc_i3.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#include "ipc_socket.h"

using json = nlohmann::json;

namespace {
constexpr uint32_t kGetTree = 4;
constexpr uint32_t kRunCommand = 0;
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
void attach_cmdlines(json& node) {
    if (node.contains("pid") && node["pid"].is_number()) {
        auto argv = read_proc_cmdline(node["pid"].get<long>());
        if (!argv.empty()) node["_cmdline"] = argv;
    }
    if (node.contains("nodes")) {
        for (auto& child : node["nodes"]) attach_cmdlines(child);
    }
    if (node.contains("floating_nodes")) {
        for (auto& child : node["floating_nodes"]) attach_cmdlines(child);
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
    attach_cmdlines(tree);

    // Find the focused output (or all outputs if we need every workspace
    // — but the design doc scopes default dump to "current output," so we
    // always resolve one output first).
    const json* focused_output = nullptr;
    if (tree.contains("nodes")) {
        for (const auto& output : tree["nodes"]) {
            if (output.value("type", "") != "output") continue;
            if (!output.contains("nodes")) continue;
            for (const auto& ws : output["nodes"]) {
                if (ws.value("type", "") == "workspace" && ws.value("focused", false)) {
                    focused_output = &output;
                }
            }
        }
    }
    if (!focused_output && tree.contains("nodes") && !tree["nodes"].empty()) {
        focused_output = &tree["nodes"][0];  // fallback: first output
    }

    std::vector<WorkspaceSession> result;
    if (!focused_output || !focused_output->contains("nodes")) return result;

    for (const auto& ws_node : (*focused_output)["nodes"]) {
        if (ws_node.value("type", "") != "workspace") continue;
        std::string ws_name = ws_node.value("name", "");
        if (!workspace_filter.empty() && ws_name != workspace_filter) continue;

        WorkspaceSession ws;
        ws.name = ws_name;
        ws.output = focused_output->value("name", "");

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

        result.push_back(std::move(ws));
    }
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
                                      const std::string& target_workspace) {
    // window_id is a con id we parsed ourselves from our own IPC response
    // (an integer, re-serialized as a string) — safe to interpolate into
    // the criteria selector; this is not untrusted external text (Section
    // 3B: structured criteria, not string-built from arbitrary data).
    // Workspace move MUST happen first — a relaunched window otherwise
    // lands on whatever workspace is currently focused, not the saved one.
    std::string cmd = "[con_id=" + window_id + "] move to workspace \"" + target_workspace +
                       "\", move position " + std::to_string(slot.x) + " " + std::to_string(slot.y) +
                       ", resize set " + std::to_string(slot.w) + " " + std::to_string(slot.h);
    request(kRunCommand, cmd);
}
