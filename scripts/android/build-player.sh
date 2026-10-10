#!/usr/bin/env bash
# Builds IlmeeePlayer for Android (arm64-v8a): libmain.so + libSDL3.so,
# which the editor's "Build ▸ Android" packs into an APK together with a game.
#
#   nix-shell shell.nix --run scripts/android/build-player.sh
#
# Environment (all optional):
#   ANDROID_HOME     SDK folder              (default ~/Android/Sdk)
#   ANDROID_NDK      NDK folder              (default: newest in $ANDROID_HOME/ndk)
#   ILMEEE_ABI       arm64-v8a | x86_64 ...  (default arm64-v8a)
#   BUILD_TYPE       Debug | Release         (default Debug)
#   SDL3_VERSION     must match the desktop SDL3 (default: pkg-config sdl3)
#   GLM_INCLUDE_DIR / STB_INCLUDE_DIR        (default: pkg-config glm / stb)
#
# Output: build-android/jniLibs/<abi>/{libmain.so,libSDL3.so}
#         build-android/sdl-java/   (SDL3's Java glue for the APK)
set -euo pipefail

ENGINE="$(cd "$(dirname "$0")/../.." && pwd)"
ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
if [[ -z "${ANDROID_NDK:-}" ]]; then
  ANDROID_NDK="$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1 || true)"
fi
ABI="${ILMEEE_ABI:-arm64-v8a}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
CACHE="$HOME/.ilmeeeengine/android"
OUT="$ENGINE/build-android"

die() { echo "error: $*" >&2; exit 1; }
[[ -d "$ANDROID_NDK" ]] || die "Android NDK not found (set ANDROID_NDK or install it with the SDK manager)"
TOOLCHAIN="$ANDROID_NDK/build/cmake/android.toolchain.cmake"
[[ -f "$TOOLCHAIN" ]] || die "$TOOLCHAIN missing"

SDL3_VERSION="${SDL3_VERSION:-$(pkg-config --modversion sdl3 2>/dev/null || true)}"
[[ -n "$SDL3_VERSION" ]] || die "cannot tell the SDL3 version; set SDL3_VERSION"
SDL3_DIR="$CACHE/SDL3-$SDL3_VERSION"
if [[ ! -f "$SDL3_DIR/CMakeLists.txt" ]]; then
  echo "Downloading SDL3 $SDL3_VERSION source ..."
  mkdir -p "$CACHE"
  tmp="$(mktemp -d "$CACHE/dl.XXXXXX")"
  curl -fL -o "$tmp/sdl3.tar.gz" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz"
  tar xzf "$tmp/sdl3.tar.gz" -C "$CACHE"
  rm -rf "$tmp"
fi

include_dir() { # pkg-config name, header to look for
  local flag
  for flag in $(pkg-config --cflags-only-I "$1" 2>/dev/null); do
    local dir="${flag#-I}"
    [[ -f "$dir/$2" ]] && { echo "$dir"; return; }
    [[ -f "$dir/../$2" ]] && { (cd "$dir/.." && pwd); return; }
  done
}
GLM_INCLUDE_DIR="${GLM_INCLUDE_DIR:-$(include_dir glm glm/glm.hpp)}"
STB_INCLUDE_DIR="${STB_INCLUDE_DIR:-$(include_dir stb stb/stb_image.h)}"
[[ -n "$GLM_INCLUDE_DIR" ]] || die "glm headers not found; set GLM_INCLUDE_DIR"
[[ -n "$STB_INCLUDE_DIR" ]] || die "stb headers not found; set STB_INCLUDE_DIR"

# CMake from PATH (nix-shell), else the one the SDK manager installed.
CMAKE="$(command -v cmake || true)"
if [[ -z "$CMAKE" ]]; then
  CMAKE="$(ls -d "$ANDROID_HOME"/cmake/*/bin/cmake 2>/dev/null | sort -V | tail -1 || true)"
fi
[[ -x "$CMAKE" ]] || die "cmake not found"
GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)

BUILD="$OUT/$ABI"
echo "Configuring ($ABI, $BUILD_TYPE, NDK $(basename "$ANDROID_NDK"), SDL3 $SDL3_VERSION) ..."
"$CMAKE" -S "$ENGINE/android" -B "$BUILD" "${GENERATOR[@]}" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 \
  -DANDROID_STL=c++_shared \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DSDL3_SOURCE_DIR="$SDL3_DIR" \
  -DGLM_INCLUDE_DIR="$GLM_INCLUDE_DIR" \
  -DSTB_INCLUDE_DIR="$STB_INCLUDE_DIR"
"$CMAKE" --build "$BUILD" --target main SDL3-shared -j"$(nproc)"

LIBS="$OUT/jniLibs/$ABI"
mkdir -p "$LIBS"
cp "$BUILD/libmain.so" "$BUILD/SDL3/libSDL3.so" "$LIBS/"
# c++_shared: SDL3 and libmain must share one C++ runtime.
case "$ABI" in
  arm64-v8a) TRIPLE=aarch64-linux-android ;;
  armeabi-v7a) TRIPLE=arm-linux-androideabi ;;
  x86_64) TRIPLE=x86_64-linux-android ;;
  x86) TRIPLE=i686-linux-android ;;
  *) die "unknown ABI $ABI" ;;
esac
cp "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$TRIPLE/libc++_shared.so" "$LIBS/"

rm -rf "$OUT/sdl-java"
mkdir -p "$OUT/sdl-java"
cp -r "$SDL3_DIR/android-project/app/src/main/java/org" "$OUT/sdl-java/"
echo "$SDL3_VERSION" > "$OUT/sdl-java/SDL_VERSION"

echo "Done:"
ls -la "$LIBS"
