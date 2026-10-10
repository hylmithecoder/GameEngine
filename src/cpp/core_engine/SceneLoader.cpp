#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ilmeee {

namespace fs = std::filesystem;

SceneLoadReport InstantiateScene(SceneRenderer &renderer,
                                 const IlmeeeScene &scene,
                                 const fs::path &projectRoot) {
  SceneLoadReport report;
  renderer.SetBackgroundColor(scene.backgroundColor.r, scene.backgroundColor.g,
                              scene.backgroundColor.b, scene.backgroundColor.a);

  // entity index -> mesh index (an entity that fails to load shifts all
  // later meshes, so parents are linked through this at the end)
  std::vector<int> loadedAs(scene.entities.size(), -1);
  for (size_t ei = 0; ei < scene.entities.size(); ++ei) {
    const SceneEntity &e = scene.entities[ei];
    if (e.kind == PrimitiveKind::Canvas || e.kind == PrimitiveKind::UiText ||
        e.kind == PrimitiveKind::UiImage || e.kind == PrimitiveKind::UiButton ||
        e.kind == PrimitiveKind::UiJoystick)
      continue; // Screen-space entities are handled by GameUI.
    bool ok = false;
    switch (e.kind) {
    case PrimitiveKind::Cube:
      ok = renderer.LoadCube(e.name);
      break;
    case PrimitiveKind::Sphere:
      ok = renderer.LoadSphere(e.name);
      break;
    case PrimitiveKind::Plane:
      ok = renderer.LoadPlane(e.name);
      break;
    case PrimitiveKind::ExternalObj: {
      fs::path full = projectRoot / e.externalPath;
      std::string ext = full.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      // Auto-detect by extension: .pmx → PMX, .fbx → FBX, else OBJ.
      if (ext == ".pmx")
        ok = renderer.LoadPMXMesh(full.string());
      else if (ext == ".fbx")
        ok = renderer.LoadFbxMesh(full.string());
      else
        ok = renderer.LoadObjMesh(full.string());
      break;
    }
    case PrimitiveKind::ExternalPmx:
      ok = renderer.LoadPMXMesh((projectRoot / e.externalPath).string());
      break;
    case PrimitiveKind::Light:
      ok = renderer.LoadLight(e.name, e.lightType, e.lightColor,
                              e.lightIntensity, e.lightRange,
                              e.lightSpotAngle, e.lightGamma);
      break;
    case PrimitiveKind::Camera:
      ok = renderer.LoadCamera(e.name, e.camProjection, e.camFov,
                               e.camOrthoSize, e.camNear, e.camFar);
      break;
    }
    if (!ok) {
      ++report.failed;
      continue;
    }
    ++report.loaded;

    size_t idx = renderer.GetMesh3DCount() - 1;
    loadedAs[ei] = (int)idx;
    // Model loaders name the mesh after its file; the scene's name (possibly
    // renamed by the user) wins.
    if (!e.name.empty())
      renderer.meshes3d[idx].displayName = e.name;
    renderer.SetMesh3DTransform(idx, e.position, e.rotationEuler, e.scale);
    renderer.meshes3d[idx].movable = e.movable;
    renderer.meshes3d[idx].movementScript = e.movementScript;
    renderer.meshes3d[idx].movementSpeed = e.movementSpeed;
    renderer.meshes3d[idx].movementRig = e.movementRig;
    // Inspect Mode: every scene-bootstrap-spawned object remembers this loop
    // so hover-to-source lands users on the deserializer.
    renderer.SetMesh3DDebugSource(idx, __FILE__, __LINE__);
    // Re-apply per-surface texture bindings saved in the scene. Paths stored
    // relative to the project resolve against it; absolute paths (textures
    // outside the project) load as-is.
    for (const auto &st : e.surfaceTextures) {
      if (st.texturePath.empty())
        continue;
      fs::path tp(st.texturePath);
      std::string full =
          tp.is_absolute() ? st.texturePath : (projectRoot / tp).string();
      renderer.BindMesh3DSubmeshTexture(idx, st.surfaceIndex, full);
    }
  }

  // Saved transforms are parent-local already: link without keepWorld.
  for (size_t ei = 0; ei < scene.entities.size(); ++ei) {
    int p = scene.entities[ei].parent;
    if (loadedAs[ei] >= 0 && p >= 0 && (size_t)p < loadedAs.size() &&
        loadedAs[(size_t)p] >= 0)
      renderer.SetMesh3DParent((size_t)loadedAs[ei], loadedAs[(size_t)p],
                               false);
    // Camera links are entity indices too.
    if (loadedAs[ei] >= 0) {
      const int cam = scene.entities[ei].movementRig.camera;
      renderer.meshes3d[(size_t)loadedAs[ei]].movementRig.camera =
          cam >= 0 && (size_t)cam < loadedAs.size() ? loadedAs[(size_t)cam] : -1;
    }
  }
  return report;
}

} // namespace ilmeee
