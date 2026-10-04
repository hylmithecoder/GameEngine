#pragma once
// Turns a loaded .ilmeeescene into live objects in a SceneRenderer. The
// editor and the runtime player (IlmeeePlayer) both go through here, so a
// built game shows the scene exactly the way the editor does.

#include "IlmeeeScene.hpp"
#include <cstddef>
#include <filesystem>

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

} // namespace ilmeee
