#include "x11_pid.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace {
// Xlib's default error handler prints the error and calls exit() — so a
// window closing between GET_TREE and this property query (a completely
// normal race on a live desktop) would crash the whole `tileroot dump`
// process on a BadWindow error. Swallow protocol errors instead and let
// pid_for_window's own return-code check (rc != Success) handle it as a
// missed-window case, same as any other unrecoverable window.
int ignore_x_errors(Display*, XErrorEvent*) { return 0; }
}  // namespace

X11PidResolver::X11PidResolver() {
    Display* dpy = XOpenDisplay(nullptr);  // nullptr = use $DISPLAY
    if (!dpy) return;                      // inert resolver — no X11 available
    XSetErrorHandler(ignore_x_errors);     // process-wide; safe to set repeatedly
    display_ = dpy;
    net_wm_pid_atom_ = static_cast<long>(XInternAtom(dpy, "_NET_WM_PID", False));
}

X11PidResolver::~X11PidResolver() {
    if (display_) XCloseDisplay(static_cast<Display*>(display_));
}

std::optional<long> X11PidResolver::pid_for_window(unsigned long window_id) {
    if (!display_) return std::nullopt;
    Display* dpy = static_cast<Display*>(display_);

    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;

    int rc = XGetWindowProperty(dpy, static_cast<Window>(window_id), static_cast<Atom>(net_wm_pid_atom_), 0, 1,
                                 False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
    if (rc != Success || !prop) return std::nullopt;

    std::optional<long> result;
    if (actual_type == XA_CARDINAL && nitems == 1) {
        result = static_cast<long>(*reinterpret_cast<unsigned long*>(prop));
    }
    XFree(prop);
    return result;
}
