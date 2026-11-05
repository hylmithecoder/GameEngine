#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/IlmeeeScene.hpp"
#include "../../../include/core_engine/MovementScript.hpp"
#include "../../../include/core_engine/InspectMode.hpp"
#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/core_engine/core_editor/EditorDockSpace.hpp"
#include "../../../include/core_engine/net/Protocol.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <nfd.h>
#include <stb/stb_image.h>

static const char *kSpawnNames[] = {"Cube",   "Sphere",       "Plane",
                                    "Light",  "Camera",       "Audio Source",
                                    "Video Screen", "Text (3D)", "Canvas"};
// Scene objects SpawnSceneObject can create; the entry after them is Canvas.
static constexpr int kSpawnObjectCount = 8;

namespace {
// Explorer sends absolute file paths. Keep scene references project-relative
// and reject links escaping assets, so built games can copy the same image.
std::string ProjectUiImagePath(const std::string &project,
                               const std::string &source) {
  namespace fs = std::filesystem;
  if (project.empty() || source.empty()) return {};
  std::error_code ec;
  const fs::path root = fs::canonical(project, ec);
  if (ec) return {};
  const fs::path assets = fs::canonical(root / "assets", ec);
  if (ec) return {};
  const fs::path assetsInProject = assets.lexically_relative(root);
  if (assetsInProject != fs::path("assets")) return {};
  const fs::path image = fs::canonical(source, ec);
  if (ec || !fs::is_regular_file(image, ec) || ec) return {};
  const fs::path withinAssets = image.lexically_relative(assets);
  if (withinAssets.empty() || *withinAssets.begin() == "..") return {};
  std::string ext = image.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" &&
      ext != ".bmp" && ext != ".tga" && ext != ".psd") return {};
  int width = 0, height = 0, channels = 0;
  if (!stbi_info(image.string().c_str(), &width, &height, &channels) ||
      width <= 0 || height <= 0) return {};
  return image.lexically_relative(root).generic_string();
}

} // namespace

// Audio/video clip reference as stored in the scene: relative to the project
// when the file is inside it (so built games find it), absolute otherwise
// (the game build then gathers it under assets/external/).
std::string ProjectMediaPath(const std::string &project,
                             const std::string &source) {
  namespace fs = std::filesystem;
  if (project.empty())
    return source;
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(project, ec);
  const fs::path file = fs::weakly_canonical(source, ec);
  if (ec)
    return source;
  const fs::path rel = file.lexically_relative(root);
  if (rel.empty() || *rel.begin() == "..")
    return file.string();
  return rel.generic_string();
}

bool MainWindow::BindUiImageAsset(int index, const std::string &sourcePath) {
  if (SceneStructureLocked() || index < 0 ||
      index >= (int)gameUi.elements.size() ||
      (gameUi.elements[index].kind != ilmeee::UiKind::Image &&
       gameUi.elements[index].kind != ilmeee::UiKind::Button)) return false;
  const std::string path = ProjectUiImagePath(projectHandler.projectPath,
                                               sourcePath);
  if (path.empty()) {
    projectHandler.ShowNotification(
        "UI Image rejected", "Drop a supported image from this project's assets",
        ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
    PushMessage("[WARNING] UI Image must be a valid PNG/JPG/BMP/TGA/PSD inside project assets");
    return false;
  }
  gameUi.elements[index].imagePath = path;
  selectedUiElement = index;
  objectName[0] = '\0';
  PushMessage("[INFO] UI Image set to " + path);
  return true;
}

void MainWindow::CreateUiImageAsset(const std::string &sourcePath,
                                     float x, float y) {
  if (SceneStructureLocked() || !hasGameUiCanvas) return;
  ilmeee::UiElement element;
  element.kind = ilmeee::UiKind::Image;
  for (int n = 1;; ++n) {
    element.id = "Image " + std::to_string(n);
    if (std::none_of(gameUi.elements.begin(), gameUi.elements.end(),
                     [&](const ilmeee::UiElement &existing) {
                       return existing.id == element.id;
                     })) break;
  }
  int width = 0, height = 0, channels = 0;
  if (stbi_info(sourcePath.c_str(), &width, &height, &channels) &&
      width > 0 && height > 0) {
    const float fit = std::min({1.0f, 320.0f / width, 320.0f / height});
    element.width = std::max(1.0f, width * fit);
    element.height = std::max(1.0f, height * fit);
  }
  element.x = std::clamp(x, 0.0f,
                         std::max(0.0f, gameUi.referenceWidth - element.width));
  element.y = std::clamp(y, 0.0f,
                         std::max(0.0f, gameUi.referenceHeight - element.height));
  gameUi.elements.push_back(std::move(element));
  if (!BindUiImageAsset((int)gameUi.elements.size() - 1, sourcePath))
    gameUi.elements.pop_back();
}

int MainWindow::SelectedMeshIndex() const {
  if (!sceneRenderer || objectName[0] == '\0')
    return -1;
  for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i)
    if (sceneRenderer->GetMesh3DName(i) == objectName)
      return (int)i;
  return -1;
}

bool MainWindow::SceneStructureLocked() const { return !builder.IsStopped(); }

namespace {
std::string TrimName(std::string name) {
  const auto first = name.find_first_not_of(" \t");
  if (first == std::string::npos)
    return {};
  name = name.substr(first, name.find_last_not_of(" \t") - first + 1);
  if (name.size() > 63)
    name.resize(63);
  return name;
}
// "Cube" → "Cube 1", "Cube 2"... until unused.
template <class Taken> std::string UniqueName(const std::string &base, Taken taken) {
  std::string name = base;
  for (int n = 1; taken(name); ++n) {
    const auto suffix = " " + std::to_string(n);
    name = base.substr(0, 63 - suffix.size()) + suffix;
  }
  return name;
}
} // namespace

bool MainWindow::RenameMesh(size_t index, std::string name) {
  if (!sceneRenderer || index >= sceneRenderer->meshes3d.size() ||
      SceneStructureLocked())
    return false;
  auto &mesh = sceneRenderer->meshes3d[index];
  name = TrimName(std::move(name));
  if (name.empty() || name == mesh.displayName)
    return false;
  name = UniqueName(name, [&](const std::string &candidate) {
    for (size_t i = 0; i < sceneRenderer->meshes3d.size(); ++i)
      if (i != index && sceneRenderer->meshes3d[i].displayName == candidate)
        return true;
    return false;
  });
  const bool selected = mesh.displayName == objectName;
  mesh.displayName = name;
  if (selected)
    std::snprintf(objectName, sizeof(objectName), "%s", name.c_str());
  RecordEditorHistory();
  return true;
}

bool MainWindow::RenameUiElement(size_t index, std::string name) {
  if (index >= gameUi.elements.size() || SceneStructureLocked())
    return false;
  auto &element = gameUi.elements[index];
  name = TrimName(std::move(name));
  if (name.empty() || name == element.id)
    return false;
  name = UniqueName(name, [&](const std::string &candidate) {
    for (size_t i = 0; i < gameUi.elements.size(); ++i)
      if (i != index && gameUi.elements[i].id == candidate)
        return true;
    return false;
  });
  // Movable objects link joysticks by name; keep those links.
  if (sceneRenderer)
    for (auto &mesh : sceneRenderer->meshes3d) {
      if (mesh.movementRig.moveJoystick == element.id)
        mesh.movementRig.moveJoystick = name;
      if (mesh.movementRig.lookJoystick == element.id)
        mesh.movementRig.lookJoystick = name;
    }
  element.id = name;
  RecordEditorHistory();
  return true;
}

void MainWindow::BeginHierarchyRename(int mesh, int ui) {
  if (SceneStructureLocked())
    return;
  const std::string *current = nullptr;
  renamingMesh = renamingUi = -1;
  if (sceneRenderer && mesh >= 0 &&
      (size_t)mesh < sceneRenderer->meshes3d.size()) {
    current = &sceneRenderer->meshes3d[mesh].displayName;
    renamingMesh = mesh;
  } else if (ui >= 0 && (size_t)ui < gameUi.elements.size()) {
    current = &gameUi.elements[ui].id;
    renamingUi = ui;
  }
  if (!current)
    return;
  std::snprintf(hierarchyRename, sizeof(hierarchyRename), "%s", current->c_str());
  renameFocus = true;
}

// Drawn on the same line as the row's (label-less) tree node.
void MainWindow::DrawHierarchyRenameField() {
  SameLine();
  SetNextItemWidth(-FLT_MIN);
  if (renameFocus) {
    SetKeyboardFocusHere();
    renameFocus = false;
  }
  const bool enter = InputText("##rename", hierarchyRename,
                               sizeof(hierarchyRename),
                               ImGuiInputTextFlags_EnterReturnsTrue |
                                   ImGuiInputTextFlags_AutoSelectAll);
  if (IsItemActive() && IsKeyPressed(ImGuiKey_Escape)) {
    renamingMesh = renamingUi = -1;
    return;
  }
  // Commit on Enter or when focus leaves the field (click elsewhere, Tab).
  if (enter || IsItemDeactivated()) {
    if (renamingMesh >= 0)
      RenameMesh((size_t)renamingMesh, hierarchyRename);
    else if (renamingUi >= 0)
      RenameUiElement((size_t)renamingUi, hierarchyRename);
    renamingMesh = renamingUi = -1;
  }
}

bool MainWindow::DeletingRemovesLastCamera(size_t index) const {
  if (!sceneRenderer)
    return false;
  for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i)
    if (sceneRenderer->IsMesh3DCamera(i) &&
        !sceneRenderer->IsMesh3DInSubtree(i, index))
      return false; // a camera survives
  return true;
}

int MainWindow::SpawnSceneObject(int kind, int parent,
                                 const glm::vec3 &localPosition) {
  if (!sceneRenderer || kind < 0 || kind >= kSpawnObjectCount ||
      SceneStructureLocked())
    return -1;
  const char *base = kSpawnNames[kind];
  // Auto-number so repeated adds don't collide by name (selection is by name).
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
    ok = sceneRenderer->LoadLight(name, 0, glm::vec3(1.0f, 0.96f, 0.88f), 1.0f,
                                  10.0f, 30.0f, 1.05f);
  else if (kind == 4)
    ok = sceneRenderer->LoadCamera(name);
  else if (kind == 5)
    ok = sceneRenderer->LoadAudioSource(name);
  else if (kind == 6)
    ok = sceneRenderer->LoadScreen(name);
  else
    ok = sceneRenderer->LoadWorldText(name);
  if (!ok)
    return -1;

  size_t idx = sceneRenderer->GetMesh3DCount() - 1;
  glm::vec3 spawnPos = localPosition;
  glm::vec3 spawnRot(0.0f);
  if ((kind == 6 || kind == 7) && parent < 0) {
    // Stand screens on the ground. Text keeps its requested origin; both
    // face the editor camera initially and remain ordinary rotatable meshes.
    if (kind == 6) spawnPos.y += 0.45f + 0.5f;
    const glm::vec3 toCam = sceneRenderer->camera3d.position - spawnPos;
    spawnRot.y = glm::degrees(std::atan2(toCam.x, toCam.z));
  } else if (kind == 5 && parent < 0) {
    spawnPos.y += 0.5f;
  }
  sceneRenderer->SetMesh3DTransform(idx, spawnPos, spawnRot, glm::vec3(1.0f));
  if (parent >= 0)
    sceneRenderer->SetMesh3DParent(idx, parent, false);
  // Record where this object was spawned so Inspect Mode (F2) can answer
  // "which line created this thing?" on hover.
  sceneRenderer->SetMesh3DDebugSource(idx, __FILE__, __LINE__);
  std::snprintf(objectName, sizeof(objectName), "%s", name.c_str());
  selectedUiElement = -1;
  RecordEditorHistory();
  return (int)idx;
}

void MainWindow::DeleteSceneObject(size_t index) {
  if (!sceneRenderer || index >= sceneRenderer->GetMesh3DCount() ||
      SceneStructureLocked() || DeletingRemovesLastCamera(index))
    return;
  std::string name = sceneRenderer->GetMesh3DName(index);
  size_t removed = sceneRenderer->RemoveMesh3D(index);
  objectName[0] = '\0';
  selectedSurface = -1;
  PushMessage("Deleted '" + name + "'" +
              (removed > 1 ? " and " + std::to_string(removed - 1) +
                                 " child object(s)"
                           : std::string()));
  RecordEditorHistory();
}

