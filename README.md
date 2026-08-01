# tileroot

Save and restore your tiling window manager's layout across a reboot or crash — sway, Hyprland, and i3, all with one tool.

## Overview

Every sway/Hyprland/i3 user eventually hand-rolls a shell script that calls `swaymsg`/`hyprctl` + `jq` to save their window layout, and every one of those scripts breaks the next time the WM updates. The one existing attempt at a real tool, [`hypr-session-restore`](https://github.com/UpayanChatterjee/hypr-session-restore), is Hyprland-only and — by its own README — can't reconstruct the actual tiling layout tree, because Hyprland's IPC doesn't expose one. [`i3-resurrect`](https://github.com/JonnyHaystack/i3-resurrect) solves this well for i3, but nothing unifies i3, sway, and Hyprland in one tool.

`tileroot` does. Sway and i3 implement the *same* IPC wire protocol, and both expose `append_layout` — a native mechanism for pre-building a placeholder container tree with per-window match criteria, which relaunched processes then "swallow" into automatically. `restore` uses this directly, so the tiling structure that comes back is the actual tree, not a geometry approximation — live-verified: dump a nested layout, close everything, restore, dump again, byte-for-byte identical structure. Hyprland's IPC genuinely doesn't expose a split tree at all, so there `tileroot` is honest about the ceiling: `dump` records every tiled window's exact geometry in left-to-right order and `restore` replays that geometry directly, rather than pretending to reconstruct a tree that doesn't exist.

![demo](demo.gif)

*Recorded live against a real running Hyprland session — not staged.*

## Install

**AUR (Arch, recommended for this audience):**
```
paru -S tileroot
```

**Manual (any distro):**
```
curl -sL https://github.com/Hinikaa/tileroot/releases/latest/download/install.sh | sh
```

**From source:**
```
git clone https://github.com/Hinikaa/tileroot
cd tileroot
make tileroot
```
Requires a C++17 compiler, the `nlohmann-json` header (`nlohmann-json` on Arch, `nlohmann-json3-dev` on Debian/Ubuntu), and `libX11` (`libx11` on Arch, `libx11-dev` on Debian/Ubuntu — used only to resolve a window's PID via `_NET_WM_PID` on i3, which doesn't include it in `GET_TREE` the way sway does; already present on any system that can run i3 as its WM).

## Usage

```
tileroot dump [--workspace NAME] [-o FILE] [--pretty] [--verbose]
tileroot restore [FILE] [--dry-run] [--verbose]
```

`dump` with no `-o` prints session JSON to stdout; `-o FILE` writes it atomically (a crash or Ctrl-C mid-write never corrupts an existing save). `--pretty` prints a human-readable box-drawing tree instead — good for a screenshot, not meant to be parsed. `restore` reads `session.json` in the current directory by default, or a path you give it; `--dry-run` shows what it *would* do without launching anything.

`restore` refuses to run if the target workspace already has windows — it never merges or clobbers. If some windows can't be matched within 5 seconds (app didn't start in time, binary went missing) it logs a warning, places everything it could, and exits non-zero so scripts can detect a partial restore.

## Example

```
# On workspace 1: a browser on the left, a terminal on the right
$ tileroot dump -o ~/.config/tileroot/work.json

# ...reboot, or close everything...

$ tileroot restore ~/.config/tileroot/work.json
2 of 2 windows restored
```

## License note on i3-resurrect

`i3-resurrect` is GPLv3, which is not compatible with this project's MIT license — no code from it is used here. `tileroot`'s sway/i3 backend is a clean-room implementation against the *public* i3-ipc wire protocol specification (the framing and message types sway/i3 themselves document, not `i3-resurrect`'s source), and the window-matching logic is independently designed.

## Security

`restore` launches processes from the saved `cmdline` in your session file. Because sharing session files (dotfiles, "here's my rice") is exactly what this tool is for, `cmdline` is stored as an argument array and executed directly via `posix_spawn` — **never** through a shell. A session file with something like `["echo", "; rm -rf ~"]` in it prints that string literally; it does not run `rm`. See `test_matcher.cpp::test_spawn_does_not_shell_interpret_cmdline` for the regression test that guards this.

## Status

- **Hyprland backend: live-validated.** Tested end-to-end against a real running Hyprland session — `dump`, `--pretty`, atomic `-o` writes, schema validation, and the wm-mismatch/occupied-workspace refusal paths all confirmed working against real windows.
- **i3 backend (`I3ProtocolBackend`): live-validated against a real headless i3 session (v0.2.0).** Not just "should work" — actually tested: `dump`, `--pretty`, and a full `restore` round-trip including the `append_layout` tree reconstruction and floating-window restoration, verified byte-for-byte against the original layout. Two real bugs were found and fixed in the process (see below) that no amount of reading the protocol spec would have caught.
- **Sway backend: shares 100% of the same code path as i3** (same `I3ProtocolBackend` class — sway implements i3's IPC directly, not a compatible reimplementation of it), so the fixes below should apply equally. Not independently re-verified against a live sway session by the maintainer, though — no sway binary available in this environment. If you run sway, testing this specific version is genuinely useful; the previous version had a real bug (below) that only surfaced on real sway.
- **What got fixed getting i3 working, in order of discovery:**
  1. A real sway user reported `dump` only capturing scratchpad windows (v0.1.1) — `GET_TREE`'s `focused` flag lives on the focused leaf window, not its ancestor workspace, so detection silently fell through to sway's internal `__i3` pseudo-output. Fixed via `GET_WORKSPACES` instead of inferring focus from `GET_TREE`.
  2. Testing against real i3 found workspaces aren't direct children of an output in `GET_TREE` — they're nested inside a `"content"` wrapper alongside dock areas for bars. Fixed with a recursive search instead of assuming a fixed nesting depth.
  3. i3's `GET_TREE` doesn't include a `"pid"` field on window nodes the way sway's does, so cmdline recovery silently failed for every window. Fixed by resolving the PID via the X11 `_NET_WM_PID` property instead (see the `libX11` dependency above).
  4. `restore` was only doing geometry-based placement (position + resize) for all three backends — true for Hyprland by necessity, but sway/i3 don't need that limitation. Rebuilt to use `append_layout` for real tree reconstruction, and separately found that a relaunched window defaults to tiled regardless of how it was saved — floating state now has to be set explicitly.
- **Multi-monitor on Hyprland:** not specifically validated. Single-monitor is the tested case.
- **Multi-monitor / multi-workspace on sway/i3:** the `dump --workspace NAME` path searches every real output, and `restore` refuses per-workspace if occupied, but this hasn't been tested with more than one real output.
- **Scratchpad windows (sway/i3) are still not captured** by `dump` — the `__i3_scratch` workspace is now correctly excluded from being mistaken for a real workspace, but its contents aren't read into the session yet. Separate from the bugs above; a real feature gap, not a bug.

None of this is silent — every gap above is either a loud error at runtime or a documented limitation, never a quiet wrong answer.

## Stats for nerds

| | |
|---|---|
| Lines of C++ (core + tests) | 1,981 |
| Test functions / assertions | 14 / 36 |
| Runtime dependencies | 2 (`nlohmann/json` header-only, `libX11`) |
| Binary size (release, unstripped) | 453 KB |
| Binary size (stripped) | 371 KB |
| Compiler warnings (`-Wall -Wextra`) | 0 |
| Shells that never see your `cmdline` | all of them (see [Security](#security)) |
| Design-review rounds before implementation | 3 rounds, 14 issues caught before a line of code existed |
| Real bugs found by real testing vs. by reading the spec | 4 vs. 0 |

## License

MIT — see [LICENSE](LICENSE).
