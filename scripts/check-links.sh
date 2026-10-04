#!/usr/bin/env bash
# Verify that binaries resolve their libraries, and that a staged bundle uses
# only relative RPATHs.
#
# Usage: scripts/check-links.sh <dir> [<dir> ...]
#
# A directory whose path contains /dist/ is treated as a staged bundle: there,
# an absolute RPATH is an error, because the tree must relocate. Libraries
# listed in host-libs.sh are expected to be unresolved on a build machine
# without /usr/lib (NixOS), since they are supplied by the target system.
set -uo pipefail

. "$(dirname -- "$0")/host-libs.sh"

fail=0
checked=0

for d in "$@"; do
    [ -d "$d" ] || continue
    case "$d" in */dist/*) staged=1 ;; *) staged=0 ;; esac

    while IFS= read -r f; do
        [ -n "$f" ] || continue
        checked=$((checked + 1))
        status=ok
        notes=()

        while read -r soname _; do
            [ -n "$soname" ] || continue
            if [[ "$soname" =~ $HOST_LIBS_RE ]]; then
                notes+=("from host: $soname")
            else
                notes+=("UNRESOLVED: $soname")
                status=MISSING
            fi
        done < <(ldd "$f" 2>/dev/null | sed -n 's/^\s*\(\S*\) => not found.*/\1/p')

        rpath=$(readelf -d "$f" 2>/dev/null | sed -n 's/.*R\(UN\)\?PATH.*\[\(.*\)\]/\2/p')
        # In a bundle every RPATH component must be $ORIGIN-relative; a single
        # absolute component means the tree only works on this machine.
        if [ "$staged" = 1 ]; then
            IFS=: read -ra parts <<<"$rpath"
            for p in "${parts[@]}"; do
                case "$p" in /*) status=ABSOLUTE-RPATH ;; esac
            done
        fi

        if [ "$status" != ok ]; then
            fail=1
            printf '  %-15s %s\n' "$status" "$f"
            [ "$staged" = 1 ] && printf '      RPATH: %s\n' "${rpath:-<none>}"
            for n in "${notes[@]}"; do
                case "$n" in UNRESOLVED:*) printf '      %s\n' "$n" ;; esac
            done
        fi
    done < <(find "$d" -maxdepth 1 -type f -print0 2>/dev/null |
             xargs -0 -r file | sed -n 's/^\(.*\):[[:space:]]*ELF.*/\1/p')
done

if [ "$checked" -eq 0 ]; then
    echo "No ELF files found — build first." >&2
    exit 1
fi

if [ "$fail" -eq 0 ]; then
    echo "OK — $checked binaries checked; all libraries resolve (host-supplied ones excepted)."
else
    echo "FAILED — see above." >&2
    exit 1
fi
