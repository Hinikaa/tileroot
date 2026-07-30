# tileroot

Save and restore your tiling window manager's layout across a reboot or crash — sway, Hyprland, and (soon) i3, all with one tool.

## Overview

Every sway/Hyprland/i3 user eventually hand-rolls a shell script that calls `swaymsg`/`hyprctl` + `jq` to save their window layout, and every one of those scripts breaks the next time the WM updates. The one existing attempt at a real tool, [`hypr-session-restore`](https://github.com/UpayanChatterjee/hypr-session-restore), is Hyprland-only and — by its own README — can't reconstruct the actual tiling layout tree, because Hyprland's IPC doesn't expose one. [`i3-resurrect`](https://github.com/JonnyHaystack/i3-resurrect) solves this well for i3, but nothing unifies i3, sway, and Hyprland in one tool.

`tileroot` does. Sway (and i3, once it lands) implement the *same* IPC wire protocol, so exact layout reconstruction there is a solved problem, not a research project. Hyprland's IPC genuinely doesn't expose a split tree, so `tileroot` is honest about it: on Hyprland, `dump` records every tiled window's exact geometry in left-to-right order and `restore` replays that geometry directly, rather than pretending to reconstruct a tree that doesn't exist.

```
$ tileroot dump --pretty
1 (DP-1)
└── split_h
    ├── org.gnome.Nautilus
    └── com.mitchellh.ghostty
```

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
Requires a C++17 compiler and the `nlohmann-json` header (`nlohmann-json` on Arch, `nlohmann-json3-dev` on Debian/Ubuntu).

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

## Security

`restore` launches processes from the saved `cmdline` in your session file. Because sharing session files (dotfiles, "here's my rice") is exactly what this tool is for, `cmdline` is stored as an argument array and executed directly via `posix_spawn` — **never** through a shell. A session file with something like `["echo", "; rm -rf ~"]` in it prints that string literally; it does not run `rm`. See `test_matcher.cpp::test_spawn_does_not_shell_interpret_cmdline` for the regression test that guards this.

## Status

- **Hyprland backend: live-validated.** Written against and tested end-to-end against a real running Hyprland 0.56 session — `dump`, `--pretty`, atomic `-o` writes, schema validation, and the wm-mismatch/occupied-workspace refusal paths all confirmed working against real windows.
- **Sway/i3 backend (`I3ProtocolBackend`): written against the public i3-ipc/sway-ipc specification, not yet validated against a live sway session.** The wire protocol framing and JSON tree-walking logic are unit-testable and covered by the socket-framing code path, but end-to-end validation against a real sway instance is a known open item — see the repo's tracked next steps.
- **i3 support:** not yet wired up (it reuses `I3ProtocolBackend` — same protocol as sway — so it's expected to be a small addition once sway is validated live, not a rewrite).
- **Multi-monitor on Hyprland:** not specifically validated. Single-monitor is the tested case.
- **Scratchpad windows (sway/i3):** not yet captured — the scratchpad workspace's tree shape needs live validation before this backend trusts it.

None of this is silent — every gap above is either a loud error at runtime or a documented limitation, never a quiet wrong answer.

## License

MIT — see [LICENSE](LICENSE).
