#include "../../../include/core_engine/SceneLoader.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ilmeee {

namespace fs = std::filesystem;

namespace {
std::string LowerExtension(const fs::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext;
}
// Project-relative when `path` is inside the project, else unchanged.
std::string ProjectRelative(const std::string &path,
                            const fs::path &projectRoot) {
  if (projectRoot.empty() || path.empty())
    return path;
  std::error_code ec;
  const fs::path rel = fs::relative(fs::path(path), projectRoot, ec);
  const std::string relStr = ec ? std::string() : rel.generic_string();
  return !relStr.empty() && relStr.rfind("..", 0) != 0 ? relStr : path;
}
} // namespace

int LoadSceneEntity(SceneRenderer &renderer, const SceneEntity &e,
                    const fs::path &projectRoot) {
  if (e.kind == PrimitiveKind::Canvas || IsUiElementKind(e.kind))
    return -1; // Screen-space entities are handled by GameUI.
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
    const fs::path full = projectRoot / e.externalPath;
    const std::string ext = LowerExtension(full);
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
                            e.lightIntensity, e.lightRange, e.lightSpotAngle,
                            e.lightGamma);
    break;
  case PrimitiveKind::Camera:
    ok = renderer.LoadCamera(e.name, e.camProjection, e.camFov,
                             e.camOrthoSize, e.camNear, e.camFar);
    break;
  case PrimitiveKind::AudioSource:
    ok = renderer.LoadAudioSource(e.name);
    break;
  case PrimitiveKind::Screen:
    ok = renderer.LoadScreen(e.name);
    break;
  case PrimitiveKind::WorldText:
    ok = renderer.LoadWorldText(e.name, e.worldText);
    break;
  default:
    break;
  }
  if (!ok)
    return -1;

  const size_t idx = renderer.GetMesh3DCount() - 1;
  auto &mesh = renderer.meshes3d[idx];
  // Model loaders name the mesh after its file; the scene's name (possibly
  // renamed by the user) wins.
  if (!e.name.empty())
    mesh.displayName = e.name;
  renderer.SetMesh3DTransform(idx, e.position, e.rotationEuler, e.scale);
  mesh.movable = e.movable;
  mesh.objectId = e.objectId ? e.objectId : NewObjectId();
  mesh.scripts = e.scripts;
  mesh.movementScript = e.movementScript;
  mesh.movementSpeed = e.movementSpeed;
  mesh.movementRig = e.movementRig;
  mesh.movementRig.camera = -1; // entity index; linked by the caller
  mesh.audio = e.audio;
  mesh.video = e.video;
  mesh.dialog = e.dialog;
  mesh.animation = e.animation;
  mesh.springBones = e.springBones;
  mesh.hasPhysics = e.rigidBody.enabled;
  mesh.useGravity = e.rigidBody.useGravity;
  mesh.isKinematic = e.rigidBody.isKinematic;
  mesh.mass = e.rigidBody.mass;
  mesh.drag = e.rigidBody.drag;
  mesh.gravityY = e.rigidBody.gravityY;
  mesh.collider = e.collider;
  // Inspect Mode: every scene-bootstrap-spawned object remembers this loop
  // so hover-to-source lands users on the deserializer.
  renderer.SetMesh3DDebugSource(idx, __FILE__, __LINE__);
  // Re-apply per-surface texture bindings saved in the scene. Paths stored
  // relative to the project resolve against it; absolute paths (textures
  // outside the project) load as-is.
  for (const auto &st : e.surfaceTextures) {
    if (st.texturePath.empty())
      continue;
    const fs::path tp(st.texturePath);
    const std::string full =
        tp.is_absolute() ? st.texturePath : (projectRoot / tp).string();
    renderer.BindMesh3DSubmeshTexture(idx, st.surfaceIndex, full);
  }
  return (int)idx;
}

