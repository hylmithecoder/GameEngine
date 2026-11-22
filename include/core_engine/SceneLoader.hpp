#pragma once
// Turns a loaded .ilmeeescene into live objects in a SceneRenderer. The
// editor and the runtime player (IlmeeePlayer) both go through here, so a
// built game shows the scene exactly the way the editor does.

#include "IlmeeeScene.hpp"
#include "Prefab.hpp"
#include <cstddef>
#include <filesystem>
#include <glm/glm.hpp>
#include <vector>

class SceneRenderer;

namespace ilmeee {

struct SceneLoadReport {
  size_t loaded = 0;
  size_t failed = 0; // entities whose asset could not be loaded (skipped)
};

// Appends every entity to `renderer` (meshes already there are kept), with
// relative asset paths resolved against `projectRoot`; then applies surface
// textures, the parent hierarchy and the scene's background colour.
SceneLoadReport InstantiateScene(SceneRenderer &renderer,
                                 const IlmeeeScene &scene,
                                 const std::filesystem::path &projectRoot);

// Loads one world entity and applies its components; appended to
// renderer.meshes3d. Parent and rig camera are left unlinked (-1), since
// they are entity indices. Returns the new mesh index, -1 on failure (UI
// entities always fail: GameUI owns them).
int LoadSceneEntity(SceneRenderer &renderer, const SceneEntity &entity,
                    const std::filesystem::path &projectRoot);

// The reverse: mesh `index` as a scene entity (parent / rig camera stay
// mesh indices; asset paths become project-relative when inside it).
SceneEntity CaptureSceneEntity(const SceneRenderer &renderer, size_t index,
                               const std::filesystem::path &projectRoot);
// Every mesh, in order (entity index == mesh index), plus background + NPR.
// UI is not included (see AppendGameUIToScene).
IlmeeeScene CaptureScene(const SceneRenderer &renderer,
                         const std::filesystem::path &projectRoot);
// Subtree of mesh `root` as a prefab (see Prefab.hpp); empty if invalid.
IlmeeeScene CapturePrefab(const SceneRenderer &renderer, size_t root,
                          const std::filesystem::path &projectRoot);

struct PrefabSpawnOptions {
  int parent = -1;              // mesh index the root is attached to
  bool overridePosition = false; // else the prefab root's own position
  glm::vec3 position{0.0f};     // local to `parent` (world for a root)
  // New objectIds + remapped internal references. False only to restore
  // objects that existed before (editor Stop after a Destroy in Play).
  bool freshIds = true;
};
struct PrefabSpawnResult {
  std::vector<size_t> meshes; // spawned mesh indices, root first
  size_t failed = 0;          // entities that could not be loaded
  bool ok() const { return !meshes.empty(); }
};
// Appends a copy of `prefab` to the scene. Children whose parent failed to
// load attach to the nearest loaded ancestor. Fails (spawns nothing) when
// the root cannot be loaded.
PrefabSpawnResult InstantiatePrefab(SceneRenderer &renderer,
                                    IlmeeeScene prefab,
                                    const std::filesystem::path &projectRoot,
                                    const PrefabSpawnOptions &options = {});

} // namespace ilmeee