void MainWindow::RenderHierarchyWindow() {
  LoadGameUiForProject();
  Begin(ICON_UI_HIERARCHY " Hierarchy###Hierarchy", nullptr,
        ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Hierarchy panel");
  ImGuiTreeNodeFlags nodeFlags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
      ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesToNodes;

  // Base Template: Ensure there is always a camera in the scene renderer!
  if (sceneRenderer && !sceneRenderer->HasPlayerCamera()) {
    if (sceneRenderer->LoadCamera("Main Camera"))
      RecordEditorHistory();
  }

  // Edits are queued and applied after the tree is drawn: they renumber the
  // meshes the recursion below is walking.
  enum class Op {
    None, Spawn, Delete, Reparent,
    SpawnCanvas, DeleteCanvas, SpawnUiText, SpawnUiImage, SpawnUiButton, SpawnUiJoystick,
    DeleteUi, MoveUi
  };
  Op op = Op::None;
  int opIndex = -1, opParent = -1, opKind = 0;
  const bool locked = SceneStructureLocked();

  auto createMenu = [&](int parent) {
    for (int k = 0; k < kSpawnObjectCount; ++k)
      if (MenuItem(kSpawnNames[k], nullptr, false, !locked)) {
        op = Op::Spawn;
        opKind = k;
        opParent = parent;
      }
    if (parent < 0 && MenuItem(kSpawnNames[kSpawnObjectCount], nullptr, false,
                                !locked && !hasGameUiCanvas))
      op = Op::SpawnCanvas;
  };

  // Unity-style Hierarchy Top Bar: [+ Create] + Search Filter
  static char hierarchySearch[64] = "";
  BeginDisabled(locked);
  if (Button(ICON_UI_PLUS " Create")) {
    OpenPopup("HierarchyCreatePopup");
  }
  EndDisabled();
  if (BeginPopup("HierarchyCreatePopup")) {
    createMenu(-1);
    EndPopup();
  }
  SameLine();
  const bool hasHierQuery = hierarchySearch[0] != '\0';
  SetNextItemWidth(hasHierQuery ? GetContentRegionAvail().x - 24.0f : -1.0f);
  InputTextWithHint("##HierarchySearch", ICON_UI_SEARCH " Search hierarchy...",
                    hierarchySearch, IM_ARRAYSIZE(hierarchySearch));
  if (hasHierQuery) {
    SameLine(0, 3.0f);
    if (Button("×##ClearHierSearch", ImVec2(20, 0))) {
      hierarchySearch[0] = '\0';
    }
  }
  Separator();

  auto matchesHierFilter = [&](const std::string &name) -> bool {
    if (hierarchySearch[0] == '\0')
      return true;
    std::string nl = name, ql = hierarchySearch;
    std::transform(nl.begin(), nl.end(), nl.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(ql.begin(), ql.end(), ql.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return nl.find(ql) != std::string::npos;
  };

  if (sceneRenderer) {
    const size_t count = sceneRenderer->GetMesh3DCount();
    std::vector<std::vector<size_t>> children(count);
    std::vector<size_t> roots;
    for (size_t i = 0; i < count; ++i) {
      int p = sceneRenderer->GetMesh3DParent(i);
      if (p >= 0 && (size_t)p < count)
        children[(size_t)p].push_back(i);
      else
        roots.push_back(i);
    }

    std::function<bool(size_t)> subtreeMatches = [&](size_t idx) -> bool {
      if (matchesHierFilter(sceneRenderer->GetMesh3DName(idx)))
        return true;
      for (size_t ch : children[idx]) {
        if (subtreeMatches(ch))
          return true;
      }
      return false;
    };

    // Accept a hierarchy entry dropped on the current item.
    auto dropTarget = [&](int newParent) {
      if (!BeginDragDropTarget())
        return;
      if (const ImGuiPayload *pl = AcceptDragDropPayload("ILMEEE_MESH")) {
        op = Op::Reparent;
        opIndex = *(const int *)pl->Data;
        opParent = newParent;
      }
      EndDragDropTarget();
    };

    std::function<void(size_t)> drawNode = [&](size_t i) {
      if (!subtreeMatches(i))
        return;
      const std::string &meshName = sceneRenderer->GetMesh3DName(i);
      bool isCamera = sceneRenderer->IsMesh3DCamera(i);
      bool isLight = sceneRenderer->IsMesh3DLight(i);

      // Pick crisp entity SVG icon (no redundant [Cam]/[Light] text clutter)
      std::string svgPath = "assets/icons/svg/ui_scene.svg";
      if (isCamera) {
        svgPath = "assets/icons/svg/camera.svg";
      } else if (isLight) {
        svgPath = "assets/icons/svg/ui_lighting.svg";
      } else if (sceneRenderer->meshes3d[i].isAudioEmitter) {
        svgPath = "assets/icons/svg/ext_audio.svg";
      } else if (sceneRenderer->meshes3d[i].video.enabled) {
        svgPath = "assets/icons/svg/ui_video.svg";
      } else if (sceneRenderer->meshes3d[i].isWorldText) {
        svgPath = "assets/icons/svg/ui_text3d.svg";
      }

      svgIcons.DrawIcon(svgPath, 15);

      ImGuiTreeNodeFlags flags = nodeFlags;
      if (children[i].empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
      if (meshName == objectName)
        flags |= ImGuiTreeNodeFlags_Selected;

      PushID((int)i);
      const std::string &label = meshName;
      const bool renaming = renamingMesh == (int)i;
      SetNextItemOpen(true, ImGuiCond_Once);
      bool open = renaming ? TreeNodeEx("##node", flags, "%s", "")
                           : TreeNodeEx(label.c_str(), flags);
      DEBUG_TRACE_ITEM("Hierarchy: scene mesh entry");
      if (IsItemClicked(ImGuiMouseButton_Left) ||
          IsItemClicked(ImGuiMouseButton_Right)) {
        std::snprintf(objectName, sizeof(objectName), "%s", meshName.c_str());
        selectedUiElement = -1;
      }
      if (!locked && IsItemHovered() &&
          IsMouseDoubleClicked(ImGuiMouseButton_Left))
        BeginHierarchyRename((int)i, -1);

      if (!locked && BeginDragDropSource()) {
        int payload = (int)i;
        SetDragDropPayload("ILMEEE_MESH", &payload, sizeof(payload));
        Text("%s", label.c_str());
        EndDragDropSource();
      }
      dropTarget((int)i);

      if (BeginPopupContextItem("HierarchyItemMenu")) {
        if (MenuItem("Rename", "F2", false, !locked))
          BeginHierarchyRename((int)i, -1);
        if (BeginMenu("Create Child", !locked)) {
          createMenu((int)i);
          EndMenu();
        }
        if (sceneRenderer->GetMesh3DParent(i) >= 0 &&
            MenuItem("Unparent", nullptr, false, !locked)) {
          op = Op::Reparent;
          opIndex = (int)i;
          opParent = -1;
        }
        Separator();
        bool lastCam = DeletingRemovesLastCamera(i);
        if (MenuItem("Delete", "Del", false, !locked && !lastCam)) {
          op = Op::Delete;
          opIndex = (int)i;
        }
        if (lastCam && IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
          SetTooltip("The scene needs at least one camera");
        if (locked)
          TextDisabled("Stop the game to edit the scene");
        EndPopup();
      }
      if (renaming)
        DrawHierarchyRenameField();

      if (open) {
        for (size_t c : children[i])
          drawNode(c);
        TreePop();
      }
      PopID();
    };

    std::string sceneRootLabel = projectHandler.currentSceneName.empty()
                                     ? std::string(ICON_UI_SCENE " Scene###SceneRootNode")
                                     : (std::string(ICON_UI_SCENE " ") +
                                        projectHandler.currentSceneName +
                                        "###SceneRootNode");
    const bool sceneOpen =
        TreeNodeEx(sceneRootLabel.c_str(),
                   nodeFlags | ImGuiTreeNodeFlags_DefaultOpen);
    if (BeginPopupContextItem("SceneRootMenu")) {
      if (BeginMenu("Create", !locked)) {
        createMenu(-1);
        EndMenu();
      }
      EndPopup();
    }
    if (sceneOpen) {
      dropTarget(-1); // dropping on "Scene" moves the object to the root
      for (size_t r : roots)
        drawNode(r);

      // Screen-space UI is a Canvas game object with Text/Image/Button
      // children. Its data is saved in the scene alongside world entities.
      if (hasGameUiCanvas) {
      PushID("GameCanvas");
      svgIcons.DrawIcon("assets/icons/svg/ui_canvas.svg", 15);
      SetNextItemOpen(true, ImGuiCond_Once);
      ImGuiTreeNodeFlags canvasFlags = nodeFlags | ImGuiTreeNodeFlags_DefaultOpen;
      if (selectedUiElement == -2)
        canvasFlags |= ImGuiTreeNodeFlags_Selected;
      bool canvasOpen = TreeNodeEx("Canvas", canvasFlags);
      if (IsItemClicked(ImGuiMouseButton_Left) ||
          IsItemClicked(ImGuiMouseButton_Right)) {
        selectedUiElement = -2;
        objectName[0] = '\0';
      }
      if (!locked && BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = AcceptDragDropPayload("ASSET_PATH"))
          CreateUiImageAsset((const char *)payload->Data, 24.0f, 24.0f);
        // A UI element dropped on the Canvas goes to the top of the list.
        if (const ImGuiPayload *payload = AcceptDragDropPayload("ILMEEE_UI")) {
          op = Op::MoveUi;
          opIndex = *(const int *)payload->Data;
          opParent = 0;
        }
        EndDragDropTarget();
      }
      if (BeginPopupContextItem("CanvasMenu")) {
        if (BeginMenu("Create Child", !locked)) {
          if (MenuItem("Text")) op = Op::SpawnUiText;
          if (MenuItem("Image")) op = Op::SpawnUiImage;
          if (MenuItem("Button")) op = Op::SpawnUiButton;
          if (MenuItem("Joystick")) op = Op::SpawnUiJoystick;
          EndMenu();
        }
        Separator();
        if (MenuItem("Delete Canvas", "Del", false, !locked))
          op = Op::DeleteCanvas;
        EndPopup();
      }
      if (canvasOpen) {
        for (int i = 0; i < (int)gameUi.elements.size(); ++i) {
          const auto &element = gameUi.elements[i];
          if (!matchesHierFilter(element.id))
            continue;
          PushID(i);
          const char *uiIconPath =
              element.kind == ilmeee::UiKind::Text
                  ? "assets/icons/svg/ui_text3d.svg"
              : element.kind == ilmeee::UiKind::Image
                  ? "assets/icons/svg/ext_image.svg"
              : element.kind == ilmeee::UiKind::Joystick
                  ? "assets/icons/svg/ui_game.svg"
                  : "assets/icons/svg/ui_canvas.svg";
          svgIcons.DrawIcon(uiIconPath, 15);
          ImGuiTreeNodeFlags uiFlags = nodeFlags | ImGuiTreeNodeFlags_Leaf |
                                     ImGuiTreeNodeFlags_NoTreePushOnOpen;
          if (selectedUiElement == i)
            uiFlags |= ImGuiTreeNodeFlags_Selected;
          const char *kind = element.kind == ilmeee::UiKind::Text ? "Text" :
              element.kind == ilmeee::UiKind::Image ? "Image" :
              element.kind == ilmeee::UiKind::Joystick ? "Joystick" : "Button";
          const bool renamingThis = renamingUi == i;
          if (renamingThis)
            TreeNodeEx("ui", uiFlags, "%s", "");
          else
            TreeNodeEx("ui", uiFlags, "%s (%s)%s", element.id.c_str(), kind,
                       element.visible ? "" : " [hidden]");
          if (IsItemClicked(ImGuiMouseButton_Left) ||
              IsItemClicked(ImGuiMouseButton_Right)) {
            selectedUiElement = i;
            objectName[0] = '\0';
          }
          if (!locked && IsItemHovered() &&
              IsMouseDoubleClicked(ImGuiMouseButton_Left))
            BeginHierarchyRename(-1, i);
          // Drag to reorder. List order is draw order: lower rows are
          // drawn on top (and get clicks first).
          if (!locked && BeginDragDropSource()) {
            SetDragDropPayload("ILMEEE_UI", &i, sizeof(i));
            Text("%s (%s)", element.id.c_str(), kind);
            EndDragDropSource();
          }
          if (!locked && BeginDragDropTarget()) {
            if (element.kind == ilmeee::UiKind::Image ||
                element.kind == ilmeee::UiKind::Button)
              if (const ImGuiPayload *payload = AcceptDragDropPayload("ASSET_PATH"))
                BindUiImageAsset(i, (const char *)payload->Data);
            // Upper half of the row inserts before it, lower half after.
            const ImVec2 rowMin = GetItemRectMin(), rowMax = GetItemRectMax();
            const bool after = GetMousePos().y > (rowMin.y + rowMax.y) * 0.5f;
            if (const ImGuiPayload *payload = AcceptDragDropPayload(
                    "ILMEEE_UI", ImGuiDragDropFlags_AcceptBeforeDelivery |
                                     ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
              const float y = after ? rowMax.y : rowMin.y;
              GetWindowDrawList()->AddLine(ImVec2(rowMin.x, y),
                                           ImVec2(rowMax.x, y),
                                           GetColorU32(ImGuiCol_DragDropTarget),
                                           2.0f);
              if (payload->IsDelivery()) {
                op = Op::MoveUi;
                opIndex = *(const int *)payload->Data;
                opParent = after ? i + 1 : i;
              }
            }
            EndDragDropTarget();
          }
          if (BeginPopupContextItem("UiItemMenu")) {
            if (MenuItem("Rename", "F2", false, !locked))
              BeginHierarchyRename(-1, i);
            if (MenuItem("Delete", "Del", false, !locked)) {
              op = Op::DeleteUi;
              opIndex = i;
            }
            EndPopup();
          }
          if (renamingThis)
            DrawHierarchyRenameField();
          PopID();
        }
        TreePop();
      }
      PopID();
      }
      TreePop();
    }

    // Right-click on empty space: create at the scene root.
    if (BeginPopupContextWindow("HierarchyEmptyMenu",
                                ImGuiPopupFlags_MouseButtonRight |
                                    ImGuiPopupFlags_NoOpenOverItems)) {
      if (BeginMenu("Create", !locked)) {
        createMenu(-1);
        EndMenu();
      }
      EndPopup();
    }

    if (!locked && IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !GetIO().WantTextInput && IsKeyPressed(ImGuiKey_F2, false)) {
      if (selectedUiElement >= 0)
        BeginHierarchyRename(-1, selectedUiElement);
      else
        BeginHierarchyRename(SelectedMeshIndex(), -1);
    }
    if (op == Op::None && IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !GetIO().WantTextInput && IsKeyPressed(ImGuiKey_Delete, false)) {
      int sel = SelectedMeshIndex();
      if (selectedUiElement == -2 && !locked) {
        op = Op::DeleteCanvas;
      } else if (selectedUiElement >= 0 && !locked) {
        op = Op::DeleteUi;
        opIndex = selectedUiElement;
      } else if (sel >= 0) {
        op = Op::Delete;
        opIndex = sel;
      }
    }
  }

  if (op != Op::None) // indices may shift; drop any rename in progress
    renamingMesh = renamingUi = -1;
  switch (op) {
  case Op::Spawn:
    SpawnSceneObject(opKind, opParent, glm::vec3(0.0f));
    break;
  case Op::Delete:
    DeleteSceneObject((size_t)opIndex);
    break;
  case Op::Reparent:
    // Keep the object where it is on screen; refuses cycles on its own.
    if (sceneRenderer->SetMesh3DParent((size_t)opIndex, opParent, true))
      RecordEditorHistory();
    break;
  case Op::None:
    break;
  case Op::SpawnCanvas:
    if (!locked && !hasGameUiCanvas) {
      gameUi = ilmeee::GameUI{};
      hasGameUiCanvas = true;
      selectedUiElement = -2;
      objectName[0] = '\0';
      PushMessage("[INFO] Canvas created. Edit it in the Scene tab.");
    }
    break;
  case Op::DeleteCanvas:
    if (!locked && hasGameUiCanvas) {
      gameUi = ilmeee::GameUI{};
      hasGameUiCanvas = false;
      selectedUiElement = -1;
      PushMessage("[INFO] Canvas deleted");
    }
    break;
  case Op::SpawnUiText:
  case Op::SpawnUiImage:
  case Op::SpawnUiButton:
  case Op::SpawnUiJoystick: {
    if (locked) break;
    ilmeee::UiElement element;
    const char *prefix = op == Op::SpawnUiText ? "Text" :
                         op == Op::SpawnUiImage ? "Image" :
                         op == Op::SpawnUiJoystick ? "Joystick" : "Button";
    for (int n = 1;; ++n) {
      element.id = std::string(prefix) + " " + std::to_string(n);
      if (std::none_of(gameUi.elements.begin(), gameUi.elements.end(),
                       [&](const ilmeee::UiElement &existing) {
                         return existing.id == element.id;
                       })) break;
    }
    element.kind = op == Op::SpawnUiText ? ilmeee::UiKind::Text :
                   op == Op::SpawnUiImage ? ilmeee::UiKind::Image :
                   op == Op::SpawnUiJoystick ? ilmeee::UiKind::Joystick :
                                               ilmeee::UiKind::Button;
    element.text = element.kind == ilmeee::UiKind::Button ? "Button" :
                   element.kind == ilmeee::UiKind::Text ? "Text" : "";
    if (element.kind == ilmeee::UiKind::Joystick) {
      // Bottom-left thumb stick; a second one can go bottom-right for look.
      element.anchor = ilmeee::UiAnchor::BottomLeft;
      element.x = element.y = 48.0f;
      element.width = element.height = 180.0f;
      element.color = ImVec4(1.0f, 1.0f, 1.0f, 0.85f);
      element.background = ImVec4(0.10f, 0.12f, 0.16f, 0.45f);
    }
    gameUi.elements.push_back(std::move(element));
    selectedUiElement = (int)gameUi.elements.size() - 1;
    objectName[0] = '\0';
    break;
  }
  case Op::DeleteUi:
    if (!locked && opIndex >= 0 && opIndex < (int)gameUi.elements.size()) {
      gameUi.elements.erase(gameUi.elements.begin() + opIndex);
      selectedUiElement = -2;
    }
    break;
  case Op::MoveUi: {
    // opIndex = dragged element, opParent = insert position before removal.
    auto &elements = gameUi.elements;
    const int n = (int)elements.size();
    if (locked || opIndex < 0 || opIndex >= n || opParent < 0 || opParent > n)
      break;
    const uint64_t selected =
        selectedUiElement >= 0 && selectedUiElement < n
            ? elements[selectedUiElement].objectId : 0;
    const int to = opParent > opIndex ? opParent - 1 : opParent;
    if (to == opIndex) break;
    ilmeee::UiElement moved = std::move(elements[opIndex]);
    elements.erase(elements.begin() + opIndex);
    elements.insert(elements.begin() + to, std::move(moved));
    for (int k = 0; selected && k < n; ++k)
      if (elements[k].objectId == selected) selectedUiElement = k;
    break;
  }
  }
  if (op != Op::None) RecordEditorHistory();
  End();
}

void MainWindow::RenderExplorerWindow(
    const HandlerProject::AssetFile &projectRoot,
    const HandlerProject::AssetFile &assetFolder,
                                      const string &assetPath,
                                      bool firstOpenProject) {
  (void)assetFolder;
  (void)assetPath;
  Begin(ICON_UI_EXPLORER " Explorer###Explorer", nullptr,
        ImGuiWindowFlags_NoCollapse);
  projectHandler.onFileClicked = [this](const HandlerProject::AssetFile &file) {
    if (ilmeee::IsAnimationPath(file.fullPath) ||
        ilmeee::IsAnimationControllerPath(file.fullPath))
      OpenMotionPreview(file.fullPath);
  };
  DEBUG_TRACE_PANEL("Explorer panel");
  ImVec2 pos = GetWindowPos();
  ImVec2 size = GetWindowSize();
  HandleBackground(pos, size);

  if (!firstOpenProject) {
    Spacing();
    TextDisabled("  No project opened yet.");
    SameLine();
    if (SmallButton(ICON_UI_EXPLORER " Open Project...")) {
      projectHandler.isOpenedProject = true;
      projectHandler.OpenFolder();
    }
    End();
    return;
  }

  BeginGroup();

  // Top Unity-style Project Browser Toolbar
  if (Button(ICON_UI_REFRESH " Refresh")) {
    projectHandler.isOpenedProject = true;
  }
  if (IsItemHovered()) {
    SetTooltip("Manually rescan project assets");
  }

  SameLine();

  bool watcherRunning = projectHandler.IsFileWatcherRunning();
  if (watcherRunning) {
    PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.34f, 0.24f, 0.90f));
    PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.44f, 0.31f, 0.95f));
  }
  if (Button(watcherRunning ? ICON_UI_EYE " Watching" : ICON_UI_EYE " Watch Off")) {
    if (watcherRunning) {
      projectHandler.StopFileWatcher();
    } else {
      projectHandler.StartFileWatcher();
    }
  }
  if (watcherRunning) {
    PopStyleColor(2);
  }
  if (IsItemHovered()) {
    SetTooltip(watcherRunning ? "Auto-refresh file watcher is active"
                              : "Enable auto-refresh file watcher");
  }

  SameLine();

  const std::filesystem::path projRootPath =
      std::filesystem::path(projectHandler.projectPath).lexically_normal();
  const std::filesystem::path curDirPath =
      std::filesystem::path(projectHandler.currentDirectory).lexically_normal();
  const bool canGoUp =
      !projectHandler.currentDirectory.empty() &&
      curDirPath != projRootPath && curDirPath.has_parent_path() &&
      curDirPath.parent_path() != curDirPath;

  BeginDisabled(!canGoUp);
  if (Button(ICON_UI_BACK " Up")) {
    projectHandler.currentDirectory = curDirPath.parent_path().string();
    projectHandler.selectedAsset = nullptr;
    projectHandler.fileExplorerSelectedPaths.clear();
  }
  EndDisabled();
  if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    SetTooltip(canGoUp ? "Navigate to parent folder"
                       : "Already at project root");
  }

  SameLine();
  HandleSearch();

  ImVec2 contentSize = GetContentRegionAvail();

  // Left panel (Two-column Unity Project Tree)
  BeginChild("ProjectRoot", ImVec2(explorerSplitPosition, 0), true);
  RenderMotionLibrary();
  projectHandler.DrawAssetTree(projectRoot);
  EndChild();

  // Crisp draggable vertical splitter
  SameLine(0, 3.0f);
  PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.26f, 0.45f));
  PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.23f, 0.51f, 0.96f, 0.75f));
  PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.23f, 0.51f, 0.96f, 1.0f));
  Button("##Splitter", ImVec2(3.0f, contentSize.y));
  PopStyleColor(3);

  if (IsItemActive()) {
    float delta = GetIO().MouseDelta.x;
    if (explorerSplitPosition + delta >= MIN_PANEL_WIDTH &&
        explorerSplitPosition + delta <= contentSize.x - MIN_PANEL_WIDTH) {
      explorerSplitPosition += delta;
    }
  }
  if (IsItemHovered())
    SetMouseCursor(ImGuiMouseCursor_ResizeEW);

  // Right panel (Breadcrumbs + Asset Card Grid)
  SameLine(0, 3.0f);
  BeginChild("AssetRoot", ImVec2(0, 0), true);
  projectHandler.DrawFolderGridView();
  EndChild();

  EndGroup();

  End();
}

