// Editor side of prefabs (.ilmeeeprefab): create one from a Hierarchy
// object, spawn copies from the Explorer. Format + runtime: Prefab.hpp,
// SceneLoader.hpp, NativeScriptScene.cpp.
#include "../../../include/core_engine/Prefab.hpp"
#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include <cstdio>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
// Characters that are awkward in file names on any desktop OS.
std::string SafeFileStem(std::string name) {
  for (char &c : name)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|' ||
        static_cast<unsigned char>(c) < 32)
      c = '_';
  while (!name.empty() && (name.back() == ' ' || name.back() == '.'))
    name.pop_back();
  return name.empty() ? std::string("Prefab") : name;
}
} // namespace

bool MainWindow::CreatePrefabFromObject(size_t index,
                                        const std::string &folder) {
  if (!sceneRenderer || index >= sceneRenderer->GetMesh3DCount())
    return false;
  if (SceneStructureLocked()) {
    PushMessage("[WARNING] Stop the game before creating a prefab");
    return false;
  }
  const std::string project = projectHandler.projectPath;
  if (project.empty()) {
    PushMessage("[WARNING] Open a project before creating a prefab");
    return false;
  }
  ilmeee::IlmeeeScene prefab =
      ilmeee::CapturePrefab(*sceneRenderer, index, project);
  if (!ilmeee::ValidPrefab(prefab)) {
    PushMessage("[ERROR] Could not capture '" +
                sceneRenderer->GetMesh3DName(index) + "' as a prefab");
    return false;
  }
  fs::path dir = folder.empty() ? fs::path(project) / "assets/prefabs"
                                : fs::path(folder);
  if (dir.is_relative())
    dir = fs::path(project) / dir;
  std::error_code ec;
  fs::create_directories(dir, ec);
  const std::string stem = SafeFileStem(prefab.entities[0].name);
  fs::path target = dir / (stem + ilmeee::kPrefabExtension);
  for (int n = 1; fs::exists(target, ec); ++n)
    target = dir / (stem + " " + std::to_string(n) + ilmeee::kPrefabExtension);
  if (!ilmeee::SavePrefab(target.string(), prefab)) {
    PushMessage("[ERROR] Could not write " + target.string());
    return false;
  }
  const std::string shown =
      fs::relative(target, project, ec).generic_string();
  PushMessage("[SUCCESS] Prefab created: " + (ec ? target.string() : shown) +
              " (" + std::to_string(prefab.entities.size()) + " object(s))");
  // Object references outside the subtree are kept by id: valid again only
  // in a scene that owns that object.
  for (const auto &ref : ilmeee::ExternalReferences(prefab))
    PushMessage("[WARNING] Prefab field " + ref +
                " points outside the prefab; it only resolves in this scene.");
  projectHandler.ShowNotification("Prefab created", target.filename().string(),
                                  ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
  projectHandler.isOpenedProject = true; // refresh the Explorer
  return true;
}

int MainWindow::SpawnPrefabAsset(const std::string &path, int parent,
                                 const glm::vec3 *position) {
  if (!sceneRenderer || SceneStructureLocked())
    return -1;
  const std::string project = projectHandler.projectPath;
  fs::path file(path);
  if (file.is_relative())
    file = fs::path(project) / file;
  ilmeee::IlmeeeScene prefab;
  if (!ilmeee::LoadPrefab(file.string(), prefab)) {
    projectHandler.ShowNotification("Prefab failed",
                                    file.filename().string() +
                                        " is missing or damaged",
                                    ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
    return -1;
  }
  // The editor selects by name: keep every spawned name unique.
  std::unordered_set<std::string> taken;
  for (size_t i = 0; i < sceneRenderer->GetMesh3DCount(); ++i)
    taken.insert(sceneRenderer->GetMesh3DName(i));
  for (auto &e : prefab.entities) {
    const std::string base = e.name.empty() ? std::string("Object") : e.name;
    std::string name = base;
    for (int n = 1; taken.contains(name); ++n)
      name = base + " " + std::to_string(n);
    taken.insert(name);
    e.name = name;
  }
  ilmeee::PrefabSpawnOptions options;
  options.parent = parent;
  if (position) {
    options.overridePosition = true;
    options.position = *position;
  }
  sceneRenderer->SetAnimationProjectRoot(project);
  const auto result =
      ilmeee::InstantiatePrefab(*sceneRenderer, prefab, project, options);
  if (!result.ok()) {
    projectHandler.ShowNotification("Prefab failed",
                                    "The root object of " +
                                        file.filename().string() +
                                        " could not be loaded",
                                    ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
    return -1;
  }
  if (result.failed)
    PushMessage("[WARNING] " + std::to_string(result.failed) +
                " object(s) of " + file.filename().string() +
                " could not be loaded");
  for (const size_t mesh : result.meshes)
    sceneRenderer->SetMesh3DDebugSource(mesh, __FILE__, __LINE__);
  const size_t root = result.meshes[0];
  std::snprintf(objectName, sizeof(objectName), "%s",
                sceneRenderer->GetMesh3DName(root).c_str());
  selectedUiElement = -1;
  selectedSurface = -1;
  RecordEditorHistory();
  return (int)root;
}
