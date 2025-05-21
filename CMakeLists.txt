cmake_minimum_required(VERSION 3.10)
project(GameEngineSDL)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")

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