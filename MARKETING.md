# Marketing tileroot

Not a checklist to blast through in one afternoon — spread over 2-3 weeks so
each post has room to breathe and you have time to respond to comments (that
responsiveness is itself what gets a repo starred, more than the post copy).

Ordered by friction, lowest first. Do tier 1 immediately (this week), tier 2
once the repo has a few stars and a working demo people can point to, tier 3
only if you want the extra reach and are OK with some risk.

---

## Tier 1 — zero karma needed, do these first

These aren't gated by account age or reputation. Nobody can downvote a pull
request into oblivion.

### 1. `hyprland-community/awesome-hyprland`
Real, active list: https://github.com/hyprland-community/awesome-hyprland
Open a PR adding one line under the relevant section (something like
"Utilities" or "Session/Workspace"):
```markdown
- [tileroot](https://github.com/Hinikaa/tileroot) - Save and restore your tiling layout across sway, Hyprland, and i3.
```
This is a high-intent audience — people browsing awesome-lists are actively
looking for tools, not scrolling past ragebait. Zero risk, permanent
backlink, and it's how a lot of small Hyprland tools actually get their
first real traffic.

### 2. i3/sway equivalents
- `mbfraga/awesome-i3wm` — https://github.com/mbfraga/awesome-i3wm
- `Syphdias/awesomeish-i3` — https://github.com/Syphdias/awesomeish-i3
Same PR-based process. Do this once the i3 backend is actually validated
live (don't advertise i3 support before it's real — see README Status).

### 3. Terminal Trove
https://terminaltrove.com/new/ — "Post a Tool" submission form, no account
reputation gate. This audience is specifically people who browse for CLI
tools, i.e. exactly the "give me something cool for my terminal" crowd.
Use the demo GIF — Terminal Trove listings are very screenshot/GIF-driven.

### 4. GitHub topics (on the repo itself)
Add these to the repo's About section (gear icon on the repo page):
`hyprland`, `sway`, `i3wm`, `tiling-window-manager`, `session-manager`,
`cli`, `cpp`, `linux`, `wayland`, `dotfiles`
People genuinely browse `github.com/topics/hyprland` — this is free,
permanent, and compounds over time as the repo gets stars.

### 5. AUR
Once you actually publish the `PKGBUILD` (see `RELEASING.md`), the AUR's
own search/browse surfaces it to Arch users with zero posting required.
This audience is your exact target — the tiling-WM crowd is overwhelmingly
Arch-based.

### 6. Fosstodon / Mastodon
Post from a Fosstodon account (or any Mastodon instance — federation means
it reaches Fosstodon regardless) with the demo GIF attached directly (not
linked) and these hashtags: `#hyprland #linux #foss #opensource #tilingwm
#cli`. No karma system, strong resharing culture among FOSS/Linux people,
and Mastodon's Linux corner is unusually tool-curious and unusually not
prone to pile-on dogpiling compared to Reddit.

**Suggested post text:**
> Lost your Hyprland/sway layout on every reboot? Built tileroot: dumps
> your tiling tree, restores it exactly (sway) or by geometry replay
> (Hyprland). cmdline is exec'd via argv, never a shell, so sharing session
> files is actually safe. MIT, C++17, no daemon.
>
> https://github.com/Hinikaa/tileroot
>
> #hyprland #linux #foss #opensource #tilingwm

### 7. Hyprland official Discord / sway community Discord
Both have a `#showcase` or `#projects`-style channel meant exactly for
this. No reputation system, direct audience, and people there will
actually try it and give real feedback (which then becomes your first
GitHub issues — a good sign, not a bad one).

### 8. Arch Linux Forums
https://bbs.archlinux.org — there's a community-projects/showcase-style
subforum. Low pile-on risk (this crowd is technical and civil by Reddit
standards), and it's indexed by search engines, so it keeps paying off
long after the post scrolls away.

---

## Tier 2 — some friction, worth it once the repo has traction

### r/hyprland, r/swaywm, r/i3wm
Smaller, narrower subreddits than r/unixporn — less driveby traffic means
less pile-on risk if a post doesn't land. Check each subreddit's sidebar
for a minimum karma/account-age rule before posting (some tiling-WM
subreddits run AutoMod filters); if you're gated, tier 1 posts will have
built you a little organic karma by the time you get here anyway.

**Suggested title:**
> I got tired of losing my Hyprland/sway layout every reboot, so I built a
> cross-WM session manager that actually reconstructs the tiling tree

