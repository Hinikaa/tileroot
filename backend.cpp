#include "backend.h"

#include <cstdio>

#include "ipc_hyprland.h"
#include "ipc_i3.h"

std::vector<std::string> read_proc_cmdline(long pid, bool* open_failed) {
    std::vector<std::string> argv;
    if (pid <= 0) {
        if (open_failed) *open_failed = true;
        return argv;
    }
    std::string path = "/proc/" + std::to_string(pid) + "/cmdline";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (open_failed) *open_failed = true;
        return argv;
    }
    if (open_failed) *open_failed = false;
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
    if (start < buf.size()) {
        argv.push_back(buf.substr(start));
    }
    return argv;
}

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
