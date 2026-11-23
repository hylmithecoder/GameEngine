# Windows build (MSYS2 UCRT64 + GCC), included from the root CMakeLists.txt
# instead of the Linux/NixOS target graph, which hard-requires GTK, libnotify,
# X11 and POSIX process APIs.
#
# The port is staged (.roadmap/claude/18-Roadmap_Port_Windows-23112026.md).
# Every target defined here is one that compiles on Windows today; a stage
# adds targets here once its sources are ported. The installer
# (installer/windows) packages whatever of the release targets exist.
#
#   stage 1  core libraries + portable tests                 (this file)
#   stage 2  IlmeeePlayer (game runtime)                      (this file)
#   stage 3  editor: GameEngineSDL + IlmeeeEngine/IlmeeeEditor DLLs
#            + HandlerIlmeeeEngine (process/dlopen/GTK ports)  (todo)
#   stage 4  IlmeeeHub                                         (todo)
#
# Layout of `cmake --install` (what the installer ships): a flat directory,
#   <prefix>/IlmeeePlayer.exe, <prefix>/*.dll, <prefix>/assets/...
# because Windows resolves DLLs from the executable's own folder.

message(STATUS "Ilmeee: Windows build (MSYS2/MinGW) - stages 1-2: core, tests, IlmeeePlayer")

if(NOT MINGW)
    message(FATAL_ERROR "The Windows build targets MSYS2 UCRT64 (MinGW GCC). "
        "MSVC is not supported: the engine relies on GCC extensions and C++26.")
endif()

set(ILMEEE_WIN_BIN_DIR .)
set(ILMEEE_WIN_ASSET_DIR assets)

