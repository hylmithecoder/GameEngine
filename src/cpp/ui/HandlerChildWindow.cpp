#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/IlmeeeScene.hpp"
#include "../../../include/core_engine/InspectMode.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nfd.h>

void MainWindow::RenderHierarchyWindow() {
  Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Hierarchy panel");
  ImGuiTreeNodeFlags nodeFlags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

  // Base Template: Ensure there is always a camera in the scene renderer!
  if (sceneRenderer && !sceneRenderer->HasPlayerCamera()) {
    sceneRenderer->LoadCamera("Main Camera");
  }

  if (TreeNodeEx("Scene", nodeFlags | ImGuiTreeNodeFlags_DefaultOpen)) {
    if (sceneRenderer) {
      for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i) {
        const std::string &meshName = sceneRenderer->GetMesh3DName(i);
        bool isCamera = sceneRenderer->IsMesh3DCamera(i);
        bool isLight = sceneRenderer->IsMesh3DLight(i);

        // Pick SVG icon and label prefix
        std::string svgPath = "assets/icons/svg/box.svg";
        std::string prefix = "";
        if (isCamera) {
          svgPath = "assets/icons/svg/file.svg";
          prefix = "[Cam] ";
        } else if (isLight) {
          svgPath = "assets/icons/svg/file.svg";
          prefix = "[Light] ";
        }

        // Draw icon
        svgIcons.DrawIcon(svgPath, 16);

        ImGuiTreeNodeFlags leafFlags = nodeFlags | ImGuiTreeNodeFlags_Leaf;
        bool selected = (meshName == objectName);
        if (selected)
          leafFlags |= ImGuiTreeNodeFlags_Selected;

        PushID((int)i);
        std::string label = prefix + meshName;
        if (TreeNodeEx(label.c_str(), leafFlags)) {
          DEBUG_TRACE_ITEM("Hierarchy: scene mesh entry");
          if (IsItemClicked()) {
            std::snprintf(objectName, sizeof(objectName), "%s",
                          meshName.c_str());
          }
          TreePop();
        }
        PopID();
      }
    }
    TreePop();
  }
  End();
}

void MainWindow::RenderExplorerWindow(HandlerProject::AssetFile projectRoot,
                                      HandlerProject::AssetFile assetFolder,
                                      const string &assetPath,
                                      bool firstOpenProject) {
  Begin("Explorer", nullptr, ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Explorer panel");
  ImVec2 pos = GetWindowPos();
  ImVec2 size = GetWindowSize();
  HandleBackground(
      pos, size); // panggil di sini!
                  // ::Log("Asset Folder: " + assetFolder.fullPath +
                  // "\nChildren: "+to_string(assetFolder.children.size()),
                  // Debug::LogLevel::INFO);
  if (firstOpenProject) {
    BeginGroup();

    // Add toolbar above assets
    PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
    PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
    PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));

    // Refresh button
    if (Button("Refresh")) {
      projectHandler.isOpenedProject = true;
    }

    // Tooltip for refresh button
    if (IsItemHovered()) {
      SetTooltip("Manually refresh asset tree");
    }

    SameLine();

    // Add file watcher toggle button
    bool watcherRunning = projectHandler.IsFileWatcherRunning();
    if (Button(watcherRunning ? "Watching" : "Watch Off")) {
      if (watcherRunning) {
        projectHandler.StopFileWatcher();
      } else {
        projectHandler.StartFileWatcher();
      }
    }

    // Tooltip for watcher button
    if (IsItemHovered()) {
      SetTooltip(watcherRunning ? "Auto-refresh is active"
                                : "Turn on auto-refresh");
    }

    SameLine();

    // Back button - disabled if in root directory
    string rootPath = projectHandler.projectPath + "\\assets";
    bool isInRootDirectory = (projectHandler.currentDirectory == rootPath);

    // Disable button if in root directory
    if (isInRootDirectory) {
      PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * 0.5f);
      PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
      Button("Back");
      PopStyleColor();
      PopStyleVar();

      if (IsItemHovered()) {
        SetTooltip("Already in root directory");
      }
    } else {
      if (Button("Back")) {
        size_t lastSlash = projectHandler.currentDirectory.find_last_of("/\\");
        if (lastSlash != string::npos) {
          projectHandler.currentDirectory =
              projectHandler.currentDirectory.substr(0, lastSlash);
          projectHandler.selectedAsset =
              nullptr; // Reset selection when navigating
        }
      }

      if (IsItemHovered()) {
        SetTooltip("Go back to parent folder");
      }
    }

    SameLine();

    // Add filter/search box
    HandleSearch();

    PopStyleColor(3);

    // Get available content region
    ImVec2 contentSize = GetContentRegionAvail();

    // Left panel (Project tree)
    BeginChild("ProjectRoot", ImVec2(explorerSplitPosition, 0), true);
    projectHandler.DrawAssetTree(projectRoot);
    EndChild();

    // Splitter
    SameLine();
    PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
    Button("##Splitter", ImVec2(4.0f, contentSize.y));
    PopStyleColor();

    // Handle splitter dragging
    if (IsItemActive()) {
      float delta = GetIO().MouseDelta.x;
      if (explorerSplitPosition + delta >= MIN_PANEL_WIDTH &&
          explorerSplitPosition + delta <= contentSize.x - MIN_PANEL_WIDTH) {
        explorerSplitPosition += delta;
      }
    }

    // Show resize cursor when hovering over splitter
    if (IsItemHovered())
      SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    // Right panel (File explorer)
    SameLine();
    BeginChild("AssetRoot", ImVec2(0, 0), true);
    // Fix: Get files directly from projectHandler instead of through
    // selectedAsset
    // ::Log("If You See it this is work in method RenderExplorerWindow",
    // Debug::LogLevel::SUCCESS);
    projectHandler.DrawFolderGridView();
    EndChild();

    EndGroup();
  }

  End();
}

