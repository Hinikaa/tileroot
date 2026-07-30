#!/bin/sh
# Universal installer: curl -sL https://.../install.sh | sh
# Detects arch, downloads the matching GitHub Release asset, installs to
# ~/.local/bin. Only x86_64 Linux is published today — see the release
# workflow for the asset-naming convention this script depends on.
set -eu

REPO="Hinikaa/tileroot"
ARCH=$(uname -m)
OS=$(uname -s)

if [ "$OS" != "Linux" ]; then
    echo "error: tileroot only supports Linux (tiling WM IPC is Linux-specific)" >&2
    exit 1
fi
if [ "$ARCH" != "x86_64" ]; then
    echo "error: no prebuilt binary for architecture: $ARCH (try building from source: make tileroot)" >&2
    exit 1
fi

ASSET="tileroot-linux-x86_64.tar.gz"
URL="https://github.com/${REPO}/releases/latest/download/${ASSET}"
DEST="${HOME}/.local/bin"

mkdir -p "$DEST"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "Downloading $URL ..."
curl -sL "$URL" -o "$TMPDIR/$ASSET"
tar -xzf "$TMPDIR/$ASSET" -C "$TMPDIR"
mv "$TMPDIR/tileroot" "$DEST/tileroot"
chmod +x "$DEST/tileroot"

echo "Installed to $DEST/tileroot"
case ":$PATH:" in
    *":$DEST:"*) ;;
    *) echo "Note: $DEST is not on your PATH. Add it, e.g.: export PATH=\"\$PATH:$DEST\"" ;;
esac