# Fold the GCC runtime into every binary: fewer DLLs to ship and no clash with
# another libstdc++-6.dll found earlier on PATH. winpthread stays a DLL.
add_link_options(-static-libgcc -static-libstdc++)
# Unicode Win32 API (W functions) for any code that includes Platform.hpp.
add_compile_definitions(UNICODE _UNICODE _WIN32_WINNT=0x0A00)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
find_package(Vulkan REQUIRED)
find_package(glm REQUIRED)
find_package(Ktx REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_path(STB_INCLUDE_DIRS NAMES stb/stb_image.h REQUIRED)

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
set(JSON_DIR ${CMAKE_SOURCE_DIR}/vendor/nlohmann)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# ---- vendored libraries -----------------------------------------------------

add_library(ufbx STATIC ${CMAKE_SOURCE_DIR}/vendor/ufbx/ufbx.c)
target_include_directories(ufbx PUBLIC ${CMAKE_SOURCE_DIR}/vendor/ufbx)

add_library(stb_impl STATIC ${CMAKE_SOURCE_DIR}/vendor/stb_impl.c
    ${CMAKE_SOURCE_DIR}/vendor/stb_vorbis_impl.c)
target_include_directories(stb_impl PUBLIC ${STB_INCLUDE_DIRS})

# Same IXWebSocket setup as Linux (see the root CMakeLists.txt), with OpenSSL
# from MSYS2 for wss:// (realtime collab).
option(ILMEEE_NET_TLS "Build IXWebSocket with OpenSSL (wss:// for realtime collab)" ON)
set(ILMEEE_HAVE_TLS OFF)
if(ILMEEE_NET_TLS)
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        set(ILMEEE_HAVE_TLS ON)
    endif()
endif()
set(USE_TLS ${ILMEEE_HAVE_TLS} CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ${ILMEEE_HAVE_TLS} CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(vendor/ixwebsocket EXCLUDE_FROM_ALL)
unset(BUILD_SHARED_LIBS)
target_compile_options(ixwebsocket PRIVATE -w)

add_library(ilmeee_net STATIC
    src/cpp/core_engine/net/Message.cpp
    src/cpp/core_engine/net/MessageBus.cpp
    src/cpp/core_engine/net/LoopbackTransport.cpp
    src/cpp/core_engine/net/WsTransport.cpp
    src/cpp/core_engine/net/McpServer.cpp
    src/cpp/core_engine/net/CollabClient.cpp
    src/cpp/core_engine/Process.cpp
)
target_include_directories(ilmeee_net PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ilmeee_net PRIVATE ixwebsocket PUBLIC ws2_32)
if(ILMEEE_HAVE_TLS)
    target_compile_definitions(ilmeee_net PRIVATE ILMEEE_NET_HAVE_TLS=1)
endif()

# ---- stage 1: portable tests (run in CI) ------------------------------------

set(ILMEEE_WINDOWS_TESTS
    TestNetProtocol TestWsTransport TestCollabClient TestMcpServer
    TestSceneFormat TestTransformMath TestPhysics TestMovementScripts TestProcess)

add_executable(TestProcess test/TestProcess.cpp)
target_link_libraries(TestProcess PRIVATE ilmeee_net)
add_executable(TestNetProtocol test/net/TestNetProtocol.cpp)
target_link_libraries(TestNetProtocol PRIVATE ilmeee_net)
add_executable(TestWsTransport test/net/TestWsTransport.cpp)
target_link_libraries(TestWsTransport PRIVATE ilmeee_net)
add_executable(TestCollabClient test/net/TestCollabClient.cpp)
target_link_libraries(TestCollabClient PRIVATE ilmeee_net glm::glm)
add_executable(TestMcpServer test/net/TestMcpServer.cpp)
target_link_libraries(TestMcpServer PRIVATE ilmeee_net ixwebsocket)
add_executable(TestSceneFormat test/TestSceneFormat.cpp)
target_link_libraries(TestSceneFormat PRIVATE glm::glm)
add_executable(TestTransformMath test/TestTransformMath.cpp)
target_link_libraries(TestTransformMath PRIVATE glm::glm)
add_executable(TestPhysics test/TestPhysics.cpp src/cpp/core_engine/PhysicsWorld.cpp)
target_link_libraries(TestPhysics PRIVATE glm::glm)
add_executable(TestMovementScripts test/TestMovementScripts.cpp
    src/cpp/core_engine/MovementScript.cpp)
target_link_libraries(TestMovementScripts PRIVATE glm::glm)
foreach(_t IN LISTS ILMEEE_WINDOWS_TESTS)
    target_include_directories(${_t} PRIVATE ${CMAKE_SOURCE_DIR}/include)
    # Tests run in the Release CI build, but several call the code under test
    # inside assert() (e.g. assert(server.Start(...))): keep assert alive.
    target_compile_options(${_t} PRIVATE -UNDEBUG)
endforeach()

# ---- stage 2: IlmeeePlayer (game runtime) -----------------------------------
# Same sources as the Linux target in the root CMakeLists.txt.

add_executable(IlmeeePlayer
    src/cpp/core_engine/WorldText.cpp
    src/cpp/core_engine/WorldTextRuntime.cpp
    src/cpp/core_engine/SkeletalAnimation.cpp
    src/cpp/core_engine/AnimationRuntime.cpp
    src/cpp/core_engine/SpringBone.cpp
    src/cpp/core_engine/AnimationController.cpp
    src/cpp/core_engine/NativeScripts.cpp src/cpp/core_engine/NativeScriptScene.cpp
    src/cpp/core_engine/ScriptWorkspace.cpp
    src/cpp/player/PlayerMain.cpp
    src/cpp/core_engine/AudioEngine.cpp
    src/cpp/core_engine/VideoDecoder.cpp
    src/cpp/core_engine/MediaHttp.cpp
    src/cpp/core_engine/MediaSystem.cpp
    src/cpp/core_engine/GameUI.cpp
    src/cpp/core_engine/DialogSystem.cpp
    src/cpp/core_engine/MovementScript.cpp
    src/cpp/core_engine/MovementRuntime.cpp
    src/cpp/core_engine/PhysicsWorld.cpp
    src/cpp/core_engine/PhysicsRuntime.cpp
    src/cpp/core_engine/UserMovementScripts.cpp
    src/cpp/core_engine/SceneLoader.cpp
    src/cpp/core_engine/SceneRenderer.cpp
    src/cpp/core_engine/BloomPass.cpp
    src/cpp/core_engine/TextureManager.cpp
    src/cpp/core_engine/PMXLoader.cpp
    src/cpp/vulkan/VulkanBase.cpp
    src/cpp/vulkan/VkTools.cpp
    ${IMGUI_SOURCES}
)
target_include_directories(IlmeeePlayer PRIVATE
    ${JSON_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/vulkan
    ${STB_INCLUDE_DIRS}
)
target_compile_definitions(IlmeeePlayer PRIVATE ILMEEE_HAS_FFMPEG)
target_link_libraries(IlmeeePlayer PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    KTX::ktx
    glm::glm
    ufbx
    stb_impl
    PkgConfig::FFMPEG
    PkgConfig::CURL
    Threads::Threads
)

# ---- stage 4 (early): IlmeeeHub (project launcher + online workspaces) ------
# SDL3 + OpenGL + ImGui, NFD's Win32 backend for the folder picker. Launches
# GameEngineSDL.exe next to it (editor port = stage 3; until then the Hub
# reports "editor binary not found" when opening a project).

find_package(OpenGL REQUIRED)
enable_language(RC)
# res/resources.rc names its icon relative to the repo root.
set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I${CMAKE_SOURCE_DIR}")
add_executable(IlmeeeHub
    src/cpp/Hub.cpp
    res/resources.rc
    vendor/nfd/src/nfd_win.cpp
    vendor/glad4.4/src/glad.c
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(IlmeeeHub PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/ui
    ${CMAKE_SOURCE_DIR}/include/core_engine
    ${CMAKE_SOURCE_DIR}/vendor/nanosvg
    ${CMAKE_SOURCE_DIR}/vendor/nfd/include
    ${CMAKE_SOURCE_DIR}/vendor/glad4.4/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${JSON_DIR}
)
target_link_libraries(IlmeeeHub PRIVATE
    PkgConfig::SDL3
    OpenGL::GL
    ilmeee_net
    Threads::Threads
    ole32 uuid shell32
)
# A GUI app: no console window when started from the Start menu. Output still
# goes to a pipe when something captures it.
# UNICODE is defined project-wide, so SDL_main.h emits wWinMain (not WinMain);
# -municode makes the MinGW CRT call that entry point.
target_link_options(IlmeeeHub PRIVATE -mwindows -municode)

# ---- install layout (consumed by installer/windows/package.sh) --------------

set(ILMEEE_WINDOWS_RELEASE_TARGETS IlmeeePlayer IlmeeeHub)
install(TARGETS ${ILMEEE_WINDOWS_RELEASE_TARGETS}
    RUNTIME DESTINATION ${ILMEEE_WIN_BIN_DIR})
# Authoring sources (.blend, archives) are not runtime data: keep them out of
# the installer (~100 MB).
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION ${ILMEEE_WIN_ASSET_DIR}
    PATTERN "*.blend" EXCLUDE
    PATTERN "*.blend1" EXCLUDE
    PATTERN "*.zip" EXCLUDE)
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/README.md
    DESTINATION ${ILMEEE_WIN_BIN_DIR} OPTIONAL)

# Lets CI and the packager know what this build produced.
file(WRITE ${CMAKE_BINARY_DIR}/windows-targets.txt
    "release=${ILMEEE_WINDOWS_RELEASE_TARGETS}\ntests=${ILMEEE_WINDOWS_TESTS}\n")
