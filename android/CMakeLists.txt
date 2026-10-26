# IlmeeePlayer for Android: the same runtime as the desktop player, built as
# libmain.so for SDL3's SDLActivity (which loads libSDL3.so and libmain.so).
# Configured by scripts/android/build-player.sh with the NDK toolchain:
#
#   cmake -S android -B build-android/arm64-v8a \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
#     -DSDL3_SOURCE_DIR=... -DGLM_INCLUDE_DIR=... -DSTB_INCLUDE_DIR=...
cmake_minimum_required(VERSION 3.20)
project(IlmeeePlayerAndroid C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# The exporter writes the editor's current scene format, even when the input
# scene is older. Publish the reader version with each successfully built ABI
# so an old Android player cannot silently be packaged with a newer scene.
set(SCENE_FORMAT_HEADER "${ENGINE_ROOT}/include/core_engine/IlmeeeScene.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SCENE_FORMAT_HEADER}")
file(STRINGS "${SCENE_FORMAT_HEADER}" SCENE_FORMAT_MINOR_DECL
    REGEX "^constexpr uint16_t kSceneFormatMinor = [0-9]+;")
if(NOT SCENE_FORMAT_MINOR_DECL)
  message(FATAL_ERROR "Cannot determine the player's scene format version")
endif()
string(REGEX MATCH "= ([0-9]+)" SCENE_FORMAT_MATCH "${SCENE_FORMAT_MINOR_DECL}")
set(SCENE_FORMAT_MINOR "${CMAKE_MATCH_1}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/player-runtime.json" CONTENT
  "{\"schema\":1,\"abi\":\"${ANDROID_ABI}\",\"sceneFormat\":{\"major\":1,\"minor\":${SCENE_FORMAT_MINOR}}}\n")
set(SDL3_SOURCE_DIR "" CACHE PATH "SDL3 source tree")
set(GLM_INCLUDE_DIR "" CACHE PATH "Folder containing glm/glm.hpp")
set(STB_INCLUDE_DIR "" CACHE PATH "Folder containing stb/stb_image.h")
foreach(dir SDL3_SOURCE_DIR GLM_INCLUDE_DIR STB_INCLUDE_DIR)
  if(NOT EXISTS "${${dir}}")
    message(FATAL_ERROR "${dir} not set or missing: '${${dir}}'")
  endif()
endforeach()

# SDL3 as a shared library: SDLActivity loads libSDL3.so itself.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL3_SOURCE_DIR} SDL3 EXCLUDE_FROM_ALL)

set(IMGUI_DIR ${ENGINE_ROOT}/vendor/imgui)
add_library(main SHARED
    ${ENGINE_ROOT}/src/cpp/core_engine/WorldText.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/WorldTextRuntime.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/SkeletalAnimation.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/AnimationRuntime.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/SpringBone.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/AnimationController.cpp
    ${ENGINE_ROOT}/src/cpp/player/PlayerMain.cpp
    ${ENGINE_ROOT}/src/cpp/player/AndroidAssets.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/GameUI.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/DialogSystem.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/MovementScript.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/MovementRuntime.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/UserMovementScripts.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/NativeScripts.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/NativeScriptScene.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/SceneLoader.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/SceneRenderer.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/AudioEngine.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/VideoDecoder.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/MediaSystem.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/AndroidMedia.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/TextureManager.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/PMXLoader.cpp
    ${ENGINE_ROOT}/src/cpp/vulkan/VulkanBase.cpp
    ${ENGINE_ROOT}/src/cpp/vulkan/VkTools.cpp
    ${ENGINE_ROOT}/vendor/stb_impl.c
    ${ENGINE_ROOT}/vendor/stb_vorbis_impl.c
    ${ENGINE_ROOT}/vendor/ufbx/ufbx.c
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(main PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${ENGINE_ROOT}/include
    ${ENGINE_ROOT}/include/vulkan
    ${ENGINE_ROOT}/vendor/ufbx
    ${ENGINE_ROOT}/vendor/nlohmann
    ${GLM_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)
# Per-pixel video conversion and audio resampling stay fast in Debug builds.
set_source_files_properties(
    ${ENGINE_ROOT}/src/cpp/core_engine/VideoDecoder.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/AndroidMedia.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/AudioEngine.cpp
    PROPERTIES COMPILE_OPTIONS "-O2")
# No libktx on Android: textures go through stb (TextureManager fallback).
# No FFmpeg either: .wav/.ogg decode in-engine, everything else (mp3, aac,
# mp4/H.264...) through the platform's MediaCodec (libmediandk).
target_compile_definitions(main PRIVATE ILMEEE_NO_KTX)
target_link_libraries(main PRIVATE SDL3::SDL3-shared vulkan android log m
    mediandk dl)
# Android 15+ devices may use 16 KB memory pages: every LOAD segment must be
# 16 KB aligned or the app warns / fails to load. (build-player.sh also sets
# ANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES, which covers SDL3; this keeps libmain
# right even when configured by hand.)
target_link_options(main PRIVATE "-Wl,-z,max-page-size=16384")

# Export the exact NDK plus a snapshot of the public script SDK. configure_file
# tracks header changes so this snapshot and the rebuilt player stay in sync.
if(NOT ANDROID_STL STREQUAL "c++_shared")
  message(FATAL_ERROR "Native script modules require ANDROID_STL=c++_shared")
endif()
file(GLOB_RECURSE SCRIPT_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${ENGINE_ROOT}/include/core_engine/script/*")
list(APPEND SCRIPT_PUBLIC_HEADERS
    "${ENGINE_ROOT}/include/core_engine/core/Component.hpp"
    "${ENGINE_ROOT}/include/core_engine/NativeScriptTypes.hpp")
foreach(header IN LISTS SCRIPT_PUBLIC_HEADERS)
  file(RELATIVE_PATH relative "${ENGINE_ROOT}/include" "${header}")
  configure_file("${header}" "${CMAKE_CURRENT_BINARY_DIR}/script-sdk/${relative}" COPYONLY)
endforeach()
string(REPLACE "\\" "\\\\" SCRIPT_COMPILER_JSON "${CMAKE_CXX_COMPILER}")
string(REPLACE "\"" "\\\"" SCRIPT_COMPILER_JSON "${SCRIPT_COMPILER_JSON}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/script-toolchain.json" CONTENT
  "{\"schema\":1,\"abi\":\"${ANDROID_ABI}\",\"api\":${ANDROID_PLATFORM_LEVEL},\"stl\":\"c++_shared\",\"compiler\":\"${SCRIPT_COMPILER_JSON}\"}\n")
