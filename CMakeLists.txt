cmake_minimum_required(VERSION 3.10)
project(GameEngineSDL)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add SDL2 and SDL2_ttf paths
set(SDL2_DIR "C:/SDL2")
set(SDL2_TTF_DIR "C:/SDL2_ttf") 
set(STB "D:/stb")
set(FFMPEG_DIR "D:/FFmpeg")
set(LIBRARY "C:/msys64/mingw64/bin")
# set(ROCKET_DIR "E:/Rocket/Include")

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

file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${IMGUI_SOURCES})

find_package(OpenGL REQUIRED)
# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    # "C:/msys64/mingw64/include"
    ${SDL2_DIR}/include
    # ${SDL2_TTF_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_SOURCE_DIR}/src/header
    ${FFMPEG_DIR}
    ${STB}
    # ${ROCKET_DIR}
    # ${ROCKET_DIR}/Rocket/Core
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${SDL2_DIR}/lib/x64/SDL2.lib
    # ${SDL2_DIR}/lib/x64/SDL2main.lib
    ${SDL2_DIR}/lib/x64/SDL2_ttf.lib
    # avformat
    # avcodec
    # avutil
    # swresample
    # swscale
    OpenGL::GL
    # glew32
    ${LIBRARY}/avformat.lib
    ${LIBRARY}/avcodec.lib 
    ${LIBRARY}/avutil.lib 
    ${LIBRARY}/swresample.lib
    ${LIBRARY}/swscale.lib
    # ${FFMPEG_DIR}
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
            $<TARGET_FILE_DIR:${PROJECT_NAME}>
            # "${LIBRARY}"
    )
endif()