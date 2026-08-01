#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "session.h"

// Section 2 error taxonomy for IPC operations against the live WM.
struct IpcConnectionError : std::runtime_error {
    using std::runtime_error::runtime_error;
};  // covers SocketNotFoundError + ConnectionRefusedError
struct IpcTimeoutError : std::runtime_error {
    using std::runtime_error::runtime_error;
};  // Section 2A — bounded connect/query timeout, same pattern as window-match timeout
struct IpcMalformedResponseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One implementation per WM protocol family (Section 5A — named for the
// protocol, not the first consumer: I3ProtocolBackend serves both sway and
// i3, since sway implements i3's IPC wire protocol directly).
class WMBackend {
public:
    virtual ~WMBackend() = default;

    // Live layout for one workspace name, or every workspace on the
    // current output if workspace_filter is empty (design doc CLI surface).
    virtual std::vector<WorkspaceSession> dump(const std::string& workspace_filter) = 0;

    // Currently-visible window ids of `wm_class`, in discovery order —
    // consumed by WindowMatcher::ListWindowsFn during restore.
    virtual std::vector<std::string> list_windows(const std::string& wm_class) = 0;

    // Moves a matched window to `target_workspace` and resizes/repositions
    // it into its saved slot. The workspace move must happen — a relaunched
    // process otherwise lands on whatever workspace is currently focused,
    // not necessarily the one it was saved from. `is_floating` must be
    // honored explicitly — a relaunched window defaults to tiled regardless
    // of what it was saved as (confirmed via live testing: a saved floating
    // window came back tiled until this was made an explicit step), so a
    // true implementation needs to force floating state before positioning.
    //
    // NOTE: for a tiled window on a backend where prepare_tree_layout()
    // succeeded, the caller skips this call entirely — the WM already
    // placed the window correctly via its own native swallow/placeholder
    // matching, and calling place_window on top of that would be redundant
    // (and for a real tiling WM, actively wrong — there's no "position" to
    // move a tiled window to outside its container). Still called normally
    // for floating windows and for any backend without tree-layout support.
    virtual void place_window(const std::string& window_id, const WindowInfo& slot,
                               const std::string& target_workspace, bool is_floating) = 0;

    // Attempts to pre-build the exact tiled container structure of `layout`
    // on `target_workspace` before any window is launched, so relaunched
    // processes land in the right split position automatically instead of
    // being placed by geometry after the fact. Returns false if this
    // backend has no such mechanism (e.g. Hyprland, whose IPC doesn't
    // expose a real split tree to rebuild in the first place) or if the
    // attempt itself failed for any reason — either way, the caller falls
    // back to per-window geometry placement via place_window(), so this is
    // always safe to call speculatively.
    virtual bool prepare_tree_layout(const LayoutNode& /*layout*/, const std::string& /*target_workspace*/) {
        return false;
    }

    // "sway" | "i3" | "hyprland" — for error messages and the session.json wm field.
    virtual std::string wm_name() const = 0;
};

// Detects which WM is running (env vars) and returns the matching backend.
// Returns nullptr if no supported WM is detected — the caller reports this
// as the SocketNotFoundError-equivalent CLI error.
std::unique_ptr<WMBackend> detect_backend(std::chrono::milliseconds ipc_timeout);

// Constructs a backend for a specific wm name — used by `restore` after it
// reads the session.json wm field, independent of what's currently running
// (the wm-field-mismatch check in restore compares this against detect_backend()).
std::unique_ptr<WMBackend> make_backend(const std::string& wm_name, std::chrono::milliseconds ipc_timeout);
