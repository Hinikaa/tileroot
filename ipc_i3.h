#pragma once

#include <chrono>
#include <string>

#include "backend.h"

// Implements the i3-ipc wire protocol: "i3-ipc" magic + u32 length + u32
// type (little-endian) + JSON payload. Sway implements this protocol
// directly (it's not "compatible with," it IS i3's IPC), so this single
// class serves sway today and i3 once v1.1 adds it — named for the
// protocol, not the first WM that uses it (Section 5A).
//
// NOTE: written against the public i3-ipc / sway-ipc specification.
// Requires a live sway (or i3) session to validate end-to-end — see
// README "Status" section.
class I3ProtocolBackend : public WMBackend {
public:
    // socket_env_var: "SWAYSOCK" or "I3SOCK" — which env var holds the
    // socket path, since sway and i3 use different variable names for the
    // same protocol.
    I3ProtocolBackend(const char* socket_env_var, std::string wm_name,
                       std::chrono::milliseconds ipc_timeout);

    std::vector<WorkspaceSession> dump(const std::string& workspace_filter) override;
    std::vector<std::string> list_windows(const std::string& wm_class) override;
    void place_window(const std::string& window_id, const WindowInfo& slot, const std::string& target_workspace,
                       bool is_floating) override;
    // Builds an i3 layout-file (append_layout) with swallow criteria
    // matching `layout`'s tree shape, writes it to a temp file, and runs
    // `append_layout` on the focused/target workspace — i3's own native
    // mechanism for exactly this (i3-resurrect uses the same command,
    // independently arrived at here — see README license note re: GPLv3).
    // Live-tested against a real headless i3 session; see README Status.
    bool prepare_tree_layout(const LayoutNode& layout, const std::string& target_workspace) override;
    std::string wm_name() const override { return wm_name_; }

    // Returns the socket path from the env var, or empty if unset (used by
    // detect_backend to decide whether this WM is even a candidate).
    static std::string socket_path(const char* env_var);

private:
    std::string socket_path_;
    std::string wm_name_;
    std::chrono::milliseconds timeout_;

    // Sends a request of `type` with `payload`, returns the decoded JSON
    // reply. Throws IpcMalformedResponseError if the reply isn't valid
    // JSON or the header magic doesn't match.
    nlohmann::json request(uint32_t type, const std::string& payload);
};