void MainWindow::RenderInspectorWindow() {
  static char currentScriptName[256] = "";
  strncpy(currentScriptName, currentScriptName, sizeof(currentScriptName) - 1);
  currentScriptName[sizeof(currentScriptName) - 1] =
      '\0'; // Ensure null-termination
  Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Inspector panel");
  ImVec2 pos = GetWindowPos();
  ImVec2 size = GetWindowSize();
  HandleBackground(pos, size);

  Text("Selected Object");
  Separator();

  // if (isLoadScene)
  // {
  //     // Still Dummy Inspector Yeah
  //     for (auto& obj : projectHandler.currentScene.objects) {
  //             //
  //             projectHandler.DrawIconFromImage("assets/images/fileicons/box.png");
  //             // if (TreeNodeEx(obj.name.c_str())) {
  //             //     TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f),
  //             "Properties");
  //             //     TreePop();
  //             // }
  //             // Use a temporary buffer for editing the name
  //             char nameBuffer[256];
  //             char spritePath[256];
  //             strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer) - 1);
  //             nameBuffer[sizeof(nameBuffer) - 1] = '\0';

  //             for (int i = 0; i < obj.name.length(); i++) {
  //                 PushID(i);
  //                 if (InputText("##Name", nameBuffer,
  //                 IM_ARRAYSIZE(nameBuffer))) {
  //                     obj.name = nameBuffer;
  //                 };
  //                 PopID();
  //                 if (CollapsingHeader("Transform",
  //                 ImGuiTreeNodeFlags_DefaultOpen)) {
  //                     // DragFloat3("Position", obj.x,obj.y,obj.width,
  //                     0.1f);
  //                     // DragFloat3("Rotation", obj.rotation, 0.1f);
  //                     // DragFloat3("Scale", obj.scale, 0.1f);
  //                 }
  //                 PushID(i);
  //                 if (CollapsingHeader("Sprite"),
  //                 ImGuiTreeNodeFlags_DefaultOpen) {
  //                     if (InputText("Sprite Path", spritePath,
  //                     IM_ARRAYSIZE(spritePath))) {
  //                         obj.spritePath = spritePath;
  //                     }
  //                 }
  //                 PopID();
  //             }
  //         }
  // }
  // else {
  InputText("Name", objectName, IM_ARRAYSIZE(objectName));

  // Find which loaded mesh (if any) corresponds to the selected name.
  int meshIdx = -1;
  if (sceneRenderer) {
    for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i) {
      if (sceneRenderer->GetMesh3DName(i) == objectName) {
        meshIdx = (int)i;
        break;
      }
    }
  }
  const bool meshSelected = meshIdx >= 0;

  if (meshSelected) {
    glm::vec3 p = sceneRenderer->GetMesh3DPosition((size_t)meshIdx);
    glm::vec3 r = sceneRenderer->GetMesh3DRotation((size_t)meshIdx);
    glm::vec3 s = sceneRenderer->GetMesh3DScale((size_t)meshIdx);
    position[0] = p.x;
    position[1] = p.y;
    position[2] = p.z;
    rotation[0] = r.x;
    rotation[1] = r.y;
    rotation[2] = r.z;
    scale[0] = s.x;
    scale[1] = s.y;
    scale[2] = s.z;
  }

  if (CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    DEBUG_TRACE_ITEM("Inspector: Transform header");
    bool changed = false;
    changed |= DragFloat3("Position", position, 0.02f);
    DEBUG_TRACE_ITEM("Inspector: Position drag");
    changed |= DragFloat3("Rotation", rotation, 0.5f);
    DEBUG_TRACE_ITEM("Inspector: Rotation drag");
    changed |= DragFloat3("Scale", scale, 0.01f, 0.001f, 100.0f);
    DEBUG_TRACE_ITEM("Inspector: Scale drag");
    if (meshSelected && changed) {
      sceneRenderer->SetMesh3DTransform(
          (size_t)meshIdx, glm::vec3(position[0], position[1], position[2]),
          glm::vec3(rotation[0], rotation[1], rotation[2]),
          glm::vec3(scale[0], scale[1], scale[2]));
    }
  }

  const bool isLight =
      meshSelected && sceneRenderer->IsMesh3DLight((size_t)meshIdx);
  const bool isCamera =
      meshSelected && sceneRenderer->IsMesh3DCamera((size_t)meshIdx);

  // Mesh section: shown for any plain mesh (not a light/camera) by name.
  if (meshSelected && !isLight && !isCamera) {
    if (CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
      Text("Path: %s", sceneRenderer->GetMesh3DPath((size_t)meshIdx).c_str());
      Text("Vertices: %u",
           sceneRenderer->GetMesh3DVertexCount((size_t)meshIdx));
      Text("Triangles: %u",
           sceneRenderer->GetMesh3DTriangleCount((size_t)meshIdx));
      uint32_t boneCount = sceneRenderer->GetMesh3DBoneCount((size_t)meshIdx);
      if (boneCount > 0) {
        Text("Bones: %u", boneCount);
      }

      // --- Surfaces (materials) → per-surface texture binding ---
      // Surfaces are auto-detected from the model's materials (PMX) or a
      // single whole-mesh surface (OBJ/primitives). Click a surface in the
      // viewport to target it, or pick it from this list, then bind a texture.
      uint32_t surfaceCount =
          sceneRenderer->GetMesh3DSubmeshCount((size_t)meshIdx);
      if (surfaceCount > 0) {
        Separator();
        Text("Surfaces: %u", surfaceCount);
        TextDisabled("Click a surface in the viewport, or select below.");
        if (selectedSurface < 0 || selectedSurface >= (int)surfaceCount)
          selectedSurface = 0;

        float rows = surfaceCount < 6u ? (float)surfaceCount : 6.0f;
        ImVec2 listSize(0.0f, rows * GetTextLineHeightWithSpacing() + 8.0f);
        if (BeginChild("##surfacelist", listSize, true)) {
          for (uint32_t s = 0; s < surfaceCount; ++s) {
            const std::string &sname =
                sceneRenderer->GetMesh3DSubmeshName((size_t)meshIdx, s);
            const std::string &stex =
                sceneRenderer->GetMesh3DSubmeshTexture((size_t)meshIdx, s);
            char label[192];
            std::snprintf(label, sizeof(label), "%u  %s%s", s,
                          sname.empty() ? "(surface)" : sname.c_str(),
                          stex.empty() ? "" : "   [textured]");
            if (Selectable(label, selectedSurface == (int)s))
              selectedSurface = (int)s;
          }
        }
        EndChild();

        const std::string &curTex = sceneRenderer->GetMesh3DSubmeshTexture(
            (size_t)meshIdx, (uint32_t)selectedSurface);
        TextWrapped("Texture: %s",
                    curTex.empty() ? "(none — diffuse color)" : curTex.c_str());
        if (Button("Bind Texture...")) {
          NFD_Init();
          nfdchar_t *outPath = nullptr;
          nfdfilteritem_t filters[1] = {
              {"Image", "png,jpg,jpeg,bmp,tga,gif,psd"}};
          if (NFD_OpenDialog(&outPath, filters, 1, nullptr) == NFD_OKAY &&
              outPath) {
            sceneRenderer->BindMesh3DSubmeshTexture(
                (size_t)meshIdx, (uint32_t)selectedSurface, outPath);
            NFD_FreePath(outPath);
          }
          NFD_Quit();
        }
        SameLine();
        if (Button("Clear Texture")) {
          sceneRenderer->ClearMesh3DSubmeshTexture((size_t)meshIdx,
                                                   (uint32_t)selectedSurface);
        }
        Separator();
      }

      static bool s_gridVisible = true;
      if (Checkbox("Show 3D Grid", &s_gridVisible)) {
        sceneRenderer->SetGrid3DVisible(s_gridVisible);
      }
      if (Button("Reset Transform")) {
        sceneRenderer->SetMesh3DTransform((size_t)meshIdx, glm::vec3(0.0f),
                                          glm::vec3(0.0f), glm::vec3(1.0f));
      }
    }
  }

  // Light Source section: only shown if selected object is a light
  if (isLight) {
    if (CollapsingHeader("Light Source", ImGuiTreeNodeFlags_DefaultOpen)) {
      int type = sceneRenderer->GetMesh3DLightType((size_t)meshIdx);
      float gamma = sceneRenderer->GetMesh3DLightGamma((size_t)meshIdx);
      glm::vec3 color = sceneRenderer->GetMesh3DLightColor((size_t)meshIdx);
      float intensity = sceneRenderer->GetMesh3DLightIntensity((size_t)meshIdx);
      float range = sceneRenderer->GetMesh3DLightRange((size_t)meshIdx);
      float spotAngle = sceneRenderer->GetMesh3DLightSpotAngle((size_t)meshIdx);

      const char *types[] = {"Directional", "Point", "Spotlight (Senter)"};
      if (Combo("Light Type", &type, types, IM_ARRAYSIZE(types))) {
        sceneRenderer->SetMesh3DLightType((size_t)meshIdx, type);
      }

      float col[3] = {color.x, color.y, color.z};
      if (ColorEdit3("Color", col)) {
        sceneRenderer->SetMesh3DLightColor((size_t)meshIdx,
                                           glm::vec3(col[0], col[1], col[2]));
      }

      if (DragFloat("Intensity", &intensity, 0.05f, 0.0f, 20.0f, "%.2f")) {
        sceneRenderer->SetMesh3DLightIntensity((size_t)meshIdx, intensity);
      }

      if (type > 0) { // Point or Spotlight
        if (DragFloat("Range", &range, 0.1f, 0.1f, 1000.0f, "%.1f")) {
          sceneRenderer->SetMesh3DLightRange((size_t)meshIdx, range);
        }
      }

      if (type == 2) { // Spotlight (Senter)
        if (SliderFloat("Spot Angle", &spotAngle, 1.0f, 179.0f, "%.1f deg")) {
          sceneRenderer->SetMesh3DLightSpotAngle((size_t)meshIdx, spotAngle);
        }
      }

      if (SliderFloat("Lighting Gamma", &gamma, 0.2f, 4.0f, "%.2f")) {
        sceneRenderer->SetMesh3DLightGamma((size_t)meshIdx, gamma);
      }
    }
  }

  // Camera section: only shown if the selected object is a player camera.
  if (isCamera) {
    if (CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
      int proj = sceneRenderer->GetMesh3DCameraProjection((size_t)meshIdx);
      float fov = sceneRenderer->GetMesh3DCameraFov((size_t)meshIdx);
      float orthoSize =
          sceneRenderer->GetMesh3DCameraOrthoSize((size_t)meshIdx);
      float nearP = sceneRenderer->GetMesh3DCameraNear((size_t)meshIdx);
      float farP = sceneRenderer->GetMesh3DCameraFar((size_t)meshIdx);

      const char *projections[] = {"Perspective", "Orthographic"};
      if (Combo("Projection", &proj, projections, IM_ARRAYSIZE(projections))) {
        sceneRenderer->SetMesh3DCameraProjection((size_t)meshIdx, proj);
      }

      if (proj == 0) { // Perspective
        if (SliderFloat("FOV", &fov, 10.0f, 120.0f, "%.0f deg")) {
          sceneRenderer->SetMesh3DCameraFov((size_t)meshIdx, fov);
        }
      } else { // Orthographic
        if (DragFloat("Ortho Size", &orthoSize, 0.1f, 0.1f, 500.0f, "%.2f")) {
          sceneRenderer->SetMesh3DCameraOrthoSize((size_t)meshIdx, orthoSize);
        }
      }

      if (DragFloat("Near", &nearP, 0.01f, 0.001f, farP - 0.01f, "%.3f")) {
        sceneRenderer->SetMesh3DCameraNear((size_t)meshIdx, nearP);
      }
      if (DragFloat("Far", &farP, 0.5f, nearP + 0.01f, 5000.0f, "%.1f")) {
        sceneRenderer->SetMesh3DCameraFar((size_t)meshIdx, farP);
      }

      TextDisabled("Aim with the object's Rotation. See the cyan");
      TextDisabled("frustum gizmo + the Camera Preview window.");
    }
  }

  if (!isLight && !isCamera) {
    if (CollapsingHeader("Material")) {
      static float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
      ColorEdit4("Color", color);

      const char *items[] = {"Standard", "Transparent", "Emission"};
      static int item_current = 0;
      Combo("Shader", &item_current, items, IM_ARRAYSIZE(items));

      static float metallic = 0.0f;
      static float smoothness = 0.5f;
      SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
      SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f);
    }
  }

  // Physics component (if attached)
  if (meshSelected && sceneRenderer->meshes3d[meshIdx].hasPhysics) {
    if (CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
      Checkbox("Use Gravity", &sceneRenderer->meshes3d[meshIdx].useGravity);
      Checkbox("Is Kinematic", &sceneRenderer->meshes3d[meshIdx].isKinematic);
      InputFloat("Mass", &sceneRenderer->meshes3d[meshIdx].mass, 0.1f);
      InputFloat("Drag", &sceneRenderer->meshes3d[meshIdx].drag, 0.01f);
      InputFloat("Gravity Y", &sceneRenderer->meshes3d[meshIdx].gravityY, 0.1f);
    }
  }

  // Audio Source component (if attached)
  if (meshSelected && sceneRenderer->meshes3d[meshIdx].hasAudio) {
    if (CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
      TextWrapped("Audio File: %s",
                  sceneRenderer->meshes3d[meshIdx].audioPath.empty()
                      ? "(none)"
                      : sceneRenderer->meshes3d[meshIdx].audioPath.c_str());
      if (Button("Browse Audio...")) {
        NFD_Init();
        nfdchar_t *outPath = nullptr;
        nfdfilteritem_t filters[1] = {{"Audio", "mp3,wav,ogg,flac,aac"}};
        if (NFD_OpenDialog(&outPath, filters, 1, nullptr) == NFD_OKAY &&
            outPath) {
          sceneRenderer->meshes3d[meshIdx].audioPath = outPath;
          NFD_FreePath(outPath);
        }
        NFD_Quit();
      }
      SameLine();
      if (Button("Play/Pause")) {
        sceneRenderer->meshes3d[meshIdx].isPlaying =
            !sceneRenderer->meshes3d[meshIdx].isPlaying;
      }
      SameLine();
      if (Button("Clear")) {
        sceneRenderer->meshes3d[meshIdx].audioPath = "";
        sceneRenderer->meshes3d[meshIdx].isPlaying = false;
      }
      if (sceneRenderer->meshes3d[meshIdx].isPlaying) {
        TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Playing...");
      } else {
        TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Stopped");
      }
    }
  }

  if (Button("Add Component", ImVec2(-1, 0))) {
    OpenPopup("AddComponentPopup");
  }
  DEBUG_TRACE_ITEM("Inspector: Add Component button");

  if (BeginPopup("AddComponentPopup")) {
    Text("Components");
    if (Selectable("Physics Body")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].hasPhysics = true;
      }
    }
    if (Selectable("Audio Source")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].hasAudio = true;
      }
    }
    if (Selectable("Light")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].isLight = true;
        sceneRenderer->meshes3d[meshIdx].isCamera = false;
      }
    }
    if (Selectable("Camera")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].isCamera = true;
        sceneRenderer->meshes3d[meshIdx].isLight = false;
      }
    }

    // Script creation section
    static bool showScriptInput = false;
    if (Selectable("Script")) {
      showScriptInput = true;
    }

    // Show script input UI when Script is selected
    if (showScriptInput) {
      Separator();
      Text("Create New Script");

      // Script input field with better styling
      PushItemWidth(-1); // Make input field fill available width
      bool entered = InputText("##ScriptName", currentScriptName,
                               IM_ARRAYSIZE(currentScriptName),
                               ImGuiInputTextFlags_EnterReturnsTrue);

      // Show placeholder if empty
      if (strlen(currentScriptName) == 0) {
        SameLine();
        SetCursorPosX(GetItemRectMin().x + 5);
        TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Enter script name...");
      }

      // Create and Cancel buttons
      if (Button("Create", ImVec2(120, 0)) || entered) {
        if (strlen(currentScriptName) > 0) {
          cout << "Create" << endl;
          projectHandler.NewScripts(currentScriptName);
          projectHandler.ShowNotification("Script Created",
                                          "Script created successfully",
                                          projectHandler.blueColor);
          // Reload file
          projectHandler.isOpenedProject = true;
          showScriptInput = false;
          memset(currentScriptName, 0,
                 sizeof(currentScriptName)); // Clear input
          CloseCurrentPopup();
        }
      }
      SameLine();
      if (Button("Cancel", ImVec2(120, 0))) {
        showScriptInput = false;
        projectHandler.ShowNotification("Script Creation Cancelled",
                                        "Script creation cancelled",
                                        projectHandler.redColor);
        memset(currentScriptName, 0, sizeof(currentScriptName)); // Clear input
      }

      PopItemWidth();
    }

    EndPopup();
    // }
  }
  End();
}