SceneLoadReport InstantiateScene(SceneRenderer &renderer,
                                 const IlmeeeScene &scene,
                                 const fs::path &projectRoot) {
  SceneLoadReport report;
  renderer.SetBackgroundColor(scene.backgroundColor.r, scene.backgroundColor.g,
                              scene.backgroundColor.b, scene.backgroundColor.a);
  renderer.SetNpr(scene.npr);
  renderer.SetBloom(scene.bloom);

  // entity index -> mesh index (an entity that fails to load shifts all
  // later meshes, so parents are linked through this at the end)
  std::vector<int> loadedAs(scene.entities.size(), -1);
  renderer.SetAnimationProjectRoot(projectRoot.string());
  for (size_t ei = 0; ei < scene.entities.size(); ++ei) {
    const SceneEntity &e = scene.entities[ei];
    if (e.kind == PrimitiveKind::Canvas || IsUiElementKind(e.kind))
      continue;
    loadedAs[ei] = LoadSceneEntity(renderer, e, projectRoot);
    if (loadedAs[ei] < 0)
      ++report.failed;
    else
      ++report.loaded;
  }

  // Saved transforms are parent-local already: link without keepWorld.
  for (size_t ei = 0; ei < scene.entities.size(); ++ei) {
    if (loadedAs[ei] < 0)
      continue;
    const int p = scene.entities[ei].parent;
    if (p >= 0 && (size_t)p < loadedAs.size() && loadedAs[(size_t)p] >= 0)
      renderer.SetMesh3DParent((size_t)loadedAs[ei], loadedAs[(size_t)p],
                               false);
    // Camera links are entity indices too.
    const int cam = scene.entities[ei].movementRig.camera;
    renderer.meshes3d[(size_t)loadedAs[ei]].movementRig.camera =
        cam >= 0 && (size_t)cam < loadedAs.size() ? loadedAs[(size_t)cam] : -1;
  }
  return report;
}

SceneEntity CaptureSceneEntity(const SceneRenderer &renderer, size_t i,
                               const fs::path &projectRoot) {
  SceneEntity e;
  if (i >= renderer.GetMesh3DCount())
    return e;
  const auto &mesh = renderer.meshes3d[i];
  e.name = renderer.GetMesh3DName(i);
  e.parent = renderer.GetMesh3DParent(i);
  e.position = renderer.GetMesh3DPosition(i);
  e.rotationEuler = renderer.GetMesh3DRotation(i);
  e.scale = renderer.GetMesh3DScale(i);
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
    e.kind = PrimitiveKind::WorldText;
  } else if (mesh.isAudioEmitter) {
    e.kind = PrimitiveKind::AudioSource;
  } else if (renderer.IsMesh3DLight(i)) {
    e.kind = PrimitiveKind::Light;
    e.lightGamma = renderer.GetMesh3DLightGamma(i);
    e.lightColor = renderer.GetMesh3DLightColor(i);
    e.lightIntensity = renderer.GetMesh3DLightIntensity(i);
    e.lightType = renderer.GetMesh3DLightType(i);
    e.lightRange = renderer.GetMesh3DLightRange(i);
    e.lightSpotAngle = renderer.GetMesh3DLightSpotAngle(i);
  } else if (renderer.IsMesh3DCamera(i)) {
    e.kind = PrimitiveKind::Camera;
    e.camProjection = renderer.GetMesh3DCameraProjection(i);
    e.camFov = renderer.GetMesh3DCameraFov(i);
    e.camOrthoSize = renderer.GetMesh3DCameraOrthoSize(i);
    e.camNear = renderer.GetMesh3DCameraNear(i);
    e.camFar = renderer.GetMesh3DCameraFar(i);
  } else {
    const std::string &path = renderer.GetMesh3DPath(i);
    // Primitives carry a synthetic "<primitive:kind>" path — the reliable
    // way to recover their kind (name can be renamed by the user).
    if (path.rfind("<primitive:", 0) == 0) {
      if (path.find("sphere") != std::string::npos)
        e.kind = PrimitiveKind::Sphere;
      else if (path.find("plane") != std::string::npos)
        e.kind = PrimitiveKind::Plane;
      else if (path.find("screen") != std::string::npos)
        e.kind = PrimitiveKind::Screen;
      else
        e.kind = PrimitiveKind::Cube;
    } else if (path.empty()) {
      e.kind = PrimitiveKind::Cube; // unknown mesh → safe default
    } else {
      if (!projectRoot.empty()) {
        std::error_code ec;
        const fs::path rel = fs::relative(fs::path(path), projectRoot, ec);
        e.externalPath = ec ? path : rel.string();
      } else {
        e.externalPath = path;
      }
      e.kind = LowerExtension(path) == ".pmx" ? PrimitiveKind::ExternalPmx
                                               : PrimitiveKind::ExternalObj;
    }

    // Capture per-surface texture bindings so re-textured surfaces
    // survive a save/load round-trip. Paths under the project are stored
    // relative; textures elsewhere are stored absolute.
    const uint32_t sc = renderer.GetMesh3DSubmeshCount(i);
    for (uint32_t s = 0; s < sc; ++s) {
      const std::string &tex = renderer.GetMesh3DSubmeshTexture(i, s);
      if (tex.empty())
        continue;
      SurfaceTexture st;
      st.surfaceIndex = s;
      st.texturePath = ProjectRelative(tex, projectRoot);
      e.surfaceTextures.push_back(std::move(st));
    }
  }
  return e;
}

