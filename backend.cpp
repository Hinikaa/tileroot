#include "backend.h"

#include "ipc_hyprland.h"
#include "ipc_i3.h"

std::unique_ptr<WMBackend> make_backend(const std::string& wm_name, std::chrono::milliseconds ipc_timeout) {
    if (wm_name == "sway") {
        return std::make_unique<I3ProtocolBackend>("SWAYSOCK", "sway", ipc_timeout);
    }
    if (wm_name == "i3") {
        return std::make_unique<I3ProtocolBackend>("I3SOCK", "i3", ipc_timeout);
    }
    if (wm_name == "hyprland") {
        return std::make_unique<HyprlandBackend>(ipc_timeout);
    }
    throw IpcConnectionError("unsupported wm: " + wm_name);
}

std::unique_ptr<WMBackend> detect_backend(std::chrono::milliseconds ipc_timeout) {
    if (!I3ProtocolBackend::socket_path("SWAYSOCK").empty()) {
        return make_backend("sway", ipc_timeout);
    }
    if (!HyprlandBackend::socket_dir().empty()) {
        return make_backend("hyprland", ipc_timeout);
    }
    if (!I3ProtocolBackend::socket_path("I3SOCK").empty()) {
        return make_backend("i3", ipc_timeout);
    }
    return nullptr;
}
