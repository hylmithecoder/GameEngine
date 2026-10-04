#!/usr/bin/env bash
# Copy every non-system shared library the staged binaries need into <stage>/lib
# and rewrite RPATHs to be purely relative, so the tree can be extracted
# anywhere on any distro.
#
# Usage: scripts/bundle-deps.sh <stage-dir>
set -euo pipefail

STAGE=${1:?usage: bundle-deps.sh <stage-dir>}
[ -d "$STAGE/bin" ] || { echo "no $STAGE/bin — nothing to bundle" >&2; exit 1; }
mkdir -p "$STAGE/lib"

. "$(dirname -- "$0")/host-libs.sh"
EXCLUDE_RE=$HOST_LIBS_RE

elf_files() {
    find "$STAGE/bin" "$STAGE/lib" -maxdepth 1 -type f -print0 2>/dev/null |
        xargs -0 -r file | sed -n 's/^\(.*\):[[:space:]]*ELF.*/\1/p'
}

# Some dependencies are linked by absolute path rather than by soname (the
# Discord Game SDK ships a bare .so with no DT_SONAME, so the linker records
# the full nix-store path). Such a NEEDED entry can never be satisfied by
# RPATH, so rewrite it to a plain filename first and let the normal soname
# pass below copy it in.
absolutise_needed() {
    command -v patchelf >/dev/null 2>&1 || return 0
    while read -r f; do
        [ -n "$f" ] || continue
        while read -r dep; do
            case "$dep" in /*) ;; *) continue ;; esac
            base=${dep##*/}
            [ -e "$dep" ] && [ ! -e "$STAGE/lib/$base" ] && {
                cp -L "$dep" "$STAGE/lib/$base"
                chmod u+w "$STAGE/lib/$base"
                echo "  + $base (was absolute: $dep)"
            }
            patchelf --replace-needed "$dep" "$base" "$f" || true
        done < <(readelf -d "$f" 2>/dev/null |
                 sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    done < <(elf_files)
}
absolutise_needed

# ldd only reports direct dependencies, so repeat until nothing new appears
# (bundled libraries pull in dependencies of their own).
copied_total=0
for _pass in 1 2 3 4 5; do
    copied=0
    while read -r f; do
        [ -n "$f" ] || continue
        while read -r soname path; do
            [ -n "${path:-}" ] || continue
            # ldd names the ELF interpreter by absolute path rather than a
            # soname; it belongs to the host libc and must never be copied.
            case "$soname" in /*) continue ;; esac
            [[ "$soname" =~ $EXCLUDE_RE ]] && continue
            [ -e "$STAGE/lib/$soname" ] && continue
            cp -L "$path" "$STAGE/lib/$soname"
            chmod u+w "$STAGE/lib/$soname"
            echo "  + $soname"
            copied=$((copied + 1))
        done < <(ldd "$f" 2>/dev/null | sed -n 's|^\s*\(\S*\) => \(/\S*\).*|\1 \2|p')
    done < <(elf_files)
    copied_total=$((copied_total + copied))
    [ "$copied" -eq 0 ] && break
done

echo "Bundled $copied_total libraries into $STAGE/lib"

# Normalise RPATHs: binaries look next to themselves and in ../lib, libraries
# look next to themselves. No absolute path survives.
if command -v patchelf >/dev/null 2>&1; then
    while read -r f; do
        case "$f" in
            "$STAGE"/bin/*) patchelf --set-rpath '$ORIGIN:$ORIGIN/../lib' "$f" || true ;;
            "$STAGE"/lib/*) patchelf --set-rpath '$ORIGIN' "$f" || true ;;
        esac
    done < <(elf_files)
    echo 'RPATHs normalised to $ORIGIN.'
else
    echo "patchelf not found — RPATHs left as linked." >&2
    echo "  Add patchelf to shell.nix for a fully relocatable bundle." >&2
fi
