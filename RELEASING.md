# Releasing

1. Bump `pkgver` in `PKGBUILD` and the version string in `main.cpp` (`--version` output).
2. `git tag vX.Y.Z && git push origin vX.Y.Z` — this triggers `.github/workflows/release.yml`, which runs the test suite, builds, smoke-tests (`--version`/`--help`), and publishes `tileroot-linux-x86_64.tar.gz` to GitHub Releases.
3. Update the AUR `PKGBUILD` (`pkgver`, `sha256sums` — compute with `updpkgsums` or `sha256sum` against the tagged tarball) and push to the AUR repo.

## If a release is broken

- **GitHub Release:** mark it as a pre-release (or delete it) from the Releases page. `install.sh` always pulls `.../releases/latest/download/...`, so removing/unpublishing a bad release immediately stops new installs from getting it. Existing installs are unaffected until they reinstall.
- **AUR:** bump `pkgver`/`pkgrel` with the fix and push — AUR has no separate "rollback" mechanism, a new version simply supersedes the broken one. Users who already built the bad version keep it until they update (`paru -Syu`).
- **No compensating action needed for the source tag itself** — a bad git tag is harmless on its own; only the *published* release/AUR artifacts matter for what users actually get.
