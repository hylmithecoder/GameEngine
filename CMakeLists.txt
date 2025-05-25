cmake_minimum_required(VERSION 3.10)
project(GameEngineSDL VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")
option(BUILD_SHARED_LIBS "Build GameEngine as a shared libraries" ON)
# Output directories
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# Compiler-specific options
if(MSVC)
    add_compile_options(/W4)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Add SDL2 and SDL2_ttf paths
set(SDL2_DIR "C:/SDL2")
set(SDL2_TTF_DIR "C:/SDL2_ttf") 
set(STB "E:/stb")
set(GLFW "C:/glfw3.4")
set(FFMPEG_DIR "E:/FFmpeg")
set(LIBRARY "C:/msys64/mingw64/bin")
set(GLES2 ${GLFW}/)
set(NFD "E:/nativefiledialog-extended")
set(GLM "E:/glm")
set(VULKAN_SDK "C:/msys64/mingw64")
set(VULKAN_DIR "C:/vulkan")
set(VULKAN_LIB "${VULKAN_SDK}/lib/libvulkan-1.dll.a")
set(DLFCN "E:/dlfcn-win32")
# set(GLEW "C:/glew-2.1.0")

# Add source files
set(SOURCES 
    src/main.cpp
    src/resources.rc
    src/scripts/ui/MainWindow.cpp
    # src/RocketInterfaces.cpp
    # src/SimpleRenderer.cpp
    src/scripts/ui/SecondaryWindow.cpp
    src/scripts/ui/Check_Environment.cpp
    src/scripts/ui/VideoPlayer.cpp
    src/scripts/ui/assets.cpp
    src/scripts/ui/HandlerProject.cpp
    src/scripts/ui/HandlerChildWindow.cpp
    src/scripts/core_engine/SceneSerializer.cpp
    src/scripts/core_engine/SceneRenderer2D.cpp
    src/scripts/core_engine/TextureManager.cpp
)

set(SOURCE_TEST_VULKAN
    src/scripts/test_vulkan.cpp
)

set(SOURCE_CORE
    src/scripts/core_engine/core/GameEngine.cpp
    src/scripts/core_engine/core/CoreDebugger.cpp
)

# Add header files
set(HEADERS
    src/header/ui/MainWindow.hpp
    # src/header/RocketInterfaces.hpp
    src/header/ui/SimpleRenderer.hpp
    src/header/ui/SecondaryWindow.hpp
    src/header/ui/FFmpegWrapper.hpp
    src/header/ui/VideoPlayer.hpp
    src/header/ui/assets.hpp
    src/header/ui/HandlerProject.hpp
    src/header/core_engine/Scene.hpp
    src/header/core_engine/SceneSerializer.hpp
    src/header/core_engine/SceneRenderer2D.hpp
    src/header/core_engine/TextureManager.hpp
    src/header/core_engine/GLHeader.hpp
    src/header/core_engine/Debugger.hpp
)

set(HEADER_GLSL
    src/header/core_engine/test_glsl.hpp
)

set(HEADER_VULKAN
    src/header/core_engine/test_vulkan.hpp
)

set(HEADER_CORE
    # src/header/core_engine/core/Component.hpp
    # src/header/core_engine/core/Scene.hpp
    src/header/core_engine/core/GameEngine.h
    src/header/core_engine/core/CoreDebugger.h
    # src/header/core_engine/core/EngineCore.hpp
    # src/header/core_engine/core/LifeCycleManager.hpp
)

set(SOURCE_TEST
    src/scripts/ui/assets.cpp
    src/header/ui/assets.hpp
    src/scripts/ui/test_main.cpp
)

set(SOURCE_TEST_GLEW
    src/scripts/test_glew.cpp
)

set(SOURCE_TEST_GLM
    src/scripts/test_glm.cpp
)

set(SOURCE_TEST_GLSL
    src/scripts/test_glsl.cpp
)

# Add ImGui source files
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/vendor/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    # ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

set(ICON_DIR ${CMAKE_SOURCE_DIR}/vendor/icons)

set(NFD_SOURCES
    # ${NFD}/src/nfd_gtk.cpp
    # ${NFD}/src/nfd_portal.cpp
    ${NFD}/src/nfd_win.cpp
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

set(GLAD_DIR ${CMAKE_SOURCE_DIR}/vendor/glad/)
set(GLAD
    ${GLAD_DIR}/src/glad.c
)

set(GLAD_4_DIR ${CMAKE_SOURCE_DIR}/vendor/glad4.4)
set(GLAD_4
    ${GLAD_4_DIR}/src/glad.c
)

set(GLAD_VULKAN_DIR ${CMAKE_SOURCE_DIR}/vendor/glad_vulkan)
set(GLAD_VULKAN
    ${GLAD_VULKAN_DIR}/src/vulkan.c
)


file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

# Create executable
# add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
# add_compile_definitions(__STDC_WANT_LIB_EXT1__=0)
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES} ${NFD_SOURCES} ${ICON_SOURCE} ${JSON} ${GLAD_4})
target_link_options(${PROJECT_NAME} PRIVATE -mconsole)
add_executable(TestBackground ${SOURCE_TEST} ${IMGUI_SOURCES})
target_link_options(TestBackground PRIVATE -mconsole)
add_executable(TestGLEW ${SOURCE_TEST_GLEW} ${IMGUI_SOURCES} ${GLAD_4})
target_link_options(TestGLEW PRIVATE -mconsole)
add_executable(TestGLM ${SOURCE_TEST_GLM})
target_link_options(TestGLM PRIVATE -mconsole)
add_executable(TestGLSL ${SOURCE_TEST_GLSL} ${GLAD_4} ${IMGUI_SOURCES} ${HEADER_GLSL} ${ICON_SOURCE})
target_link_options(TestGLSL PRIVATE -mconsole)
add_executable(TestVulkan ${SOURCE_TEST_VULKAN} ${IMGUI_TEST} ${HEADER_VULKAN} ${ICON_SOURCE})
target_link_options(TestVulkan PRIVATE -mconsole)

