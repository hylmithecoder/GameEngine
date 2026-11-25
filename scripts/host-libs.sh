# Shared policy: which shared libraries come from the host rather than the
# bundle. Sourced by bundle-deps.sh (what not to copy) and check-links.sh
# (what is allowed to be unresolved on the build machine).
#
# These are bound to the running kernel, the GPU driver, the system libc, or a
# daemon whose protocol version must match. Shipping our own copy breaks the
# machine we install onto.
#
# X11/xcb/xkb/wayland client libraries are deliberately absent from this list:
# they are plain protocol clients, not driver-coupled like libGL, so bundling
# them is what lets the archive run on a distro with older X libraries.
#
# libvulkan and libOpenCL are absent too, and that is deliberate: both are ICD
# *loaders*, not drivers. They find the real driver at runtime through
# /usr/share/vulkan/icd.d and /etc/OpenCL/vendors, so shipping our own copy is
# the supported arrangement (and the only way the bundle starts on a host whose
# loader is older than the one we built against).
#
# The same argument covers GLVND — libGL/libEGL/libGLX/libGLdispatch are the
# vendor-NEUTRAL dispatch layer, not the driver. The driver is the vendor
# library GLVND dlopen()s by name (libEGL_mesa.so.0, libGLX_nvidia.so.0) after
# reading the host's vendor manifests, so a bundled GLVND still ends up on the
# host's real driver. Nothing graphics-related is host-supplied any more; what
# the host must provide is the driver *directory*, which bin/ilmeee locates.
#
# libdrm/libgbm/libdbus-1/libudev were on this list once and are not any more.
# They are ABI-stable client libraries, and leaving them to the host made the
# bundle unrunnable on any system without /usr/lib (NixOS being the one we
# develop on). Mesa still loads the host's own libdrm through its own RUNPATH,
# so ours is only ever used by our binaries.
HOST_LIBS='^(linux-vdso|ld-linux.*|libc|libm|libmvec|libdl|librt|libpthread|libresolv|libgcc_s)\.so'

HOST_LIBS_RE=$(printf '%s' "$HOST_LIBS" | paste -sd'|' -)