**Suggested body:**
> Every tiling-WM session tool I found either only works for one WM or
> can't reconstruct the actual tiling layout (Hyprland's IPC doesn't
> expose the split tree, so tools built for it fall back to geometry
> guessing — [hypr-session-restore](https://github.com/UpayanChatterjee/hypr-session-restore)
> says this outright in its own README).
>
> Sway and i3 actually share the same IPC protocol though, so exact
> reconstruction there is solved — `i3-resurrect` already proves it works
> for i3. Nothing unified all three, so I built `tileroot`:
>
> - Sway/i3: exact split-tree reconstruction
> - Hyprland: honest geometry replay (documented as best-effort, not
>   pretending to be exact)
> - `cmdline` is exec'd via argv, never a shell — sharing session files
>   (which is the whole point) can't become a code-execution vector
> - No daemon, single static binary, AUR package
>
> [demo gif] [repo link]
>
> Feedback and issues very welcome — sway backend is written against the
> public IPC spec but I'd love someone to actually beat on it on a real
> sway setup.

### r/commandline
Similar framing, lean harder on the CLI-tool-craft angle (the box-drawing
`--pretty` output, shell completions, the atomic-write detail) — this
crowd cares about tool polish more than the WM-specific pitch.

### r/opensource
Frame it as "first real release of a new tool" rather than the WM pitch —
this audience responds to the story of shipping something, not the
technical specifics.

---

## Tier 3 — optional, higher variance

### Show HN
Correcting an assumption: HN does **not** require pre-built karma to
submit — any account, including a brand-new one, can post a Show HN
immediately. What you were probably thinking of is Reddit's per-subreddit
AutoMod karma/age gates (real, and correctly avoided above) — HN doesn't
have that mechanism. The actual risk on HN isn't a karma wall, it's just
variance: a fresh, narrow tool can land anywhere from the front page to
zero traction, and HN's comment section skews toward "well actually"
nitpicking on technical claims. Given the security work you did here
(3A's argv-exec fix), that nitpicking will mostly work in your favor if
someone digs into the code.

**Suggested title:**
> Show HN: tileroot – save/restore tiling WM layouts (sway, Hyprland, i3)

**Suggested first comment (post this yourself right after submitting —
standard Show HN practice, explains the "why" the title can't fit):**
> Built this after losing a Hyprland layout for the third time in a week.
> The interesting bit: sway/i3 share the same IPC wire protocol, so exact
> layout reconstruction there is basically solved (i3-resurrect proved
> it for i3 alone). Hyprland's IPC doesn't expose a real split tree
> though, so instead of faking it, tileroot is upfront that Hyprland gets
> geometry replay, not exact reconstruction — the README says so
> directly rather than burying it.
>
> Also spent real effort on the boring-but-important part: cmdline is
> stored as an argv array and exec'd via posix_spawn, never a shell —
> since sharing session files (dotfiles-style) is the actual use case,
> a shared file with shell metacharacters in it can't become a code-exec
> vector. There's a regression test specifically for this.
>
> Sway backend is written against the public protocol spec but not yet
> validated on a live sway session (I only had Hyprland to test against)
> — if anyone here runs sway, I'd genuinely appreciate someone kicking
> the tires and filing issues.

### r/unixporn
Real traffic potential (huge, visually-driven audience), but confirmed to
run karma/account-age AutoMod gating, and its scale means more exposure to
drive-by downvotes if the post doesn't immediately land. Skip for now per
your own call — revisit once tier 1/2 have organically built some account
history, or skip permanently, it's not required for this to succeed.

---

## What not to do

- **Don't post everywhere the same day.** Spread across 2-3 weeks. A GitHub
  star graph with one spike and then silence reads as abandoned; a slow
  climb across several communities reads as a live project.
- **Don't argue with critics.** If someone nitpicks (e.g. "why not just use
  i3-resurrect"), answer once, factually, and move on. Arguing is what
  turns a neutral thread into a pile-on.
- **Don't cross-post identical text.** Tailor the framing per audience (WM
  pitch for r/hyprland, CLI-craft pitch for r/commandline, technical
  deep-dive for HN/dev.to) — copy-pasted posts read as spam even when the
  tool is genuinely good.
- **Don't oversell the sway backend before it's live-validated.** The
  README is honest about this; keep the marketing copy honest too. If
  someone on sway hits a bug, that's expected and fine — say so, fix it,
  and you've just turned a stranger into your first real contributor.