void MainWindow::RenderSceneToolbarView(ImVec2 parentPos, ImVec2 parentSize) {
  // Calculate toolbar position (top-left corner of scene window with some
  // padding)
  ImVec2 toolbarPos = ImVec2(parentPos.x + 10, parentPos.y + 30);

  // Set toolbar window properties
  SetNextWindowPos(toolbarPos, ImGuiCond_Always);
  SetNextWindowBgAlpha(0.8f); // Semi-transparent background

  // Toolbar window flags
  ImGuiWindowFlags toolbar_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      // ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
  // Begin floating toolbar
  if (Begin("Scene Toolbar", nullptr, toolbar_flags)) {
    DEBUG_TRACE_PANEL("Scene Toolbar (Reset/Zoom/Grid/Snap)");
    // Reset View Button
    if (Button("Reset View")) {
      ::Log("Resetting camera view", Debug::LogLevel::INFO);
      sceneRenderer->ResetCamera();
    }

    SameLine();

    // Zoom controls
    // static float zoom = sceneRenderer->zoom;
    SetNextItemWidth(100);
    if (SliderFloat("##Zoom", &sceneRenderer->zoom, 1.0f, 10.0f, "%.2fx")) {
      sceneRenderer->SetCameraZoom(sceneRenderer->zoom);
    }

    SameLine();
    Text("Zoom");

    // New line for more controls
    NewLine();

    // Grid size control
    static float gridSize = 50.0f;
    SetNextItemWidth(80);
    if (DragFloat("##GridSize", &gridSize, 1.0f, 10.0f, 200.0f, "%.0f")) {
      sceneRenderer->SetGridSize(gridSize);
    }

    SameLine();
    Text("Grid Size");

    // Third line for color controls
    NewLine();

    // Grid color picker (compact)
    static float gridColor[3] = {0.5f, 0.5f, 0.5f};
    SetNextItemWidth(60);
    if (ColorEdit3("##GridColor", gridColor,
                   ImGuiColorEditFlags_NoInputs |
                       ImGuiColorEditFlags_NoLabel)) {
      sceneRenderer->SetGridColor(gridColor[0], gridColor[1], gridColor[2],
                                  1.0f);
    }

    SameLine();
    Text("Grid");

    SameLine();

    // Background color picker (compact)
    static float bgColor[3] = {0.2f, 0.2f, 0.2f};
    SetNextItemWidth(60);
    if (ColorEdit3("##BgColor", bgColor,
                   ImGuiColorEditFlags_NoInputs |
                       ImGuiColorEditFlags_NoLabel)) {
      // if (sceneRenderer) {
      sceneRenderer->SetBackgroundColor(bgColor[0], bgColor[1], bgColor[2],
                                        1.0f); // You'll need to implement this
                                               // }
    }

    SameLine();
    Text("Background");

    // Snap to grid toggle
    NewLine();
    static bool snapToGrid = false;
    if (Checkbox("Snap to Grid", &snapToGrid)) {
      sceneRenderer->SetSnapToGrid(snapToGrid);
    }

    SameLine();

    // View mode selector
    static int viewMode = 0;
    const char *viewModes[] = {"2D", "3D", "Wireframe"};
    SetNextItemWidth(80);
    Combo("##ViewMode", &viewMode, viewModes, IM_ARRAYSIZE(viewModes));
  }
  End();

  PopStyleVar(1);
}