void MainWindow::RenderInspectorWindow() {
  Begin(ICON_UI_INSPECTOR " Inspector###Inspector", nullptr,
        ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Inspector panel");
  ImVec2 pos = GetWindowPos();
  ImVec2 size = GetWindowSize();
  HandleBackground(pos, size);

  if (selectedUiElement == -2 ||
      (selectedUiElement >= 0 &&
       selectedUiElement < (int)gameUi.elements.size())) {
    svgIcons.DrawIcon("assets/icons/svg/ui_canvas.svg", 18);
    TextUnformatted(selectedUiElement == -2 ? "UI Canvas" : "UI Element");
    Separator();
    if (SceneStructureLocked()) BeginDisabled();
    RenderGameUiEditor();
    if (SceneStructureLocked()) EndDisabled();
    End();
    return;
  }

  if (SceneStructureLocked()) BeginDisabled();

  // Unity-style GameObject Header Card
  int meshIdx = SelectedMeshIndex();
  if (meshIdx >= 0) {
    bool hdrCam = sceneRenderer->IsMesh3DCamera((size_t)meshIdx);
    bool hdrLight = sceneRenderer->IsMesh3DLight((size_t)meshIdx);
    bool hdrText = sceneRenderer->meshes3d[(size_t)meshIdx].isWorldText;
    const char *hdrIcon =
        hdrCam     ? "assets/icons/svg/camera.svg"
        : hdrLight ? "assets/icons/svg/ui_lighting.svg"
        : hdrText  ? "assets/icons/svg/ui_text3d.svg"
                   : "assets/icons/svg/ui_scene.svg";
    const char *hdrTag =
        hdrCam     ? "Camera"
        : hdrLight ? "Light"
        : hdrText  ? "3D Text"
        : (sceneRenderer->GetMesh3DBoneCount((size_t)meshIdx) > 0)
            ? "Skinned Mesh"
            : "GameObject";

    svgIcons.DrawIcon(hdrIcon, 20);
    static char nameEdit[64];
    static bool nameEditing = false;
    if (!nameEditing)
      std::snprintf(nameEdit, sizeof(nameEdit), "%s", objectName);
    BeginDisabled(SceneStructureLocked());
    SetNextItemWidth(GetContentRegionAvail().x - 86.0f);
    InputText("##ObjNameHeader", nameEdit, sizeof(nameEdit));
    nameEditing = IsItemActive();
    if (IsItemDeactivatedAfterEdit())
      RenameMesh((size_t)meshIdx, nameEdit);
    EndDisabled();
    SameLine();
    TextDisabled("%s", hdrTag);
  } else {
    svgIcons.DrawIcon("assets/icons/svg/ui_inspector.svg", 18);
    TextDisabled("No object selected");
    SetNextItemWidth(-1.0f);
    InputTextWithHint("##ObjNameFallback", "Select or type object name...",
                      objectName, IM_ARRAYSIZE(objectName));
    meshIdx = SelectedMeshIndex();
  }
  Separator();
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

  auto drawVec3AxisRow = [&](const char *rowLabel, float vals[3], float speed,
                             float resetVal, float minV, float maxV) -> std::pair<bool, bool> {
    bool valChanged = false;
    bool deactivated = false;
    PushID(rowLabel);

    const float labelColW = 68.0f;
    AlignTextToFramePadding();
    TextUnformatted(rowLabel);
    SameLine(labelColW);

    const float totalAvail = std::max(90.0f, GetContentRegionAvail().x);
    const float badgeW = 17.0f;
    const float gap = 3.0f;
    const float dragW =
        std::max(28.0f, (totalAvail - (badgeW * 3.0f + gap * 5.0f)) / 3.0f);

    static const char *kAxisNames[3] = {"X", "Y", "Z"};
    static const ImVec4 kAxisColors[3] = {
        ImVec4(0.78f, 0.20f, 0.22f, 1.0f), // X Red
        ImVec4(0.18f, 0.62f, 0.28f, 1.0f), // Y Green
        ImVec4(0.20f, 0.44f, 0.85f, 1.0f), // Z Blue
    };

    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 4.0f));
    for (int a = 0; a < 3; ++a) {
      if (a > 0)
        SameLine();
      PushID(a);
      PushStyleColor(ImGuiCol_Button, kAxisColors[a]);
      PushStyleColor(ImGuiCol_ButtonHovered,
                     ImVec4(std::min(1.0f, kAxisColors[a].x + 0.14f),
                            std::min(1.0f, kAxisColors[a].y + 0.14f),
                            std::min(1.0f, kAxisColors[a].z + 0.14f), 1.0f));
      PushStyleColor(ImGuiCol_ButtonActive, kAxisColors[a]);
      PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 4));
      if (Button(kAxisNames[a], ImVec2(badgeW, GetFrameHeight()))) {
        vals[a] = resetVal;
        valChanged = true;
        deactivated = true;
      }
      PopStyleVar();
      PopStyleColor(3);
      if (IsItemHovered())
        SetTooltip("Reset %s.%s to %.2f", rowLabel, kAxisNames[a], resetVal);

      SameLine(0, 1.0f);
      SetNextItemWidth(dragW);
      if (minV < maxV) {
        valChanged |= DragFloat("##v", &vals[a], speed, minV, maxV, "%.3f");
      } else {
        valChanged |= DragFloat("##v", &vals[a], speed, 0.0f, 0.0f, "%.3f");
      }
      deactivated |= IsItemDeactivatedAfterEdit();
      PopID();
    }
    PopStyleVar();
    PopID();
    return {valChanged, deactivated};
  };

  if (CollapsingHeader(ICON_UI_MOVE " Transform###Transform",
                       ImGuiTreeNodeFlags_DefaultOpen)) {
    DEBUG_TRACE_ITEM("Inspector: Transform header");
    bool changed = false;
    bool editFinished = false;
    auto [cp, dp] = drawVec3AxisRow("Position", position, 0.02f, 0.0f, 0.0f, 0.0f);
    changed |= cp;
    editFinished |= dp;
    DEBUG_TRACE_ITEM("Inspector: Position drag");
    auto [cr, dr] = drawVec3AxisRow("Rotation", rotation, 0.5f, 0.0f, 0.0f, 0.0f);
    changed |= cr;
    editFinished |= dr;
    DEBUG_TRACE_ITEM("Inspector: Rotation drag");
    auto [cs, ds] = drawVec3AxisRow("Scale", scale, 0.01f, 1.0f, 0.001f, 100.0f);
    changed |= cs;
    editFinished |= ds;
    DEBUG_TRACE_ITEM("Inspector: Scale drag");
    if (meshSelected && changed) {
      sceneRenderer->SetMesh3DTransform(
          (size_t)meshIdx, glm::vec3(position[0], position[1], position[2]),
          glm::vec3(rotation[0], rotation[1], rotation[2]),
          glm::vec3(scale[0], scale[1], scale[2]));
    }
    if (meshSelected && editFinished)
      RecordEditorHistory();
  }

  const bool isLight =
      meshSelected && sceneRenderer->IsMesh3DLight((size_t)meshIdx);
  const bool isCamera =
      meshSelected && sceneRenderer->IsMesh3DCamera((size_t)meshIdx);
  const bool isWorldText = meshSelected && sceneRenderer->meshes3d[(size_t)meshIdx].isWorldText;
  if (isWorldText && CollapsingHeader("Text (3D)", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &text = sceneRenderer->meshes3d[(size_t)meshIdx].worldText;
    char content[16385];
    std::snprintf(content, sizeof(content), "%s", text.text.c_str());
    bool finished = false;
    TextUnformatted("Content");
    if (InputTextMultiline("##WorldTextContent", content, sizeof(content), ImVec2(-1, 100))) text.text = content;
    finished |= IsItemDeactivatedAfterEdit();
    SetNextItemWidth(105);
    DragFloat("Font size (world units)", &text.size, 0.01f, 0.001f, 100.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    finished |= IsItemDeactivatedAfterEdit();
    ColorEdit4("Text Color", &text.color.x);
    finished |= IsItemDeactivatedAfterEdit();
    finished |= Combo("Alignment", &text.alignment, "Left\0Center\0Right\0");
    finished |= Checkbox("Text Visible", &text.visible);
    TextDisabled("World-space text. Rotate/scale or parent it like a mesh.");
    if (finished) RecordEditorHistory();
  }

  // Mesh section: shown for any plain mesh (not a light/camera) by name.
  if (meshSelected && !isLight && !isCamera && !isWorldText) {
    if (CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen)) {
      Text("Path: %s", sceneRenderer->GetMesh3DPath((size_t)meshIdx).c_str());
      Text("Vertices: %u",
           sceneRenderer->GetMesh3DVertexCount((size_t)meshIdx));
      Text("Triangles: %u",
           sceneRenderer->GetMesh3DTriangleCount((size_t)meshIdx));
      uint32_t boneCount = sceneRenderer->GetMesh3DBoneCount((size_t)meshIdx);
      if (boneCount > 0) {
        Text("Bones: %u", boneCount);
        RenderAnimationInspector((size_t)meshIdx);
        RenderSpringBoneInspector((size_t)meshIdx);
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
            const bool bound = sceneRenderer->BindMesh3DSubmeshTexture(
                (size_t)meshIdx, (uint32_t)selectedSurface, outPath);
            if (bound)
              RecordEditorHistory();
            NFD_FreePath(outPath);
          }
          NFD_Quit();
        }
        SameLine();
        if (Button("Clear Texture")) {
          sceneRenderer->ClearMesh3DSubmeshTexture((size_t)meshIdx,
                                                   (uint32_t)selectedSurface);
          RecordEditorHistory();
        }
        Separator();
      }

      static bool s_gridVisible = true;
      s_gridVisible = sceneRenderer->IsGrid3DVisible();
      if (Checkbox("Show 3D Grid", &s_gridVisible)) {
        sceneRenderer->SetGrid3DVisible(s_gridVisible);
        RecordEditorHistory();
      }
      if (Button("Reset Transform")) {
        sceneRenderer->SetMesh3DTransform((size_t)meshIdx, glm::vec3(0.0f),
                                          glm::vec3(0.0f), glm::vec3(1.0f));
        RecordEditorHistory();
      }
    }
  }

  const bool hasMovementScript =
      meshSelected && !isLight && !isCamera &&
      (sceneRenderer->meshes3d[(size_t)meshIdx].movable ||
       !sceneRenderer->meshes3d[(size_t)meshIdx].movementScript.empty());
  if (hasMovementScript) {
    auto &mesh = sceneRenderer->meshes3d[(size_t)meshIdx];
    bool keep = true;
    const bool open = CollapsingHeader("Movement Script", &keep,
                                       ImGuiTreeNodeFlags_DefaultOpen);
    if (SceneStructureLocked())
      BeginDisabled();
    if (open && keep) {
      if (Checkbox("Movable", &mesh.movable)) {
        if (mesh.movable && mesh.movementScript.empty())
          mesh.movementScript = "WASD XZ";
        RecordEditorHistory();
      }
      if (mesh.movable) {
        const auto names = ilmeee::MovementScriptNames();
        const char *selected =
            mesh.movementScript.empty() ? "None" : mesh.movementScript.c_str();
        if (BeginCombo("Script", selected)) {
          for (const auto &name : names) {
            if (Selectable(name.c_str(), mesh.movementScript == name)) {
              mesh.movementScript = name;
              RecordEditorHistory();
            }
          }
          EndCombo();
        }
        DragFloat("Speed", &mesh.movementSpeed, 0.1f, 0.0f, 100.0f,
                  "%.2f units/s");
        if (IsItemDeactivatedAfterEdit())
          RecordEditorHistory();

        SeparatorText("Camera");
        auto &rig = mesh.movementRig;
        const auto cameraLabel = [&](int index) -> std::string {
          if (index < 0)
            return "Game View Camera";
          return sceneRenderer->GetMesh3DName((size_t)index);
        };
        if (BeginCombo("Camera", cameraLabel(rig.camera).c_str())) {
          if (Selectable("Game View Camera", rig.camera < 0)) {
            rig.camera = -1;
            RecordEditorHistory();
          }
          for (size_t c = 0; c < sceneRenderer->GetMesh3DCount(); ++c) {
            if (!sceneRenderer->IsMesh3DCamera(c))
              continue;
            PushID((int)c);
            if (Selectable(cameraLabel((int)c).c_str(), rig.camera == (int)c)) {
              rig.camera = (int)c;
              RecordEditorHistory();
            }
            PopID();
          }
          EndCombo();
        }
        int style = static_cast<int>(rig.style);
        if (Combo("POV Style", &style, ilmeee::kCameraStyleNames)) {
          rig.style = static_cast<ilmeee::CameraStyle>(style);
          // Pair the style with the script that moves the way it looks.
          if (rig.style == ilmeee::CameraStyle::SideScroller)
            mesh.movementScript = "Platformer";
          else if (rig.style != ilmeee::CameraStyle::None &&
                   mesh.movementScript == "WASD XZ")
            mesh.movementScript = "Camera Relative";
          RecordEditorHistory();
        }
        if (rig.style != ilmeee::CameraStyle::None) {
          if (rig.style != ilmeee::CameraStyle::FirstPerson) {
            DragFloat("Distance", &rig.distance, 0.05f, 0.0f, 100.0f, "%.2f");
            if (IsItemDeactivatedAfterEdit())
              RecordEditorHistory();
          }
          DragFloat(rig.style == ilmeee::CameraStyle::FirstPerson ? "Eye Height"
                                                                  : "Height",
                    &rig.height, 0.05f, -10.0f, 50.0f, "%.2f");
          if (IsItemDeactivatedAfterEdit())
            RecordEditorHistory();
        }

        SeparatorText("UI Input");
        const auto joystickCombo = [&](const char *label, std::string &id) {
          if (!BeginCombo(label, id.empty() ? "None" : id.c_str()))
            return;
          if (Selectable("None", id.empty())) {
            id.clear();
            RecordEditorHistory();
          }
          for (const auto &element : gameUi.elements) {
            if (element.kind != ilmeee::UiKind::Joystick)
              continue;
            if (Selectable(element.id.c_str(), id == element.id)) {
              id = element.id;
              RecordEditorHistory();
            }
          }
          EndCombo();
        };
        LoadGameUiForProject();
        joystickCombo("Move Joystick", rig.moveJoystick);
        joystickCombo("Look Joystick", rig.lookJoystick);
        if (std::none_of(gameUi.elements.begin(), gameUi.elements.end(),
                         [](const ilmeee::UiElement &e) {
                           return e.kind == ilmeee::UiKind::Joystick;
                         }))
          TextDisabled("Add one: Canvas > Create Child > Joystick");

        TextDisabled("Game tab: WASD move, Space jump,");
        TextDisabled("right-drag / arrow keys look");
        TextDisabled("Custom C++ functions: UserMovementScripts.cpp");
      }
    }
    if (!keep && !SceneStructureLocked()) {
      mesh.movable = false;
      mesh.movementScript.clear();
      mesh.movementSpeed = 3.0f;
      mesh.movementRig = {};
      RecordEditorHistory();
    }
    if (SceneStructureLocked())
      EndDisabled();
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
      bool propertyFinished = false;

      const char *types[] = {"Directional", "Point", "Spotlight (Senter)"};
      if (Combo("Light Type", &type, types, IM_ARRAYSIZE(types))) {
        sceneRenderer->SetMesh3DLightType((size_t)meshIdx, type);
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();

      float col[3] = {color.x, color.y, color.z};
      if (ColorEdit3("Color", col)) {
        sceneRenderer->SetMesh3DLightColor((size_t)meshIdx,
                                           glm::vec3(col[0], col[1], col[2]));
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();

      if (DragFloat("Intensity", &intensity, 0.05f, 0.0f, 20.0f, "%.2f")) {
        sceneRenderer->SetMesh3DLightIntensity((size_t)meshIdx, intensity);
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();

      if (type > 0) { // Point or Spotlight
        if (DragFloat("Range", &range, 0.1f, 0.1f, 1000.0f, "%.1f")) {
          sceneRenderer->SetMesh3DLightRange((size_t)meshIdx, range);
        }
        propertyFinished |= IsItemDeactivatedAfterEdit();
      }

      if (type == 2) { // Spotlight (Senter)
        if (SliderFloat("Spot Angle", &spotAngle, 1.0f, 179.0f, "%.1f deg")) {
          sceneRenderer->SetMesh3DLightSpotAngle((size_t)meshIdx, spotAngle);
        }
        propertyFinished |= IsItemDeactivatedAfterEdit();
      }

      if (SliderFloat("Lighting Gamma", &gamma, 0.2f, 4.0f, "%.2f")) {
        sceneRenderer->SetMesh3DLightGamma((size_t)meshIdx, gamma);
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();
      if (propertyFinished)
        RecordEditorHistory();
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
      bool propertyFinished = false;
      if (Combo("Projection", &proj, projections, IM_ARRAYSIZE(projections))) {
        sceneRenderer->SetMesh3DCameraProjection((size_t)meshIdx, proj);
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();

      if (proj == 0) { // Perspective
        if (SliderFloat("FOV", &fov, 10.0f, 120.0f, "%.0f deg")) {
          sceneRenderer->SetMesh3DCameraFov((size_t)meshIdx, fov);
        }
        propertyFinished |= IsItemDeactivatedAfterEdit();
      } else { // Orthographic
        if (DragFloat("Ortho Size", &orthoSize, 0.1f, 0.1f, 500.0f, "%.2f")) {
          sceneRenderer->SetMesh3DCameraOrthoSize((size_t)meshIdx, orthoSize);
        }
        propertyFinished |= IsItemDeactivatedAfterEdit();
      }

      if (DragFloat("Near", &nearP, 0.01f, 0.001f, farP - 0.01f, "%.3f")) {
        sceneRenderer->SetMesh3DCameraNear((size_t)meshIdx, nearP);
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();
      if (DragFloat("Far", &farP, 0.5f, nearP + 0.01f, 5000.0f, "%.1f")) {
        sceneRenderer->SetMesh3DCameraFar((size_t)meshIdx, farP);
      }
      propertyFinished |= IsItemDeactivatedAfterEdit();
      if (propertyFinished)
        RecordEditorHistory();

      TextDisabled("Aim with the object's Rotation. See the cyan");
      TextDisabled("frustum gizmo + the Game tab.");
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

  // Sizes a collider to the selected mesh as drawn (unit cube without CPU
  // geometry, e.g. markers).
  const auto fitCollider = [&](ilmeee::ColliderComponent &collider) {
    glm::vec3 lo(-0.5f), hi(0.5f);
    sceneRenderer->GetMesh3DLocalBounds((size_t)meshIdx, lo, hi);
    ilmeee::FitColliderToBounds(collider, lo, hi);
  };

  // Physics Body component (if attached)
  if (meshSelected && sceneRenderer->meshes3d[meshIdx].hasPhysics) {
    auto &mesh = sceneRenderer->meshes3d[meshIdx];
    bool keep = true;
    if (CollapsingHeader("Physics Body", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
      bool record = Checkbox("Use Gravity", &mesh.useGravity);
      record |= Checkbox("Is Kinematic", &mesh.isKinematic);
      DragFloat("Mass", &mesh.mass, 0.05f, 0.001f, 100000.0f, "%.3f kg",
                ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      DragFloat("Drag", &mesh.drag, 0.01f, 0.0f, 1000.0f, "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      DragFloat("Gravity Y", &mesh.gravityY, 0.1f, -1000.0f, 1000.0f, "%.2f",
                ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      if (mesh.movable && !mesh.movementScript.empty())
        TextDisabled("Movement Script drives this object: gravity off.");
      else if (mesh.isKinematic)
        TextDisabled("Kinematic: moved by scripts, pushes bodies.");
      else
        TextDisabled("Dynamic: falls, bounces and is pushed.");
      if (!mesh.collider.enabled) {
        TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                    "No Collider: falls through everything.");
        if (Button("Add Collider")) {
          mesh.collider.enabled = true;
          fitCollider(mesh.collider);
          record = true;
        }
      }
      if (record)
        RecordEditorHistory();
    }
    if (!keep) {
      mesh.hasPhysics = false;
      mesh.velocity = glm::vec3(0.0f);
      RecordEditorHistory();
    }
  }

  // Collider component (if attached)
  if (meshSelected && !isLight && !isCamera &&
      sceneRenderer->meshes3d[meshIdx].collider.enabled) {
    auto &mesh = sceneRenderer->meshes3d[meshIdx];
    auto &c = mesh.collider;
    bool keep = true;
    if (CollapsingHeader("Collider", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
      bool record = false;
      int shape = static_cast<int>(c.shape);
      if (Combo("Shape", &shape, ilmeee::kColliderShapeNames)) {
        c.shape = static_cast<ilmeee::ColliderShape>(shape);
        fitCollider(c); // a new shape starts sized to the mesh
        record = true;
      }
      DragFloat3("Center", &c.center.x, 0.01f);
      record |= IsItemDeactivatedAfterEdit();
      switch (c.shape) {
      case ilmeee::ColliderShape::Box:
        DragFloat3("Size", &c.size.x, 0.01f, 0.0f, 10000.0f, "%.3f",
                   ImGuiSliderFlags_AlwaysClamp);
        record |= IsItemDeactivatedAfterEdit();
        break;
      case ilmeee::ColliderShape::Sphere:
        DragFloat("Radius", &c.radius, 0.01f, 0.0f, 10000.0f, "%.3f",
                  ImGuiSliderFlags_AlwaysClamp);
        record |= IsItemDeactivatedAfterEdit();
        break;
      case ilmeee::ColliderShape::Capsule:
        DragFloat("Radius", &c.radius, 0.01f, 0.0f, 10000.0f, "%.3f",
                  ImGuiSliderFlags_AlwaysClamp);
        record |= IsItemDeactivatedAfterEdit();
        DragFloat("Height", &c.height, 0.01f, 0.0f, 10000.0f, "%.3f",
                  ImGuiSliderFlags_AlwaysClamp);
        record |= IsItemDeactivatedAfterEdit();
        break;
      }
      if (Button("Fit to Mesh")) {
        fitCollider(c);
        record = true;
      }
      record |= Checkbox("Is Trigger", &c.isTrigger);
      if (!c.isTrigger) {
        SliderFloat("Friction", &c.friction, 0.0f, 1.0f, "%.2f");
        record |= IsItemDeactivatedAfterEdit();
        SliderFloat("Bounciness", &c.bounciness, 0.0f, 1.0f, "%.2f");
        record |= IsItemDeactivatedAfterEdit();
      }
      c.Clamp();
      if (c.isTrigger)
        TextDisabled("Trigger: reports overlaps, never blocks.");
      else if (mesh.movable && !mesh.movementScript.empty())
        TextDisabled("Character: stopped by colliders, stands on them.");
      else if (!mesh.hasPhysics)
        TextDisabled("Static: others collide with it, it never moves.");
      TextDisabled("Green wireframe in the Scene view; runs in Play.");
      if (record)
        RecordEditorHistory();
    }
    if (!keep) {
      c.enabled = false;
      RecordEditorHistory();
    }
  }

  // Clip picker shared by Audio Source and Video Player: Browse, drop from
  // the Explorer, Clear. Stores project-relative paths when possible.
  const auto clipField = [&](const char *id, std::string &clip, bool video) {
    bool changed = false;
    PushID(id);
    TextWrapped("Clip: %s", clip.empty() ? "(none - drop a file here)"
                                         : clip.c_str());
    if (BeginDragDropTarget()) {
      if (const ImGuiPayload *pl = AcceptDragDropPayload("ASSET_PATH")) {
        const std::string path((const char *)pl->Data);
        if (video ? ilmeee::IsVideoClipPath(path)
                  : ilmeee::IsAudioClipPath(path)) {
          clip = ProjectMediaPath(projectHandler.projectPath, path);
          changed = true;
        } else {
          PushMessage(std::string("[WARNING] Not a supported ") +
                      (video ? "video" : "audio") + " file: " + path);
        }
      }
      EndDragDropTarget();
    }
    if (Button("Browse...")) {
      NFD_Init();
      nfdchar_t *outPath = nullptr;
      nfdfilteritem_t audioFilter[1] = {
          {"Audio", "wav,ogg,mp3,flac,m4a,aac,opus"}};
      nfdfilteritem_t videoFilter[1] = {{"Video", "mp4,mkv,webm,mov,avi"}};
      if (NFD_OpenDialog(&outPath, video ? videoFilter : audioFilter, 1,
                         nullptr) == NFD_OKAY &&
          outPath) {
        clip = ProjectMediaPath(projectHandler.projectPath, outPath);
        changed = true;
        NFD_FreePath(outPath);
      }
      NFD_Quit();
    }
    SameLine();
    if (Button("Clear") && !clip.empty()) {
      clip.clear();
      changed = true;
    }
    const std::string err = media.ErrorFor(clip);
    if (!clip.empty() && !err.empty())
      TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", err.c_str());
    PopID();
    return changed;
  };

  // Audio Source component (if attached)
  if (meshSelected && sceneRenderer->meshes3d[meshIdx].audio.enabled) {
    auto &mesh = sceneRenderer->meshes3d[meshIdx];
    auto &a = mesh.audio;
    bool keep = true;
    if (CollapsingHeader("Audio Source", mesh.isAudioEmitter ? nullptr : &keep,
                         ImGuiTreeNodeFlags_DefaultOpen)) {
      bool record = clipField("audioClip", a.clip, false);
      if (!SceneStructureLocked() && !a.clip.empty()) {
        SameLine();
        if (media.IsPreviewing()) {
          if (Button("Stop Preview"))
            media.StopPreview();
        } else if (Button("Preview")) {
          std::string err;
          if (!media.Preview(a.clip, &err))
            PushMessage("[WARNING] Cannot play " + a.clip + ": " + err);
        }
      }
      SliderFloat("Volume##audio", &a.volume, 0.0f, 2.0f, "%.2f");
      record |= IsItemDeactivatedAfterEdit();
      record |= Checkbox("Loop##audio", &a.loop);
      SameLine();
      record |= Checkbox("Play On Start##audio", &a.playOnStart);
      record |= Checkbox("3D (spatial)", &a.spatial);
      if (a.spatial) {
        DragFloat("Min Distance", &a.minDistance, 0.05f, 0.0f, a.maxDistance,
                  "%.2f");
        record |= IsItemDeactivatedAfterEdit();
        DragFloat("Max Distance", &a.maxDistance, 0.1f, a.minDistance + 0.01f,
                  1000.0f, "%.1f");
        record |= IsItemDeactivatedAfterEdit();
        TextDisabled("Heard from the game camera: full volume inside Min,");
        TextDisabled("fading to silent at Max, panned left/right.");
      }
      TextDisabled(".wav/.ogg play everywhere; other formats need FFmpeg");
      TextDisabled("(not available in Android builds).");
      if (record)
        RecordEditorHistory();
    }
    if (!keep) {
      a.enabled = false;
      RecordEditorHistory();
    }
  }

  // Video Player component (if attached)
  if (meshSelected && sceneRenderer->meshes3d[meshIdx].video.enabled) {
    auto &mesh = sceneRenderer->meshes3d[meshIdx];
    auto &v = mesh.video;
    bool keep = true;
    if (CollapsingHeader("Video Player", &keep,
                         ImGuiTreeNodeFlags_DefaultOpen)) {
      bool record = clipField("videoClip", v.clip, true);
      if (!ilmeee::MediaSystem::VideoSupported())
        TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                    "This build has no FFmpeg: video cannot play.");
      record |= Checkbox("Loop##video", &v.loop);
      SameLine();
      record |= Checkbox("Play On Start##video", &v.playOnStart);
      record |= Checkbox("Unlit (screen glow)", &v.unlit);
      SliderFloat("Volume##video", &v.volume, 0.0f, 2.0f, "%.2f");
      record |= IsItemDeactivatedAfterEdit();
      const uint32_t surfaces =
          sceneRenderer->GetMesh3DSubmeshCount((size_t)meshIdx);
      std::string current =
          v.surface < 0 ? std::string("All surfaces")
                        : std::to_string(v.surface) + " " +
                              sceneRenderer->GetMesh3DSubmeshName(
                                  (size_t)meshIdx, (uint32_t)v.surface);
      if (BeginCombo("Surface", current.c_str())) {
        if (Selectable("All surfaces", v.surface < 0)) {
          v.surface = -1;
          record = true;
        }
        for (uint32_t s = 0; s < surfaces; ++s) {
          const std::string label =
              std::to_string(s) + " " +
              sceneRenderer->GetMesh3DSubmeshName((size_t)meshIdx, s);
          if (Selectable(label.c_str(), v.surface == (int)s)) {
            v.surface = (int)s;
            record = true;
          }
        }
        EndCombo();
      }
      TextDisabled("Stopped: shows the first frame. Play: plays with sound.");
      if (record)
        RecordEditorHistory();
    }
    if (!keep) {
      v.enabled = false;
      RecordEditorHistory();
    }
  }

  if (meshSelected && sceneRenderer->meshes3d[meshIdx].dialog.enabled)
    RenderDialogInspector(static_cast<size_t>(meshIdx));

  if (meshSelected) RenderNativeScriptInspector(static_cast<size_t>(meshIdx));

  Spacing();
  Separator();
  Spacing();
  PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.65f, 0.90f));
  PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.23f, 0.46f, 0.82f, 0.98f));
  PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.54f, 0.98f, 1.00f));
  if (Button(ICON_UI_PLUS " Add Component", ImVec2(-1, 28))) {
    OpenPopup("AddComponentPopup");
  }
  PopStyleColor(3);
  DEBUG_TRACE_ITEM("Inspector: Add Component button");

  if (BeginPopup("AddComponentPopup")) {
    Text("Components");
    BeginDisabled(!meshSelected || isLight || isCamera || hasMovementScript);
    if (Selectable("Movement Script")) {
      auto &mesh = sceneRenderer->meshes3d[(size_t)meshIdx];
      mesh.movable = true;
      mesh.movementScript = "WASD XZ";
      RecordEditorHistory();
    }
    EndDisabled();
    BeginDisabled(!meshSelected || isLight || isCamera ||
                  sceneRenderer->meshes3d[meshIdx].hasPhysics);
    if (Selectable("Physics Body")) {
      auto &mesh = sceneRenderer->meshes3d[meshIdx];
      mesh.hasPhysics = true;
      // A body without a collider falls through everything; give it one.
      if (!mesh.collider.enabled) {
        mesh.collider.enabled = true;
        fitCollider(mesh.collider);
      }
      RecordEditorHistory();
    }
    EndDisabled();
    BeginDisabled(!meshSelected || isLight || isCamera ||
                  sceneRenderer->meshes3d[meshIdx].collider.enabled);
    if (Selectable("Collider")) {
      auto &collider = sceneRenderer->meshes3d[meshIdx].collider;
      collider.enabled = true;
      fitCollider(collider);
      RecordEditorHistory();
    }
    EndDisabled();
    if (Selectable("Audio Source")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].audio.enabled = true;
        RecordEditorHistory();
      }
    }
    if (Selectable("Video Player")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].video.enabled = true;
        RecordEditorHistory();
      }
    }
    BeginDisabled(meshSelected &&
                  sceneRenderer->meshes3d[meshIdx].dialog.enabled);
    if (Selectable("Dialog")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].dialog.enabled = true;
        RecordEditorHistory();
      }
    }
    EndDisabled();
    if (Selectable("Light")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].isLight = true;
        sceneRenderer->meshes3d[meshIdx].isCamera = false;
        RecordEditorHistory();
      }
    }
    if (Selectable("Camera")) {
      if (meshSelected) {
        sceneRenderer->meshes3d[meshIdx].isCamera = true;
        sceneRenderer->meshes3d[meshIdx].isLight = false;
        RecordEditorHistory();
      }
    }

    Separator();
    if (meshSelected) RenderNativeScriptAddMenu(static_cast<size_t>(meshIdx));
    if (Selectable(nativeScripts.IsCompiling() ? "Compiling Scripts..." : "Compile Project Scripts", false,
                   nativeScripts.IsCompiling() ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None))
      nativeScripts.CompileAll();
    if (Selectable("Create C++ Script..."))
      projectHandler.RequestCreateScript();

    EndPopup();
    // }
  }
  if (SceneStructureLocked()) EndDisabled();
  End();
}

