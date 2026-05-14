cmake_minimum_required(VERSION 4.1)
project(GameEngineSDL VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
option(BUILD_SHARED_LIBS "Build GameEngine as a shared libraries" ON)
# Set output directories specifically for library targets

add_subdirectory(test)
add_subdirectory(src)
add_subdirectory(include)
add_subdirectory(vendor)
message(STATUS "SOURCES_CPP: ${SOURCES}")
message(STATUS "SOURCE_CORE_ENGINE: ${SOURCES_CORE_ENGINE}")
message(STATUS "SOURCE_UI: ${SOURCES_UI}")
message(STATUS "SOURCE_VULKAN: ${SOURCES_VULKAN}")
# Compiler-specific options
# if(MSVC)
#     add_compile_options(/W4)
#     add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
# else()
#     add_compile_options(-Wall -Wextra -pedantic)
# endif()

set(SOURCE_TEST
    src/cpp/ui/assets.cpp
    include/ui/assets.hpp
    src/cpp/ui/test_main.cpp
)

set(SOURCE_TEST_GLSL
    src/cpp/test_glsl.cpp
)

# Add ImGui source files
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

set(ICON_DIR ${CMAKE_SOURCE_DIR}/vendor/icons)

set(SOURCE_HANDLER
    src/cpp/core_engine/core/Launcher.cpp 
    src/cpp/core_engine/core/HandlerLauncher.cpp
)

set(IMGUI_TEST
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

set(JSON_DIR ${CMAKE_SOURCE_DIR}/vendor/nlohmann)
set(JSON
    ${JSON_DIR}/json.hpp
)

set(GLAD_4_DIR ${CMAKE_SOURCE_DIR}/vendor/glad4.4)
set(GLAD_4
    ${GLAD_4_DIR}/src/glad.c
)

file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
find_package(glfw3 REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(Vulkan REQUIRED)
find_package(GLEW REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Ktx REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
pkg_check_modules(LIBNOTIFY REQUIRED libnotify)
pkg_check_modules(XCB REQUIRED xcb)
pkg_check_modules(X11 REQUIRED x11)
# FFMPEG is usually handled via pkg-config or custom find module
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswresample libswscale)

link_directories(
    ${GTK3_LIBRARY_DIRS}
    ${LIBNOTIFY_LIBRARY_DIRS}
    ${XCB_LIBRARY_DIRS}
    ${X11_LIBRARY_DIRS}
    ${FFMPEG_LIBRARY_DIRS}
)
find_package(glm REQUIRED)
# Create executable
# add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
# add_compile_definitions(__STDC_WANT_LIB_EXT1__=0)
add_executable(${PROJECT_NAME} ${HEADERS_MAIN_PROJECT} ${SOURCES} ${GLAD_4} ${IMGUI_SOURCES} ${NFD_LIB})
# target_link_options(${PROJECT_NAME} PRIVATE -mconsole)
add_executable(TestBackground ${SOURCE_TEST} ${GLAD_4} ${IMGUI_SOURCES})
if(WIN32)
    target_link_options(TestBackground PRIVATE -mconsole)
endif()

add_executable(TestGLSL ${SOURCE_TEST_GLSL} ${GLAD_4} ${IMGUI_SOURCES})
# target_link_options(TestGLSL PRIVATE -mconsole)

add_library(IlmeeeEngine SHARED res/IlmeeeEngine.rc ${SOURCES_CORE})
# target_link_options(IlmeeeEngine PRIVATE -mconsole)
add_definitions(-DILMEEEENGINE_EXPORTS)
set_target_properties(IlmeeeEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Define GAMEENGINE_EXPORTS when building the DLL
target_compile_definitions(IlmeeeEngine PRIVATE GAMEENGINE_EXPORTS)

add_executable(HandlerIlmeeeEngine ${SOURCE_HANDLER})
# target_link_options(HandlerIlmeeeEngine PRIVATE -mconsole)

add_executable(IlmeeeHub src/cpp/Hub.cpp res/resources.rc ${NFD_LIB} ${IMGUI_SOURCES} ${ICON_SOURCE} ${JSON} ${GLAD_4})
# target_link_options(IlmeeeHub PRIVATE -mconsole)
# Set properties for the DLL
set_target_properties(IlmeeeEngine PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "IlmeeeEngine"
)

include_directories(
    ${GTK3_INCLUDE_DIRS}
    include/
)

add_library(IlmeeeEditor SHARED ${NFD_LIB} src/cpp/core_engine/core_editor/IlmeeeEditor.cpp res/IlmeeeEditor.rc ${SOURCES_CORE_EDITOR} ${IMGUI_SOURCES} ${JSON} ${GLAD_4})
# target_link_options(IlmeeeEditor PRIVATE -mconsole)
# Define macro untuk export symbols
add_definitions(-DILMEEEDITOR_EXPORTS)
set_target_properties(IlmeeeEditor PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Define GAMEENGINE_EXPORTS when building the DLL
target_compile_definitions(IlmeeeEditor PRIVATE ILMEEEDITOR_EXPORTS)
# Set properties for the DLL
message(STATUS "Vieport Source : ${VIEWPORT_SOURCES}")
add_executable(ViewPort3D vendor/nfd/src/nfd_gtk.cpp ${VIEWPORT_SOURCES} ${IMGUI_SOURCES})

set_target_properties(IlmeeeEditor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "IlmeeeEditor"
    POSITION_INDEPENDENT_CODE ON
)

cmake_policy(SET CMP0072 NEW) # Prefer GLVND
set(OpenGL_GL_PREFERENCE "GLVND")
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(Vulkan REQUIRED)
# find_package(GLFW3 REQUIRED)
# set(Vulkan_DIR "${VULKAN_SDK}/lib/cmake/VulkanLoader")

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    # Project directories
    ${CMAKE_SOURCE_DIR}/vendor/icons
    # Custom/third-party libraries
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    include/nfd/include
    ${JSON_DIR}
    ${NFD_INCLUDE_DIR}
    ${GLAD_4_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/vulkan
    /usr/include/discord
    /usr/local/include/discord
)

# Search for Discord Game SDK header in Nix store or standard paths
find_path(DISCORD_SDK_INCLUDE_DIR discord_game_sdk.h
    PATHS
    $ENV{DISCORD_SDK_PATH}/lib/include
    /usr/include/discord
    /usr/local/include/discord
)

if(DISCORD_SDK_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${DISCORD_SDK_INCLUDE_DIR})
endif()

# Find the Discord Game SDK library
find_library(DISCORD_SDK_LIBRARY discord_game_sdk
    PATHS
    $ENV{DISCORD_SDK_PATH}/lib
    ${DISCORD_SDK_PATH}/lib
    /usr/lib
    /usr/local/lib
)


target_include_directories(TestBackground PRIVATE
    # ${SDL2_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/include/ui
    ${CMAKE_SOURCE_DIR}/include/core_engine
    # ${FFMPEG_DIR}
    # ${STB}
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
    Vulkan
    # ${VULKAN_DIR}
    ${GLAD_4_DIR}/include
    # GLES2
    # GLEW
    
)

target_include_directories(TestGLSL PRIVATE
    ${ICON_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/include/ui
    ${CMAKE_SOURCE_DIR}/include/core_engine
    ${NFD_INCLUDE_DIR}
    ${JSON_DIR}
    ${GLAD_4_DIR}/include
)

target_include_directories(HandlerIlmeeeEngine PRIVATE
    # ${DLFCN}/src
    ${GTK3_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/include/core_engine
)

target_include_directories(IlmeeeEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/core_engine/core>
    $<INSTALL_INTERFACE:include/core_engine/core>
)

target_include_directories(IlmeeeEditor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/core_engine/core_editor>
    $<BUILD_INTERFACE:${ICON_DIR}>
    $<BUILD_INTERFACE:${IMGUI_DIR}>
    $<BUILD_INTERFACE:${IMGUI_DIR}/backends>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include/ui>
    $<BUILD_INTERFACE:${NFD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${JSON_DIR}>
    $<BUILD_INTERFACE:${GLAD_4_DIR}/include>
    $<BUILD_INTERFACE:${XCB_INCLUDE_DIRS}>
    $<BUILD_INTERFACE:${X11_INCLUDE_DIRS}>
    # Install interface paths
    $<INSTALL_INTERFACE:include/core_engine/core_editor>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/SDL2>
    $<INSTALL_INTERFACE:include/imgui>
    $<INSTALL_INTERFACE:include/FFmpeg>
    $<INSTALL_INTERFACE:include/nfd>
    $<INSTALL_INTERFACE:include/json>
)

target_include_directories(IlmeeeHub PRIVATE
    ${ICON_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/include/ui
    ${CMAKE_SOURCE_DIR}/include/core_engine
    ${NFD_INCLUDE_DIR}
    ${JSON_DIR}
    ${GLAD_4_DIR}/include
    ${XCB_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
)

target_include_directories(ViewPort3D PRIVATE
    ${CMAKE_SOURCE_DIR}/include/core_engine
    ${CMAKE_SOURCE_DIR}/include/vulkan
    ${CMAKE_SOURCE_DIR}/include/audio
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/include/ui
    ${NFD_INCLUDE_DIR}
    ${GTK3_INCLUDE_DIRS}
    ${LIBNOTIFY_INCLUDE_DIRS}
    ${XCB_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
)
# Removed forced include of xcb/xcb.h as it causes build failures and is already included where needed.

# First, find this section and modify it:
install(TARGETS IlmeeeEngine
    EXPORT IlmeeeEngineTargets    # Add this line
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Then keep your existing export commands:
install(EXPORT IlmeeeEngineTargets
    FILE IlmeeeEngineTargets.cmake
    NAMESPACE IlmeeeEngine::
    DESTINATION lib/cmake/IlmeeeEngine
)

export(EXPORT IlmeeeEngineTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEngineTargets.cmake"
    NAMESPACE IlmeeeEngine::
)
# Install headers files
install(FILES ${headers_CORE} DESTINATION include)

include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR lib/cmake/IlmeeeEngine)

# Generate the config file
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/IlmeeeEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEngineConfig.cmake
    INSTALL_DESTINATION ${CONFIG_INSTALL_DIR}
    PATH_VARS CONFIG_INSTALL_DIR
)

# Generate the version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install the config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEngineConfigVersion.cmake
    DESTINATION ${CONFIG_INSTALL_DIR}
)

# First, find this section and modify it:
install(TARGETS IlmeeeEditor
    EXPORT IlmeeeEditorTargets    # Add this line
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(EXPORT IlmeeeEditorTargets
    FILE IlmeeeEditorTargets.cmake
    NAMESPACE IlmeeeEditor::
    DESTINATION lib/cmake/IlmeeeEditor
)

export(EXPORT IlmeeeEditorTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEditorTargets.cmake"
    NAMESPACE IlmeeeEditor::
)

# Install headers files
install(FILES ${headers_CORE_EDITOR} DESTINATION include)

include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR lib/cmake/IlmeeeEditor)

# Generate the config file
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/IlmeeeEditorConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEditorConfig.cmake
    INSTALL_DESTINATION ${CONFIG_INSTALL_DIR}
    PATH_VARS CONFIG_INSTALL_DIR
)

# Generate the version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEditorConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install the config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEditorConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/IlmeeeEditorConfigVersion.cmake
    DESTINATION ${CONFIG_INSTALL_DIR}
)

# Documentation (optional)
find_package(Doxygen)
if(DOXYGEN_FOUND)
    option(BUILD_DOCUMENTATION "Create and install the HTML based API documentation (requires Doxygen)" ${DOXYGEN_FOUND})
    
    if(BUILD_DOCUMENTATION)
        set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in)
        set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
        
        configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
        
        add_custom_target(doc_doxygen ALL
            COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
        
        install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/html DESTINATION share/doc)
    endif()
endif()

# Print build information
message(STATUS "=== GameEngine Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATIS "OpenGL version: ${OPENGL_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build example: ${BUILD_EXAMPLE}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Build documentation: ${BUILD_DOCUMENTATION}")
message(STATUS "=====================================")

# CPack configuration for creating installers
include(CPack)
set(CPACK_PACKAGE_NAME "GameEngine")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A simple game engine library")
set(CPACK_PACKAGE_VENDOR "Your Company")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    KTX::ktx
    ${FFMPEG_LIBRARIES}
    glfw
    ${GTK3_LIBRARIES}
    glm::glm
    dl
    pthread
    ${DISCORD_SDK_LIBRARY}
    curl
)

target_link_libraries(TestBackground PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    OpenGL::GL
    glfw
    dl
    pthread
)

target_link_libraries(TestGLSL PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    OpenGL::GL
    ${FFMPEG_LIBRARIES}
    glfw
)

target_link_libraries(ViewPort3D PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    KTX::ktx
    ${FFMPEG_LIBRARIES}
    ${GTK3_LIBRARIES}
    ${LIBNOTIFY_LIBRARIES}
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
    glm::glm
)

target_link_libraries(HandlerIlmeeeEngine PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    ${GTK3_LIBRARIES}
)

target_link_libraries(IlmeeeHub PRIVATE    
    PkgConfig::SDL3
    Vulkan::Vulkan
    OpenGL::GL
    ${FFMPEG_LIBRARIES}
    ${GTK3_LIBRARIES}
    nfd
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
    glm::glm
)

target_link_libraries(IlmeeeEditor PRIVATE
    PkgConfig::SDL3
    Vulkan::Vulkan
    OpenGL::GL
    ${GTK3_LIBRARIES}
    ${XCB_LIBRARIES}
    ${X11_LIBRARIES}
)
