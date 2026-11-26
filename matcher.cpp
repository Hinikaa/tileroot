#include "matcher.h"

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>
#include <unordered_map>
#include <unordered_set>

extern char** environ;

std::optional<pid_t> spawn_process(const std::vector<std::string>& cmdline) {
    if (cmdline.empty()) return std::nullopt;

    // posix_spawn wants char* argv[], not const std::string — build a
    // throwaway mutable copy. Never system()/popen()/sh -c: cmdline came
    // from a session.json that may have been shared/downloaded (Section 3A).
    std::vector<char*> argv;
    argv.reserve(cmdline.size() + 1);
    for (const auto& arg : cmdline) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv.data(), environ);
    if (rc != 0) {
        // posix_spawn(p) reports "binary not found / not executable" as a
        // direct return code — no pipe tricks needed to detect it
        // immediately, which is exactly what lets us report LaunchFailed
        // without waiting out the match timeout (Section 2D).
        return std::nullopt;
    }
    return pid;
}

WindowMatcher::WindowMatcher(Config cfg, ListWindowsFn list_windows, PlaceFn place)
    : cfg_(cfg), list_windows_(std::move(list_windows)), place_(std::move(place)) {}

std::vector<WindowMatcher::Result> WindowMatcher::restore_all(const std::vector<WindowInfo>& slots,
                                                                const std::atomic<bool>* interrupted,
                                                                const std::unordered_set<std::string>& pre_existing) {
    std::vector<Result> results(slots.size());
    std::vector<size_t> pending;  // indices into slots/results still waiting for a match

    for (size_t i = 0; i < slots.size(); ++i) {
        if (spawn_process(slots[i].cmdline)) {
            pending.push_back(i);
        } else {
            results[i] = {Status::LaunchFailed, slots[i].wm_class, ""};
        }
    }

    // Seed claimed with pre-existing windows so windows already living on
    // other workspaces aren't mistaken for newly launched ones and stolen.
    std::unordered_set<std::string> claimed = pre_existing;
    const auto deadline = std::chrono::steady_clock::now() + cfg_.timeout;

    while (!pending.empty() && std::chrono::steady_clock::now() < deadline) {
        if (interrupted && interrupted->load()) {
            for (size_t i : pending) results[i] = {Status::Interrupted, slots[i].wm_class, ""};
            return results;
        }

        // Cache list_windows_ per wm_class within a single poll tick so
        // restoring N windows of the same class incurs 1 IPC round-trip per
        // tick instead of N.
        std::unordered_map<std::string, std::vector<std::string>> tick_cache;
        std::vector<size_t> still_pending;
        for (size_t i : pending) {
            const std::string& wm_class = slots[i].wm_class;
            auto it = tick_cache.find(wm_class);
            if (it == tick_cache.end()) {
                it = tick_cache.emplace(wm_class, list_windows_(wm_class)).first;
            }
            // list_windows_ returns windows of this class in discovery
            // order; the first one NOT already claimed by an earlier slot
            // fills this slot — FIFO across same-class slots, in slot order
            // (design doc: "first new window of that class fills the first
            // saved slot of that class").
            std::string matched_id;
            for (const auto& id : it->second) {
                if (claimed.find(id) == claimed.end()) {
                    matched_id = id;
                    break;
                }
            }
            if (!matched_id.empty()) {
                claimed.insert(matched_id);
                place_(matched_id, i, slots[i]);
                results[i] = {Status::Placed, slots[i].wm_class, matched_id};
            } else {
                still_pending.push_back(i);
            }
        }
        pending = std::move(still_pending);
        if (!pending.empty()) std::this_thread::sleep_for(cfg_.poll_interval);
    }

    for (size_t i : pending) results[i] = {Status::TimedOut, slots[i].wm_class, ""};
    return results;
}
