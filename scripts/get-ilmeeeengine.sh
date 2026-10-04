#!/bin/sh
# Ilmeee Engine — one-line network installer.
#
#   curl -fsSL https://raw.githubusercontent.com/hylmithecoder/GameEngine/main/scripts/get-ilmeeeengine.sh | sudo sh
#
# Downloads the release archive for this machine's architecture, verifies its
# checksum, and runs the install.sh that ships inside it.
set -eu

REPO="${ILMEEE_REPO:-hylmithecoder/GameEngine}"
VERSION="${ILMEEE_VERSION:-latest}"
PREFIX="${PREFIX:-/opt/ilmeeeengine}"
BINDIR="${BINDIR:-/usr/local/bin}"

ARCH=$(uname -m)
case "$ARCH" in
    x86_64|amd64)  ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
    *) echo "error: unsupported architecture: $ARCH" >&2; exit 1 ;;
esac
[ "$(uname -s)" = Linux ] || { echo "error: Linux only." >&2; exit 1; }

fetch() {
    if command -v curl >/dev/null 2>&1; then curl -fsSL "$1" -o "$2"
    elif command -v wget >/dev/null 2>&1; then wget -qO "$2" "$1"
    else echo "error: need curl or wget." >&2; exit 1
    fi
}

if [ "$VERSION" = latest ]; then
    base="https://github.com/$REPO/releases/latest/download"
    # The latest-download endpoint resolves the tag for us, but we still need
    # the versioned filename, so read it out of the checksum manifest.
    tmp_manifest=$(mktemp)
    fetch "$base/SHA256SUMS" "$tmp_manifest" ||
        { echo "error: no SHA256SUMS in the latest release of $REPO." >&2; exit 1; }
    ASSET=$(awk -v a="linux-$ARCH" '$2 ~ a {print $2}' "$tmp_manifest" | head -1)
    rm -f "$tmp_manifest"
    [ -n "$ASSET" ] || { echo "error: no linux-$ARCH archive in the latest release." >&2; exit 1; }
else
    base="https://github.com/$REPO/releases/download/$VERSION"
    ASSET="ilmeeeengine-${VERSION#v}-linux-$ARCH.tar.gz"
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

echo "Downloading $ASSET"
fetch "$base/$ASSET" "$TMP/$ASSET"

if fetch "$base/$ASSET.sha256" "$TMP/$ASSET.sha256" 2>/dev/null &&
   command -v sha256sum >/dev/null 2>&1; then
    ( cd "$TMP" && sha256sum -c "$ASSET.sha256" >/dev/null ) ||
        { echo "error: checksum mismatch — refusing to install." >&2; exit 1; }
    echo "Checksum verified."
else
    echo "warning: checksum not verified (no .sha256 published or sha256sum missing)." >&2
fi

tar -C "$TMP" -xzf "$TMP/$ASSET"
DIR=$(find "$TMP" -maxdepth 1 -type d -name 'ilmeeeengine-*' | head -1)
[ -n "$DIR" ] || { echo "error: unexpected archive layout." >&2; exit 1; }

PREFIX="$PREFIX" BINDIR="$BINDIR" sh "$DIR/install.sh"