IlmeeeScene CaptureScene(const SceneRenderer &renderer,
                         const fs::path &projectRoot) {
  IlmeeeScene scene;
  scene.backgroundColor = renderer.GetBackgroundColor();
  scene.npr = renderer.GetNpr();
  scene.bloom = renderer.GetBloom();
  scene.entities.reserve(renderer.GetMesh3DCount());
  // Entities are written in mesh order, so the mesh index is the entity
  // index and the parent link carries over unchanged.
  for (size_t i = 0; i < renderer.GetMesh3DCount(); ++i)
    scene.entities.push_back(CaptureSceneEntity(renderer, i, projectRoot));
  return scene;
}

IlmeeeScene CapturePrefab(const SceneRenderer &renderer, size_t root,
                          const fs::path &projectRoot) {
  if (root >= renderer.GetMesh3DCount())
    return {};
  return ExtractSubtree(CaptureScene(renderer, projectRoot), (int)root);
}

PrefabSpawnResult InstantiatePrefab(SceneRenderer &renderer,
                                    IlmeeeScene prefab,
                                    const fs::path &projectRoot,
                                    const PrefabSpawnOptions &options) {
  PrefabSpawnResult result;
  if (!ValidPrefab(prefab))
    return result;
  if (options.freshIds)
    AssignFreshObjectIds(prefab);
  if (options.overridePosition)
    prefab.entities[0].position = options.position;

  const size_t first = renderer.GetMesh3DCount();
  std::vector<int> loadedAs(prefab.entities.size(), -1);
  for (size_t ei = 0; ei < prefab.entities.size(); ++ei) {
    loadedAs[ei] = LoadSceneEntity(renderer, prefab.entities[ei], projectRoot);
    if (loadedAs[ei] < 0) {
      if (ei == 0)
        break; // no root, no copy
      ++result.failed;
    }
  }
  if (loadedAs[0] < 0) {
    // Nothing else was loaded (the loop stopped at the root).
    result.failed = prefab.entities.size();
    while (renderer.GetMesh3DCount() > first)
      renderer.RemoveMesh3D(renderer.GetMesh3DCount() - 1);
    return result;
  }
  for (size_t ei = 0; ei < prefab.entities.size(); ++ei) {
    if (loadedAs[ei] < 0)
      continue;
    const size_t mesh = (size_t)loadedAs[ei];
    int parent = -1;
    if (ei == 0) {
      parent = options.parent >= 0 && (size_t)options.parent < first
                   ? options.parent
                   : -1;
    } else {
      // Nearest loaded ancestor (ValidPrefab: parents come first).
      for (int p = prefab.entities[ei].parent; p >= 0;
           p = prefab.entities[(size_t)p].parent)
        if (loadedAs[(size_t)p] >= 0) {
          parent = loadedAs[(size_t)p];
          break;
        }
    }
    if (parent >= 0)
      renderer.SetMesh3DParent(mesh, parent, false);
    const int cam = prefab.entities[ei].movementRig.camera;
    renderer.meshes3d[mesh].movementRig.camera =
        cam >= 0 && (size_t)cam < loadedAs.size() ? loadedAs[(size_t)cam] : -1;
    result.meshes.push_back(mesh);
  }
  return result;
}

} // namespace ilmeee
