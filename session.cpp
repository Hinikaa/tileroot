#include "session.h"

#include <sstream>

using json = nlohmann::json;

namespace {

// Fetches a required field, throwing SchemaValidationError (naming the
// field) instead of letting nlohmann::json's generic type_error surface —
// this is what stands between a hand-edited/corrupted session.json and a
// crash (Section 2 GAP: SchemaValidationError).
template <typename T>
T require(const json& j, const char* field) {
    auto it = j.find(field);
    if (it == j.end()) {
        throw SchemaValidationError(std::string("missing required field: ") + field);
    }
    try {
        return it->get<T>();
    } catch (const json::exception&) {
        throw SchemaValidationError(std::string("field has wrong type: ") + field);
    }
}

json window_to_json(const WindowInfo& w) {
    return json{
        {"class", w.wm_class},
        {"cmdline", w.cmdline},
        {"geometry", json{{"x", w.x}, {"y", w.y}, {"w", w.w}, {"h", w.h}}},
    };
}

WindowInfo window_from_json(const json& j) {
    WindowInfo w;
    w.wm_class = require<std::string>(j, "class");
    w.cmdline = require<std::vector<std::string>>(j, "cmdline");
    if (w.cmdline.empty()) {
        throw SchemaValidationError("window cmdline must not be empty");
    }
    const json& geom = require<json>(j, "geometry");
    w.x = require<int>(geom, "x");
    w.y = require<int>(geom, "y");
    w.w = require<int>(geom, "w");
    w.h = require<int>(geom, "h");
    return w;
}

json layout_to_json(const LayoutNode& node) {
    if (node.type == LayoutNode::Type::Window) {
        json j = window_to_json(*node.window);
        j["type"] = "window";
        return j;
    }
    json children = json::array();
    for (const auto& child : node.children) {
        children.push_back(layout_to_json(child));
    }
    return json{
        {"type", node.type == LayoutNode::Type::SplitH ? "split_h" : "split_v"},
        {"children", children},
    };
}

LayoutNode layout_from_json(const json& j) {
    std::string type = require<std::string>(j, "type");
    if (type == "window") {
        LayoutNode node;
        node.type = LayoutNode::Type::Window;
        node.window = window_from_json(j);
        return node;
    }
    if (type != "split_h" && type != "split_v") {
        throw SchemaValidationError("layout node has unknown type: " + type);
    }
    LayoutNode node;
    node.type = type == "split_h" ? LayoutNode::Type::SplitH : LayoutNode::Type::SplitV;
    const json& children = require<json>(j, "children");
    if (!children.is_array()) {
        throw SchemaValidationError("layout node children must be an array");
    }
    for (const auto& child : children) {
        node.children.push_back(layout_from_json(child));
    }
    return node;
}

}  // namespace

json to_json(const SessionFile& session) {
    json workspaces = json::array();
    for (const auto& ws : session.workspaces) {
        json floating = json::array();
        for (const auto& w : ws.floating) floating.push_back(window_to_json(w));
        json scratchpad = json::array();
        for (const auto& w : ws.scratchpad) scratchpad.push_back(window_to_json(w));

        workspaces.push_back(json{
            {"name", ws.name},
            {"output", ws.output},
            {"layout", ws.layout ? layout_to_json(*ws.layout) : json(nullptr)},
            {"floating", floating},
            {"scratchpad", scratchpad},
        });
    }
    return json{{"wm", session.wm}, {"workspaces", workspaces}};
}

SessionFile session_from_json(const json& j) {
    if (!j.is_object()) {
        throw SchemaValidationError("session root must be a JSON object");
    }
    SessionFile session;
    session.wm = require<std::string>(j, "wm");
    if (session.wm != "sway" && session.wm != "i3" && session.wm != "hyprland") {
        throw SchemaValidationError("wm field must be sway, i3, or hyprland, got: " + session.wm);
    }
    const json& workspaces = require<json>(j, "workspaces");
    if (!workspaces.is_array()) {
        throw SchemaValidationError("workspaces must be an array");
    }
    for (const auto& wsj : workspaces) {
        WorkspaceSession ws;
        ws.name = require<std::string>(wsj, "name");
        ws.output = require<std::string>(wsj, "output");

        auto layout_it = wsj.find("layout");
        if (layout_it == wsj.end()) {
            throw SchemaValidationError("missing required field: layout");
        }
        if (!layout_it->is_null()) {
            ws.layout = layout_from_json(*layout_it);
        }  // else: std::nullopt — empty workspace (Section 1 decision 1A)

        for (const auto& fj : require<json>(wsj, "floating")) ws.floating.push_back(window_from_json(fj));
        for (const auto& sj : require<json>(wsj, "scratchpad")) ws.scratchpad.push_back(window_from_json(sj));

        session.workspaces.push_back(std::move(ws));
    }
    return session;
}

namespace {

// Recursive box-drawing renderer. `prefix` accumulates the connector
// characters for ancestor levels; `is_last` controls this node's own
// connector (└── vs ├──) and whether it contributes "│   " or "    " to
// its children's prefix.
void render_node(const LayoutNode& node, const std::string& prefix, bool is_last, std::ostringstream& out) {
    out << prefix << (is_last ? "└── " : "├── ");
    if (node.type == LayoutNode::Type::Window) {
        out << node.window->wm_class << "\n";
        return;
    }
    out << (node.type == LayoutNode::Type::SplitH ? "split_h" : "split_v") << "\n";
    std::string child_prefix = prefix + (is_last ? "    " : "│   ");
    for (size_t i = 0; i < node.children.size(); ++i) {
        render_node(node.children[i], child_prefix, i + 1 == node.children.size(), out);
    }
}

}  // namespace

std::string render_pretty(const WorkspaceSession& ws) {
    std::ostringstream out;
    out << ws.name << " (" << ws.output << ")\n";
    if (!ws.layout) {
        out << "  (empty)\n";
    } else {
        render_node(*ws.layout, "", true, out);
    }
    if (!ws.floating.empty()) {
        out << "floating:\n";
        for (const auto& w : ws.floating) out << "  " << w.wm_class << "\n";
    }
    if (!ws.scratchpad.empty()) {
        out << "scratchpad:\n";
        for (const auto& w : ws.scratchpad) out << "  " << w.wm_class << "\n";
    }
    return out.str();
}