void MainWindow::RenderSceneToolbarView(ImVec2 parentPos, ImVec2 parentSize) {
  (void)parentSize;
  ImVec2 toolbarPos = ImVec2(parentPos.x + 10.0f, parentPos.y + 34.0f);

  SetNextWindowPos(toolbarPos, ImGuiCond_Always);
  SetNextWindowBgAlpha(0.88f);

  ImGuiWindowFlags toolbar_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking;

  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 4));
  PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
  PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
  if (Begin("Scene Toolbar", nullptr, toolbar_flags)) {
    DEBUG_TRACE_PANEL("Scene Toolbar (Compact Unity Overlay Pill)");

    // Transform gizmo tools (keys 1-4)
    static const char *kToolNames[4] = {"Select", "Move", "Rotate", "Scale"};
    static const char *kToolIcons[4] = {
        "assets/icons/svg/ui_select.svg",
        "assets/icons/svg/ui_move.svg",
        "assets/icons/svg/ui_rotate.svg",
        "assets/icons/svg/ui_scale.svg",
    };
    const int cur = (int)sceneRenderer->GetEditMode();
    for (int k = 0; k < 4; ++k) {
      if (k > 0)
        SameLine(0, 2);
      const bool on = (cur == k);
      ImVec4 bg = on ? ImVec4(0.22f, 0.50f, 0.95f, 0.95f)
                     : ImVec4(0.16f, 0.18f, 0.22f, 0.70f);
      ImVec4 tint = on ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                       : ImVec4(0.78f, 0.82f, 0.88f, 1.0f);
      char btnId[24];
      std::snprintf(btnId, sizeof(btnId), "##VpTool%d", k);
      if (svgIcons.DrawIconButton(btnId, kToolIcons[k], 15, bg, tint)) {
        sceneRenderer->SetEditMode((SceneRenderer::EditMode)k);
      }
      if (IsItemHovered())
        SetTooltip("%s (%d)%s", kToolNames[k], k + 1,
                   k == 0 ? " - drag in screen plane" : " - Ctrl to snap");
    }

    SameLine(0, 6);
    TextDisabled("|");
    SameLine(0, 6);

    // Grid visibility toggle
    bool gridVis = sceneRenderer->IsGrid3DVisible();
    if (gridVis) {
      PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.76f, 0.85f));
    }
    if (Button(ICON_UI_GRID "##ToggleGrid")) {
      sceneRenderer->SetGrid3DVisible(!gridVis);
      RecordEditorHistory();
    }
    if (gridVis)
      PopStyleColor();
    if (IsItemHovered())
      SetTooltip("Toggle 3D Grid (%s)", gridVis ? "Visible" : "Hidden");

    // Snap toggle
    SameLine(0, 3);
    bool snapOn = sceneRenderer->IsSnapToGrid();
    if (snapOn) {
      PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.76f, 0.85f));
    }
    if (Button(ICON_UI_SNAP "##ToggleSnap")) {
      sceneRenderer->SetSnapToGrid(!snapOn);
      RecordEditorHistory();
    }
    if (snapOn)
      PopStyleColor();
    if (IsItemHovered())
      SetTooltip("Snap to Grid (%s)", snapOn ? "On" : "Off");

    // View mode selector
    SameLine(0, 4);
    static int viewMode = 1; // Default 3D
    const char *viewModes[] = {"2D", "3D", "Wireframe"};
    SetNextItemWidth(78);
    Combo("##ViewMode", &viewMode, viewModes, IM_ARRAYSIZE(viewModes));

    // Reset camera
    SameLine(0, 4);
    if (Button(ICON_UI_REFRESH "##ResetCam")) {
      ::Log("Resetting camera view", Debug::LogLevel::INFO);
      sceneRenderer->ResetCamera();
      RecordEditorHistory();
    }
    if (IsItemHovered())
      SetTooltip("Reset Scene Camera");

    // Compact Viewport Settings Popover (Zoom, Grid Size, Grid/BG Color)
    SameLine(0, 3);
    if (Button(ICON_UI_INSPECTOR "##SceneOverlayPrefs")) {
      OpenPopup("SceneViewportSettingsPopup");
    }
    if (IsItemHovered())
      SetTooltip("Scene Viewport Settings (Zoom, Grid, Background)");

    if (BeginPopup("SceneViewportSettingsPopup")) {
      TextDisabled("Scene Viewport Settings");
      Separator();

      SetNextItemWidth(130);
      if (SliderFloat("Camera Zoom", &sceneRenderer->zoom, 1.0f, 10.0f,
                      "%.2fx")) {
        sceneRenderer->SetCameraZoom(sceneRenderer->zoom);
      }
      if (IsItemDeactivatedAfterEdit())
        RecordEditorHistory();

      float gridSize = sceneRenderer->GetGridSize();
      SetNextItemWidth(130);
      if (DragFloat("Grid Size", &gridSize, 1.0f, 10.0f, 200.0f, "%.0f")) {
        sceneRenderer->SetGridSize(gridSize);
      }
      if (IsItemDeactivatedAfterEdit())
        RecordEditorHistory();

      const glm::vec4 currentGridColor = sceneRenderer->GetGridColor();
      float gridColor[3] = {currentGridColor.x, currentGridColor.y,
                            currentGridColor.z};
      if (ColorEdit3("Grid Color", gridColor, ImGuiColorEditFlags_NoInputs)) {
        sceneRenderer->SetGridColor(gridColor[0], gridColor[1], gridColor[2],
                                    1.0f);
      }
      if (IsItemDeactivatedAfterEdit())
        RecordEditorHistory();

      const glm::vec4 currentBackgroundColor =
          sceneRenderer->GetBackgroundColor();
      float bgColor[3] = {currentBackgroundColor.x, currentBackgroundColor.y,
                          currentBackgroundColor.z};
      if (ColorEdit3("Sky / Clear Color", bgColor,
                     ImGuiColorEditFlags_NoInputs)) {
        sceneRenderer->SetBackgroundColor(bgColor[0], bgColor[1], bgColor[2],
                                          1.0f);
      }
      if (IsItemDeactivatedAfterEdit())
        RecordEditorHistory();

      EndPopup();
    }
  }
  End();

  PopStyleVar(3);
}

