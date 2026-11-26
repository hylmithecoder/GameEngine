#!/usr/bin/env bash
# Stages the Linux build into a relocatable folder and packs it as a tarball.
#
# CI: .github/workflows/release-linux.yaml (Ubuntu 24.04). Locally (NixOS):
#   nix-shell shell.nix --run 'installer/linux/package.sh build 0.0.0-local'
# (a NixOS build links against the nix store glibc, so that archive only runs on
# hosts with an equally new glibc; release archives come from CI.)
#
#   installer/linux/package.sh <build-dir> <version> [arch]
#
# Output in dist/:
#   ilmeeeengine-<version>-linux-<arch>.tar.gz          (top folder ilmeeeengine-<version>/)
#   ilmeeeengine-<version>-linux-<arch>.tar.gz.sha256
# The layout is what scripts/get-ilmeeeengine.sh expects (it runs install.sh).
set -euo pipefail

BUILD=${1:?usage: package.sh <build-dir> <version> [arch]}
VERSION=${2:?usage: package.sh <build-dir> <version> [arch]}
ARCH=${3:-$(uname -m)}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
# Output folder; override with DIST=... (its path must contain /dist/, which
# is how check-links.sh recognises a staged bundle).
DIST=${DIST:-$ROOT/dist}
NAME="ilmeeeengine-$VERSION"
ARCHIVE="$NAME-linux-$ARCH.tar.gz"
STAGE="$DIST/stage/$NAME"

rm -rf "$DIST"
mkdir -p "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE"

# Dev-only programs that the install rules also pick up.
rm -f "$STAGE/bin/TestBackground" "$STAGE/bin/ViewPort3D"

# LAN server for the Hub's "Host on LAN" (server/realtime). Rust links only
# libc dynamically, so it needs nothing from lib/.
RT_BIN=${RT_SERVER_BIN:-$ROOT/server/realtime/target/release/ilmeee-realtime}
if [ -f "$RT_BIN" ]; then
    install -m 755 "$RT_BIN" "$STAGE/bin/ilmeee-realtime"
    echo "  + bin/ilmeee-realtime"
elif [ -n "${CI:-}" ]; then
    echo "::error::$RT_BIN not found (cargo build --release in server/realtime)"; exit 1
else
    echo "ilmeee-realtime not built: Host on LAN will be unavailable"
fi

echo "Bundling shared libraries"
bash "$ROOT/scripts/bundle-deps.sh" "$STAGE"

# bin/ilmeee: finds the host GPU driver (also on NixOS) and GTK schemas.
install -m 755 "$ROOT/scripts/launcher.sh" "$STAGE/bin/ilmeee"

# Desktop entry + icon, used by install.sh.
mkdir -p "$STAGE/share/applications" "$STAGE/share/icons"
# Desktops want PNG/SVG; the repo has the BMP the windows use.
if [ -f "$ROOT/assets/icons/app_icon.png" ]; then
    cp "$ROOT/assets/icons/app_icon.png" "$STAGE/share/icons/ilmeeeengine.png"
elif command -v magick >/dev/null 2>&1; then
    magick "$ROOT/assets/icons/app_icon.bmp" "$STAGE/share/icons/ilmeeeengine.png" || true
elif command -v convert >/dev/null 2>&1; then
    convert "$ROOT/assets/icons/app_icon.bmp" "$STAGE/share/icons/ilmeeeengine.png" || true
fi
cat > "$STAGE/share/applications/ilmeeeengine-hub.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Ilmeee Engine Hub
Comment=Create and open Ilmeee Engine projects
Exec=@PREFIX@/bin/ilmeee IlmeeeHub
Icon=@ICON@
Terminal=false
Categories=Development;Game;
EOF

cat > "$STAGE/install.sh" <<'EOF'
#!/bin/sh
# Installs this Ilmeee Engine folder system-wide.
#   sudo ./install.sh                      -> /opt/ilmeeeengine + /usr/local/bin
#   PREFIX=~/.local/opt/ilmeeeengine BINDIR=~/.local/bin ./install.sh
# Nothing needs installing to just try it: ./bin/ilmeee IlmeeeHub
set -eu
SRC=$(cd "$(dirname "$0")" && pwd)
PREFIX="${PREFIX:-/opt/ilmeeeengine}"
BINDIR="${BINDIR:-/usr/local/bin}"
case "$PREFIX" in /|/usr|/usr/local|"$HOME") echo "refusing PREFIX=$PREFIX" >&2; exit 1 ;; esac

echo "Installing to $PREFIX"
[ "$SRC" = "$PREFIX" ] || { rm -rf "$PREFIX"; mkdir -p "$PREFIX"; cp -a "$SRC/." "$PREFIX/"; }

mkdir -p "$BINDIR"
for pair in ilmeee-hub:IlmeeeHub ilmeee-editor:GameEngineSDL ilmeee-player:IlmeeePlayer; do
    name=${pair%%:*}; target=${pair#*:}
    printf '#!/bin/sh\nexec "%s/bin/ilmeee" %s "$@"\n' "$PREFIX" "$target" > "$BINDIR/$name"
    chmod 755 "$BINDIR/$name"
    echo "  $BINDIR/$name"
done

if [ "$(id -u)" = 0 ]; then apps=/usr/share/applications; else apps="$HOME/.local/share/applications"; fi
icon="$(ls "$PREFIX"/share/icons/ilmeeeengine.* 2>/dev/null | head -1)"
mkdir -p "$apps"
sed -e "s|@PREFIX@|$PREFIX|g" -e "s|@ICON@|${icon:-applications-games}|g" \
    "$PREFIX/share/applications/ilmeeeengine-hub.desktop" > "$apps/ilmeeeengine-hub.desktop"
echo "  $apps/ilmeeeengine-hub.desktop"
echo "Done. Start with: ilmeee-hub"
EOF
chmod 755 "$STAGE/install.sh"

cat > "$STAGE/README-linux.txt" <<EOF
Ilmeee Engine $VERSION (Linux $ARCH)

Run without installing:   ./bin/ilmeee IlmeeeHub     (editor: ./bin/ilmeee GameEngineSDL)
Install:                  sudo ./install.sh          (/opt/ilmeeeengine, commands ilmeee-hub / ilmeee-editor)
Per-user install:         PREFIX=~/.local/opt/ilmeeeengine BINDIR=~/.local/bin ./install.sh

Needs: glibc 2.39+ (Ubuntu 24.04, Debian 13, Fedora 40, Arch or newer) and a
Vulkan driver (Mesa or NVIDIA). Every other library is inside lib/.
EOF

# The bundle must relocate: no absolute RPATH, nothing unresolved but host libs.
bash "$ROOT/scripts/check-links.sh" "$STAGE/bin" "$STAGE/lib"

echo "Packing $ARCHIVE"
tar -C "$DIST/stage" -czf "$DIST/$ARCHIVE" "$NAME"
( cd "$DIST" && sha256sum "$ARCHIVE" > "$ARCHIVE.sha256" )
ls -lh "$DIST"/*.tar.gz
