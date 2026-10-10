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
    ${ENGINE_ROOT}/src/cpp/player/PlayerMain.cpp
    ${ENGINE_ROOT}/src/cpp/player/AndroidAssets.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/GameUI.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/MovementScript.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/MovementRuntime.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/UserMovementScripts.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/SceneLoader.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/SceneRenderer.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/TextureManager.cpp
    ${ENGINE_ROOT}/src/cpp/core_engine/PMXLoader.cpp
    ${ENGINE_ROOT}/src/cpp/vulkan/VulkanBase.cpp
    ${ENGINE_ROOT}/src/cpp/vulkan/VkTools.cpp
    ${ENGINE_ROOT}/vendor/stb_impl.c
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
    ${GLM_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)
# No libktx on Android: textures go through stb (TextureManager fallback).
target_compile_definitions(main PRIVATE ILMEEE_NO_KTX)
target_link_libraries(main PRIVATE SDL3::SDL3-shared vulkan android log m)
