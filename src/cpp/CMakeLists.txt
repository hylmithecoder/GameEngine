add_subdirectory(core_engine)
add_subdirectory(ui)
add_subdirectory(vulkan)
add_subdirectory(optimizer)

file(GLOB_RECURSE CPP_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(SOURCES_CPP ${CPP_FILES} ${SOURCES_CORE_ENGINE} ${SOURCES_UI} ${SOURCES_VULKAN} ${OPTIMIZER} PARENT_SCOPE)

set(SOURCE_CORE ${SOURCES_CORE} PARENT_SCOPE)
set(SOURCE_CORE_ENGINE ${SOURCES_CORE_ENGINE} PARENT_SCOPE)
set(SOURCE_UI ${SOURCES_UI} PARENT_SCOPE)
set(SOURCE_VULKAN ${SOURCES_VULKAN} PARENT_SCOPE)
set(SOURCE_OPTIMIZER ${OPTIMIZER} PARENT_SCOPE)

set(HANDLER_ENGINE
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core/Launcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core/HandlerLauncher.cpp
    PARENT_SCOPE
)

set(MAIN_PROJECT_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/WorldText.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/WorldTextRuntime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/MainWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/McpTools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LightingPanel.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/RocketInterfaces.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/SimpleRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ApplicationManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SecondaryWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/VideoPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/assets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/HandlerProject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ScriptDialogs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/NativeScriptInspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/DialogInspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AnimationEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/AnimationControllerEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SpringBoneInspector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/HandlerChildWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/GameBuildPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/ViewportGizmo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SceneSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SceneRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SkeletalAnimation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/AnimationAuthoring.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/AnimationTooling.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/AnimationRuntime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SpringBone.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/AnimationController.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/AudioEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/VideoDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/MediaSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SceneLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/GameUI.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/DialogSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/MovementScript.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/MovementRuntime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/PhysicsWorld.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/PhysicsRuntime.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/UserMovementScripts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/ScriptWorkspace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/NativeScripts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/NativeScriptScene.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/GameBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/ModelDependencies.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/EditorSessionHistory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/Builder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/TextureManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/Check_Environment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/Discordrich.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/PMXLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/ModelThumbnail.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/SoftwareRaster.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/SvgIconManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core_editor/EditorDockSpace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core_editor/EditorTheme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core_editor/panels/PanelManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core_engine/core_editor/panels/VideoPlayerPanel.cpp
    ${OPTIMIZER}
    ${SOURCES_VULKAN}
    PARENT_SCOPE
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