add_library(CoreEngine SHARED ${SOURCE_CORE} ${HEADER_CORE})
set_target_properties(CoreEngine PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Define GAMEENGINE_EXPORTS when building the DLL
target_compile_definitions(CoreEngine PRIVATE GAMEENGINE_EXPORTS)

add_executable(HandlerIlmeeeEngine src/scripts/core_engine/core/Launcher.cpp)
target_link_options(HandlerIlmeeeEngine PRIVATE -mconsole)

# Set properties for the DLL
set_target_properties(CoreEngine PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    OUTPUT_NAME "CoreEngine"
)

target_link_options(CoreEngine PRIVATE -mconsole)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
# set(Vulkan_DIR "${VULKAN_SDK}/lib/cmake/VulkanLoader")

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_DIR}/include
    ${ICON_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header/ui
    ${CMAKE_SOURCE_DIR}/src/header/core_engine
    ${FFMPEG_DIR}
    ${NFD}/src/include
    ${JSON_DIR}
    ${STB}
    ${GLM}
    # GL
    # ${GLEW}/include/GL
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
    ${GLAD_4_DIR}/include
    ${VULKAN_DIR}
)

target_include_directories(TestBackground PRIVATE
    ${SDL2_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header/ui
    ${FFMPEG_DIR}
    ${STB}
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
    ${VULKAN_DIR}
    # GLES2
    # GLEW
    
)

target_include_directories(TestGLEW PRIVATE
    ${SDL2_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header/ui
    ${FFMPEG_DIR}
    ${STB}
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
    ${GLAD_4_DIR}/include
    ${VULKAN_DIR}
    # GLES2
    # GLEW
)

target_include_directories(TestGLM PRIVATE
    ${GLM}
    ${VULKAN_DIR}
    # GLES2
    # GLEW
)

target_include_directories(TestGLSL PRIVATE
    ${SDL2_DIR}/include
    ${ICON_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header/ui
    ${CMAKE_SOURCE_DIR}/src/header/core_engine
    ${FFMPEG_DIR}
    ${NFD}/src/include
    ${JSON_DIR}
    ${STB}
    ${GLM}
    # GL
    # ${GLEW}/include/GL
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
    ${GLAD_4_DIR}/include
    ${VULKAN_DIR}
)

target_include_directories(TestVulkan PRIVATE
    ${SDL2_DIR}/include
    ${ICON_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header/ui
    ${CMAKE_SOURCE_DIR}/src/header/core_engine
    ${FFMPEG_DIR}
    ${NFD}/src/include
    ${JSON_DIR}
    ${STB}
    ${GLM}
    # GL
    # ${GLEW}/include/GL
    ${GLFW}/include
    # ${GLES2}
    opengl32
    gdi32
    # ${GLAD_VULKAN_DIR}/include
    # ${VULKAN_SDK}/include
    ${VULKAN_DIR}
) 

target_include_directories(HandlerIlmeeeEngine PRIVATE
    ${DLFCN}/src
)

target_include_directories(CoreEngine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/src/header/core_engine/core>
    $<INSTALL_INTERFACE:include/core_engine/core>
)

# First, find this section and modify it:
install(TARGETS CoreEngine
    EXPORT CoreEngineTargets    # Add this line
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Then keep your existing export commands:
install(EXPORT CoreEngineTargets
    FILE CoreEngineTargets.cmake
    NAMESPACE CoreEngine::
    DESTINATION lib/cmake/CoreEngine
)

export(EXPORT CoreEngineTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/CoreEngineTargets.cmake"
    NAMESPACE CoreEngine::
)
# Install header files
install(FILES ${HEADER_CORE} DESTINATION include)

include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR lib/cmake/CoreEngine)

# Generate the config file
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/CoreEngineConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/CoreEngineConfig.cmake
    INSTALL_DESTINATION ${CONFIG_INSTALL_DIR}
    PATH_VARS CONFIG_INSTALL_DIR
)

# Generate the version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/CoreEngineConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install the config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/CoreEngineConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/CoreEngineConfigVersion.cmake
    DESTINATION ${CONFIG_INSTALL_DIR}
)

# Export targets
install(EXPORT CoreEngineTargets
    FILE CoreEngineTargets.cmake
    NAMESPACE CoreEngine::
    DESTINATION lib/cmake/CoreEngine
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
    ${SDL2_DIR}/lib/x64/SDL2.lib
    ${SDL2_DIR}/lib/x64/SDL2_ttf.lib
    OpenGL::GL
    ${LIBRARY}/avformat.lib
    ${LIBRARY}/avcodec.lib 
    ${LIBRARY}/avutil.lib 
    ${LIBRARY}/swresample.lib
    ${LIBRARY}/swscale.lib    
    ${GLFW}/lib-static-ucrt/glfw3dll.lib
    # ${GLEW}/lib/Release/x64/glew32.lib
    # GLEW::GLEW
)

target_link_libraries(TestBackground PRIVATE
    ${SDL2_DIR}/lib/x64/SDL2.lib
    ${SDL2_DIR}/lib/x64/SDL2_ttf.lib
    OpenGL::GL
    ${LIBRARY}/avformat.lib
    ${LIBRARY}/avcodec.lib 
    ${LIBRARY}/avutil.lib 
    ${LIBRARY}/swresample.lib
    ${LIBRARY}/swscale.lib
    ${GLFW}/lib-static-ucrt/glfw3dll.lib
    # GLEW::GLEW
)

target_link_libraries(TestGLEW PRIVATE
    ${SDL2_DIR}/lib/x64/SDL2.lib
    ${SDL2_DIR}/lib/x64/SDL2_ttf.lib
    OpenGL::GL
    ${LIBRARY}/avformat.lib
    ${LIBRARY}/avcodec.lib 
    ${LIBRARY}/avutil.lib 
    ${LIBRARY}/swresample.lib
    ${LIBRARY}/swscale.lib
    ${GLFW}/lib-static-ucrt/glfw3dll.lib
    # GLEW::GLEW
)

target_link_libraries(TestGLSL PRIVATE
    ${SDL2_DIR}/lib/x64/SDL2.lib
    ${SDL2_DIR}/lib/x64/SDL2_ttf.lib
    OpenGL::GL
    ${LIBRARY}/avformat.lib
    ${LIBRARY}/avcodec.lib 
    ${LIBRARY}/avutil.lib 
    ${LIBRARY}/swresample.lib
    ${LIBRARY}/swscale.lib    
    ${GLFW}/lib-static-ucrt/glfw3dll.lib
    # ${GLEW}/lib/Release/x64/glew32.lib
    # GLEW::GLEW
)

target_link_libraries(TestVulkan PRIVATE
    ${SDL2_DIR}/lib/x64/SDL2.lib
    ${SDL2_DIR}/lib/x64/SDL2_ttf.lib
    ${LIBRARY}/avformat.lib
    ${LIBRARY}/avcodec.lib 
    ${LIBRARY}/avutil.lib 
    ${LIBRARY}/swresample.lib
    ${LIBRARY}/swscale.lib
    ${LIBRARY}/vulkan-1.dll
    ${VULKAN_LIB}
    ${GLFW}/lib-static-ucrt/glfw3dll.lib
    # ${GLEW}/lib/Release/x64/glew32.lib
    # GLEW::GLEW
)

target_link_libraries(HandlerIlmeeeEngine PRIVATE
    ${DLFCN}/build/lib/libdl.dll.a
)

# Copy DLL files to output directory
if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_DIR}/lib/x64/SDL2.dll"
            "${SDL2_DIR}/lib/x64/SDL2_ttf.dll"
            "${LIBRARY}/avformat-62.dll"
            "${LIBRARY}/avcodec-62.dll"
            "${LIBRARY}/avutil-60.dll"
            "${LIBRARY}/swresample-6.dll"
            "${LIBRARY}/swscale-9.dll"
            "${GLFW}/lib-static-ucrt/glfw3.dll"
            # "${GLEW}/bin/Release/x64/glew32.dll"
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    add_custom_command(TARGET TestVulkan POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIBRARY}/vulkan-1.dll"
            "${VULKAN_LIB}"
            $<TARGET_FILE_DIR:TestVulkan>
    )
endif()