void MainWindow::RenderSceneWindow() {
  if (!showScene)
    return;

  // Window flags untuk menghilangkan padding dan scrollbar
  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse; // Tambahkan flag ini

  // Set window properties
  SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
  SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
  SetNextWindowBgAlpha(0.0f);

  // Push style untuk menghilangkan padding window
  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  // PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  // PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
  // PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

  if (Begin("Scene", &showScene, window_flags)) {
    DEBUG_TRACE_PANEL("Scene viewport panel");
    ImVec2 windowPos = GetWindowPos();
    ImVec2 windowSize = GetWindowSize();
    ImVec2 contentSize = GetContentRegionAvail();

    // First-frame scene bootstrap. Two modes:
    //   - Standalone debug (no project loaded): boot either the 3D OBJ
    //     fallback (Yixuan) or, when --2d was passed, a 2D sprite scene
    //     using assets/testimage.png — both are intentional dev paths
    //     so the engine renders something without needing a project.
    //   - Project mode (projectPath set): obey the project's
    //     scenes/main.ilmeeescene blueprint. Auto-create it with a
    //     single Cube if missing so a fresh project still renders.
    static std::string s_loadedForProject = "<none>";
    const std::string activeProject = projectHandler.projectPath;
    const std::string desired =
        activeProject.empty()
            ? (debug2D ? "<standalone-2d>" : "<standalone-3d>")
            : activeProject;
    if (s_loadedForProject != desired) {
      sceneRenderer->ClearMeshes3D();
      projectHandler.currentScene.objects.clear();

      if (activeProject.empty()) {
        if (debug2D) {
          // 2D debug fallback: drop a single sprite at the world origin
          // sized 300x300 px. The 2D orthographic projection is pixel-
          // based (-W/2..+W/2), so this lands centered in any panel
          // size. The mesh-pipeline 3D grid is hidden to keep the view
          // clean — the 2D grid (toggle in the Scene toolbar) still
          // works.
          GameObject sprite;
          sprite.name = "TestSprite";
          sprite.spritePath = "assets/testimage.png";
          sprite.x = 0.0f;
          sprite.y = 0.0f;
          sprite.width = 300.0f;
          sprite.height = 300.0f;
          projectHandler.currentScene.objects.push_back(sprite);
          sceneRenderer->SetGrid3DVisible(false);
          ::Log("Standalone 2D debug scene: testimage sprite loaded.",
                Debug::LogLevel::SUCCESS);
          s_loadedForProject = desired;
        } else {
          // 3D fallback (intentional — kept so engineers can boot
          // GameEngineSDL directly for first-gen debugging).
          // Prefer PMX model if available, fall back to OBJ.
          bool loaded = false;
          if (!loaded)
            loaded =
                sceneRenderer->LoadPMXMesh("assets/3dmodels/wise/wise.pmx");
          if (!loaded)
            loaded = sceneRenderer->LoadObjMesh("assets/3dmodels/belle.obj");
          if (loaded) {
            sceneRenderer->SetMesh3DDebugSource(
                sceneRenderer->GetMesh3DCount() - 1, __FILE__, __LINE__);
            s_loadedForProject = desired;
          }
        }
      } else {
        namespace fs = std::filesystem;
        fs::path scenesDir = fs::path(activeProject) / "scenes";
        std::error_code ec;
        fs::create_directories(scenesDir, ec);
        fs::path mainScene = scenesDir / "main.ilmeeescene";

        ilmeee::IlmeeeScene scene;
        if (!ilmeee::LoadScene(mainScene.string(), scene)) {
          scene = ilmeee::DefaultScene();
          ilmeee::SaveScene(mainScene.string(), scene);
          ::Log("Created default scene at " + mainScene.string(),
                Debug::LogLevel::SUCCESS);
        } else {
          ::Log("Loaded scene " + mainScene.string(), Debug::LogLevel::SUCCESS);
        }

        for (const auto &e : scene.entities) {
          bool ok = false;
          switch (e.kind) {
          case ilmeee::PrimitiveKind::Cube:
            ok = sceneRenderer->LoadCube(e.name);
            break;
          case ilmeee::PrimitiveKind::Sphere:
            ok = sceneRenderer->LoadSphere(e.name);
            break;
          case ilmeee::PrimitiveKind::Plane:
            ok = sceneRenderer->LoadPlane(e.name);
            break;
          case ilmeee::PrimitiveKind::ExternalObj: {
            fs::path full = fs::path(activeProject) / e.externalPath;
            std::string ext = full.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            // Auto-detect by extension: .pmx → PMX, .fbx → FBX, else OBJ.
            if (ext == ".pmx")
              ok = sceneRenderer->LoadPMXMesh(full.string());
            else if (ext == ".fbx")
              ok = sceneRenderer->LoadFbxMesh(full.string());
            else
              ok = sceneRenderer->LoadObjMesh(full.string());
            break;
          }
          case ilmeee::PrimitiveKind::ExternalPmx: {
            fs::path full = fs::path(activeProject) / e.externalPath;
            ok = sceneRenderer->LoadPMXMesh(full.string());
            break;
          }
          case ilmeee::PrimitiveKind::Light: {
            ok = sceneRenderer->LoadLight(e.name, e.lightType, e.lightColor,
                                          e.lightIntensity, e.lightRange,
                                          e.lightSpotAngle, e.lightGamma);
            break;
          }
          case ilmeee::PrimitiveKind::Camera: {
            ok = sceneRenderer->LoadCamera(e.name, e.camProjection, e.camFov,
                                           e.camOrthoSize, e.camNear, e.camFar);
            break;
          }
          }
          if (ok) {
            size_t idx = sceneRenderer->GetMesh3DCount() - 1;
            sceneRenderer->SetMesh3DTransform(idx, e.position, e.rotationEuler,
                                              e.scale);
            // Inspect Mode: every scene-bootstrap-spawned object remembers
            // this loop so hover-to-source lands users on the deserializer.
            sceneRenderer->SetMesh3DDebugSource(idx, __FILE__, __LINE__);
            // Re-apply per-surface texture bindings saved in the scene.
            // Paths stored relative to the project resolve against it;
            // absolute paths (textures outside the project) load as-is.
            for (const auto &st : e.surfaceTextures) {
              if (st.texturePath.empty())
                continue;
              fs::path tp(st.texturePath);
              std::string full = tp.is_absolute()
                                     ? st.texturePath
                                     : (fs::path(activeProject) / tp).string();
              sceneRenderer->BindMesh3DSubmeshTexture(idx, st.surfaceIndex,
                                                      full);
            }
          }
        }
        s_loadedForProject = desired;
      }
    }

    // Resize the offscreen target to match the panel so the 3D viewport
    // fills the entire window without letterboxing.
    if (contentSize.x > 0 && contentSize.y > 0) {
      sceneRenderer->SetViewportSize((int)contentSize.x, (int)contentSize.y);
    }

    // Gather one-frame input for the 3D camera. Only feed it when the
    // panel is hovered so editor shortcuts elsewhere keep working.
    SceneRenderer::ViewportInput vpIn;
    vpIn.hovered = IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImGuiIO &io = GetIO();
    vpIn.deltaTime = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;
    vpIn.rmbDown = IsMouseDown(ImGuiMouseButton_Right);
    ImVec2 dragDelta = io.MouseDelta;
    vpIn.mouseDeltaX = dragDelta.x;
    vpIn.mouseDeltaY = dragDelta.y;
    vpIn.scroll = io.MouseWheel;
    if (vpIn.hovered) {
      vpIn.wDown = IsKeyDown(ImGuiKey_W);
      vpIn.aDown = IsKeyDown(ImGuiKey_A);
      vpIn.sDown = IsKeyDown(ImGuiKey_S);
      vpIn.dDown = IsKeyDown(ImGuiKey_D);
      vpIn.qDown = IsKeyDown(ImGuiKey_Q);
      vpIn.eDown = IsKeyDown(ImGuiKey_E);
      vpIn.shiftDown =
          IsKeyDown(ImGuiKey_LeftShift) || IsKeyDown(ImGuiKey_RightShift);
    }
    // Route input depending on what's loaded. With 3D meshes present we
    // drive the FPS camera; without them (the 2D sprite fallback or any
    // empty scene) we treat the viewport as a 2D editor: RMB drag pans
    // cameraPosition, wheel zooms.
    const bool sceneIs2D = !sceneRenderer->HasMesh3D();
    if (sceneIs2D) {
      if (vpIn.hovered && vpIn.rmbDown &&
          (vpIn.mouseDeltaX != 0.0f || vpIn.mouseDeltaY != 0.0f)) {
        sceneRenderer->HandleDrag(vpIn.mouseDeltaX, vpIn.mouseDeltaY);
      }
      if (vpIn.hovered && vpIn.scroll != 0.0f) {
        sceneRenderer->HandleZoom(vpIn.scroll);
      }
    } else {
      sceneRenderer->UpdateCamera3D(vpIn);
    }

    // Render scene dengan ukuran penuh
    sceneRenderer->RenderSceneToTexture(projectHandler.currentScene);

    // Tampilkan offscreen image di panel. Y dibalik (ImVec2(0,1)→(1,0))
    // karena framebuffer Vulkan top-left origin sedangkan ImGui sample
    // bottom-up; tanpa flip, segitiga terbalik vertikal.
    VkDescriptorSet sceneDesc = sceneRenderer->GetViewportDescriptorSet();
    if (sceneDesc != VK_NULL_HANDLE && contentSize.x > 0 && contentSize.y > 0) {
      Image((ImTextureID)sceneDesc, contentSize, ImVec2(0, 1), ImVec2(1, 0));

      // Inspect Mode: raycast the cursor against the loaded meshes and
      // register the hit object as a trace region. The hover overlay
      // (rendered at the end of OnRender) will outline the viewport and
      // print "<mesh-name>\n<spawn-file>:<line>" on top.
      if (Debug::g_InspectModeActive && IsItemHovered()) {
        const ImVec2 imgMin = GetItemRectMin();
        const ImVec2 imgMax = GetItemRectMax();
        const ImVec2 mp = GetMousePos();
        const float lx = mp.x - imgMin.x;
        const float ly = mp.y - imgMin.y;
        int hitMesh = -1, hitSub = -1;
        if (sceneRenderer->PickMesh3DSurface(lx, ly, hitMesh, hitSub) &&
            hitMesh >= 0) {
          const std::string &srcFile =
              sceneRenderer->GetMesh3DDebugSrcFile((size_t)hitMesh);
          const int srcLine =
              sceneRenderer->GetMesh3DDebugSrcLine((size_t)hitMesh);
          const std::string &mname =
              sceneRenderer->GetMesh3DName((size_t)hitMesh);
          char label[256];
          std::snprintf(label, sizeof(label), "Scene object: %s",
                        mname.empty() ? "(unnamed)" : mname.c_str());
          if (!srcFile.empty() && srcLine > 0) {
            DEBUG_TRACE_RECT(label, imgMin, imgMax);
            // Patch the just-pushed region with the mesh's actual source.
            auto &regs = Debug::Inspect::GetTraceRegions();
            if (!regs.empty()) {
              regs.back().file = srcFile;
              regs.back().line = srcLine;
            }
          } else {
            // No recorded spawn site — still outline so users know we picked
            // something, and point them at the loader.
            DEBUG_TRACE_RECT(label, imgMin, imgMax);
          }
        }
      }

      // Drop target: drag a file from the Explorer onto the viewport. Models
      // (.obj/.pmx/.fbx) spawn at the drop point projected onto the ground;
      // images bind as the texture of the surface under the cursor.
      if (BeginDragDropTarget()) {
        if (const ImGuiPayload *pl = AcceptDragDropPayload("ASSET_PATH")) {
          std::string assetPath((const char *)pl->Data);
          const ImVec2 dmin = GetItemRectMin();
          const ImVec2 mp = GetMousePos();
          const float lx = mp.x - dmin.x, ly = mp.y - dmin.y;

          std::string ext =
              std::filesystem::path(assetPath).extension().string();
          std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
          const std::string fname =
              std::filesystem::path(assetPath).filename().string();

          const bool isModel =
              (ext == ".obj" || ext == ".pmx" || ext == ".fbx");
          const bool isImage =
              (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp" || ext == ".tga" || ext == ".gif" ||
               ext == ".psd");
          const bool isVideo =
              (ext == ".mp4" || ext == ".mkv" || ext == ".avi" ||
               ext == ".mov" || ext == ".webm");

          if (isModel) {
            bool ok = (ext == ".pmx")   ? sceneRenderer->LoadPMXMesh(assetPath)
                      : (ext == ".fbx") ? sceneRenderer->LoadFbxMesh(assetPath)
                                        : sceneRenderer->LoadObjMesh(assetPath);
            if (ok && sceneRenderer->GetMesh3DCount() > 0) {
              size_t idx = sceneRenderer->GetMesh3DCount() - 1;
              glm::vec3 world(0.0f);
              if (sceneRenderer->ScreenToGround(lx, ly, world))
                sceneRenderer->SetMesh3DTransform(idx, world, glm::vec3(0.0f),
                                                  glm::vec3(1.0f));
              std::snprintf(objectName, sizeof(objectName), "%s",
                            sceneRenderer->GetMesh3DName(idx).c_str());
              projectHandler.ShowNotification("Model added", fname,
                                              ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            } else {
              projectHandler.ShowNotification("Load failed", fname,
                                              ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
            }
          } else if (isImage) {
            int hm = -1, hs = -1;
            if (sceneRenderer->PickMesh3DSurface(lx, ly, hm, hs) && hm >= 0) {
              sceneRenderer->BindMesh3DSubmeshTexture((size_t)hm, (uint32_t)hs,
                                                      assetPath);
              std::snprintf(objectName, sizeof(objectName), "%s",
                            sceneRenderer->GetMesh3DName((size_t)hm).c_str());
              selectedSurface = hs;
              projectHandler.ShowNotification(
                  "Texture bound", fname + " → surface " + std::to_string(hs),
                  ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            } else {
              projectHandler.ShowNotification(
                  "Drop on a surface",
                  "Hover a model surface to bind the image",
                  ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            }
          } else if (isVideo) {
            projectHandler.ShowNotification("Video texture",
                                            "Video-as-texture is coming next",
                                            ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
          } else {
            projectHandler.ShowNotification("Unsupported",
                                            "Can't drop " + ext + " here",
                                            ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
          }
        }
        EndDragDropTarget();
      }

      // Dynamic 2D grid overlay. Drawn via ImDrawList on top of the
      // viewport image so it instantly tracks pan/zoom without needing
      // its own Vulkan pipeline. World→screen mapping uses the same
      // ortho/center convention as the renderer (Y is inverted because
      // ImGui is Y-down while the displayed image is Y-up post UV
      // flip). Step adapts to zoom so lines stay 16–128 px apart.
      if (sceneIs2D && sceneRenderer->IsGridVisible()) {
        ImDrawList *dl = GetWindowDrawList();
        const ImVec2 imgMin = GetItemRectMin();
        const ImVec2 imgMax = GetItemRectMax();
        const ImVec2 imgSize = ImVec2(imgMax.x - imgMin.x, imgMax.y - imgMin.y);
        const ImVec2 center =
            ImVec2(imgMin.x + imgSize.x * 0.5f, imgMin.y + imgSize.y * 0.5f);
        const glm::vec2 cam = sceneRenderer->cameraPosition;
        const float zoom =
            sceneRenderer->cameraZoom > 0.0f ? sceneRenderer->cameraZoom : 1.0f;
        float step = sceneRenderer->GetGridSize();
        if (step <= 0.0f)
          step = 50.0f;
        float pxStep = step * zoom;
        // Keep lines in a comfortable density: rescale step by powers of 2.
        while (pxStep > 0.0f && pxStep < 16.0f) {
          step *= 2.0f;
          pxStep = step * zoom;
        }
        while (pxStep > 128.0f) {
          step *= 0.5f;
          pxStep = step * zoom;
        }

        const float worldL = cam.x - imgSize.x * 0.5f / zoom;
        const float worldR = cam.x + imgSize.x * 0.5f / zoom;
        const float worldB = cam.y - imgSize.y * 0.5f / zoom;
        const float worldT = cam.y + imgSize.y * 0.5f / zoom;

        const ImU32 minorCol = IM_COL32(90, 90, 100, 160);
        const ImU32 majorCol = IM_COL32(140, 140, 160, 200);
        const ImU32 axisXCol = IM_COL32(220, 70, 70, 230);  // red — X
        const ImU32 axisYCol = IM_COL32(70, 130, 230, 230); // blue — Y

        dl->PushClipRect(imgMin, imgMax, true);

        // Vertical lines (constant world X)
        const float startX = std::floor(worldL / step) * step;
        for (float wx = startX; wx <= worldR + step * 0.5f; wx += step) {
          const float sx = center.x + (wx - cam.x) * zoom;
          const bool major =
              std::fabs(std::fmod(wx, step * 5.0f)) < step * 0.5f;
          dl->AddLine(ImVec2(sx, imgMin.y), ImVec2(sx, imgMax.y),
                      major ? majorCol : minorCol, major ? 1.2f : 1.0f);
        }
        // Horizontal lines (constant world Y) — Y inverted for screen.
        const float startY = std::floor(worldB / step) * step;
        for (float wy = startY; wy <= worldT + step * 0.5f; wy += step) {
          const float sy = center.y - (wy - cam.y) * zoom;
          const bool major =
              std::fabs(std::fmod(wy, step * 5.0f)) < step * 0.5f;
          dl->AddLine(ImVec2(imgMin.x, sy), ImVec2(imgMax.x, sy),
                      major ? majorCol : minorCol, major ? 1.2f : 1.0f);
        }
        // Axes through world origin.
        const float ay0 = center.y - (0.0f - cam.y) * zoom;
        const float ax0 = center.x + (0.0f - cam.x) * zoom;
        dl->AddLine(ImVec2(imgMin.x, ay0), ImVec2(imgMax.x, ay0), axisXCol,
                    1.6f);
        dl->AddLine(ImVec2(ax0, imgMin.y), ImVec2(ax0, imgMax.y), axisYCol,
                    1.6f);

        dl->PopClipRect();
      }

      // LMB click on the viewport ray-picks the mesh + surface under the
      // cursor. Populates the Inspector Name field (which drives the
      // Hierarchy highlight) and records the hit surface so the Inspector
      // can target it for texturing.
      if (IsItemClicked(ImGuiMouseButton_Left) && sceneRenderer->HasMesh3D()) {
        const ImVec2 pickMin = GetItemRectMin();
        ImVec2 mp = GetMousePos();
        int hitMesh = -1, hitSurface = -1;
        if (sceneRenderer->PickMesh3DSurface(mp.x - pickMin.x, mp.y - pickMin.y,
                                             hitMesh, hitSurface)) {
          const std::string &nm = sceneRenderer->GetMesh3DName((size_t)hitMesh);
          std::snprintf(objectName, sizeof(objectName), "%s", nm.c_str());
          selectedSurface = hitSurface;
        }
      }
      // LMB drag → move the currently-selected mesh in screen plane.
      int selIdx = -1;
      for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i) {
        if (sceneRenderer->GetMesh3DName(i) == objectName) {
          selIdx = (int)i;
          break;
        }
      }
      const bool dragging = IsItemHovered() &&
                            IsMouseDragging(ImGuiMouseButton_Left) &&
                            !IsMouseDown(ImGuiMouseButton_Right) && selIdx >= 0;
      if (dragging) {
        ImVec2 md = GetIO().MouseDelta;
        if (md.x != 0.0f || md.y != 0.0f) {
          sceneRenderer->DragMesh3DScreen((size_t)selIdx, md.x, md.y,
                                          (int)contentSize.y);
        }
      }

      // Right-click WITHOUT drag opens an "Add" menu and instances a
      // default object on the ground where the cursor was. RMB-drag is
      // camera mouselook, so we snapshot the press position; ImGui only
      // opens the context popup when the button is released without
      // dragging past the threshold, which gives us click-vs-drag for
      // free without stealing the orbit gesture.
      const ImVec2 imgMin = GetItemRectMin();
      static ImVec2 s_rmbAnchor(0.0f, 0.0f);
      if (IsItemHovered() && IsMouseClicked(ImGuiMouseButton_Right)) {
        ImVec2 m = GetMousePos();
        s_rmbAnchor = ImVec2(m.x - imgMin.x, m.y - imgMin.y);
      }
      PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
      PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
      PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
      bool open = BeginPopupContextItem("SceneAddMenu",
                                        ImGuiPopupFlags_MouseButtonRight);
      if (open) {
        if (BeginMenu("Add")) {
          // 0 = Cube (default), 1 = Sphere, 2 = Plane
          auto spawn = [&](int kind) {
            glm::vec3 world(0.0f);
            if (!sceneRenderer->ScreenToGround(s_rmbAnchor.x, s_rmbAnchor.y,
                                               world)) {
              // Ground not under cursor (looking up/at horizon): drop it
              // on the ground a few units ahead of the camera.
              glm::vec3 f = sceneRenderer->GetCameraForward();
              glm::vec3 cp = sceneRenderer->camera3d.position;
              glm::vec2 fh = glm::vec2(f.x, f.z);
              if (glm::length(fh) > 1e-4f)
                fh = glm::normalize(fh);
              world = glm::vec3(cp.x + fh.x * 6.0f, 0.0f, cp.z + fh.y * 6.0f);
            }
            if (sceneRenderer->IsSnapToGrid()) {
              world.x = std::round(world.x);
              world.z = std::round(world.z);
            }
            const char *base = (kind == 0)   ? "Cube"
                               : (kind == 1) ? "Sphere"
                               : (kind == 2) ? "Plane"
                               : (kind == 3) ? "Light"
                                             : "Camera";
            // Auto-number so repeated adds don't collide by name.
            std::string name = base;
            auto taken = [&](const std::string &s) {
              for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i)
                if (sceneRenderer->GetMesh3DName(i) == s)
                  return true;
              return false;
            };
            for (int n = 1; taken(name); ++n)
              name = std::string(base) + " " + std::to_string(n);

            bool ok = false;
            if (kind == 0)
              ok = sceneRenderer->LoadCube(name);
            else if (kind == 1)
              ok = sceneRenderer->LoadSphere(name);
            else if (kind == 2)
              ok = sceneRenderer->LoadPlane(name);
            else if (kind == 3)
              ok = sceneRenderer->LoadLight(name, 0,
                                            glm::vec3(1.0f, 0.96f, 0.88f), 1.0f,
                                            10.0f, 30.0f, 1.05f);
            else if (kind == 4)
              ok = sceneRenderer->LoadCamera(name);

            if (ok) {
              size_t idx = sceneRenderer->GetMesh3DCount() - 1;
              sceneRenderer->SetMesh3DTransform(idx, world, glm::vec3(0.0f),
                                                glm::vec3(1.0f));
              // Record where this object was spawned so Inspect Mode (F2)
              // can answer "which line created this thing?" on hover.
              sceneRenderer->SetMesh3DDebugSource(idx, __FILE__, __LINE__);
              std::snprintf(objectName, sizeof(objectName), "%s", name.c_str());
            }
          };
          if (MenuItem("Cube"))
            spawn(0);
          if (MenuItem("Sphere"))
            spawn(1);
          if (MenuItem("Plane"))
            spawn(2);
          if (MenuItem("Light"))
            spawn(3);
          if (MenuItem("Camera"))
            spawn(4);
          EndMenu();
        }
        EndPopup();
      }
      PopStyleVar(3);
    }

    // Render toolbar di atas viewport
    RenderSceneToolbarView(windowPos, windowSize);

    // Status bar dengan background semi-transparan
    SetCursorPos(ImVec2(0, windowSize.y - 25));
    // PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
    BeginChild("StatusBar", ImVec2(windowSize.x, 25), false);
    Text(" Scene View | FPS: %.1f | Zoom: %.2fx", GetIO().Framerate,
         sceneRenderer->GetZoom());
    EndChild();
    // PopStyleColor();
  }
  End();

  // Pop semua style yang di-push
  PopStyleVar(1);

  // Player-camera preview: render the scene from the first camera object in
  // the scene and show it in its own window. Only present when a camera
  // exists, so the editor stays uncluttered otherwise.
  if (sceneRenderer && sceneRenderer->HasPlayerCamera()) {
    sceneRenderer->RenderPlayerCameraPreview();
    if (Begin("Camera Preview", nullptr, ImGuiWindowFlags_NoScrollbar)) {
      VkDescriptorSet d = sceneRenderer->GetPlayerCameraPreviewDescriptor();
      if (d != VK_NULL_HANDLE) {
        ImVec2 avail = GetContentRegionAvail();
        float aspect = (float)sceneRenderer->GetPreviewWidth() /
                       (float)sceneRenderer->GetPreviewHeight();
        float w = avail.x;
        float h = w / aspect;
        if (h > avail.y && avail.y > 0.0f) {
          h = avail.y;
          w = h * aspect;
        }
        // Flip V like the main viewport (framebuffer is top-left origin).
        Image((ImTextureID)d, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
      }
    }
    End();
  }
}

void MainWindow::RenderMainViewWindow() {
  if (!showMainView)
    return;

  Begin("Main View", &showMainView, ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Main View panel (tabs)");

  if (BeginTabBar("MainTabs")) {
    if (BeginTabItem("Viewport")) {
      EndTabItem();
    }

    if (BeginTabItem("Animation")) {
      Text("Animation editor will be displayed here");
      EndTabItem();
    }

    if (BeginTabItem("Particle Editor")) {
      Text("Particle system editor will be displayed here");
      EndTabItem();
    }

    EndTabBar();
  }

  End();
}

void MainWindow::RenderViewportToolbar() {
  // Style untuk toolbar
  float toolbarHeight = 28.0f;

  // Mode dropdown
  Text("Mode:");
  SameLine();
  const char *modes[] = {"Select", "Move", "Rotate", "Scale"};
  static int currentMode = 0;
  SetNextItemWidth(100);
  if (Combo("##viewportMode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
    // Handle mode change
    sceneRenderer->SetEditMode((SceneRenderer::EditMode)currentMode);
  }

  // Tombol grid
  SameLine(0, 15);
  static bool showGrid = true;
  if (Checkbox("Show Grid", &showGrid)) {
    sceneRenderer->SetGridVisible(showGrid);
  }

  // Grid size slider
  SameLine(0, 15);
  Text("Grid Size:");
  SameLine();
  static float gridSize = 32.0f;
  SetNextItemWidth(80);
  if (SliderFloat("##gridSize", &gridSize, 8.0f, 64.0f, "%.0f")) {
    sceneRenderer->SetGridSize(gridSize);
  }

  // Snap to grid
  SameLine(0, 15);
  static bool snapToGrid = true;
  if (Checkbox("Snap to Grid", &snapToGrid)) {
    sceneRenderer->SetSnapToGrid(snapToGrid);
  }

  // Camera controls
  SameLine(0, 20);
  if (Button("Reset Camera")) {
    sceneRenderer->ResetCamera();
  }

  SameLine();
  Text("Zoom:");
  SameLine();
  static float zoom = 1.0f;
  SetNextItemWidth(80);
  if (SliderFloat("##zoom", &zoom, 0.1f, 5.0f, "%.1fx")) {
    sceneRenderer->SetCameraZoom(zoom);
  }

  Separator();
}

void MainWindow::HandleViewportInteraction(ImVec2 viewportPos,
                                           ImVec2 viewportSize) {
  // Cek apakah mouse berada di dalam viewport
  ImVec2 mousePos = GetMousePos();
  bool isHovered = IsItemHovered();

  // Jika viewport dihover, tampilkan overlay informasi di pojok kanan bawah
  if (isHovered) {
    // Text("Now Hovered View Port");
    // SameLine();
    // Hitung posisi mouse relatif terhadap viewport (dalam piksel viewport)
    float viewportX = mousePos.x - viewportPos.x;
    float viewportY = mousePos.y - viewportPos.y;

    // Konversi koordinat viewport ke koordinat world (dengan memperhitungkan
    // zoom/pan)
    glm::vec2 worldPos =
        sceneRenderer->ViewportToWorldPosition(viewportX, viewportY);

    // Tampilkan informasi koordinat di pojok kanan bawah viewport
    char coordText[64];
    snprintf(coordText, sizeof(coordText), "X: %.1f, Y: %.1f", worldPos.x,
             worldPos.y);

    ImVec2 textSize = CalcTextSize(coordText);
    ImVec2 textPos = ImVec2(viewportPos.x + viewportSize.x - textSize.x - 10,
                            viewportPos.y + viewportSize.y - textSize.y - 5);

    GetWindowDrawList()->AddText(textPos, IM_COL32(0, 0, 0, 220), coordText);

    // Handling click untuk seleksi objek
    if (IsMouseClicked(ImGuiMouseButton_Left)) {
      // Text("Select Object");
      // SameLine();
      cout << "Click" << endl;
      // projectHandler.sceneRenderer->HandleClick(worldPos.x, worldPos.y);
    }

    // Handling drag untuk move objek atau pan kamera
    if (IsMouseDragging(ImGuiMouseButton_Left)) {
      // Text("Drag Object");
      // SameLine();
      cout << "Dragging" << endl;
      ImVec2 delta = GetMouseDragDelta(ImGuiMouseButton_Left);
      sceneRenderer->HandleDrag(delta.x, delta.y);
      ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // Handling zoom dengan mouse wheel
    float wheel = GetIO().MouseWheel;
    if (wheel != 0) {
      cout << "Handle Zoom" << endl;
      sceneRenderer->HandleZoom(wheel);
    }

    // Handling key input untuk precision movement
    ImGuiIO &io = GetIO();
    if (sceneRenderer->HasSelectedObject()) {
      // cout << "Receive Input" << endl;
      float moveAmount = io.KeyShift ? 10.0f : 1.0f;

      if (IsKeyPressed(ImGuiKey_LeftArrow)) {
        cout << "Left Arrow" << endl;
        sceneRenderer->MoveSelected(-moveAmount, 0);
      }
      if (IsKeyPressed(ImGuiKey_RightArrow)) {
        cout << "Right Arrow" << endl;
        sceneRenderer->MoveSelected(moveAmount, 0);
      }
      if (IsKeyPressed(ImGuiKey_UpArrow)) {
        cout << "Up Arrow" << endl;
        sceneRenderer->MoveSelected(0, -moveAmount);
      }
      if (IsKeyPressed(ImGuiKey_DownArrow)) {
        cout << "Down Arrow" << endl;
        sceneRenderer->MoveSelected(0, moveAmount);
      }

      // Delete key untuk menghapus objek
      if (IsKeyPressed(ImGuiKey_Delete)) {
        sceneRenderer->DeleteSelected();
      }
    }
  }
}

void MainWindow::RenderConsoleWindow() {
  if (!showConsole)
    return;
  // Set window properties
  Begin("Console", &showConsole, ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Console panel (output/build tabs)");
  ImVec2 pos = GetWindowPos();
  ImVec2 size = GetWindowSize();
  HandleBackground(pos, size);

  static int selectedTab = 0;
  BeginTabBar("ConsoleTabs");

  if (BeginTabItem("Output")) {
    // Toolbar area
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
    BeginChild("ConsoleToolbar", ImVec2(0, 30), false);

    // Clear button with better styling
    PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    if (Button("Clear", ImVec2(60, 24))) {
      messages.clear();
    }
    PopStyleColor(2);

    // Filter dropdown
    SameLine();
    const char *filters[] = {"All", "Info", "Warning", "Error"};
    static int currentFilter = 0;
    SetNextItemWidth(100);
    Combo("##Filter", &currentFilter, filters, IM_ARRAYSIZE(filters));

    // Search box
    SameLine();
    static char searchBuffer[128] = "";
    SetNextItemWidth(-1); // Take remaining width
    InputTextWithHint("##search", "Search in console...", searchBuffer,
                      IM_ARRAYSIZE(searchBuffer));

    EndChild();
    PopStyleVar();

    // Console output area
    BeginChild("ConsoleOutput", ImVec2(0, -5), true);

    // Style for console text
    PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));

    static char consoleBuffer[4096];
    string combinedLog;
    int number = 1;

    for (const auto &line : messages) {
      // Filter messages based on selected filter
      if (currentFilter == 0 || // All
          (currentFilter == 1 && line.find("[INFO]") != string::npos) ||
          (currentFilter == 2 && line.find("[WARNING]") != string::npos) ||
          (currentFilter == 3 && line.find("[ERROR]") != string::npos)) {

        // Search filter
        if (strlen(searchBuffer) == 0 ||
            line.find(searchBuffer) != string::npos) {
          combinedLog += "[" + to_string(number++) + "] " + line + "\n";
        }
      }
    }

    strncpy(consoleBuffer, combinedLog.c_str(), sizeof(consoleBuffer) - 1);
    consoleBuffer[sizeof(consoleBuffer) - 1] = '\0';

    InputTextMultiline("##console", consoleBuffer, IM_ARRAYSIZE(consoleBuffer),
                       ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);

    // Auto-scroll
    if (GetScrollY() >= GetScrollMaxY()) {
      SetScrollHereY(1.0f);
    }

    PopStyleVar();
    PopStyleColor();
    EndChild();

    EndTabItem();
  }

  if (BeginTabItem("Build")) {
    selectedTab = 2;
    Text("Build output will be displayed here");
    EndTabItem();
  }

  EndTabBar();

  End();
}

void MainWindow::RenderMenuBar() {
  if (BeginMainMenuBar()) {
    DEBUG_TRACE_PANEL("Main menu bar");
    if (BeginMenu("File")) {
      if (MenuItem("New Scene", "Ctrl+N")) {
        projectHandler.SaveNewScene();
      }
      if (MenuItem("Open Project", "Ctrl+O")) {
        projectHandler.isOpenedProject = true;
        projectHandler.OpenFolder();
      }
      if (MenuItem("Load Scene", "Ctrl+O")) {
        // projectHandler.OpenScene();
        networkManager->sendMessage("LoadScene");
      }
      if (MenuItem("Save", "Ctrl+S")) {
        Save3DScene();
      }
      if (MenuItem("Save As...", "Ctrl+Shift+S")) {
        projectHandler.SaveAsScene();
      }
      Separator();
      if (MenuItem("Exit", "Alt+F4"))
        isRunning = false;
      EndMenu();
    }

    if (BeginMenu("Edit")) {
      if (MenuItem("Undo", "Ctrl+Z")) {
      }
      if (MenuItem("Redo", "Ctrl+Y")) {
      }
      Separator();
      if (MenuItem("Cut", "Ctrl+X")) {
      }
      if (MenuItem("Copy", "Ctrl+C")) {
      }
      if (MenuItem("Paste", "Ctrl+V")) {
      }
      EndMenu();
    }

    if (BeginMenu("View")) {
      if (MenuItem("Toggle Dark/Light Theme", "Ctrl+T")) {
        darkTheme = !darkTheme;
        setTheme(darkTheme);
      }
      if (MenuItem("Toggle Fullscreen", "F")) {
        fullscreen = !fullscreen;
        SDL_SetWindowFullscreen(window, fullscreen);
      }
      EndMenu();
    }

    if (BeginMenu("Tools")) {
      if (MenuItem("Secondary Window", nullptr, &showSecondary)) {
      }
      EndMenu();
    }

    if (BeginMenu("Help")) {
      if (MenuItem("Documentation")) {
      }
      if (MenuItem("About")) {
      }
      EndMenu();
    }

    if (BeginMenu("Background")) {
      Checkbox("Use Background", &isBackgroundActived);
      if (Combo(" ", &currentBgInt, backgroundOptions, Background_Count)) {
        currentBg = static_cast<CurrentBackground>(currentBgInt);
        cout << currentBg << endl;
        isBackgroundChanged = true;
        isBackgroundActived = true;
      }

      // Tambahan pengaturan background opacity
      SliderFloat("Opacity", &volume, 0.1f, 1.0f);
      EndMenu();
    }

    if (BeginMenu("Windows")) {
      if (MenuItem("Main View", nullptr, &showMainView)) {
      }
      if (MenuItem("Explorer", nullptr, &showExplorer)) {
      }
      if (MenuItem("Inspector", nullptr, &showInspector)) {
      }
      if (MenuItem("Scene", "Ctrl+1", &showScene)) {
      }
      if (MenuItem("Hierarchy", nullptr, &showHierarchy)) {
        RenderHierarchyWindow();
      }
      if (MenuItem("Console", "Ctrl+2", &showConsole)) {
      }
      EndMenu();
    }

    // Status bar di menu kanan
    SetCursorPosX(GetWindowWidth() - 200);
    Text("FPS: %.1f", GetIO().Framerate);

    EndMainMenuBar();
  }
}

void MainWindow::HandleBackground(const ImVec2 &windowPos,
                                  const ImVec2 &windowSize) {
  if (!isBackgroundActived || backgroundTexture.TextureID == 0)
    return;

  if (isBackgroundChanged) {
    HandleUpdateBackground(currentBg);
    isBackgroundChanged = false;
  }

  ImDrawList *drawList = GetWindowDrawList();

  // Calculate image dimensions while maintaining aspect ratio
  float imageAspect = (float)backgroundTexture.Width / backgroundTexture.Height;
  float windowAspect = windowSize.x / windowSize.y;

  // Calculate image size that maintains original aspect ratio
  float imageWidth, imageHeight;
  if (windowAspect > imageAspect) {
    // Window is wider than image
    imageHeight = windowSize.y;
    imageWidth = imageHeight * imageAspect;
  } else {
    // Window is taller than image
    imageWidth = windowSize.x;
    imageHeight = imageWidth / imageAspect;
  }

  // Calculate position to center the image
  float imageX = windowPos.x + (windowSize.x - imageWidth) * 0.5f;
  float imageY = windowPos.y + (windowSize.y - imageHeight) * 0.5f;

  // Extract Color
  float r, g, b;
  r = assets.r;
  g = assets.g;
  b = assets.b;
  // ::Log("Dominant Color: " + to_string(r) + ", " + to_string(g) + ", " +
  // to_string(b), Debug::LogLevel::INFO); First draw the dominant color
  // background for the entire window
  ImU32 fillColor = ColorConvertFloat4ToU32(ImVec4(r, g, b, volume));
  drawList->AddRectFilled(
      windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
      fillColor);

  // Then draw the actual image maintaining its aspect ratio
  drawList->AddImage((ImTextureID)(intptr_t)backgroundTexture.TextureID,
                     ImVec2(imageX, imageY),
                     ImVec2(imageX + imageWidth, imageY + imageHeight),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ColorConvertFloat4ToU32(ImVec4(1, 1, 1, volume)));
}

void MainWindow::HandleSearch() {
  // Buffer pencarian dan container hasil pencarian.
  static char searchBuffer[128] = "";
  static vector<HandlerProject::AssetFile> searchResults;

  // Atur lebar input sesuai jendela.
  // PushItemWidth(-1);
  // Menampilkan input text dengan hint. Menunggu Enter untuk trigger pencarian.
  if (InputTextWithHint("##search", "Search assets...", searchBuffer,
                        IM_ARRAYSIZE(searchBuffer))) {
    projectHandler.currentFilter = searchBuffer;
    // Jika buffer tidak kosong, lakukan pencarian.
    if (strlen(searchBuffer) > 0 || projectHandler.currentFilter != "") {
      searchResults.clear();
      // Asumsikan BuildAssetTree menghasilkan struktur asset
      auto assetTree =
          projectHandler.BuildAssetTree(projectHandler.projectPath);
      projectHandler.SearchFileOrFolder(assetTree, searchBuffer, searchResults);
    }
    // Jika input dikosongkan, bersihkan hasil pencarian.
    else {
      searchResults.clear();
    }
  }

  // Tombol tambahan untuk filter cepat
  SameLine();
  if (Button("Filter")) {
    OpenPopup("FilterOptions");
    // showingFilterPopup = true;
  }

  // Popup filter
  if (BeginPopup("FilterOptions")) {
    if (MenuItem("All Files")) {
      projectHandler.currentFilter = "";
      strcpy(searchBuffer, "");
    }
    if (MenuItem("Scripts (.cpp, .c)")) {
      projectHandler.currentFilter = ".cpp .c";
      strcpy(searchBuffer, ".cpp .c");
    }
    if (MenuItem("Models (.fbx, .obj)")) {
      projectHandler.currentFilter = ".fbx .obj";
      strcpy(searchBuffer, ".fbx .obj");
    }
    if (MenuItem("Images (.png, .jpg)")) {
      projectHandler.currentFilter = ".png .jpg .jpeg";
      strcpy(searchBuffer, ".png .jpg .jpeg");
    }
    EndPopup();
    // showingFilterPopup = false;
  }
  // PopItemWidth();

  // Jika ada hasil pencarian, tampilkan daftar hasil.
  if (!searchResults.empty()) {
    Separator();
    Text("Search Results:");
    for (const auto &result : searchResults) {
      // Buat label dengan menampilkan nama dan menandai direktori.
      string label;
      if (result.isDirectory)
        label = "[DIR] " + result.name;
      else
        label = result.name;

      // Tampilkan hasil sebagai selectable item.
      if (Selectable(label.c_str())) {
        // Jika yang dipilih adalah file, periksa ekstensi file.
        if (!result.isDirectory) {
          string extension;
          size_t pos = result.name.find_last_of('.');
          if (pos != string::npos)
            extension = result.name.substr(pos);

          // Jika file ber-ekstensi '.cpp', buka dengan VS Code.
          if (extension == ".cpp") {
            // Gunakan perintah sistem; pastikan "code" sudah ada di PATH.
            string command = "code \"" + result.fullPath + "\"";
            system(command.c_str());
          }
        }
      }
      if (IsItemHovered()) {
        BeginTooltip();
        Text("%s", result.fullPath.c_str());
        EndTooltip();
      }
    }
    // Jika input search dikosongkan, pastikan daftar hasil juga dikosongkan.
    if (strlen(searchBuffer) == 0)
      searchResults.clear();
  }
}

void MainWindow::RenderPlayMenu() {
  ImVec2 viewportSize = GetMainViewport()->Size;
  float buttonHeight = 24.0f;
  float toolbarHeight = buttonHeight + 8.0f;

  float menuBarHeight = GetFrameHeight();
  SetNextWindowPos(ImVec2(0, menuBarHeight));
  SetNextWindowSize(ImVec2(viewportSize.x, toolbarHeight));

  ImGuiWindowFlags toolbar_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoNavFocus;

  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
  PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
  PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));

  if (Begin("PlayControlsToolbar", nullptr, toolbar_flags)) {
    int iconSize = 16;
    float spacing = 4.0f;
    float totalWidth = (iconSize + 8) * 3 + spacing * 2;
    float startX = (viewportSize.x - totalWidth) * 0.5f;

    SetCursorPosX(startX);

    // Play / Resume button
    bool isPlaying = builder.IsPlaying();
    bool isPaused = builder.IsPaused();

    ImVec4 playBg = isPlaying ? ImVec4(0.15f, 0.45f, 0.15f, 1.0f)
                              : ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
    ImVec4 playTint = isPlaying ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                : ImVec4(0.7f, 0.9f, 0.7f, 1.0f);

    if (svgIcons.DrawIconButton("##Play", "assets/icons/svg/play.svg", iconSize,
                                playBg, playTint)) {
      if (builder.IsStopped()) {
        builder.Play();
      } else if (isPaused) {
        builder.Resume();
      }
    }
    if (IsItemHovered())
      SetTooltip("Play (Ctrl+P)");

    // Pause button
    SameLine(0, spacing);
    ImVec4 pauseBg = isPaused ? ImVec4(0.45f, 0.40f, 0.12f, 1.0f)
                              : ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
    ImVec4 pauseTint = isPaused ? ImVec4(1.0f, 0.9f, 0.3f, 1.0f)
                                : ImVec4(0.9f, 0.9f, 0.6f, 1.0f);

    if (svgIcons.DrawIconButton("##Pause", "assets/icons/svg/pause.svg",
                                iconSize, pauseBg, pauseTint)) {
      if (isPlaying) {
        builder.Pause();
      }
    }
    if (IsItemHovered())
      SetTooltip("Pause (Ctrl+Shift+P)");

    // Stop button
    SameLine(0, spacing);
    bool isStopped = builder.IsStopped();
    ImVec4 stopBg = ImVec4(0.2f, 0.2f, 0.22f, 1.0f);
    ImVec4 stopTint = isStopped ? ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                                : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

    if (svgIcons.DrawIconButton("##Stop", "assets/icons/svg/stop.svg", iconSize,
                                stopBg, stopTint)) {
      builder.Stop();
    }
    if (IsItemHovered())
      SetTooltip("Stop (Ctrl+Q)");

    // Keyboard shortcuts
    if (GetIO().KeyCtrl && !GetIO().KeyShift &&
        IsKeyPressed(ImGuiKey_P, false)) {
      if (builder.IsStopped())
        builder.Play();
      else if (isPaused)
        builder.Resume();
    }
    if (GetIO().KeyCtrl && GetIO().KeyShift &&
        IsKeyPressed(ImGuiKey_P, false)) {
      if (isPlaying)
        builder.Pause();
    }
  }
  End();
  PopStyleColor(1);
  PopStyleVar(4);
}

void MainWindow::PushMessage(const string &message) {
  lock_guard<mutex> lock(messagesMutex);
  messages.push_back(message);

  // Limit buffer size
  if (messages.size() > MAX_MESSAGES) {
    messages.erase(messages.begin());
  }
}

void MainWindow::ClearMessages() {
  lock_guard<mutex> lock(messagesMutex);
  messages.clear();
}

void MainWindow::Save3DScene() {
  if (!sceneRenderer)
    return;
  const std::string activeProject = projectHandler.projectPath;
  if (activeProject.empty()) {
    ::Log("Cannot save scene: No active project opened.",
          Debug::LogLevel::WARNING);
    projectHandler.ShowNotification("Save Warning",
                                    "No active project is opened",
                                    ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
    return;
  }
  namespace fs = std::filesystem;
  fs::path scenesDir = fs::path(activeProject) / "scenes";
  fs::path mainScene = scenesDir / "main.ilmeeescene";

  ilmeee::IlmeeeScene scene;
  for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i) {
    ilmeee::SceneEntity e;
    e.name = sceneRenderer->GetMesh3DName(i);
    e.position = sceneRenderer->GetMesh3DPosition(i);
    e.rotationEuler = sceneRenderer->GetMesh3DRotation(i);
    e.scale = sceneRenderer->GetMesh3DScale(i);

    if (sceneRenderer->IsMesh3DLight(i)) {
      e.kind = ilmeee::PrimitiveKind::Light;
      e.lightGamma = sceneRenderer->GetMesh3DLightGamma(i);
      e.lightColor = sceneRenderer->GetMesh3DLightColor(i);
      e.lightIntensity = sceneRenderer->GetMesh3DLightIntensity(i);
      e.lightType = sceneRenderer->GetMesh3DLightType(i);
      e.lightRange = sceneRenderer->GetMesh3DLightRange(i);
      e.lightSpotAngle = sceneRenderer->GetMesh3DLightSpotAngle(i);
    } else if (sceneRenderer->IsMesh3DCamera(i)) {
      e.kind = ilmeee::PrimitiveKind::Camera;
      e.camProjection = sceneRenderer->GetMesh3DCameraProjection(i);
      e.camFov = sceneRenderer->GetMesh3DCameraFov(i);
      e.camOrthoSize = sceneRenderer->GetMesh3DCameraOrthoSize(i);
      e.camNear = sceneRenderer->GetMesh3DCameraNear(i);
      e.camFar = sceneRenderer->GetMesh3DCameraFar(i);
    } else {
      std::string path = sceneRenderer->GetMesh3DPath(i);
      // Primitives carry a synthetic "<primitive:kind>" path — the reliable
      // way to recover their kind (name can be renamed by the user).
      if (path.rfind("<primitive:", 0) == 0) {
        if (path.find("sphere") != std::string::npos)
          e.kind = ilmeee::PrimitiveKind::Sphere;
        else if (path.find("plane") != std::string::npos)
          e.kind = ilmeee::PrimitiveKind::Plane;
        else
          e.kind = ilmeee::PrimitiveKind::Cube;
      } else if (path.empty()) {
        e.kind = ilmeee::PrimitiveKind::Cube; // unknown mesh → safe default
      } else {
        fs::path absPath = path;
        std::error_code ec;
        fs::path rel = fs::relative(absPath, activeProject, ec);
        e.externalPath = ec ? path : rel.string();
        std::string ext = absPath.extension().string();
        if (ext == ".pmx" || ext == ".PMX")
          e.kind = ilmeee::PrimitiveKind::ExternalPmx;
        else
          e.kind = ilmeee::PrimitiveKind::ExternalObj;
      }

      // Capture per-surface texture bindings so re-textured surfaces
      // survive a save/load round-trip. Paths under the project are stored
      // relative; textures elsewhere are stored absolute.
      uint32_t sc = sceneRenderer->GetMesh3DSubmeshCount(i);
      for (uint32_t s = 0; s < sc; ++s) {
        const std::string &tex = sceneRenderer->GetMesh3DSubmeshTexture(i, s);
        if (tex.empty())
          continue;
        ilmeee::SurfaceTexture st;
        st.surfaceIndex = s;
        std::error_code ec2;
        fs::path rel = fs::relative(fs::path(tex), activeProject, ec2);
        std::string relStr = ec2 ? std::string() : rel.string();
        st.texturePath =
            (!relStr.empty() && relStr.rfind("..", 0) != 0) ? relStr : tex;
        e.surfaceTextures.push_back(std::move(st));
      }
    }
    scene.entities.push_back(std::move(e));
  }

  if (ilmeee::SaveScene(mainScene.string(), scene)) {
    ::Log("Saved 3D scene to " + mainScene.string(), Debug::LogLevel::SUCCESS);
    projectHandler.ShowNotification("Scene Saved",
                                    "Successfully saved main.ilmeeescene",
                                    ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
  } else {
    ::Log("Failed to save 3D scene to " + mainScene.string(),
          Debug::LogLevel::ERROR);
    projectHandler.ShowNotification("Save Error", "Failed to save scene",
                                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
}