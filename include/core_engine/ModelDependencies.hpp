#pragma once
// Files a model needs next to it at runtime — what the game build copies
// along with a model instead of its whole folder. Mirrors how SceneRenderer
// resolves them:
//   .pmx  each material's diffuse texture, relative to the .pmx folder
//   .fbx  each material's diffuse / base colour texture (absolute path,
//         else relative, else the bare file name next to the .fbx)
//   .obj  mtllib files and the map_* textures they name
// No GPU involved.

#include <filesystem>
#include <string>
#include <vector>

namespace ilmeee {

struct ModelDependency {
  std::filesystem::path source;   // existing file on disk
  std::filesystem::path relative; // where it must sit, relative to the
                                  // model's folder, in the built game
};

// False when the model itself cannot be read (the caller should then fall
// back to copying its folder). Missing dependencies are skipped silently:
// the editor could not load them either.
bool ListModelDependencies(const std::filesystem::path &model,
                           std::vector<ModelDependency> &out,
                           std::string *error = nullptr);

} // namespace ilmeee
