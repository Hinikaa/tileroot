#pragma once

#include <chrono>
#include <string>

#include "backend.h"

// Implements Hyprland's socket protocol: a plain-text command sent to
// .socket.sock, response terminated by EOF (no length prefix). "j/<cmd>"
// requests JSON output. Hyprland's IPC does not expose a real split tree
// (unlike sway/i3), so dump() always produces a single flat split_h node
// — see design doc "Data & CLI Surface" for the schema note.
//
// NOTE: written against Hyprland's documented socket protocol. Requires a
// live Hyprland session to validate end-to-end — see README "Status".
class HyprlandBackend : public WMBackend {
public:
    explicit HyprlandBackend(std::chrono::milliseconds ipc_timeout);

    std::vector<WorkspaceSession> dump(const std::string& workspace_filter) override;
    std::vector<std::string> list_windows(const std::string& wm_class) override;
    void place_window(const std::string& window_id, const WindowInfo& slot, const std::string& target_workspace,
                       bool is_floating) override;
    std::string wm_name() const override { return "hyprland"; }

    // Empty if Hyprland isn't running (HYPRLAND_INSTANCE_SIGNATURE unset).
    static std::string socket_dir();

private:
    std::string command_socket_path_;
    std::chrono::milliseconds timeout_;

    // Sends `cmd` (verbatim — see backend.cpp for the "j/" JSON-mode
    // convention) and returns the raw response.
    std::string send_command(const std::string& cmd);

    // Section 1D: warns (does not fail) if the running Hyprland version is
    // outside the list this backend was written against.
    void check_version();
};
