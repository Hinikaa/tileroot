#pragma once

#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// A saved/restored window: argv-style command (never a shell string — see
// README "Security" section for why), plus geometry and the WM class used
// to match a relaunched process back to its saved slot.
struct WindowInfo {
    std::string wm_class;
    std::vector<std::string> cmdline;  // argv, exec'd directly — never shell-interpreted
    int x = 0, y = 0, w = 0, h = 0;
};

// Recursive tiled-window tree. A leaf is a Window; an internal node splits
// its children horizontally or vertically. Hyprland's IPC exposes no real
// split tree, so a Hyprland-sourced LayoutNode is always a single flat
// SplitH whose children are windows in left-to-right geometry order.
struct LayoutNode {
    enum class Type { SplitH, SplitV, Window };
    Type type;
    std::vector<LayoutNode> children;  // populated when type is SplitH/SplitV
    std::optional<WindowInfo> window;  // populated when type is Window
};

struct WorkspaceSession {
    std::string name;
    std::string output;
    std::optional<LayoutNode> layout;  // std::nullopt == empty workspace (see README schema note)
    std::vector<WindowInfo> floating;
    std::vector<WindowInfo> scratchpad;
};

struct SessionFile {
    std::string wm;  // "sway" | "i3" | "hyprland"
    std::vector<WorkspaceSession> workspaces;
};

// Thrown when a session.json is well-formed JSON but missing/mistyped
// required fields (hand-edited or corrupted file) — never lets a missing
// field surface as an unchecked nlohmann::json exception or a crash.
struct SchemaValidationError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Serializes to/from the session.json wire format described in the design
// doc. Throws SchemaValidationError with a specific field name on any
// missing/mistyped required field — never partially constructs a SessionFile.
nlohmann::json to_json(const SessionFile& session);
SessionFile session_from_json(const nlohmann::json& j);

// Unicode box-drawing tree renderer for `tileroot dump --pretty` — the
// actual demo-GIF payoff (see design doc "What Makes This Cool").
std::string render_pretty(const WorkspaceSession& ws);

// Orders workspaces the way i3/sway/Hyprland bars do: by leading numeric
// prefix ("1", "2: web", "10"), with non-numeric names sorted after all
// numbered ones. Shared by every backend's no-filter dump() so `tileroot
// dump` always returns workspaces "sorted after workspace 1,2,3,4"
// regardless of which WM produced them or what order its IPC returned them.
bool workspace_name_less(const WorkspaceSession& a, const WorkspaceSession& b);
