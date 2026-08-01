#pragma once

#include <optional>

// Resolves an X11 window's PID via the _NET_WM_PID property (EWMH). Real
// i3's GET_TREE window nodes don't include a "pid" field the way sway's
// do (found via live testing — see ipc_i3.cpp attach_cmdlines), so this
// is the fallback that makes cmdline recovery work on i3 at all.
//
// Deliberately uses void*/long here instead of Xlib's Display*/Atom types
// so this header doesn't drag X11/Xlib.h's macro soup (None, Bool, Status,
// ...) into every translation unit that includes it — the real types live
// only in x11_pid.cpp.
class X11PidResolver {
public:
    // Opens $DISPLAY. If that fails (no X11 available, e.g. running under
    // sway/Hyprland with no XWayland), the resolver is inert — every call
    // to pid_for_window returns std::nullopt rather than throwing, since
    // this is a best-effort fallback, not a required capability.
    X11PidResolver();
    ~X11PidResolver();
    X11PidResolver(const X11PidResolver&) = delete;
    X11PidResolver& operator=(const X11PidResolver&) = delete;

    std::optional<long> pid_for_window(unsigned long window_id);

private:
    void* display_ = nullptr;
    long net_wm_pid_atom_ = 0;
};
