add_subdirectory(core_engine)
add_subdirectory(ui)
add_subdirectory(vulkan)

file(GLOB_RECURSE CPP_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(SOURCES_CPP ${CPP_FILES} ${SOURCES_CORE_ENGINE} ${SOURCES_UI} ${SOURCES_VULKAN} PARENT_SCOPE)

set(SOURCE_CORE ${SOURCES_CORE} PARENT_SCOPE)
set(SOURCE_CORE_ENGINE ${SOURCES_CORE_ENGINE} PARENT_SCOPE)
set(SOURCE_UI ${SOURCES_UI} PARENT_SCOPE)
set(SOURCE_VULKAN ${SOURCES_VULKAN} PARENT_SCOPE)

set(VIEWPORT_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/main_viewport3d.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/handlewindow_viewport3d.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/vulkanhandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/texture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/VkBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/VkDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vulkan/VkTools.cpp
    PARENT_SCOPE
)

set(HANDLER_ENGINE
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core/Launcher.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core/HandlerLauncher.cpp
    PARENT_SCOPE
)

set(MAIN_PROJECT_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/MainWindow.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/RocketInterfaces.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/SimpleRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ApplicationManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SecondaryWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/VideoPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/assets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/HandlerProject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/HandlerChildWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SceneSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SceneRenderer2D.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/TextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/Check_Environment.cpp
    PARENT_SCOPE
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)