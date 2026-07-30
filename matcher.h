#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "session.h"

// Handles the actual hard problem in `restore`: launch a saved window's
// process and match the resulting live window back to its saved tree slot.
// See design doc "Restore's actual hard problem — window matching".
class WindowMatcher {
public:
    struct Config {
        std::chrono::milliseconds timeout{5000};   // Section 2A/6D: configurable so tests don't wait 5s
        std::chrono::milliseconds poll_interval{50};
    };

    // Returns every currently-visible window id of `wm_class`, in the WM's
    // own discovery order. The real implementation queries the live WM
    // tree; tests inject a fake that simulates windows appearing over
    // successive calls. The matcher — not this function — is responsible
    // for tracking which ids have already been claimed, so a real
    // implementation can be a pure "list windows of this class" query.
    using ListWindowsFn = std::function<std::vector<std::string>(const std::string& wm_class)>;

    // Places a matched window id into its saved tree slot (moves/resizes
    // it via WM IPC in the real implementation). `slot_index` is the
    // position in the `slots` vector passed to restore_all — callers that
    // need per-slot context restore_all doesn't carry itself (e.g. which
    // workspace a slot belongs to, for a multi-workspace restore) look it
    // up by index rather than WindowInfo growing fields unrelated to a
    // saved window's own shape.
    using PlaceFn = std::function<void(const std::string& window_id, size_t slot_index, const WindowInfo& slot)>;

    enum class Status { Placed, LaunchFailed, TimedOut, Interrupted };

    struct Result {
        Status status;
        std::string wm_class;
        std::string window_id;  // populated only when status == Placed
    };

    WindowMatcher(Config cfg, ListWindowsFn list_windows, PlaceFn place);

    // Launches every slot's process in parallel (Section 7A — a 10-window
    // restore takes roughly as long as the slowest single window, not the
    // sum), then polls until every pending slot is matched or the shared
    // timeout budget expires. `interrupted`, if non-null, is checked each
    // poll tick — on SIGINT (Section 4A) the loop stops early and returns
    // Interrupted for every slot that hadn't yet placed. Never throws for
    // a per-slot failure — every slot gets a Result (Section 2: log-and-skip).
    std::vector<Result> restore_all(const std::vector<WindowInfo>& slots,
                                     const std::atomic<bool>* interrupted = nullptr);

private:
    Config cfg_;
    ListWindowsFn list_windows_;
    PlaceFn place_;
};

// Launches `cmdline` via posix_spawn (argv directly — never a shell, see
// design doc Section 3A). Returns the child pid on success. Returns
// std::nullopt immediately (no polling, no timeout) if posix_spawn itself
// fails — e.g. the binary doesn't exist — which is what lets
// WindowMatcher distinguish LaunchFailed from TimedOut (Section 2D).
#include <sys/types.h>
#include <optional>
std::optional<pid_t> spawn_process(const std::vector<std::string>& cmdline);