void MainWindow::EnsureSceneLoaded() {
  if (!sceneRenderer) return;
  const std::string activeProject = projectHandler.projectPath;
  const std::string desired = activeProject.empty()
      ? (debug2D ? "<standalone-2d>" : "<standalone-3d>") : activeProject;
  if (loadedSceneProject == desired) return;

  sceneRenderer->ClearMeshes3D();
  projectHandler.currentScene.objects.clear();
  if (activeProject.empty()) {
    projectHandler.currentScenePath.clear();
    projectHandler.currentSceneName.clear();
    if (debug2D) {
      GameObject sprite;
      sprite.name = "TestSprite";
      sprite.spritePath = "assets/testimage.png";
      sprite.width = 300.0f;
      sprite.height = 300.0f;
      projectHandler.currentScene.objects.push_back(sprite);
      sceneRenderer->SetGrid3DVisible(false);
      loadedSceneProject = desired;
    } else {
      bool loaded = sceneRenderer->LoadPMXMesh(
          "assets/3dmodels/wise/wise.pmx");
      if (!loaded)
        loaded = sceneRenderer->LoadObjMesh("assets/3dmodels/belle.obj");
      if (loaded) {
        sceneRenderer->SetMesh3DDebugSource(
            sceneRenderer->GetMesh3DCount() - 1, __FILE__, __LINE__);
        loadedSceneProject = desired;
      }
    }
  } else {
    namespace fs = std::filesystem;
    const fs::path scenesDir = fs::path(activeProject) / "scenes";
    std::error_code ec;
    fs::create_directories(scenesDir, ec);
    const fs::path mainScene = scenesDir / "main.ilmeeescene";
    ilmeee::IlmeeeScene scene;
    if (!ilmeee::LoadScene(mainScene.string(), scene)) {
      scene = ilmeee::DefaultScene();
      ilmeee::SaveScene(mainScene.string(), scene);
      ::Log("Created default scene at " + mainScene.string(),
            Debug::LogLevel::SUCCESS);
    } else {
      ::Log("Loaded scene " + mainScene.string(), Debug::LogLevel::SUCCESS);
    }
    const ilmeee::SceneLoadReport report =
        ilmeee::InstantiateScene(*sceneRenderer, scene, activeProject);
    if (report.failed > 0)
      ::Log(std::to_string(report.failed) +
                " scene object(s) could not be loaded",
            Debug::LogLevel::WARNING);
    projectHandler.currentScenePath = mainScene.string();
    projectHandler.currentSceneName = "main";
    loadedSceneProject = desired;
  }
  if (loadedSceneProject == desired)
    StartEditorSession();
}

