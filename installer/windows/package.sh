#!/usr/bin/env bash
# Stages the Windows build into a self-contained folder, then makes a portable
# zip and (when Inno Setup is available) an installer.
#
# Runs inside the MSYS2 UCRT64 shell (CI: .github/workflows/release-windows.yaml):
#
#   installer/windows/package.sh <build-dir> <version>
#
# Output in dist/:
#   IlmeeeEngine-<version>-windows-x64.zip
#   IlmeeeEngine-<version>-windows-x64-setup.exe
set -euo pipefail

BUILD=${1:?usage: package.sh <build-dir> <version>}
VERSION=${2:?usage: package.sh <build-dir> <version>}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
STAGE="$ROOT/dist/stage"
NAME="IlmeeeEngine-$VERSION-windows-x64"

rm -rf "$ROOT/dist"
mkdir -p "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE"

# Copy every DLL the binaries need from the MSYS2 prefix, recursively.
# Left out on purpose:
#   - Windows system DLLs (C:\Windows\...)
#   - vulkan-1.dll: the GPU driver installs the real Vulkan loader; a bundled
#     copy from MSYS2 would shadow it.
copy_deps() {
    local added=1
    while [ "$added" = 1 ]; do   # repeat until a pass adds nothing (transitive deps)
        added=0
        while IFS= read -r -d '' bin; do
            # ntldd line: "  name.dll => C:\msys64\ucrt64\bin\name.dll (0x...)"
            while read -r dll arrow winpath _; do
                [ "$arrow" = "=>" ] || continue
                case "${winpath,,}" in
                    *\\windows\\*|not|"") continue ;;
                esac
                [ "${dll,,}" = "vulkan-1.dll" ] && continue
                [ -e "$STAGE/$dll" ] && continue
                cp "$(cygpath -u "$winpath")" "$STAGE/$dll"
                echo "  + $dll"
                added=1
            done < <(ntldd "$bin" 2>/dev/null)
        done < <(find "$STAGE" -maxdepth 1 \( -iname '*.exe' -o -iname '*.dll' \) -print0)
    done
}
echo "Bundling DLL dependencies"
copy_deps

# Nothing may still resolve into the MSYS2 prefix: that would work on the
# build machine and fail on a user's PC.
leaks=0
while IFS= read -r -d '' bin; do
    if ntldd "$bin" | grep -iE '\\(msys64|ucrt64)\\'; then
        echo "::error::$bin still depends on the MSYS2 prefix"; leaks=1
    fi
done < <(find "$STAGE" -maxdepth 1 \( -iname '*.exe' -o -iname '*.dll' \) -print0)
[ "$leaks" = 0 ] || exit 1

cp "$ROOT/assets/icons/app_icon.ico" "$STAGE/IlmeeeEngine.ico"
cat > "$STAGE/VERSION.txt" <<EOF
Ilmeee Engine $VERSION
commit ${GITHUB_SHA:-$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)}
built $(date -u +%Y-%m-%dT%H:%M:%SZ)
EOF

echo "Staged:"
find "$STAGE" -maxdepth 1 -type f | sed "s|$STAGE/|  |" | sort

# Portable zip.
(cd "$ROOT/dist" && mv stage "$NAME" && zip -qr "$NAME.zip" "$NAME" && mv "$NAME" stage)
echo "dist/$NAME.zip"

# Installer.
ISCC=""
for c in "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" "/c/Program Files/Inno Setup 6/ISCC.exe"; do
    [ -x "$c" ] && ISCC=$c && break
done
if [ -z "$ISCC" ]; then
    echo "Inno Setup not found: skipping the installer (zip only)"
    exit 0
fi
"$ISCC" /Qp \
    "/DAppVersion=$VERSION" \
    "/DStageDir=$(cygpath -w "$STAGE")" \
    "/DOutputDir=$(cygpath -w "$ROOT/dist")" \
    "/DOutputName=$NAME-setup" \
    "$(cygpath -w "$ROOT/installer/windows/IlmeeeEngine.iss")"
echo "dist/$NAME-setup.exe"
