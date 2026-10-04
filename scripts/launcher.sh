#!/usr/bin/env bash
# Generic launcher for a staged Ilmeee Engine bundle.
#
#   bin/ilmeee <binary-name> [args...]      e.g. bin/ilmeee GameEngineSDL
#
# Everything the engine needs is resolved through $ORIGIN/../lib, so a plain
# ./bin/GameEngineSDL works on any ordinary distribution and this wrapper is
# not required there. It exists for the one thing the bundle must NOT carry:
# the GPU driver itself. We ship the vendor-neutral loaders (GLVND, the Vulkan
# and OpenCL ICD loaders); each of them then dlopen()s the real driver named by
# a manifest on the host. Ordinary distributions keep those manifests and
# driver objects where the loaders already look, so this wrapper does nothing
# there. NixOS instead puts the whole set under /run/opengl-driver, which
# nothing searches by default -- pointing the loaders at it is what removes the
# need for a nix-shell.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(dirname -- "$here")

drv=()
for d in /run/opengl-driver/lib /run/opengl-driver-32/lib; do
    [ -d "$d" ] && drv+=("$d")
done
if [ ${#drv[@]} -gt 0 ]; then
    printf -v joined '%s:' "${drv[@]}"
    export LD_LIBRARY_PATH="${joined%:}${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# Each loader finds its driver through a different manifest directory. Set
# only the ones we can see and that the caller has not already chosen, so a
# user override always wins.
if [ -d /run/opengl-driver ]; then
    [ -z "${__EGL_VENDOR_LIBRARY_DIRS:-}" ] &&
        [ -d /run/opengl-driver/share/glvnd/egl_vendor.d ] &&
        export __EGL_VENDOR_LIBRARY_DIRS=/run/opengl-driver/share/glvnd/egl_vendor.d
    [ -z "${LIBGL_DRIVERS_PATH:-}" ] && [ -d /run/opengl-driver/lib/dri ] &&
        export LIBGL_DRIVERS_PATH=/run/opengl-driver/lib/dri
    if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -d /run/opengl-driver/share/vulkan/icd.d ]; then
        icds=$(find /run/opengl-driver/share/vulkan/icd.d -name '*.json' 2>/dev/null | paste -sd: -)
        [ -n "$icds" ] && export VK_ICD_FILENAMES="$icds"
    fi
    [ -z "${OCL_ICD_VENDORS:-}" ] && [ -d /run/opengl-driver/etc/OpenCL/vendors ] &&
        export OCL_ICD_VENDORS=/run/opengl-driver/etc/OpenCL/vendors
fi

bin=${1:?usage: ilmeee <binary-name> [args...]}
shift
exec "$root/bin/$bin" "$@"
