#include "../../../include/ui/MainWindow.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>

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

// Optional [x, y] argument; false when present but malformed.
bool Vec2(const json &args, const char *key, float &x, float &y) {
  auto it = args.find(key);
  if (it == args.end())
    return true;
  if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() ||
      !(*it)[1].is_number())
    return false;
  const double a = (*it)[0].get<double>(), b = (*it)[1].get<double>();
  if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 100000.0 ||
      std::abs(b) > 100000.0)
    return false;
  x = static_cast<float>(a);
  y = static_cast<float>(b);
  return true;
}

// Optional typed argument; false when present with the wrong type.
template <class T> bool Optional(const json &args, const char *key, T &out) {
  auto it = args.find(key);
  if (it == args.end())
    return true;
  if constexpr (std::is_same_v<T, bool>) {
    if (!it->is_boolean()) return false;
  } else if constexpr (std::is_same_v<T, std::string>) {
    if (!it->is_string()) return false;
  } else {
    if (!it->is_number() || !std::isfinite(it->get<double>())) return false;
  }
  out = it->get<T>();
  return true;
}

const char *UiTypeName(ilmeee::UiKind kind) {
  switch (kind) {
  case ilmeee::UiKind::Text: return "text";
  case ilmeee::UiKind::Image: return "image";
  case ilmeee::UiKind::Button: return "button";
  case ilmeee::UiKind::Joystick: return "joystick";
  }
  return "text";
}