void MainWindow::RenderSceneWindow() {
  EnsureSceneLoaded();
  ImGuiID sceneDockId = 0;
  if (showScene) {

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

  if (focusSceneTab) {
    SetNextWindowFocus();
    focusSceneTab = false;
  }
  const bool sceneVisible =
      Begin(ICON_UI_SCENE " Scene###Scene", &showScene, window_flags);
  sceneDockId = GetWindowDockID();
  if (sceneVisible) {
    DEBUG_TRACE_PANEL("Scene viewport panel");
    ImVec2 windowPos = GetWindowPos();
    ImVec2 windowSize = GetWindowSize();
    ImVec2 contentSize = GetContentRegionAvail();

    // Resize the offscreen target to match the panel so the 3D viewport
    // fills the entire window without letterboxing.
    if (contentSize.x > 0 && contentSize.y > 0) {
      sceneRenderer->SetViewportSize((int)contentSize.x, (int)contentSize.y);
    }
    LoadGameUiForProject();
    const bool uiEditing = hasGameUiCanvas && selectedUiElement != -1 &&
                           builder.IsStopped();
    const bool gamePlaying = !builder.IsStopped();
    const bool gameCameraView = uiEditing;

    // Gather one-frame input for the 3D camera. Only feed it when the
    // panel is hovered so editor shortcuts elsewhere keep working.
    SceneRenderer::ViewportInput vpIn;
    vpIn.hovered = !gameCameraView &&
                   IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
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
    if (gameCameraView) {
      // UI editing and Play use the player camera, not the editor orbit camera.
    } else if (sceneIs2D) {
      if (vpIn.hovered && vpIn.rmbDown &&
          (vpIn.mouseDeltaX != 0.0f || vpIn.mouseDeltaY != 0.0f)) {
        sceneRenderer->HandleDrag(vpIn.mouseDeltaX, vpIn.mouseDeltaY);
        viewportCameraHistoryActive = true;
      }
      if (vpIn.hovered && vpIn.scroll != 0.0f) {
        sceneRenderer->HandleZoom(vpIn.scroll);
        if (!viewportCameraHistoryActive)
          RecordEditorHistory();
      }
      if (!vpIn.rmbDown && viewportCameraHistoryActive) {
        viewportCameraHistoryActive = false;
        RecordEditorHistory();
      }
    } else {
      sceneRenderer->UpdateCamera3D(vpIn);
      const bool cameraContinuousInput =
          vpIn.hovered &&
          (vpIn.rmbDown || vpIn.wDown || vpIn.aDown || vpIn.sDown ||
           vpIn.dDown || vpIn.qDown || vpIn.eDown);
      if (cameraContinuousInput)
        viewportCameraHistoryActive = true;
      else if (viewportCameraHistoryActive) {
        viewportCameraHistoryActive = false;
        RecordEditorHistory();
      } else if (vpIn.hovered && vpIn.scroll != 0.0f) {
        RecordEditorHistory();
      }
    }

    // Render scene dengan ukuran penuh
    sceneRenderer->RenderSceneToTexture(projectHandler.currentScene);
    if (gameCameraView && sceneRenderer->HasPlayerCamera())
      sceneRenderer->RenderPlayerCameraPreview();

    // Tampilkan offscreen image di panel. Y dibalik (ImVec2(0,1)→(1,0))
    // karena framebuffer Vulkan top-left origin sedangkan ImGui sample
    // bottom-up; tanpa flip, segitiga terbalik vertikal.
    VkDescriptorSet sceneDesc = gameCameraView && sceneRenderer->HasPlayerCamera()
        ? sceneRenderer->GetPlayerCameraPreviewDescriptor()
        : sceneRenderer->GetViewportDescriptorSet();
    if (sceneDesc != VK_NULL_HANDLE && contentSize.x > 0 && contentSize.y > 0) {
      ImVec2 imageSize = contentSize;
      if (gameCameraView && sceneRenderer->HasPlayerCamera()) {
        const float aspect = (float)sceneRenderer->GetPreviewWidth() /
                             (float)sceneRenderer->GetPreviewHeight();
        imageSize.x = std::min(contentSize.x, contentSize.y * aspect);
        imageSize.y = imageSize.x / aspect;
        SetCursorPosX(GetCursorPosX() + (contentSize.x - imageSize.x) * 0.5f);
        SetCursorPosY(GetCursorPosY() + (contentSize.y - imageSize.y) * 0.5f);
      }
      Image((ImTextureID)sceneDesc, imageSize, ImVec2(0, 1), ImVec2(1, 0));
      if (gameCameraView) {
        const ImVec2 imagePos = GetItemRectMin();
        const bool overImage = IsItemHovered();
        if (uiEditing && BeginDragDropTarget()) {
          if (const ImGuiPayload *payload = AcceptDragDropPayload("ASSET_PATH")) {
            const ImVec2 mouse = GetMousePos();
            int target = -1;
            for (int i = (int)gameUi.elements.size() - 1; i >= 0; --i) {
              const auto rect = ilmeee::GameUIElementRect(
                  gameUi, gameUi.elements[i], imagePos, imageSize);
              if (mouse.x >= rect.min.x && mouse.x <= rect.max.x &&
                  mouse.y >= rect.min.y && mouse.y <= rect.max.y) {
                if (gameUi.elements[i].kind == ilmeee::UiKind::Image ||
                    gameUi.elements[i].kind == ilmeee::UiKind::Button)
                  target = i;
                break;
              }
            }
            if (target >= 0) {
              BindUiImageAsset(target, (const char *)payload->Data);
            } else {
              const float scale = std::min(imageSize.x / gameUi.referenceWidth,
                                           imageSize.y / gameUi.referenceHeight);
              if (scale > 0.0f)
                CreateUiImageAsset((const char *)payload->Data,
                                   (mouse.x - imagePos.x) / scale,
                                   (mouse.y - imagePos.y) / scale);
            }
          }
          EndDragDropTarget();
        }
        const std::filesystem::path projectRoot(projectHandler.projectPath);
        const auto texture = [&](const std::string &asset) -> ImTextureID {
          if (asset.empty()) return ImTextureID{};
          const auto path = projectRoot.empty() ? std::filesystem::path(asset)
              : projectRoot / asset;
          return (ImTextureID)sceneRenderer->GetUiTextureDescriptor(path.string());
        };
        if (hasGameUiCanvas) {
          if (uiEditing && overImage && IsMouseClicked(ImGuiMouseButton_Left)) {
            selectedUiElement = -2;
            const ImVec2 mouse = GetMousePos();
            for (int i = (int)gameUi.elements.size() - 1; i >= 0; --i) {
              const auto rect = ilmeee::GameUIElementRect(
                  gameUi, gameUi.elements[i], imagePos, imageSize);
              if (mouse.x >= rect.min.x && mouse.x <= rect.max.x &&
                  mouse.y >= rect.min.y && mouse.y <= rect.max.y) {
                selectedUiElement = i;
                break;
              }
            }
          }
          const std::string action = ilmeee::DrawGameUI(
              gameUi, imagePos, imageSize, texture, gamePlaying,
              builder.IsPaused(), nullptr, nullptr,
              &gameMovementInput.uiPressed);
          if (gamePlaying && action == "Interact") {
            gameMovementInput.interact = true;
          } else if (gamePlaying && action == "TogglePause") {
            if (builder.IsPaused()) builder.Resume();
            else builder.Pause();
            PushMessage(builder.IsPaused() ? "[INFO] Game paused" :
                                          "[INFO] Game resumed");
          } else if (gamePlaying && !action.empty()) {
            PushMessage("[INFO] UI action: " + action);
          }
          if (uiEditing && selectedUiElement >= 0 &&
              selectedUiElement < (int)gameUi.elements.size()) {
            auto &element = gameUi.elements[selectedUiElement];
            const auto rect = ilmeee::GameUIElementRect(
                gameUi, element, imagePos, imageSize);
            GetWindowDrawList()->AddRect(rect.min, rect.max,
                                         IM_COL32(80, 190, 255, 255), 0, 0, 2.0f);
            if (overImage && IsMouseDragging(ImGuiMouseButton_Left) &&
                !SceneStructureLocked()) {
              const float scale = std::min(imageSize.x / gameUi.referenceWidth,
                                           imageSize.y / gameUi.referenceHeight);
              if (scale > 0.0f) {
                const ImVec2 delta = GetIO().MouseDelta;
                const bool right = element.anchor == ilmeee::UiAnchor::TopRight ||
                                   element.anchor == ilmeee::UiAnchor::BottomRight ||
                                   element.anchor == ilmeee::UiAnchor::CenterRight;
                const bool bottom = element.anchor == ilmeee::UiAnchor::BottomLeft ||
                                    element.anchor == ilmeee::UiAnchor::BottomRight ||
                                    element.anchor == ilmeee::UiAnchor::BottomCenter ||
                                    element.anchor == ilmeee::UiAnchor::StretchBottom;
                element.x += (right ? -delta.x : delta.x) / scale;
                element.y += (bottom ? -delta.y : delta.y) / scale;
              }
            }
          }
        }
        if (!sceneRenderer->HasPlayerCamera())
          GetWindowDrawList()->AddText(
              ImVec2(imagePos.x + 12, imagePos.y + 12),
              IM_COL32(255, 200, 100, 255),
              "Add a Camera to preview the game UI");
      } else {

      // Transform gizmo on the selection + orientation gizmo in the top-right
      // corner. Run first so a press on a handle never reaches the pick /
      // free-drag code below.
      bool gizmoOwnsMouse = false;
      if (sceneRenderer->HasMesh3D()) {
        const ImVec2 vpMin = GetItemRectMin();
        const ImVec2 vpSize = GetItemRectSize();
        const bool vpFree = IsItemHovered() && !vpIn.rmbDown;
        // Tool shortcuts (WASD/QE fly the camera, so Unity's QWER can't be
        // used): 1 Select, 2 Move, 3 Rotate, 4 Scale.
        if (vpIn.hovered && !io.WantTextInput && transformGizmo.active < 0) {
          const ImGuiKey toolKeys[4] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3,
                                        ImGuiKey_4};
          for (int k = 0; k < 4; ++k)
            if (IsKeyPressed(toolKeys[k], false))
              sceneRenderer->SetEditMode((SceneRenderer::EditMode)k);
        }
        const int sel = SelectedMeshIndex();
        if (sel >= 0 && !SceneStructureLocked()) {
          const ilmeee::GizmoResult g = ilmeee::TransformGizmo(
              *sceneRenderer, (size_t)sel, sceneRenderer->GetEditMode(), vpMin,
              vpSize, vpFree, transformGizmo);
          gizmoOwnsMouse |= g.usingMouse;
          if (g.editFinished)
            RecordEditorHistory();
        } else {
          transformGizmo.active = -1;
        }
        const bool flying = vpIn.rmbDown || vpIn.wDown || vpIn.aDown ||
                            vpIn.sDown || vpIn.dDown || vpIn.qDown ||
                            vpIn.eDown || (vpIn.hovered && vpIn.scroll != 0.0f);
        const ilmeee::GizmoResult o = ilmeee::OrientationGizmo(
            *sceneRenderer, sel, vpMin, vpSize,
            vpFree && transformGizmo.active < 0, flying, vpIn.deltaTime,
            orientationGizmo);
        gizmoOwnsMouse |= o.usingMouse;
        if (o.editFinished)
          RecordEditorHistory();
      }

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
      if (!SceneStructureLocked() && BeginDragDropTarget()) {
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
          const bool isVideo = ilmeee::IsVideoClipPath(assetPath);
          const bool isAudio = ilmeee::IsAudioClipPath(assetPath);
          // Ground point under the drop (or a few units ahead of the camera).
          const auto dropPoint = [&]() {
            glm::vec3 world(0.0f);
            if (!sceneRenderer->ScreenToGround(lx, ly, world)) {
              const glm::vec3 f = sceneRenderer->GetCameraForward();
              world = sceneRenderer->camera3d.position + f * 6.0f;
              world.y = 0.0f;
            }
            return world;
          };

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
              selectedUiElement = -1;
              projectHandler.ShowNotification("Model added", fname,
                                              ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
              RecordEditorHistory();
            } else {
              projectHandler.ShowNotification("Load failed", fname,
                                              ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
            }
          } else if (isImage) {
            int hm = -1, hs = -1;
            if (sceneRenderer->PickMesh3DSurface(lx, ly, hm, hs) && hm >= 0) {
              const bool bound = sceneRenderer->BindMesh3DSubmeshTexture(
                  (size_t)hm, (uint32_t)hs, assetPath);
              if (bound) {
                std::snprintf(objectName, sizeof(objectName), "%s",
                              sceneRenderer->GetMesh3DName((size_t)hm).c_str());
                selectedUiElement = -1;
                selectedSurface = hs;
                projectHandler.ShowNotification(
                    "Texture bound", fname + " → surface " + std::to_string(hs),
                    ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                RecordEditorHistory();
              } else {
                projectHandler.ShowNotification("Texture failed", fname,
                                                ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
              }
            } else {
              projectHandler.ShowNotification(
                  "Drop on a surface",
                  "Hover a model surface to bind the image",
                  ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            }
          } else if (isVideo) {
            // On a model surface: that surface becomes the screen. On empty
            // ground: a new Video Screen.
            const std::string clip =
                ProjectMediaPath(projectHandler.projectPath, assetPath);
            int hm = -1, hs = -1;
            const bool onSurface =
                sceneRenderer->PickMesh3DSurface(lx, ly, hm, hs) && hm >= 0 &&
                !sceneRenderer->IsMesh3DLight((size_t)hm) &&
                !sceneRenderer->IsMesh3DCamera((size_t)hm) &&
                !sceneRenderer->meshes3d[(size_t)hm].isAudioEmitter;
            int target = -1;
            if (onSurface) {
              target = hm;
              auto &video = sceneRenderer->meshes3d[(size_t)hm].video;
              const bool isScreen = sceneRenderer->GetMesh3DPath((size_t)hm) ==
                                    "<primitive:screen>";
              video.surface = isScreen ? -1 : hs;
            } else {
              target = SpawnSceneObject(6, -1, dropPoint());
            }
            if (target >= 0) {
              auto &video = sceneRenderer->meshes3d[(size_t)target].video;
              video.enabled = true;
              video.clip = clip;
              std::snprintf(objectName, sizeof(objectName), "%s",
                            sceneRenderer->GetMesh3DName((size_t)target).c_str());
              selectedUiElement = -1;
              RecordEditorHistory();
              projectHandler.ShowNotification(
                  "Video assigned",
                  fname + " → " + sceneRenderer->GetMesh3DName((size_t)target),
                  ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            }
          } else if (isAudio) {
            const int idx = SpawnSceneObject(5, -1, dropPoint());
            if (idx >= 0) {
              sceneRenderer->meshes3d[(size_t)idx].audio.clip =
                  ProjectMediaPath(projectHandler.projectPath, assetPath);
              RecordEditorHistory();
              projectHandler.ShowNotification("Audio Source added", fname,
                                              ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            }
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
      if (!gizmoOwnsMouse && IsItemClicked(ImGuiMouseButton_Left) &&
          sceneRenderer->HasMesh3D()) {
        const ImVec2 pickMin = GetItemRectMin();
        ImVec2 mp = GetMousePos();
        int hitMesh = -1, hitSurface = -1;
        if (sceneRenderer->PickMesh3DSurface(mp.x - pickMin.x, mp.y - pickMin.y,
                                             hitMesh, hitSurface)) {
          const std::string &nm = sceneRenderer->GetMesh3DName((size_t)hitMesh);
          std::snprintf(objectName, sizeof(objectName), "%s", nm.c_str());
          selectedUiElement = -1;
          selectedSurface = hitSurface;
        }
      }
      // LMB drag → move the currently-selected mesh in screen plane. Only
      // in the Select tool; Move/Rotate/Scale go through the gizmo.
      int selIdx = -1;
      for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i) {
        if (sceneRenderer->GetMesh3DName(i) == objectName) {
          selIdx = (int)i;
          break;
        }
      }
      const bool dragging = IsItemHovered() && !gizmoOwnsMouse &&
                            sceneRenderer->GetEditMode() ==
                                SceneRenderer::EditMode::SELECT &&
                            IsMouseDragging(ImGuiMouseButton_Left) &&
                            !IsMouseDown(ImGuiMouseButton_Right) && selIdx >= 0 &&
                            !SceneStructureLocked();
      if (dragging) {
        viewportDragHistoryActive = true;
        ImVec2 md = GetIO().MouseDelta;
        if (md.x != 0.0f || md.y != 0.0f) {
          sceneRenderer->DragMesh3DScreen((size_t)selIdx, md.x, md.y,
                                          (int)contentSize.y);
        }
      } else if (viewportDragHistoryActive) {
        viewportDragHistoryActive = false;
        RecordEditorHistory();
      }

      // Right-click WITHOUT drag opens an "Add" menu and instances a
      // default object on the ground where the cursor was. RMB-drag is
      // camera mouselook, so the menu must not open when the button comes
      // up after a drag: BeginPopupContextItem cannot tell the two apart (it
      // opens on any release over the item), so open it by hand, only when
      // the press started here and the mouse barely moved while held.
      const ImVec2 imgMin = GetItemRectMin();
      static ImVec2 s_rmbAnchor(0.0f, 0.0f);
      static bool s_rmbPressedHere = false;
      if (IsItemHovered() && IsMouseClicked(ImGuiMouseButton_Right)) {
        ImVec2 m = GetMousePos();
        s_rmbAnchor = ImVec2(m.x - imgMin.x, m.y - imgMin.y);
        s_rmbPressedHere = true;
      }
      if (s_rmbPressedHere && IsMouseReleased(ImGuiMouseButton_Right)) {
        s_rmbPressedHere = false;
        const float t = GetIO().MouseDragThreshold;
        if (IsItemHovered() &&
            GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < t * t)
          OpenPopup("SceneAddMenu");
      }
      PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 10.0f));
      PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
      PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 5.0f));
      if (BeginPopup("SceneAddMenu")) {
        const bool locked = SceneStructureLocked();
        if (BeginMenu("Add", !locked)) {
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
            SpawnSceneObject(kind, -1, world);
          };
          for (int k = 0; k < kSpawnObjectCount; ++k)
            if (MenuItem(kSpawnNames[k]))
              spawn(k);
          EndMenu();
        }
        int sel = SelectedMeshIndex();
        if (sel >= 0) {
          Separator();
          bool lastCam = DeletingRemovesLastCamera((size_t)sel);
          std::string del = "Delete '" + std::string(objectName) + "'";
          if (MenuItem(del.c_str(), "Del", false, !locked && !lastCam))
            DeleteSceneObject((size_t)sel);
        }
        EndPopup();
      }
      PopStyleVar(3);

      // Delete key while the viewport has focus removes the selection.
      if (IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
          !GetIO().WantTextInput && IsKeyPressed(ImGuiKey_Delete, false)) {
        int sel = SelectedMeshIndex();
        if (sel >= 0)
          DeleteSceneObject((size_t)sel);
      }
      }
    }

    // Render toolbar di atas viewport
    if (!gameCameraView)
      RenderSceneToolbarView(windowPos, windowSize);
    else {
      SetCursorPos(ImVec2(12, 12));
      TextDisabled(gamePlaying ? "Play Mode  |  Stop to edit" :
                                 "UI Edit Mode  |  Drag elements, edit in Inspector");
    }

    // Sleek semi-transparent bottom viewport status bar
    SetCursorPos(ImVec2(0, windowSize.y - 24));
    PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.11f, 0.72f));
    BeginChild("StatusBar", ImVec2(windowSize.x, 24), false);
    SetCursorPosY(3.0f);
    TextColored(ImVec4(0.76f, 0.80f, 0.86f, 1.0f),
                "  Scene  •  %.1f FPS  •  Zoom %.2fx  •  Objects: %zu",
                GetIO().Framerate, sceneRenderer->GetZoom(),
                sceneRenderer->GetMesh3DCount());
    EndChild();
    PopStyleColor();
  }
  End();

  // Pop semua style yang di-push
  PopStyleVar(1);
  }

  // The Game tab is the player-camera view; Scene remains the editor view.
  if (sceneRenderer) {
    LoadGameUiForProject();
    if (sceneDockId != 0)
      SetNextWindowDockID(sceneDockId, ImGuiCond_FirstUseEver);
    if (focusGameTab) {
      SetNextWindowFocus();
      focusGameTab = false;
    }
    if (Begin(ICON_UI_GAME " Game###Game", nullptr,
              ImGuiWindowFlags_NoScrollbar)) {
      const bool inputFocused = IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
                                && !GetIO().WantTextInput;
      if (builder.IsPlaying() && inputFocused)
        gameMovementInput = ilmeee::ReadKeyboardMouseInput();
      // On-screen sticks mirror WASD/arrows; a held stick overrides below.
      ilmeee::EchoKeyboardOnJoysticks(*sceneRenderer, gameMovementInput);
      if (!sceneRenderer->HasPlayerCamera()) {
        TextDisabled("Add a Camera game object to preview the game.");
      } else {
        sceneRenderer->RenderPlayerCameraPreview();
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
          const ImVec2 imagePos = GetItemRectMin();
          const std::filesystem::path projectRoot(projectHandler.projectPath);
          const auto texture = [&](const std::string &asset) -> ImTextureID {
            if (asset.empty()) return ImTextureID{};
            const auto path = projectRoot.empty() ? std::filesystem::path(asset)
                : projectRoot / asset;
            return (ImTextureID)sceneRenderer->GetUiTextureDescriptor(path.string());
          };
          const bool gamePaused = builder.IsPaused();
          const std::string action = hasGameUiCanvas
              ? ilmeee::DrawGameUI(gameUi, imagePos, ImVec2(w, h), texture,
                                   !builder.IsStopped(), gamePaused,
                                   &gameMovementInput.axes, nullptr,
                                   &gameMovementInput.uiPressed)
              : std::string();
          // A button with action "Interact" works like the E key (phones).
          if (action == "Interact") {
            gameMovementInput.interact = true;
          } else if (action == "TogglePause") {
            if (builder.IsPaused()) builder.Resume();
            else builder.Pause();
            PushMessage(builder.IsPaused() ? "[INFO] Game paused" :
                                             "[INFO] Game resumed");
          }
          if (builder.IsPaused())
            GetWindowDrawList()->AddText(
                ImVec2(imagePos.x + w * 0.5f - 35, imagePos.y + h * 0.5f),
                IM_COL32(255, 255, 255, 255), "PAUSED");
        }
      }
    }
    End();
  }
}

void MainWindow::LoadGameUiForProject() {
  if (gameUiProject == projectHandler.projectPath)
    return;
  gameUiProject = projectHandler.projectPath;
  gameUi = ilmeee::DefaultGameUI();
  hasGameUiCanvas = false;
  selectedUiElement = -1;
  if (!gameUiProject.empty()) {
    const auto root = std::filesystem::path(gameUiProject);
    const auto scenePath = root / "scenes/main.ilmeeescene";
    ilmeee::IlmeeeScene scene;
    if (ilmeee::LoadScene(scenePath.string(), scene) &&
        ilmeee::GameUIFromScene(scene, gameUi)) {
      hasGameUiCanvas = true;
      return;
    }
    // Import the earlier standalone Game UI asset when a scene has no Canvas.
    const auto path = root / "assets/ui/main.json";
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      std::string error;
      if (!ilmeee::LoadGameUI(path.string(), gameUi, &error))
        ::Log("Game UI: " + error, Debug::LogLevel::WARNING);
      else
        hasGameUiCanvas = true;
    }
  }
}

