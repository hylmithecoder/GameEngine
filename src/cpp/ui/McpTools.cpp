#include "../../../include/ui/MainWindow.hpp"
#include <cmath>
#include <limits>

namespace {
using json = ilmeee::net::json;

json Error(const std::string &message) { return {{"error", message}}; }

bool Index(const json &args, const char *key, size_t limit, size_t &out) {
  auto it = args.find(key);
  if (it == args.end() || !it->is_number_integer())
    return false;
  if (it->is_number_unsigned()) {
    const auto n = it->get<std::uint64_t>();
    if (n >= limit)
      return false;
    out = static_cast<size_t>(n);
    return true;
  }
  const auto n = it->get<std::int64_t>();
  if (n < 0 || static_cast<std::uint64_t>(n) >= limit)
    return false;
  out = static_cast<size_t>(n);
  return true;
}

bool Vec3(const json &args, const char *key, glm::vec3 &out) {
  auto it = args.find(key);
  if (it == args.end())
    return true;
  if (!it->is_array() || it->size() != 3)
    return false;
  float values[3];
  for (int i = 0; i < 3; ++i) {
    if (!(*it)[i].is_number())
      return false;
    const double n = (*it)[i].get<double>();
    if (!std::isfinite(n) || std::abs(n) > 100000.0)
      return false;
    values[i] = static_cast<float>(n);
  }
  out = {values[0], values[1], values[2]};
  return true;
}

json Array(const glm::vec3 &v) { return json::array({v.x, v.y, v.z}); }
} // namespace

ilmeee::net::json MainWindow::HandleMcpTool(const std::string &name,
                                            const ilmeee::net::json &args) {
  if (!args.is_object())
    return Error("Arguments must be an object");
  if (name == "get_project_info") {
    return {
        {"projectPath", projectHandler.projectPath},
        {"sceneName", projectHandler.currentScene.sceneName},
        {"sceneLoaded", sceneRenderer != nullptr},
        {"playing", !builder.IsStopped()},
        {"objectCount", sceneRenderer ? sceneRenderer->GetMesh3DCount() : 0}};
  }
  if (!sceneRenderer)
    return Error("Scene is not ready");
  const size_t count = sceneRenderer->GetMesh3DCount();
  auto object = [&](size_t i) -> json {
    std::string kind = "mesh";
    if (sceneRenderer->IsMesh3DLight(i))
      kind = "light";
    else if (sceneRenderer->IsMesh3DCamera(i))
      kind = "camera";
    else if (sceneRenderer->meshes3d[i].isAudioEmitter)
      kind = "audio_source";
    else {
      const std::string &path = sceneRenderer->GetMesh3DPath(i);
      if (path == "<primitive:cube>")
        kind = "cube";
      else if (path == "<primitive:sphere>")
        kind = "sphere";
      else if (path == "<primitive:plane>")
        kind = "plane";
      else if (path == "<primitive:screen>")
        kind = "screen";
    }
    return {{"index", i},
            {"name", sceneRenderer->GetMesh3DName(i)},
            {"kind", kind},
            {"parent", sceneRenderer->GetMesh3DParent(i)},
            {"position", Array(sceneRenderer->GetMesh3DPosition(i))},
            {"rotation", Array(sceneRenderer->GetMesh3DRotation(i))},
            {"scale", Array(sceneRenderer->GetMesh3DScale(i))}};
  };
  if (name == "list_scene_objects") {
    json objects = json::array();
    for (size_t i = 0; i < count; ++i)
      objects.push_back(object(i));
    return {{"objects", std::move(objects)}};
  }
  if (name == "get_object") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    return object(i);
  }
  if (SceneStructureLocked())
    return Error("Stop play mode before editing the scene");
  if (name == "create_object") {
    if (!args.contains("kind") || !args["kind"].is_string())
      return Error("kind is required");
    const std::string kind = args["kind"].get<std::string>();
    int kindIndex = -1;
    if (kind == "cube")
      kindIndex = 0;
    else if (kind == "sphere")
      kindIndex = 1;
    else if (kind == "plane")
      kindIndex = 2;
    else if (kind == "light")
      kindIndex = 3;
    else if (kind == "camera")
      kindIndex = 4;
    if (kindIndex < 0)
      return Error("Unsupported object kind");
    glm::vec3 position(0.0f);
    if (!Vec3(args, "position", position))
      return Error("Invalid position");
    int parent = -1;
    if (args.contains("parent")) {
      size_t p;
      if (!Index(args, "parent", count, p) ||
          p > static_cast<size_t>(std::numeric_limits<int>::max()))
        return Error("Invalid parent index");
      parent = static_cast<int>(p);
    }
    const int created = SpawnSceneObject(kindIndex, parent, position);
    if (created < 0)
      return Error("Could not create object");
    return object(static_cast<size_t>(created));
  }
  if (name == "set_transform") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    if (!args.contains("position") && !args.contains("rotation") &&
        !args.contains("scale"))
      return Error("Provide position, rotation, or scale");
    glm::vec3 position = sceneRenderer->GetMesh3DPosition(i);
    glm::vec3 rotation = sceneRenderer->GetMesh3DRotation(i);
    glm::vec3 scale = sceneRenderer->GetMesh3DScale(i);
    if (!Vec3(args, "position", position) ||
        !Vec3(args, "rotation", rotation) || !Vec3(args, "scale", scale))
      return Error("Invalid transform vector");
    if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
      return Error("Scale values must be positive");
    sceneRenderer->SetMesh3DTransform(i, position, rotation, scale);
    RecordEditorHistory();
    return object(i);
  }
  if (name == "save_scene") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    if (!Save3DScene())
      return Error("Could not save scene");
    return {{"saved", true},
            {"path", projectHandler.projectPath + "/scenes/main.ilmeeescene"}};
  }
  return Error("Unknown tool");
}
