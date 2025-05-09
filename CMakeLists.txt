cmake_minimum_required(VERSION 3.10)
project(GameEngineSDL)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add SDL2 and SDL2_ttf paths
set(SDL2_DIR "C:/SDL2")
set(SDL2_TTF_DIR "C:/SDL2_ttf") 
set(STB "E:/stb")
set(GLFW "C:/glfw3.4")
set(FFMPEG_DIR "E:/FFmpeg")
set(LIBRARY "C:/msys64/mingw64/bin")
set(GLES2 ${GLFW}/)

# Add source files
set(SOURCES 
    src/main.cpp
    src/scripts/MainWindow.cpp
    # src/RocketInterfaces.cpp
    src/SimpleRenderer.cpp
    src/scripts/SecondaryWindow.cpp
    src/scripts/Check_Environment.cpp
    src/scripts/VideoPlayer.cpp
    src/scripts/assets.cpp
)

# Add header files
set(HEADERS
    src/header/MainWindow.hpp
    # src/header/RocketInterfaces.hpp
    src/header/SimpleRenderer.hpp
    src/header/SecondaryWindow.hpp
    src/header/FFmpegWrapper.hpp
    src/header/VideoPlayer.hpp
    src/header/assets.hpp
)

set(SOURCE_TEST
    src/scripts/assets.cpp
    src/header/assets.hpp
    src/scripts/test_main.cpp
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
)


file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})
target_link_options(${PROJECT_NAME} PRIVATE -mconsole)
add_executable(TestBackground ${SOURCE_TEST} ${IMGUI_TEST})
target_link_options(TestBackground PRIVATE -mconsole)
find_package(OpenGL REQUIRED)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${SDL2_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header
    ${FFMPEG_DIR}
    ${STB}
    # GL
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
)

target_include_directories(TestBackground PRIVATE
    ${SDL2_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header
    ${FFMPEG_DIR}
    ${STB}
    ${GLFW}/include
    ${GLES2}
    opengl32
    gdi32
    # GLES2
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
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
    )
    # add_custom_command(TARGET TestBackground POST_BUILD
    #     COMMAND ${CMAKE_COMMAND} -E copy_if_different
    #         "${SDL2_DIR}/lib/x64/SDL2.dll"
    #         "${SDL2_DIR}/lib/x64/SDL2_ttf.dll"
    #         "${LIBRARY}/avformat-62.dll"
    #         "${LIBRARY}/avcodec-62.dll"
    #         "${LIBRARY}/avutil-60.dll"
    #         "${LIBRARY}/swresample-6.dll"
    #         "${LIBRARY}/swscale-9.dll"
    #         "${GLFW}/lib-static-ucrt/glfw3.dll"
    #         $<TARGET_FILE_DIR:TestBackground>
    # )
endif()