void MainWindow::RenderGameUiEditor() {
  if (selectedUiElement == -2) {
    Text("Canvas");
    TextDisabled("Screen-space root for the game UI");
    DragFloat2("Reference size", &gameUi.referenceWidth, 1.0f, 1.0f, 8192.0f);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    TextWrapped("Design in reference pixels. Elements scale uniformly; anchors "
                "attach to the viewport edges. Use Stretch for panels that "
                "should grow on wider or taller screens.");
  }
  if (selectedUiElement >= 0 &&
      selectedUiElement < (int)gameUi.elements.size()) {
    auto &e = gameUi.elements[selectedUiElement];
    char buffer[512];
    auto editString = [&](const char *label, std::string &value) {
      std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
      if (InputText(label, buffer, sizeof(buffer))) value = buffer;
      if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    };
    const std::string oldId = e.id;
    editString("Name", e.id);
    // Movable objects link joysticks by name; keep those links on rename.
    if (e.id != oldId && sceneRenderer) {
      for (auto &mesh : sceneRenderer->meshes3d) {
        if (mesh.movementRig.moveJoystick == oldId)
          mesh.movementRig.moveJoystick = e.id;
        if (mesh.movementRig.lookJoystick == oldId)
          mesh.movementRig.lookJoystick = e.id;
      }
    }
    int kind = (int)e.kind;
    if (Combo("Type", &kind, "Text\0Image\0Button\0Joystick\0")) {
      e.kind = (ilmeee::UiKind)kind;
      RecordEditorHistory();
    }
    int anchor = (int)e.anchor;
    if (Combo("Anchor", &anchor,
              "Top Left\0Top Right\0Bottom Left\0Bottom Right\0Center\0"
              "Top Center\0Bottom Center\0Center Left\0Center Right\0"
              "Stretch Top\0Stretch Bottom\0Stretch Center\0Stretch Full\0")) {
      ilmeee::SetUiElementAnchor(gameUi, e, (ilmeee::UiAnchor)anchor);
      RecordEditorHistory();
    }
    if (IsItemHovered())
      SetTooltip("Keeps the reference position when changed. Stretch adds the "
                 "screen's extra space to Width (and Height for Stretch Full).");
    if (e.anchor >= ilmeee::UiAnchor::StretchTop)
      TextWrapped("Width/Height are the size at the Canvas reference resolution. "
                  "Extra screen space is added automatically.");
    if (Checkbox("Visible", &e.visible)) RecordEditorHistory();
    if (IsItemHovered())
      SetTooltip("Hidden elements are skipped in Play; scripts and dialogs "
                 "can show them");
    DragFloat("X", &e.x, 1.0f);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    DragFloat("Y", &e.y, 1.0f);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    DragFloat("Width", &e.width, 1.0f, 1.0f, 8192.0f);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    DragFloat("Height", &e.height, 1.0f, 1.0f, 8192.0f);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    if (e.kind == ilmeee::UiKind::Joystick) {
      TextDisabled("Analog stick: link it from a movable object's");
      TextDisabled("Movement Script > Move/Look Joystick");
    } else if (e.kind != ilmeee::UiKind::Image) {
      editString("Text", e.text);
      DragFloat("Font size", &e.fontSize, 1.0f, 1.0f, 200.0f);
      if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    }
    if (e.kind == ilmeee::UiKind::Image || e.kind == ilmeee::UiKind::Button) {
      editString(e.kind == ilmeee::UiKind::Button ? "Sprite" : "Image path",
                 e.imagePath);
      if (!SceneStructureLocked() && BeginDragDropTarget()) {
        if (const ImGuiPayload *payload = AcceptDragDropPayload("ASSET_PATH"))
          BindUiImageAsset(selectedUiElement, (const char *)payload->Data);
        EndDragDropTarget();
      }
      TextDisabled("Path relative to project, e.g. assets/logo.png");
      TextDisabled("Drop an image here or onto the Scene canvas");
      if (e.kind == ilmeee::UiKind::Button) {
        TextDisabled("Empty: plain Background colour. Held: darker.");
        if (!e.imagePath.empty() && SmallButton("Clear sprite"))
          e.imagePath.clear();
      }
    }
    if (e.kind == ilmeee::UiKind::Button) {
      editString("Action", e.action);
      // Built-in actions; anything else is just logged (and scripts can read
      // clicks with UiGameObject::WasClicked).
      if (BeginCombo("##actionPreset", "Built-in actions",
                     ImGuiComboFlags_NoPreview)) {
        if (Selectable("Interact  (talk to the nearest Dialog / E key)"))
          e.action = "Interact";
        if (Selectable("TogglePause  (pause / resume the game)"))
          e.action = "TogglePause";
        EndCombo();
      }
      SameLine();
      TextDisabled("Built-in actions");
    }
    ColorEdit4("Color", &e.color.x);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    ColorEdit4("Background", &e.background.x);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
    Separator();
    RenderNativeScriptBindings(e.scripts);
    if (Button("Add Component", ImVec2(-1, 0))) OpenPopup("UiAddScript");
    if (BeginPopup("UiAddScript")) {
      RenderNativeScriptChoices(e.scripts);
      if (Selectable("Compile Project Scripts", false,
                     nativeScripts.IsCompiling() ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None))
        nativeScripts.CompileAll();
      if (Selectable("Create C++ Script...")) projectHandler.RequestCreateScript();
      if (nativeScripts.Sources().empty()) TextDisabled("Create a C++ script in Explorer first");
      EndPopup();
    }
    if (Button("Delete element") && !SceneStructureLocked()) {
      gameUi.elements.erase(gameUi.elements.begin() + selectedUiElement);
      selectedUiElement = -2;
      RecordEditorHistory();
    }
  }
  Separator();
  TextDisabled("Preview in Game. Save with Ctrl+S.");
  if (Button("Save Scene")) Save3DScene();
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
  Begin(ICON_UI_CONSOLE " Console###Console", &showConsole,
        ImGuiWindowFlags_NoCollapse);
  DEBUG_TRACE_PANEL("Console panel (output/build tabs)");
  ImVec2 pos = GetWindowPos();
  ImVec2 size = GetWindowSize();
  HandleBackground(pos, size);

  BeginTabBar("ConsoleTabs");

  const ImGuiTabItemFlags outputTabFlags =
      focusOutputTab ? ImGuiTabItemFlags_SetSelected : 0;
  focusOutputTab = false;
  if (BeginTabItem(ICON_UI_CONSOLE " Output###Output", nullptr,
                   outputTabFlags)) {
    std::vector<std::string> logLines;
    {
      std::lock_guard<std::mutex> lock(messagesMutex);
      logLines = messages;
    }

    int infoCount = 0, warnCount = 0, errCount = 0;
    auto classifyLine = [](const std::string &l) -> int {
      if (l.find("[ERROR]") != string::npos || l.find("[CRASH]") != string::npos ||
          l.find("error:") != string::npos || l.find("Invalid ") != string::npos)
        return 3;
      if (l.find("[WARNING]") != string::npos || l.find("warning:") != string::npos)
        return 2;
      return 1;
    };
    for (const auto &l : logLines) {
      int cls = classifyLine(l);
      if (cls == 3)
        ++errCount;
      else if (cls == 2)
        ++warnCount;
      else
        ++infoCount;
    }

    static int currentFilter = 0; // 0 All, 1 Info, 2 Warn, 3 Error
    static char consoleSearch[128] = "";

    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));
    BeginChild("ConsoleToolbar", ImVec2(0, 28), false);

    if (Button(ICON_UI_TRASH " Clear")) {
      ClearMessages();
    }

    // Unity-style clickable severity badge counters
    SameLine();
    auto drawSeverityPill = [&](int filterVal, const char *iconGlyph, int cnt,
                                const ImVec4 &activeCol, const char *tip) {
      bool active = (currentFilter == filterVal);
      if (active)
        PushStyleColor(ImGuiCol_Button, activeCol);
      char buf[48];
      std::snprintf(buf, sizeof(buf), "%s %d##sev%d", iconGlyph, cnt, filterVal);
      if (Button(buf)) {
        currentFilter = active ? 0 : filterVal;
      }
      if (active)
        PopStyleColor();
      if (IsItemHovered())
        SetTooltip("%s (click to toggle filter)", tip);
    };

    drawSeverityPill(1, ICON_UI_INFO, infoCount,
                     ImVec4(0.18f, 0.36f, 0.62f, 0.90f), "Info messages");
    SameLine(0, 4);
    drawSeverityPill(2, ICON_UI_WARN, warnCount,
                     ImVec4(0.55f, 0.38f, 0.10f, 0.92f), "Warnings");
    SameLine(0, 4);
    drawSeverityPill(3, ICON_UI_ERROR, errCount,
                     ImVec4(0.58f, 0.16f, 0.18f, 0.92f), "Errors");

    SameLine();
    SetNextItemWidth(-1);
    InputTextWithHint("##consoleSearch", ICON_UI_SEARCH " Search console...",
                      consoleSearch, IM_ARRAYSIZE(consoleSearch));

    EndChild();
    PopStyleVar();

    BeginChild("ConsoleOutput", ImVec2(0, -4), true);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

    std::string queryLower = consoleSearch;
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    int number = 1;
    const bool wasAtBottom = GetScrollY() >= GetScrollMaxY() - 4.0f;
    for (const auto &line : logLines) {
      int cls = classifyLine(line);
      if (currentFilter != 0 && cls != currentFilter)
        continue;

      if (!queryLower.empty()) {
        std::string lineLower = line;
        std::transform(lineLower.begin(), lineLower.end(), lineLower.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (lineLower.find(queryLower) == string::npos)
          continue;
      }

      ImVec4 col = ImVec4(0.84f, 0.87f, 0.91f, 1.0f);
      if (cls == 3)
        col = ImVec4(0.98f, 0.42f, 0.42f, 1.0f);
      else if (cls == 2)
        col = ImVec4(0.98f, 0.78f, 0.32f, 1.0f);
      else if (line.find("compiled") != string::npos ||
               line.find("[SUCCESS]") != string::npos)
        col = ImVec4(0.45f, 0.88f, 0.60f, 1.0f);

      TextColored(col, "[%d] %s", number++, line.c_str());
    }

    if (wasAtBottom)
      SetScrollHereY(1.0f);

    PopStyleVar();
    EndChild();

    EndTabItem();
  }

  const ImGuiTabItemFlags buildTabFlags =
      focusBuildTab ? ImGuiTabItemFlags_SetSelected : 0;
  focusBuildTab = false;
  if (BeginTabItem(ICON_UI_BUILD " Build###Build", nullptr, buildTabFlags)) {
    RenderBuildTab();
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
      if (MenuItem("Open Project...", "Ctrl+O")) {
        projectHandler.isOpenedProject = true;
        projectHandler.OpenFolder();
      }
      if (MenuItem("Load Scene...", nullptr)) {
        RequestEngineSceneLoad();
      }
      Separator();
      if (MenuItem(ICON_UI_SAVE " Save", "Ctrl+S")) {
        Save3DScene();
      }
      if (MenuItem("Save As...", "Ctrl+Shift+S")) {
        SaveAs3DScene();
      }
      Separator();
      if (MenuItem("Exit", "Alt+F4"))
        isRunning = false;
      EndMenu();
    }

    if (BeginMenu("Edit")) {
      if (MenuItem("Undo", "Ctrl+Z")) {
        UndoEditor();
      }
      if (MenuItem("Redo", "Ctrl+Y")) {
        RedoEditor();
      }
      Separator();
      if (MenuItem("Cut", "Ctrl+X")) {
      }
      if (MenuItem("Copy", "Ctrl+C")) {
      }
      if (MenuItem("Paste", "Ctrl+V")) {
      }
      Separator();
      if (MenuItem("Code Editor..."))
        projectHandler.RequestCodeEditorSettings();
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

    RenderBuildMenu();
    if (BeginMenu("Tools")) {
      if (MenuItem("Compile Project Scripts", nullptr, false,
                   !SceneStructureLocked() && !nativeScripts.IsCompiling()))
        nativeScripts.CompileAll();
      Separator();
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

      SliderFloat("Opacity", &volume, 0.1f, 1.0f);
      EndMenu();
    }

    if (BeginMenu("Windows")) {
      MenuItem(ICON_UI_HIERARCHY " Hierarchy", nullptr, &showHierarchy);
      MenuItem(ICON_UI_SCENE " Scene", "Ctrl+1", &showScene);
      MenuItem(ICON_UI_INSPECTOR " Inspector", nullptr, &showInspector);
      MenuItem(ICON_UI_EXPLORER " Explorer", nullptr, &showExplorer);
      MenuItem(ICON_UI_CONSOLE " Console", "Ctrl+2", &showConsole);
      MenuItem(ICON_UI_ANIMATION " Animation", nullptr, &showAnimation);
      MenuItem(ICON_UI_LIGHTING " Lighting", nullptr, &showLighting);
      MenuItem(ICON_UI_DEVICE " Device Monitor", nullptr, &showDeviceMonitor);
      panelManager.RenderWindowMenuItems();
      Separator();
      if (MenuItem("Reset Default Layout")) {
        Ilmeee::EditorDockSpace::ResetLayout();
      }
      EndMenu();
    }

    RenderLightingMenu();

    // Right-side live status telemetry in menu bar
    if (deviceMonitor.Live(GetTime())) {
      SetCursorPosX(GetWindowWidth() - 360);
      const double fps = deviceMonitor.Samples().back().fps;
      TextColored(fps >= 55   ? ImVec4(0.3f, 0.9f, 0.4f, 1.0f)
                  : fps >= 30 ? ImVec4(0.95f, 0.8f, 0.3f, 1.0f)
                              : ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                  ICON_UI_DEVICE " %s: %.1f FPS",
                  deviceMonitor.Target().c_str(), fps);
      if (IsItemClicked())
        showDeviceMonitor = true;
      if (IsItemHovered())
        SetTooltip("Running game telemetry (click to open Device Monitor)");
    }
    SetCursorPosX(GetWindowWidth() - 125);
    TextDisabled("FPS: %.1f", GetIO().Framerate);

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

  float imageAspect = (float)backgroundTexture.Width / backgroundTexture.Height;
  float windowAspect = windowSize.x / windowSize.y;

  float imageWidth, imageHeight;
  if (windowAspect > imageAspect) {
    imageHeight = windowSize.y;
    imageWidth = imageHeight * imageAspect;
  } else {
    imageWidth = windowSize.x;
    imageHeight = imageWidth / imageAspect;
  }

  float imageX = windowPos.x + (windowSize.x - imageWidth) * 0.5f;
  float imageY = windowPos.y + (windowSize.y - imageHeight) * 0.5f;

  float r = assets.r;
  float g = assets.g;
  float b = assets.b;
  ImU32 fillColor = ColorConvertFloat4ToU32(ImVec4(r, g, b, volume));
  drawList->AddRectFilled(
      windowPos, ImVec2(windowPos.x + windowSize.x, windowPos.y + windowSize.y),
      fillColor);

  drawList->AddImage((ImTextureID)(intptr_t)backgroundTexture.TextureID,
                     ImVec2(imageX, imageY),
                     ImVec2(imageX + imageWidth, imageY + imageHeight),
                     ImVec2(0, 0), ImVec2(1, 1),
                     ColorConvertFloat4ToU32(ImVec4(1, 1, 1, volume)));
}

void MainWindow::HandleSearch() {
  static char searchBuffer[128] = "";
  static vector<HandlerProject::AssetFile> searchResults;

  // Keep searchBuffer synced if user cleared filter chip in grid header
  if (projectHandler.currentFilter.empty() && searchBuffer[0] != '\0') {
    searchBuffer[0] = '\0';
    searchResults.clear();
  }

  const bool hasSearchText = searchBuffer[0] != '\0';
  const float filterBtnWidth = 84.0f;
  const float clearBtnWidth = hasSearchText ? 24.0f : 0.0f;
  float searchWidth =
      std::max(100.0f, GetContentRegionAvail().x - filterBtnWidth - clearBtnWidth - 8.0f);

  SetNextItemWidth(searchWidth);
  if (InputTextWithHint("##search", ICON_UI_SEARCH " Search assets...",
                        searchBuffer, IM_ARRAYSIZE(searchBuffer))) {
    projectHandler.currentFilter = searchBuffer;
    searchResults.clear();
    if (searchBuffer[0] != '\0' && searchBuffer[0] != '.') {
      projectHandler.SearchFileOrFolder(explorerTree, searchBuffer,
                                        searchResults);
    }
  }

  if (hasSearchText) {
    SameLine(0, 3.0f);
    if (Button("×##ClearAssetSearch", ImVec2(20, 0))) {
      searchBuffer[0] = '\0';
      projectHandler.currentFilter.clear();
      searchResults.clear();
    }
    if (IsItemHovered())
      SetTooltip("Clear search / filter");
  }

  SameLine(0, 4.0f);
  if (Button(ICON_UI_FILTER " Filter")) {
    OpenPopup("FilterOptions");
  }

  if (BeginPopup("FilterOptions")) {
    auto applyPreset = [&](const char *label, const char *exts) {
      bool active = (projectHandler.currentFilter == exts);
      if (MenuItem(label, nullptr, active)) {
        projectHandler.currentFilter = exts;
        std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", exts);
        searchResults.clear();
      }
    };
    applyPreset("All Assets", "");
    Separator();
    applyPreset("3D Models (.pmx, .fbx, .obj, .gltf, .glb)",
                ".pmx .pmd .fbx .obj .gltf .glb .dae .stl");
    applyPreset("Blender Files (.blend, .blend1)", ".blend .blend1 .blend2");
    applyPreset("C++ & Scripts (.cpp, .hpp, .h, .c, .lua, .py)",
                ".cpp .cc .cxx .c .hpp .h .lua .py .sh");
    applyPreset("Shaders (.glsl, .vert, .frag, .spv)",
                ".glsl .vert .frag .comp .spv .shader .hlsl");
    applyPreset("Textures & Images (.png, .jpg, .webp, .tga)",
                ".png .jpg .jpeg .webp .bmp .tga .hdr .psd .svg");
    applyPreset("Audio (.wav, .mp3, .ogg, .flac)",
                ".wav .mp3 .ogg .flac .m4a .opus");
    applyPreset("Video (.mp4, .webm, .mkv, .avi)",
                ".mp4 .webm .mkv .avi .mov");
    applyPreset("Animations & Controllers (.vmd, .ilmeeeanim)",
                ".vmd .ilmeeeanim .anim .bvh .ilmeeectrl");
    applyPreset("Scenes & Prefabs (.ilmeeescene, .prefab)",
                ".ilmeeescene .scene .unity .prefab");
    applyPreset("Archives & Packages (.zip, .7z, .tar, .apk)",
                ".zip .7z .rar .tar .gz .tgz .xz .apk .aab");
    applyPreset("Configs & Docs (.json, .yaml, .txt, .md)",
                ".json .toml .yaml .yml .ini .xml .txt .md");
    EndPopup();
  }

  // Compact jump-to-asset results bar when searching across folders by name
  if (!searchResults.empty()) {
    BeginChild("##GlobalSearchMatches", ImVec2(0, 96), true);
    TextDisabled("Project-wide matches (%zu) — click to locate:",
                 searchResults.size());
    for (const auto &result : searchResults) {
      AssetIconInfo meta = SvgIconManager::ResolveAssetIcon(
          result.fullPath, result.isDirectory, false);
      svgIcons.DrawIcon(meta.svgPath == result.fullPath
                            ? "assets/icons/svg/ext_image.svg"
                            : meta.svgPath,
                        14);
      std::string rowLabel = result.name + "##sr_" + result.fullPath;
      if (Selectable(rowLabel.c_str())) {
        if (result.isDirectory) {
          projectHandler.currentDirectory = result.fullPath;
          projectHandler.fileExplorerSelectedPaths.clear();
        } else {
          projectHandler.currentDirectory =
              std::filesystem::path(result.fullPath).parent_path().string();
          projectHandler.fileExplorerSelectedPaths.clear();
          projectHandler.fileExplorerSelectedPaths.insert(result.fullPath);
        }
      }
      if (IsItemHovered()) {
        SetTooltip("%s", result.fullPath.c_str());
      }
    }
    EndChild();
  }
}