const char *TriggerName(ilmeee::DialogTrigger trigger) {
  switch (trigger) {
  case ilmeee::DialogTrigger::OnStart: return "on_start";
  case ilmeee::DialogTrigger::ScriptOnly: return "script_only";
  default: return "interact";
  }
}
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
  if (name == "get_device_stats") {
    size_t samples = 20;
    if (args.contains("samples") && !Index(args, "samples", 601, samples))
      return Error("samples must be 0..600");
    return deviceMonitor.ToJson(ImGui::GetTime(), samples);
  }
  if (name == "get_game_log") {
    size_t lines = 200;
    if (args.contains("lines") &&
        (!Index(args, "lines", 2001, lines) || lines == 0))
      return Error("lines must be 1..2000");
    std::string contains;
    if (auto it = args.find("contains"); it != args.end()) {
      if (!it->is_string())
        return Error("contains must be a string");
      contains = it->get<std::string>();
    }
    bool errorsOnly = false;
    if (auto it = args.find("errorsOnly"); it != args.end()) {
      if (!it->is_boolean())
        return Error("errorsOnly must be a boolean");
      errorsOnly = it->get<bool>();
    }
    const auto &log = deviceMonitor.Log();
    json out = json::array();
    for (auto it = log.rbegin(); it != log.rend() && out.size() < lines;
         ++it) {
      const std::string &line = *it;
      if (!contains.empty() && line.find(contains) == std::string::npos)
        continue;
      if (errorsOnly &&
          !(line.size() > 2 && line[1] == '/' &&
            (line[0] == 'E' || line[0] == 'F')) &&
          line.find("FATAL") == std::string::npos &&
          line.find("Fatal signal") == std::string::npos)
        continue;
      out.push_back(line);
    }
    std::reverse(out.begin(), out.end());
    return {{"target", deviceMonitor.Target()},
            {"running", deviceMonitor.Running()},
            {"totalBuffered", log.size()},
            {"lines", std::move(out)}};
  }
  if (name == "list_ui_elements") {
    LoadGameUiForProject();
    json elements = json::array();
    for (size_t i = 0; i < gameUi.elements.size(); ++i) {
      const auto &e = gameUi.elements[i];
      elements.push_back({{"index", i},
                          {"name", e.id},
                          {"type", UiTypeName(e.kind)},
                          {"visible", e.visible},
                          {"position", json::array({e.x, e.y})},
                          {"size", json::array({e.width, e.height})},
                          {"text", e.text},
                          {"action", e.action},
                          {"image", e.imagePath}});
    }
    return {{"canvas", hasGameUiCanvas},
            {"referenceSize",
             json::array({gameUi.referenceWidth, gameUi.referenceHeight})},
            {"elements", std::move(elements)}};
  }
  if (!sceneRenderer)
    return Error("Scene is not ready");
  const size_t count = sceneRenderer->GetMesh3DCount();
  const auto uiName = [&](uint64_t id) -> json {
    if (!id)
      return nullptr;
    const auto *e = ilmeee::FindUiElement(gameUi, id);
    return e ? json(e->id) : json("(missing)");
  };
  const auto dialogJson = [&](size_t i) -> json {
    const auto &mesh = sceneRenderer->meshes3d[i];
    const auto &d = mesh.dialog;
    json out = {{"index", i},
                {"name", mesh.displayName},
                {"enabled", d.enabled}};
    if (!d.enabled)
      return out;
    out.update({{"trigger", TriggerName(d.trigger)},
                {"range", d.range},
                {"box", uiName(d.box)},
                {"text", uiName(d.text)},
                {"speaker", uiName(d.speaker)},
                {"prompt", uiName(d.prompt)},
                {"lines", d.lines},
                {"file", d.file},
                {"lineCount",
                 ilmeee::ParseDialogScript(d.lines, mesh.displayName).size()},
                {"typingSpeed", d.charsPerSecond},
                {"repeatable", d.repeatable},
                {"freezePlayer", d.freezePlayer},
                {"talking", dialogSystem.Owner() == mesh.objectId}});
    if (dialogSystem.Owner() == mesh.objectId)
      out["lineIndex"] = dialogSystem.LineIndex();
    return out;
  };
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
            {"scale", Array(sceneRenderer->GetMesh3DScale(i))},
            {"hasDialog", sceneRenderer->meshes3d[i].dialog.enabled}};
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
  if (name == "get_dialog") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    LoadGameUiForProject();
    return dialogJson(i);
  }
  if (name == "start_dialog") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    if (!builder.IsPlaying())
      return Error("Press Play first; dialogs only run in play mode");
    if (!sceneRenderer->meshes3d[i].dialog.enabled)
      return Error("This object has no Dialog component");
    if (dialogSystem.IsTalking())
      return Error("Another dialog is open");
    if (!dialogSystem.Begin(*sceneRenderer, gameUi, i))
      return Error("Dialog did not start (no lines?); see get_game_log or "
                   "the Console");
    return dialogJson(i);
  }
  if (SceneStructureLocked())
    return Error("Stop play mode before editing the scene");
  if (name == "set_ui_element") {
    LoadGameUiForProject();
    std::string elementName;
    if (!Optional(args, "name", elementName) || elementName.empty())
      return Error("name is required");
    auto it = std::find_if(gameUi.elements.begin(), gameUi.elements.end(),
                           [&](const auto &e) { return e.id == elementName; });
    if (it == gameUi.elements.end())
      return Error("No UI element named " + elementName);
    ilmeee::UiElement e = *it;
    float fontSize = e.fontSize;
    if (!Optional(args, "text", e.text) || !Optional(args, "action", e.action) ||
        !Optional(args, "image", e.imagePath) ||
        !Optional(args, "visible", e.visible) ||
        !Optional(args, "fontSize", fontSize) ||
        !Vec2(args, "position", e.x, e.y) ||
        !Vec2(args, "size", e.width, e.height))
      return Error("Invalid argument type");
    if (e.width < 1 || e.height < 1 || fontSize < 1 || fontSize > 512)
      return Error("size must be >= 1 and fontSize 1..512");
    e.fontSize = fontSize;
    // Relative paths are already project-relative; absolute ones inside the
    // project are made relative so built games find them.
    if (std::filesystem::path(e.imagePath).is_absolute())
      e.imagePath = ProjectMediaPath(projectHandler.projectPath, e.imagePath);
    *it = std::move(e);
    return {{"name", it->id}, {"saved", false},
            {"note", "Call save_scene to keep the change"}};
  }
  if (name == "set_dialog") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    LoadGameUiForProject();
    ilmeee::DialogComponent d = sceneRenderer->meshes3d[i].dialog;
    d.enabled = true;
    std::string trigger = TriggerName(d.trigger);
    float range = d.range, speed = d.charsPerSecond;
    bool createUi = false;
    if (!Optional(args, "enabled", d.enabled) ||
        !Optional(args, "trigger", trigger) || !Optional(args, "range", range) ||
        !Optional(args, "lines", d.lines) || !Optional(args, "file", d.file) ||
        !Optional(args, "typingSpeed", speed) ||
        !Optional(args, "repeatable", d.repeatable) ||
        !Optional(args, "freezePlayer", d.freezePlayer) ||
        !Optional(args, "createUi", createUi))
      return Error("Invalid argument type");
    if (!d.enabled) {
      sceneRenderer->meshes3d[i].dialog = {};
      RecordEditorHistory();
      return dialogJson(i);
    }
    if (trigger == "interact")
      d.trigger = ilmeee::DialogTrigger::Interact;
    else if (trigger == "on_start")
      d.trigger = ilmeee::DialogTrigger::OnStart;
    else if (trigger == "script_only")
      d.trigger = ilmeee::DialogTrigger::ScriptOnly;
    else
      return Error("trigger must be interact, on_start or script_only");
    if (range < 0 || speed < 0 || range > 100000 || speed > 100000)
      return Error("range and typingSpeed must be >= 0");
    d.range = range;
    d.charsPerSecond = speed;
    if (std::filesystem::path(d.file).is_absolute())
      d.file = ProjectMediaPath(projectHandler.projectPath, d.file);
    for (auto [key, link] :
         {std::pair<const char *, uint64_t *>{"box", &d.box},
          {"text", &d.text}, {"speaker", &d.speaker}, {"prompt", &d.prompt}}) {
      std::string element;
      if (!Optional(args, key, element))
        return Error(std::string(key) + " must be a UI element name");
      if (!args.contains(key))
        continue;
      if (element.empty()) {
        *link = 0;
        continue;
      }
      auto it = std::find_if(gameUi.elements.begin(), gameUi.elements.end(),
                             [&](const auto &e) { return e.id == element; });
      if (it == gameUi.elements.end())
        return Error("No UI element named " + element);
      *link = it->objectId;
    }
    if (createUi)
      CreateDialogUi(d);
    sceneRenderer->meshes3d[i].dialog = d;
    RecordEditorHistory();
    return dialogJson(i);
  }
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
