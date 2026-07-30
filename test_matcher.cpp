#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "matcher.h"

// Section 6A — the most important test in this project: proves a cmdline
// array element containing shell metacharacters is passed to the child as
// ONE literal argument, never interpreted by a shell (Section 3A). If this
// regresses, a session.json shared/downloaded from someone else becomes an
// arbitrary-command-execution vector.
static void test_spawn_does_not_shell_interpret_cmdline() {
    std::string marker = "/tmp/tileroot_test_pwned_marker_" + std::to_string(getpid());
    std::remove(marker.c_str());

    // If cmdline were ever concatenated into a shell string, the ';' here
    // would end the echo and run `touch <marker>` as a second command.
    // Executed via execve directly, /bin/echo receives this whole string
    // as a single literal argv[1] and just prints it.
    auto pid = spawn_process({"/bin/echo", "; touch " + marker});
    assert(pid.has_value());
    int status = 0;
    waitpid(*pid, &status, 0);

    FILE* f = std::fopen(marker.c_str(), "r");
    assert(f == nullptr);  // marker must NOT exist — no shell interpretation occurred
    if (f) std::fclose(f);
    std::remove(marker.c_str());
}

static void test_spawn_nonexistent_binary_returns_nullopt() {
    auto pid = spawn_process({"/nonexistent/tileroot-test-binary-xyz123"});
    assert(!pid.has_value());
}

static void test_spawn_empty_cmdline_returns_nullopt() {
    auto pid = spawn_process({});
    assert(!pid.has_value());
}

// Section 2D — LaunchFailed (binary missing) must be reported immediately,
// not conflated with TimedOut (binary ran, window never appeared).
static void test_restore_all_distinguishes_launch_failed_from_timeout() {
    WindowMatcher::Config cfg;
    cfg.timeout = std::chrono::milliseconds(80);
    cfg.poll_interval = std::chrono::milliseconds(10);

    // list_windows_ never returns a match for anything — simulates "process
    // launched fine but its window never mapped."
    WindowMatcher matcher(cfg, [](const std::string&) { return std::vector<std::string>{}; },
                           [](const std::string&, size_t, const WindowInfo&) {});

    std::vector<WindowInfo> slots = {
        {"nonexistent-app", {"/nonexistent/tileroot-test-binary-xyz123"}, 0, 0, 100, 100},
        {"true-app", {"/usr/bin/true"}, 0, 0, 100, 100},
    };
    auto results = matcher.restore_all(slots);
    assert(results.size() == 2);
    assert(results[0].status == WindowMatcher::Status::LaunchFailed);
    assert(results[1].status == WindowMatcher::Status::TimedOut);
}

// Section 2A/6D — a short configured timeout must actually be honored
// (this test must run fast, not wait out a hardcoded 5s).
static void test_restore_all_honors_configured_timeout() {
    WindowMatcher::Config cfg;
    cfg.timeout = std::chrono::milliseconds(60);
    cfg.poll_interval = std::chrono::milliseconds(10);
    WindowMatcher matcher(cfg, [](const std::string&) { return std::vector<std::string>{}; },
                           [](const std::string&, size_t, const WindowInfo&) {});

    auto start = std::chrono::steady_clock::now();
    auto results = matcher.restore_all({{"app", {"/usr/bin/true"}, 0, 0, 10, 10}});
    auto elapsed = std::chrono::steady_clock::now() - start;

    assert(results[0].status == WindowMatcher::Status::TimedOut);
    assert(elapsed < std::chrono::milliseconds(500));  // nowhere near a real 5s default
}

// Section 2 (window matching, FIFO rule) — two slots share a class; the
// first slot (in slot order) must claim the first window returned by
// list_windows_, the second slot the second.
static void test_restore_all_fifo_matches_same_class_in_slot_order() {
    WindowMatcher::Config cfg;
    cfg.timeout = std::chrono::milliseconds(200);
    cfg.poll_interval = std::chrono::milliseconds(5);

    // Simulates two "term" windows already visible by the first poll tick.
    auto list_windows = [](const std::string& wm_class) -> std::vector<std::string> {
        if (wm_class == "term") return {"win-A", "win-B"};
        return {};
    };
    std::vector<std::pair<std::string, std::string>> placements;  // (window_id, expected slot marker)
    auto place = [&placements](const std::string& window_id, size_t /*slot_index*/, const WindowInfo& slot) {
        placements.emplace_back(window_id, slot.wm_class + ":" + std::to_string(slot.x));
    };

    WindowMatcher matcher(cfg, list_windows, place);
    std::vector<WindowInfo> slots = {
        {"term", {"/usr/bin/true"}, 0, 0, 10, 10},   // slot 0 — should claim win-A
        {"term", {"/usr/bin/true"}, 1, 0, 10, 10},   // slot 1 — should claim win-B
    };
    auto results = matcher.restore_all(slots);

    assert(results[0].status == WindowMatcher::Status::Placed);
    assert(results[1].status == WindowMatcher::Status::Placed);
    assert(results[0].window_id == "win-A");
    assert(results[1].window_id == "win-B");
    assert(placements.size() == 2);
    assert(placements[0].first == "win-A" && placements[0].second == "term:0");
    assert(placements[1].first == "win-B" && placements[1].second == "term:1");
}

// Section 4A — SIGINT mid-restore must leave already-placed slots alone
// and mark the rest Interrupted, not TimedOut, so a caller can tell the
// difference between "gave up" and "user cancelled."
static void test_restore_all_respects_interrupt_flag() {
    WindowMatcher::Config cfg;
    cfg.timeout = std::chrono::milliseconds(500);
    cfg.poll_interval = std::chrono::milliseconds(5);
    WindowMatcher matcher(cfg, [](const std::string&) { return std::vector<std::string>{}; },
                           [](const std::string&, size_t, const WindowInfo&) {});

    std::atomic<bool> interrupted{true};  // already set before restore_all starts
    auto results = matcher.restore_all({{"app", {"/usr/bin/true"}, 0, 0, 10, 10}}, &interrupted);
    assert(results[0].status == WindowMatcher::Status::Interrupted);
}

int main() {
    test_spawn_does_not_shell_interpret_cmdline();
    test_spawn_nonexistent_binary_returns_nullopt();
    test_spawn_empty_cmdline_returns_nullopt();
    test_restore_all_distinguishes_launch_failed_from_timeout();
    test_restore_all_honors_configured_timeout();
    test_restore_all_fifo_matches_same_class_in_slot_order();
    test_restore_all_respects_interrupt_flag();
    std::cout << "ok" << std::endl;
    return 0;
}