void MainWindow::RenderPlayMenu() {
  ImVec2 viewportSize = GetMainViewport()->Size;
  const float toolbarHeight = 32.0f;
  const float menuBarHeight = GetFrameHeight();

  SetNextWindowPos(ImVec2(0, menuBarHeight));
  SetNextWindowSize(ImVec2(viewportSize.x, toolbarHeight));

  ImGuiWindowFlags toolbar_flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoNavFocus;

  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
  PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
  PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.105f, 0.114f, 0.133f, 1.0f));

  if (Begin("PlayControlsToolbar", nullptr, toolbar_flags)) {
    const int iconSize = 15;
    const float spacing = 3.0f;

    // ---- LEFT CLUSTER: Unity Transform Tools + Save + Grid/Snap ----
    if (sceneRenderer) {
      static const char *kToolNames[4] = {"Select", "Move", "Rotate", "Scale"};
      static const char *kToolIcons[4] = {
          "assets/icons/svg/ui_select.svg",
          "assets/icons/svg/ui_move.svg",
          "assets/icons/svg/ui_rotate.svg",
          "assets/icons/svg/ui_scale.svg",
      };
      const int curMode = (int)sceneRenderer->GetEditMode();
      for (int k = 0; k < 4; ++k) {
        if (k > 0)
          SameLine(0, 2.0f);
        bool active = (curMode == k);
        ImVec4 bg = active ? ImVec4(0.22f, 0.50f, 0.95f, 0.95f)
                           : ImVec4(0.17f, 0.19f, 0.22f, 1.0f);
        ImVec4 tint = active ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                             : ImVec4(0.78f, 0.82f, 0.88f, 1.0f);
        char tid[24];
        std::snprintf(tid, sizeof(tid), "##MainTool%d", k);
        if (svgIcons.DrawIconButton(tid, kToolIcons[k], iconSize, bg, tint)) {
          sceneRenderer->SetEditMode((SceneRenderer::EditMode)k);
        }
        if (IsItemHovered())
          SetTooltip("%s Tool (%d)", kToolNames[k], k + 1);
      }

      SameLine(0, 8.0f);
      if (svgIcons.DrawIconButton("##MainSave", "assets/icons/svg/ui_save.svg",
                                  iconSize, ImVec4(0.17f, 0.19f, 0.22f, 1.0f),
                                  ImVec4(0.82f, 0.86f, 0.92f, 1.0f))) {
        Save3DScene();
      }
      if (IsItemHovered())
        SetTooltip("Save Scene (Ctrl+S)");

      SameLine(0, 4.0f);
      bool gVis = sceneRenderer->IsGrid3DVisible();
      if (svgIcons.DrawIconButton(
              "##MainGrid", "assets/icons/svg/ui_grid.svg", iconSize,
              gVis ? ImVec4(0.18f, 0.38f, 0.68f, 0.90f)
                   : ImVec4(0.17f, 0.19f, 0.22f, 1.0f),
              ImVec4(0.90f, 0.92f, 0.96f, 1.0f))) {
        sceneRenderer->SetGrid3DVisible(!gVis);
        RecordEditorHistory();
      }
      if (IsItemHovered())
        SetTooltip("Toggle 3D Grid");

      SameLine(0, 3.0f);
      bool sOn = sceneRenderer->IsSnapToGrid();
      if (svgIcons.DrawIconButton(
              "##MainSnap", "assets/icons/svg/ui_snap.svg", iconSize,
              sOn ? ImVec4(0.18f, 0.38f, 0.68f, 0.90f)
                  : ImVec4(0.17f, 0.19f, 0.22f, 1.0f),
              ImVec4(0.90f, 0.92f, 0.96f, 1.0f))) {
        sceneRenderer->SetSnapToGrid(!sOn);
        RecordEditorHistory();
      }
      if (IsItemHovered())
        SetTooltip("Snap to Grid");
    }

    // ---- CENTER CLUSTER: Play / Pause / Stop Transport ----
    const float transportWidth = (iconSize + 14.0f) * 3.0f + spacing * 2.0f;
    const float centerX = (viewportSize.x - transportWidth) * 0.5f;
    if (centerX > GetCursorPosX() + 12.0f) {
      SameLine(centerX);
    } else {
      SameLine(0, 12.0f);
    }

    bool isPlaying = builder.IsPlaying();
    bool isPaused = builder.IsPaused();

    ImVec4 playBg = isPlaying ? ImVec4(0.16f, 0.46f, 0.24f, 1.0f)
                              : ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
    ImVec4 playTint = isPlaying ? ImVec4(0.45f, 1.0f, 0.58f, 1.0f)
                                : ImVec4(0.82f, 0.92f, 0.84f, 1.0f);

    BeginDisabled(builder.IsStopped() && nativeScripts.IsCompiling());
    if (svgIcons.DrawIconButton("##Play", "assets/icons/svg/play.svg", iconSize,
                                playBg, playTint)) {
      if (builder.IsStopped()) {
        builder.Play();
        focusGameTab = true;
        showConsole = true;
        focusOutputTab = true;
        PushMessage("[INFO] Game started in Game tab");
      } else if (isPaused) {
        builder.Resume();
        focusGameTab = true;
        PushMessage("[INFO] Game resumed");
      }
    }
    EndDisabled();
    if (IsItemHovered())
      SetTooltip(nativeScripts.IsCompiling() ? "Waiting for script compilation"
                                             : "Play (Ctrl+P)");

    SameLine(0, spacing);
    ImVec4 pauseBg = isPaused ? ImVec4(0.50f, 0.38f, 0.12f, 1.0f)
                              : ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
    ImVec4 pauseTint = isPaused ? ImVec4(1.0f, 0.88f, 0.32f, 1.0f)
                                : ImVec4(0.86f, 0.88f, 0.90f, 1.0f);

    if (svgIcons.DrawIconButton("##Pause", "assets/icons/svg/pause.svg",
                                iconSize, pauseBg, pauseTint)) {
      if (isPlaying) {
        builder.Pause();
        PushMessage("[INFO] Game paused");
      }
    }
    if (IsItemHovered())
      SetTooltip("Pause (Ctrl+Shift+P)");

    SameLine(0, spacing);
    bool isStopped = builder.IsStopped();
    ImVec4 stopBg = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
    ImVec4 stopTint = isStopped ? ImVec4(0.48f, 0.52f, 0.58f, 1.0f)
                                : ImVec4(0.98f, 0.42f, 0.42f, 1.0f);

    if (svgIcons.DrawIconButton("##Stop", "assets/icons/svg/stop.svg", iconSize,
                                stopBg, stopTint)) {
      if (!builder.IsStopped())
        PushMessage("[INFO] Game stopped");
      nativeScripts.Stop();
      builder.Stop();
      focusSceneTab = true;
    }
    if (IsItemHovered())
      SetTooltip("Stop (Ctrl+Q)");

    // ---- RIGHT CLUSTER: Build Target + Build & Run + Lighting ----
    const float rightClusterW = 240.0f;
    const float rightX = viewportSize.x - rightClusterW;
    if (rightX > GetCursorPosX() + 12.0f) {
      SameLine(rightX);
    } else {
      SameLine(0, 12.0f);
    }

    const bool isAndroidTarget =
        (gameBuildTarget == ilmeee::BuildTarget::Android);
    if (Button(isAndroidTarget ? ICON_UI_DEVICE " Android"
                               : ICON_UI_GAME " Linux")) {
      gameBuildTarget = isAndroidTarget ? ilmeee::BuildTarget::Linux
                                        : ilmeee::BuildTarget::Android;
    }
    if (IsItemHovered())
      SetTooltip("Active Build Target (click to switch Linux / Android)");

    SameLine(0, 4.0f);
    const bool building = gameBuildRunning.load();
    BeginDisabled(building || projectHandler.projectPath.empty());
    PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.38f, 0.68f, 0.92f));
    PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.48f, 0.84f, 0.98f));
    if (Button(building ? ICON_UI_BUILD " Building..."
                        : ICON_UI_BUILD " Build & Run")) {
      StartGameBuild(true);
    }
    PopStyleColor(2);
    EndDisabled();
    if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      SetTooltip(projectHandler.projectPath.empty()
                     ? "Open a project first to Build & Run"
                     : "Save scene, build target binary/APK, and launch");

    SameLine(0, 4.0f);
    if (Button(ICON_UI_LIGHTING "##ToolbarLighting")) {
      showLighting = !showLighting;
    }
    if (IsItemHovered())
      SetTooltip("Toggle Lighting Panel");

    // Keyboard shortcuts
    if (GetIO().KeyCtrl && !GetIO().KeyShift &&
        IsKeyPressed(ImGuiKey_P, false)) {
      if (builder.IsStopped() && !nativeScripts.IsCompiling()) {
        builder.Play();
        focusGameTab = true;
        showConsole = true;
        focusOutputTab = true;
        PushMessage("[INFO] Game started in Game tab");
      } else if (isPaused) {
        builder.Resume();
        focusGameTab = true;
        PushMessage("[INFO] Game resumed");
      }
    }
    if (GetIO().KeyCtrl && GetIO().KeyShift &&
        IsKeyPressed(ImGuiKey_P, false)) {
      if (isPlaying) {
        builder.Pause();
        PushMessage("[INFO] Game paused");
      }
    }
    if (GetIO().KeyCtrl && IsKeyPressed(ImGuiKey_Q, false) &&
        !builder.IsStopped()) {
      nativeScripts.Stop();
      builder.Stop();
      focusSceneTab = true;
      PushMessage("[INFO] Game stopped");
    }
  }
  End();
  PopStyleColor(1);
  PopStyleVar(4);
}

void MainWindow::RequestEngineSceneLoad() {
  using namespace ilmeee::net;
  ConnectionId engine =
      ipcBus ? ipcBus->PeerByRole(proto::kRoleEngine) : kInvalidConnection;
  if (engine == kInvalidConnection) {
    PushMessage("[IPC] Load Scene: engine is not connected");
    return;
  }
  // The engine shows a file dialog, so the answer can take as long as the
  // user does.
  ipcBus->Request(
      engine, proto::kSceneLoad, nlohmann::json::object(),
      [this](const Response &r) {
        if (r.ok)
          PushMessage("[IPC] Engine loaded scene '" +
                      r.payload.value("sceneName", std::string()) + "' (" +
                      std::to_string(r.payload.value("objects", 0)) +
                      " objects) from " +
                      r.payload.value("path", std::string()));
        else if (r.error != "cancelled")
          PushMessage("[IPC] Load Scene failed: " + r.error);
      },
      std::chrono::minutes(30));
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

bool MainWindow::Save3DSceneToPath(const std::string &targetPath) {
  if (!sceneRenderer)
    return false;
  if (SceneStructureLocked()) {
    PushMessage("[WARNING] Stop the game before saving the scene");
    return false;
  }
  if (targetPath.empty())
    return false;

  LoadGameUiForProject();
  const std::string activeProject = projectHandler.projectPath;
  namespace fs = std::filesystem;

  ilmeee::IlmeeeScene scene;
  scene.backgroundColor = sceneRenderer->GetBackgroundColor();
  scene.npr = sceneRenderer->GetNpr();
  for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i) {
    ilmeee::SceneEntity e;
    e.name = sceneRenderer->GetMesh3DName(i);
    // Entities are written in mesh order, so the mesh index is the entity
    // index and the parent link carries over unchanged.
    e.parent = sceneRenderer->GetMesh3DParent(i);
    e.position = sceneRenderer->GetMesh3DPosition(i);
    e.rotationEuler = sceneRenderer->GetMesh3DRotation(i);
    e.scale = sceneRenderer->GetMesh3DScale(i);
    const auto &mesh = sceneRenderer->meshes3d[i];
    e.objectId = mesh.objectId;
    e.scripts = mesh.scripts;
    e.movable = mesh.movable;
    e.movementScript = mesh.movementScript;
    e.movementSpeed = mesh.movementSpeed;
    e.movementRig = mesh.movementRig;
    e.audio = mesh.audio;
    e.video = mesh.video;
    e.dialog = mesh.dialog;
    e.animation = mesh.animation;
    e.springBones = mesh.springBones;
    e.rigidBody.enabled = mesh.hasPhysics;
    e.rigidBody.useGravity = mesh.useGravity;
    e.rigidBody.isKinematic = mesh.isKinematic;
    e.rigidBody.mass = mesh.mass;
    e.rigidBody.drag = mesh.drag;
    e.rigidBody.gravityY = mesh.gravityY;
    e.collider = mesh.collider;
    e.worldText = mesh.worldText;

    if (mesh.isWorldText) {
      e.kind = ilmeee::PrimitiveKind::WorldText;
    } else if (mesh.isAudioEmitter) {
      e.kind = ilmeee::PrimitiveKind::AudioSource;
    } else if (sceneRenderer->IsMesh3DLight(i)) {
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
        else if (path.find("screen") != std::string::npos)
          e.kind = ilmeee::PrimitiveKind::Screen;
        else
          e.kind = ilmeee::PrimitiveKind::Cube;
      } else if (path.empty()) {
        e.kind = ilmeee::PrimitiveKind::Cube; // unknown mesh → safe default
      } else {
        fs::path absPath = path;
        if (!activeProject.empty()) {
          std::error_code ec;
          fs::path rel = fs::relative(absPath, activeProject, ec);
          e.externalPath = ec ? path : rel.string();
        } else {
          e.externalPath = path;
        }
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
        if (!activeProject.empty()) {
          std::error_code ec2;
          fs::path rel = fs::relative(fs::path(tex), activeProject, ec2);
          std::string relStr = ec2 ? std::string() : rel.string();
          st.texturePath =
              (!relStr.empty() && relStr.rfind("..", 0) != 0) ? relStr : tex;
        } else {
          st.texturePath = tex;
        }
        e.surfaceTextures.push_back(std::move(st));
      }
    }
    scene.entities.push_back(std::move(e));
  }

  if (hasGameUiCanvas)
    ilmeee::AppendGameUIToScene(scene, gameUi);

  const fs::path dstPath(targetPath);
  if (dstPath.has_parent_path()) {
    std::error_code ecDir;
    fs::create_directories(dstPath.parent_path(), ecDir);
  }
  const std::string filename = dstPath.filename().string();

  if (ilmeee::SaveScene(targetPath, scene)) {
    ::Log("Saved 3D scene to " + targetPath, Debug::LogLevel::SUCCESS);
    projectHandler.ShowNotification("Scene Saved",
                                    "Successfully saved " + filename,
                                    ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    return true;
  } else {
    ::Log("Failed to save 3D scene to " + targetPath,
          Debug::LogLevel::ERROR);
    projectHandler.ShowNotification("Save Error",
                                    "Failed to save " + filename,
                                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    return false;
  }
}

bool MainWindow::Save3DScene() {
  if (!sceneRenderer)
    return false;
  if (SceneStructureLocked()) {
    PushMessage("[WARNING] Stop the game before saving the scene");
    return false;
  }
  if (!projectHandler.currentScenePath.empty()) {
    return Save3DSceneToPath(projectHandler.currentScenePath);
  }
  const std::string activeProject = projectHandler.projectPath;
  if (activeProject.empty()) {
    ::Log("Cannot save scene: No active project opened.",
          Debug::LogLevel::WARNING);
    projectHandler.ShowNotification("Save Warning",
                                    "No active project is opened",
                                    ImVec4(1.0f, 0.6f, 0.0f, 1.0f));
    return false;
  }
  namespace fs = std::filesystem;
  const fs::path mainScene =
      fs::path(activeProject) / "scenes" / "main.ilmeeescene";
  if (Save3DSceneToPath(mainScene.string())) {
    projectHandler.currentScenePath = mainScene.string();
    projectHandler.currentSceneName = "main";
    return true;
  }
  return false;
}

bool MainWindow::SaveAs3DScene() {
  if (!sceneRenderer)
    return false;
  if (SceneStructureLocked()) {
    PushMessage("[WARNING] Stop the game before saving the scene");
    return false;
  }

  namespace fs = std::filesystem;
  std::string defaultDir;
  if (!projectHandler.currentScenePath.empty()) {
    defaultDir = fs::path(projectHandler.currentScenePath).parent_path().string();
  } else if (!projectHandler.projectPath.empty()) {
    const fs::path scenesDir = fs::path(projectHandler.projectPath) / "scenes";
    std::error_code ec;
    fs::create_directories(scenesDir, ec);
    defaultDir = scenesDir.string();
  }

  const std::string baseName = projectHandler.currentSceneName.empty()
                                   ? "main"
                                   : projectHandler.currentSceneName;
  const std::string defaultFile = baseName + ".ilmeeescene";

  NFD::Guard nfdGuard;
  NFD::UniquePath savePath;
  nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeeescene"}};
  const nfdresult_t result = NFD::SaveDialog(
      savePath, filterItem, 1,
      defaultDir.empty() ? nullptr : defaultDir.c_str(), defaultFile.c_str());

  if (result == NFD_OKAY && savePath.get() != nullptr) {
    fs::path chosen(savePath.get());
    if (chosen.extension() != ".ilmeeescene")
      chosen.replace_extension(".ilmeeescene");

    if (!Save3DSceneToPath(chosen.string()))
      return false;

    projectHandler.currentScenePath = chosen.string();
    projectHandler.currentSceneName = chosen.stem().string();
    if (!projectHandler.projectPath.empty()) {
      projectHandler.rootAsset =
          projectHandler.BuildAssetTree(projectHandler.projectPath);
    }
    return true;
  }
  if (result == NFD_ERROR) {
    const char *err = NFD::GetError();
    const std::string errMsg = err ? err : "Failed to open Save As dialog";
    ::Log("Save As dialog failed: " + errMsg, Debug::LogLevel::ERROR);
    projectHandler.ShowNotification("Save Error", errMsg,
                                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
  return false;
}
