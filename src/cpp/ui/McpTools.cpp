#include "../../../include/ui/MainWindow.hpp"
#include "core_engine/AnimationTooling.hpp"
#include "core_engine/AnimationController.hpp"
#include "core_engine/ScriptWorkspace.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cmath>
#include <limits>
#include <sstream>
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

// Component types addressable by list/add/remove_component. Light, camera,
// text_3d and audio emitters are object kinds, not components.
constexpr const char *kComponentTypes[] = {
    "movement", "physics", "collider", "audio_source", "video_player",
    "dialog",   "animation", "spring_bones", "script"};

constexpr const char *kColliderShapes[] = {"box", "sphere", "capsule"};

constexpr const char *kCameraStyles[] = {"none", "third_person", "first_person",
                                         "side_scroller", "top_down"};

bool HasAnimation(const SceneRenderer::Mesh3D &mesh) {
  return mesh.animation.enabled || !mesh.animation.clip.empty() ||
         !mesh.animation.controller.empty();
}

bool HasMovement(const SceneRenderer::Mesh3D &mesh) {
  return !mesh.isLight && !mesh.isCamera &&
         (mesh.movable || !mesh.movementScript.empty());
}

// Compact "which components are attached" list for get_object.
json AttachedComponents(const SceneRenderer::Mesh3D &mesh) {
  json out = json::array();
  if (HasMovement(mesh)) out.push_back("movement");
  if (mesh.hasPhysics) out.push_back("physics");
  if (mesh.collider.enabled) out.push_back("collider");
  if (mesh.audio.enabled) out.push_back("audio_source");
  if (mesh.video.enabled) out.push_back("video_player");
  if (mesh.dialog.enabled) out.push_back("dialog");
  if (HasAnimation(mesh)) out.push_back("animation");
  if (mesh.springBones.enabled || !mesh.springBones.chains.empty())
    out.push_back("spring_bones");
  for (const auto &s : mesh.scripts) out.push_back("script:" + s.className);
  return out;
}

// Rejects keys outside `allowed` so typos surface instead of being ignored.
std::string UnknownKey(const json &props,
                       std::initializer_list<const char *> allowed) {
  for (const auto &[key, value] : props.items())
    if (std::none_of(allowed.begin(), allowed.end(),
                     [&](const char *k) { return key == k; }))
      return key;
  return {};
}

const char *ScriptTypeName(ilmeee::ScriptValueType type) {
  switch (type) {
  case ilmeee::ScriptValueType::Float: return "float";
  case ilmeee::ScriptValueType::Bool: return "bool";
  case ilmeee::ScriptValueType::Object: return "GameObject";
  case ilmeee::ScriptValueType::Vec3: return "Vec3";
  case ilmeee::ScriptValueType::String: return "string";
  default: return "List";
  }
}

// GameObject fields: scene object index, UI element name, or null.
using ObjectNamer = std::function<json(uint64_t)>;
using ObjectResolver = std::function<bool(const json &, uint64_t &)>;

json ScalarJson(ilmeee::ScriptValueType type, const ilmeee::ScriptValue &v,
                const ObjectNamer &object) {
  using T = ilmeee::ScriptValueType;
  switch (type) {
  case T::Float: return v.number;
  case T::Bool: return v.boolean;
  case T::Vec3: return json::array({v.vec3[0], v.vec3[1], v.vec3[2]});
  case T::String: return v.string;
  case T::Object: return object(v.object);
  default: return nullptr;
  }
}

std::string ScalarFromJson(ilmeee::ScriptValueType type, const json &in,
                           ilmeee::ScriptValue &v, const ObjectResolver &object) {
  using T = ilmeee::ScriptValueType;
  switch (type) {
  case T::Float:
    if (!in.is_number() || !std::isfinite(in.get<double>()))
      return "expects a number";
    v.number = in.get<float>();
    return {};
  case T::Bool:
    if (!in.is_boolean()) return "expects a boolean";
    v.boolean = in.get<bool>();
    return {};
  case T::Vec3: {
    glm::vec3 vec;
    if (!Vec3(json{{"v", in}}, "v", vec)) return "expects [x, y, z]";
    v.vec3[0] = vec.x, v.vec3[1] = vec.y, v.vec3[2] = vec.z;
    return {};
  }
  case T::String:
    if (!in.is_string()) return "expects a string";
    v.string = in.get<std::string>();
    return {};
  case T::Object:
    if (!object(in, v.object))
      return "expects a scene object index, a UI element name, or null";
    return {};
  default: return "has an unsupported type";
  }
}

constexpr size_t kMaxScriptBytes = 256 * 1024;

// Accepts "Door", "Door.cpp", "Door.hpp", or "assets/scripts/Door.cpp".
std::string NormalizeScriptName(const std::string &raw, std::string &className) {
  std::string s = raw;
  const std::string prefix = "assets/scripts/";
  if (s.rfind(prefix, 0) == 0)
    s.erase(0, prefix.size());
  if (s.size() > 4 &&
      (s.substr(s.size() - 4) == ".cpp" || s.substr(s.size() - 4) == ".hpp"))
    s.resize(s.size() - 4);
  className = s;
  return ilmeee::ValidateScriptName(className);
}

bool ReadScriptFile(const std::filesystem::path &path, std::string &out,
                    std::string &error) {
  std::error_code ec;
  if (std::filesystem::is_symlink(path, ec)) {
    error = "Refusing to read symlink " + path.filename().string();
    return false;
  }
  if (!std::filesystem::is_regular_file(path, ec)) {
    error = "Missing file " + path.filename().string();
    return false;
  }
  const auto bytes = std::filesystem::file_size(path, ec);
  if (ec || bytes > kMaxScriptBytes) {
    error = path.filename().string() + " exceeds the 256 KiB script limit";
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "Could not read " + path.filename().string();
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool WriteScriptFile(const std::filesystem::path &path,
                     const std::string &content, std::string &error) {
  if (content.size() > kMaxScriptBytes) {
    error = path.filename().string() + " exceeds the 256 KiB script limit";
    return false;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec || std::filesystem::is_symlink(path.parent_path(), ec) ||
      std::filesystem::is_symlink(path, ec)) {
    error = "Invalid destination for " + path.filename().string();
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    error = "Could not write " + path.filename().string();
    return false;
  }
  out << content;
  if (!out.good()) {
    error = "Failed while writing " + path.filename().string();
    return false;
  }
  return true;
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
  if (name == "get_skeleton" || name == "get_animation" ||
      name == "sample_animation" || name == "create_animation" ||
      name == "edit_animation" || name == "generate_animation") {
    const bool writing = name == "create_animation" || name == "edit_animation" ||
                         name == "generate_animation";
    if (writing && SceneStructureLocked())
      return Error("Stop play mode before editing animation assets");
    const std::vector<ilmeee::SkeletonBone> *bones = nullptr;
    if (args.contains("index") || name == "get_skeleton" || name == "generate_animation") {
      size_t i;
      if (!sceneRenderer || !Index(args, "index", sceneRenderer->GetMesh3DCount(), i))
        return Error("Invalid object index");
      bones = &sceneRenderer->meshes3d[i].skeleton;
      if (bones->empty()) return Error("Select a skinned object with bones");
    }
    const std::vector<ilmeee::SkeletonBone> empty;
    json result = ilmeee::HandleAnimationTool(name, args, projectHandler.projectPath,
                                              bones ? *bones : empty);
    if (writing && !result.contains("error") && sceneRenderer) {
      // Controllers may share any edited clip. Reload their cached assets on
      // the next Play, as well as direct clip bindings.
      for (auto &mesh : sceneRenderer->meshes3d) {
        mesh.loadedAnimationPath.clear();
        mesh.animationError.clear();
        mesh.locomotion = {};
      }
    }
    return result;
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
      else if (path == "<primitive:text>")
        kind = "text_3d";
    }
    return {{"index", i},
            {"name", sceneRenderer->GetMesh3DName(i)},
            {"kind", kind},
            {"parent", sceneRenderer->GetMesh3DParent(i)},
            {"position", Array(sceneRenderer->GetMesh3DPosition(i))},
            {"rotation", Array(sceneRenderer->GetMesh3DRotation(i))},
            {"scale", Array(sceneRenderer->GetMesh3DScale(i))},
            {"hasDialog", sceneRenderer->meshes3d[i].dialog.enabled},
            {"bones", sceneRenderer->meshes3d[i].skeleton.size()},
            {"animation", {{"enabled", sceneRenderer->meshes3d[i].animation.enabled},
                            {"clip", sceneRenderer->meshes3d[i].animation.clip},
                            {"controller", sceneRenderer->meshes3d[i].animation.controller},
                            {"loop", sceneRenderer->meshes3d[i].animation.loop},
                            {"speed", sceneRenderer->meshes3d[i].animation.speed},
                            {"time", sceneRenderer->meshes3d[i].animationTime},
                            {"error", sceneRenderer->meshes3d[i].animationError}}},
            {"springBones", sceneRenderer->meshes3d[i].springBones.enabled},
            {"components", AttachedComponents(sceneRenderer->meshes3d[i])}};
  };
  const auto springJson = [&](size_t i) {
    const auto &mesh = sceneRenderer->meshes3d[i];
    const auto &spring = mesh.springBones;
    json chains = json::array(), colliders = json::array();
    for (const auto &c : spring.chains)
      chains.push_back({{"rootBone", c.rootBone}, {"stiffness", c.stiffness},
                        {"drag", c.drag}, {"gravity", c.gravity},
                        {"hitRadius", c.hitRadius}, {"useColliders", c.useColliders}});
    for (const auto &c : spring.colliders)
      colliders.push_back({{"bone", c.bone}, {"offset", Array(c.offset)},
                           {"tailOffset", Array(c.tailOffset)}, {"radius", c.radius}});
    return json{{"index", i}, {"name", sceneRenderer->GetMesh3DName(i)},
                {"bones", mesh.skeleton.size()}, {"enabled", spring.enabled},
                {"wind", Array(spring.wind)},
                {"simulatedBones", ilmeee::CountSpringJoints(mesh.skeleton, spring)},
                {"chains", std::move(chains)}, {"colliders", std::move(colliders)}};
  };
  if (name == "list_components" || name == "add_component" ||
      name == "remove_component")
    return HandleMcpComponentTool(name, args);
  const auto scriptInfoJson = [&](const std::string &relSource) -> json {
    const std::string className =
        std::filesystem::path(relSource).stem().string();
    const ObjectNamer objectName = [&](uint64_t id) -> json {
      if (!id) return nullptr;
      for (size_t m = 0; m < sceneRenderer->meshes3d.size(); ++m)
        if (sceneRenderer->meshes3d[m].objectId == id)
          return {{"index", m}, {"name", sceneRenderer->meshes3d[m].displayName}};
      if (const auto *e = ilmeee::FindUiElement(gameUi, id))
        return {{"ui", e->id}};
      return "(missing)";
    };
    ilmeee::NativeScriptBinding probe;
    probe.source = relSource;
    probe.className = className;
    json fields = json::object();
    for (const auto &value : nativeScripts.Properties(probe)) {
      json field = {{"type", ScriptTypeName(value.type)}};
      if (value.type == ilmeee::ScriptValueType::List) {
        field["elementType"] = ScriptTypeName(value.elementType);
        json items = json::array();
        for (const auto &item : value.items)
          items.push_back(ScalarJson(value.elementType, item, objectName));
        field["default"] = std::move(items);
      } else {
        field["default"] = ScalarJson(value.type, value, objectName);
      }
      fields[value.name] = std::move(field);
    }
    json attachedTo = json::array();
    for (size_t m = 0; m < sceneRenderer->meshes3d.size(); ++m) {
      for (const auto &b : sceneRenderer->meshes3d[m].scripts) {
        if (b.source == relSource || b.className == className) {
          attachedTo.push_back({{"index", m},
                                {"name", sceneRenderer->meshes3d[m].displayName},
                                {"enabled", b.enabled}});
        }
      }
    }
    return {{"className", className},
            {"source", relSource},
            {"header", "assets/scripts/" + className + ".hpp"},
            {"ready", nativeScripts.Ready(relSource)},
            {"diagnostics", nativeScripts.Diagnostics(relSource)},
            {"fields", std::move(fields)},
            {"attachedTo", std::move(attachedTo)}};
  };
  if (name == "list_scripts") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    LoadGameUiForProject();
    nativeScripts.Configure(
        ilmeee::DefaultScriptCompileOptions(projectHandler.projectPath));
    json scripts = json::array();
    for (const auto &source :
         ilmeee::DiscoverNativeScripts(projectHandler.projectPath))
      scripts.push_back(scriptInfoJson(source));
    return {{"scripts", std::move(scripts)},
            {"compiling", nativeScripts.IsCompiling()}};
  }
  if (name == "get_script") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    std::string rawName, className;
    if (!Optional(args, "name", rawName) || rawName.empty())
      return Error("name is required");
    if (const std::string err = NormalizeScriptName(rawName, className);
        !err.empty())
      return Error(err);
    const std::filesystem::path scriptsDir =
        std::filesystem::path(projectHandler.projectPath) / "assets" / "scripts";
    const std::filesystem::path hppPath = scriptsDir / (className + ".hpp");
    const std::filesystem::path cppPath = scriptsDir / (className + ".cpp");
    std::string headerContent, sourceContent, readErr;
    if (!ReadScriptFile(cppPath, sourceContent, readErr))
      return Error(readErr);
    if (std::filesystem::exists(hppPath) &&
        !ReadScriptFile(hppPath, headerContent, readErr))
      return Error(readErr);
    LoadGameUiForProject();
    nativeScripts.Configure(
        ilmeee::DefaultScriptCompileOptions(projectHandler.projectPath));
    const std::string relSource = "assets/scripts/" + className + ".cpp";
    json out = scriptInfoJson(relSource);
    out["headerContent"] = std::move(headerContent);
    out["sourceContent"] = std::move(sourceContent);
    return out;
  }
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
  if (name == "get_spring_bones") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    return springJson(i);
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
  if (name == "create_script") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    if (const std::string bad =
            UnknownKey(args, {"name", "header", "source", "attachTo", "overwrite"});
        !bad.empty())
      return Error("Unknown argument '" + bad + "'");
    std::string rawName, className;
    if (!Optional(args, "name", rawName) || rawName.empty())
      return Error("name is required");
    if (const std::string err = NormalizeScriptName(rawName, className);
        !err.empty())
      return Error(err);
    std::string headerContent, sourceContent;
    bool overwrite = false;
    if (!Optional(args, "header", headerContent) ||
        !Optional(args, "source", sourceContent) ||
        !Optional(args, "overwrite", overwrite))
      return Error("Invalid argument type");
    const bool hasHeader = args.contains("header");
    const bool hasSource = args.contains("source");
    if (hasHeader != hasSource)
      return Error("Provide both header and source, or omit both to use the "
                   "default template");
    size_t attachIndex = 0;
    const bool hasAttach = args.contains("attachTo");
    if (hasAttach && !Index(args, "attachTo", count, attachIndex))
      return Error("Invalid attachTo object index");
    const std::filesystem::path scriptsDir =
        std::filesystem::path(projectHandler.projectPath) / "assets" / "scripts";
    const std::filesystem::path hppPath = scriptsDir / (className + ".hpp");
    const std::filesystem::path cppPath = scriptsDir / (className + ".cpp");
    std::error_code ec;
    const bool exists = std::filesystem::exists(hppPath, ec) ||
                        std::filesystem::exists(cppPath, ec);
    if (exists && !overwrite)
      return Error("Script " + className +
                   " already exists (pass overwrite: true to replace)");
    std::string ioErr;
    const auto opts =
        ilmeee::DefaultScriptCompileOptions(projectHandler.projectPath);
    if (hasHeader) {
      if (!WriteScriptFile(hppPath, headerContent, ioErr) ||
          !WriteScriptFile(cppPath, sourceContent, ioErr))
        return Error(ioErr);
      std::string dbErr;
      ilmeee::WriteScriptCompilationDatabase(opts, dbErr);
    } else {
      if (overwrite) {
        std::filesystem::remove(hppPath, ec);
        std::filesystem::remove(cppPath, ec);
      }
      if (!ilmeee::CreateScriptFiles(scriptsDir, className, ioErr))
        return Error(ioErr);
    }
    LoadGameUiForProject();
    nativeScripts.Configure(opts);
    const std::string relSource = "assets/scripts/" + className + ".cpp";
    const auto compiled = nativeScripts.CompileAndLoad(relSource);
    const bool ready = compiled.ok && nativeScripts.Ready(relSource);
    bool attached = false;
    if (hasAttach && ready) {
      auto &mesh = sceneRenderer->meshes3d[attachIndex];
      auto it = std::find_if(mesh.scripts.begin(), mesh.scripts.end(),
                             [&](const auto &b) {
                               return b.source == relSource ||
                                      b.className == className;
                             });
      ilmeee::NativeScriptBinding binding;
      binding.source = relSource;
      binding.className = className;
      binding.enabled = it != mesh.scripts.end() ? it->enabled : true;
      if (it != mesh.scripts.end())
        binding.values = it->values;
      binding.values = nativeScripts.Properties(binding);
      if (it != mesh.scripts.end())
        *it = std::move(binding);
      else
        mesh.scripts.push_back(std::move(binding));
      RecordEditorHistory();
      attached = true;
    }
    json out = scriptInfoJson(relSource);
    out["ok"] = ready;
    if (hasAttach) {
      out["attached"] = attached;
      out["saved"] = false;
    }
    return out;
  }
  if (name == "edit_script") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    if (const std::string bad = UnknownKey(args, {"name", "header", "source"});
        !bad.empty())
      return Error("Unknown argument '" + bad + "'");
    std::string rawName, className;
    if (!Optional(args, "name", rawName) || rawName.empty())
      return Error("name is required");
    if (const std::string err = NormalizeScriptName(rawName, className);
        !err.empty())
      return Error(err);
    std::string headerContent, sourceContent;
    if (!Optional(args, "header", headerContent) ||
        !Optional(args, "source", sourceContent))
      return Error("Invalid argument type");
    const bool hasHeader = args.contains("header");
    const bool hasSource = args.contains("source");
    if (!hasHeader && !hasSource)
      return Error("Provide header and/or source to update");
    const std::filesystem::path scriptsDir =
        std::filesystem::path(projectHandler.projectPath) / "assets" / "scripts";
    const std::filesystem::path hppPath = scriptsDir / (className + ".hpp");
    const std::filesystem::path cppPath = scriptsDir / (className + ".cpp");
    std::error_code ec;
    if (!std::filesystem::exists(cppPath, ec) &&
        !std::filesystem::exists(hppPath, ec))
      return Error("No script named " + className +
                   "; use create_script first");
    std::string ioErr;
    if (hasHeader && !WriteScriptFile(hppPath, headerContent, ioErr))
      return Error(ioErr);
    if (hasSource && !WriteScriptFile(cppPath, sourceContent, ioErr))
      return Error(ioErr);
    LoadGameUiForProject();
    const auto opts =
        ilmeee::DefaultScriptCompileOptions(projectHandler.projectPath);
    nativeScripts.Configure(opts);
    const std::string relSource = "assets/scripts/" + className + ".cpp";
    const auto compiled = nativeScripts.CompileAndLoad(relSource);
    json out = scriptInfoJson(relSource);
    out["ok"] = compiled.ok && nativeScripts.Ready(relSource);
    return out;
  }
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
    else if (kind == "text_3d")
      kindIndex = 7;
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
  if (name == "set_spring_bones") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    auto &mesh = sceneRenderer->meshes3d[i];
    if (mesh.skeleton.empty())
      return Error("Spring bones need a skinned (PMX) model with bones");
    ilmeee::SpringBoneComponent spring = mesh.springBones;
    bool autoSetup = false;
    if (!Optional(args, "auto", autoSetup))
      return Error("auto must be a boolean");
    if (autoSetup) {
      spring = ilmeee::AutoSetupSpringBones(mesh.skeleton);
      if (spring.chains.empty())
        return Error("Auto setup found no hair/breast/skirt/ribbon/tail bones");
    }
    if (!Optional(args, "enabled", spring.enabled) || !Vec3(args, "wind", spring.wind))
      return Error("Invalid enabled or wind");
    const auto hasBone = [&](const std::string &bone) {
      for (const auto &b : mesh.skeleton) if (b.name == bone) return true;
      return false;
    };
    if (auto it = args.find("chains"); it != args.end()) {
      if (!it->is_array() || it->size() > 256) return Error("chains must be an array");
      spring.chains.clear();
      for (const auto &item : *it) {
        ilmeee::SpringBoneChain c;
        if (!item.is_object() || !Optional(item, "rootBone", c.rootBone) ||
            !Optional(item, "stiffness", c.stiffness) || !Optional(item, "drag", c.drag) ||
            !Optional(item, "gravity", c.gravity) || !Optional(item, "hitRadius", c.hitRadius) ||
            !Optional(item, "useColliders", c.useColliders))
          return Error("Invalid chain entry");
        if (!hasBone(c.rootBone)) return Error("Unknown chain root bone: " + c.rootBone);
        c.stiffness = std::clamp(c.stiffness, 0.0f, 100.0f);
        c.drag = std::clamp(c.drag, 0.0f, 1.0f);
        c.gravity = std::clamp(c.gravity, -10.0f, 10.0f);
        c.hitRadius = std::clamp(c.hitRadius, 0.0f, 10.0f);
        spring.chains.push_back(c);
      }
    }
    if (auto it = args.find("colliders"); it != args.end()) {
      if (!it->is_array() || it->size() > 256) return Error("colliders must be an array");
      spring.colliders.clear();
      for (const auto &item : *it) {
        ilmeee::SpringCollider c;
        if (!item.is_object() || !Optional(item, "bone", c.bone) ||
            !Vec3(item, "offset", c.offset) || !Optional(item, "radius", c.radius))
          return Error("Invalid collider entry");
        c.tailOffset = c.offset; // sphere unless a tail is given
        if (!Vec3(item, "tailOffset", c.tailOffset)) return Error("Invalid collider tailOffset");
        if (!hasBone(c.bone)) return Error("Unknown collider bone: " + c.bone);
        c.radius = std::clamp(c.radius, 0.0f, 100.0f);
        spring.colliders.push_back(c);
      }
    }
    mesh.springBones = std::move(spring);
    mesh.springRuntime.Reset();
    RecordEditorHistory();
    json result = springJson(i);
    result["saved"] = false;
    result["note"] = "Runs in Play mode; call save_scene to keep it";
    return result;
  }
  if (name == "set_animation") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    auto &mesh = sceneRenderer->meshes3d[i];
    if (mesh.skeleton.empty())
      return Error("Animation needs a skinned (PMX) model with bones");
    ilmeee::AnimationComponent animation = mesh.animation;
    if (!Optional(args, "clip", animation.clip) ||
        !Optional(args, "controller", animation.controller) ||
        !Optional(args, "enabled", animation.enabled) ||
        !Optional(args, "loop", animation.loop) ||
        !Optional(args, "speed", animation.speed))
      return Error("Invalid animation field");
    if (!std::isfinite(animation.speed) || animation.speed < 0 || animation.speed > 4)
      return Error("speed must be 0..4");
    // Supplying a clip selects direct playback unless the caller explicitly
    // also supplies a controller, which continues to take precedence.
    if (args.contains("clip") && !args.contains("controller")) animation.controller.clear();
    if ((args.contains("clip") || args.contains("controller")) && !args.contains("enabled"))
      animation.enabled = !animation.clip.empty() || !animation.controller.empty();
    for (std::string *asset : {&animation.clip, &animation.controller}) {
      if (asset->empty()) continue;
      std::string path, relative, error;
      if (!ilmeee::ResolveAnimationAsset(projectHandler.projectPath, *asset, false, path, relative, error))
        return Error(error);
      if (asset == &animation.clip) {
        ilmeee::AnimationClip clip;
        if (!ilmeee::IsAnimationPath(path) || !ilmeee::LoadAnimationClip(path, clip, error))
          return Error(error.empty() ? "Expected .ilmeeeanim or .vmd" : error);
        bool matched = clip.tracks.empty();
        for (const auto &t : clip.tracks)
          for (const auto &b : mesh.skeleton) if (t.bone == b.name) matched = true;
        if (!matched) return Error("Clip has no matching bone tracks on this object");
      } else {
        ilmeee::AnimationController controller;
        if (!ilmeee::IsAnimationControllerPath(path) || !ilmeee::LoadAnimationController(path, controller, error))
          return Error(error.empty() ? "Expected .ilmeeecontroller" : error);
      }
      *asset = relative;
    }
    mesh.animation = animation;
    mesh.loadedAnimationPath.clear();
    mesh.animationError.clear();
    mesh.animationTime = 0;
    mesh.springRuntime.Reset();
    mesh.locomotion = {};
    RecordEditorHistory();
    return {{"index", i}, {"name", sceneRenderer->GetMesh3DName(i)},
            {"enabled", animation.enabled}, {"clip", animation.clip},
            {"controller", animation.controller}, {"loop", animation.loop},
            {"speed", animation.speed}, {"saved", false},
            {"note", "Plays in Play mode; call save_scene to keep it"}};
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

ilmeee::net::json MainWindow::McpComponentList(size_t i) {
  const auto &mesh = sceneRenderer->meshes3d[i];
  const ObjectNamer objectName = [&](uint64_t id) -> json {
    if (!id) return nullptr;
    for (size_t m = 0; m < sceneRenderer->meshes3d.size(); ++m)
      if (sceneRenderer->meshes3d[m].objectId == id)
        return {{"index", m}, {"name", sceneRenderer->meshes3d[m].displayName}};
    if (const auto *e = ilmeee::FindUiElement(gameUi, id))
      return {{"ui", e->id}};
    return "(missing)";
  };
  json components = json::array();
  if (HasMovement(mesh)) {
    const auto &rig = mesh.movementRig;
    components.push_back(
        {{"type", "movement"}, {"movable", mesh.movable},
         {"script", mesh.movementScript}, {"speed", mesh.movementSpeed},
         {"camera", rig.camera}, {"cameraStyle", kCameraStyles[(int)rig.style]},
         {"distance", rig.distance}, {"height", rig.height},
         {"moveJoystick", rig.moveJoystick}, {"lookJoystick", rig.lookJoystick}});
  }
  if (mesh.hasPhysics)
    components.push_back({{"type", "physics"}, {"useGravity", mesh.useGravity},
                          {"isKinematic", mesh.isKinematic}, {"mass", mesh.mass},
                          {"drag", mesh.drag}, {"gravityY", mesh.gravityY}});
  if (mesh.collider.enabled) {
    const auto &c = mesh.collider;
    components.push_back({{"type", "collider"},
                          {"shape", kColliderShapes[static_cast<int>(c.shape)]},
                          {"center", Array(c.center)}, {"size", Array(c.size)},
                          {"radius", c.radius}, {"height", c.height},
                          {"isTrigger", c.isTrigger}, {"friction", c.friction},
                          {"bounciness", c.bounciness}});
  }
  if (mesh.audio.enabled) {
    const auto &a = mesh.audio;
    components.push_back({{"type", "audio_source"}, {"clip", a.clip},
                          {"volume", a.volume}, {"loop", a.loop},
                          {"playOnStart", a.playOnStart}, {"spatial", a.spatial},
                          {"minDistance", a.minDistance},
                          {"maxDistance", a.maxDistance},
                          {"removable", !mesh.isAudioEmitter}});
  }
  if (mesh.video.enabled) {
    const auto &v = mesh.video;
    components.push_back({{"type", "video_player"}, {"clip", v.clip},
                          {"loop", v.loop}, {"playOnStart", v.playOnStart},
                          {"volume", v.volume}, {"unlit", v.unlit},
                          {"surface", v.surface}});
  }
  if (mesh.dialog.enabled)
    components.push_back({{"type", "dialog"},
                          {"trigger", TriggerName(mesh.dialog.trigger)},
                          {"note", "Details: get_dialog"}});
  if (HasAnimation(mesh))
    components.push_back({{"type", "animation"},
                          {"enabled", mesh.animation.enabled},
                          {"clip", mesh.animation.clip},
                          {"controller", mesh.animation.controller},
                          {"loop", mesh.animation.loop},
                          {"speed", mesh.animation.speed},
                          {"error", mesh.animationError}});
  if (mesh.springBones.enabled || !mesh.springBones.chains.empty())
    components.push_back({{"type", "spring_bones"},
                          {"enabled", mesh.springBones.enabled},
                          {"chains", mesh.springBones.chains.size()},
                          {"colliders", mesh.springBones.colliders.size()},
                          {"note", "Details: get_spring_bones"}});
  for (const auto &binding : mesh.scripts) {
    json fields = json::object();
    for (const auto &value : nativeScripts.Properties(binding)) {
      json field = {{"type", ScriptTypeName(value.type)}};
      if (value.type == ilmeee::ScriptValueType::List) {
        field["elementType"] = ScriptTypeName(value.elementType);
        json items = json::array();
        for (const auto &item : value.items)
          items.push_back(ScalarJson(value.elementType, item, objectName));
        field["value"] = std::move(items);
      } else {
        field["value"] = ScalarJson(value.type, value, objectName);
      }
      fields[value.name] = std::move(field);
    }
    components.push_back({{"type", "script"}, {"source", binding.source},
                          {"className", binding.className},
                          {"enabled", binding.enabled},
                          {"ready", nativeScripts.Ready(binding.source)},
                          {"fields", std::move(fields)}});
  }

  // What add_component can attach here.
  const bool skinned = !mesh.skeleton.empty();
  json addable = json::array();
  for (const char *type : kComponentTypes) {
    const std::string t = type;
    if ((t == "movement" || t == "physics" || t == "collider") &&
        (mesh.isLight || mesh.isCamera)) continue;
    if ((t == "animation" || t == "spring_bones") && !skinned) continue;
    addable.push_back(t);
  }
  json scripts = json::array();
  for (const auto &source : nativeScripts.Sources())
    scripts.push_back({{"source", source},
                       {"className", std::filesystem::path(source).stem().string()},
                       {"ready", nativeScripts.Ready(source)},
                       {"diagnostics", nativeScripts.Diagnostics(source)},
                       {"attached", std::any_of(mesh.scripts.begin(), mesh.scripts.end(),
                                                [&](const auto &b) { return b.source == source; })}});
  return {{"index", i},
          {"name", mesh.displayName},
          {"components", std::move(components)},
          {"addable", std::move(addable)},
          {"movementScripts", ilmeee::MovementScriptNames()},
          {"projectScripts", std::move(scripts)},
          {"compiling", nativeScripts.IsCompiling()}};
}

ilmeee::net::json MainWindow::HandleMcpComponentTool(const std::string &name,
                                                     const ilmeee::net::json &args) {
  const size_t count = sceneRenderer->GetMesh3DCount();
  size_t i;
  if (!Index(args, "index", count, i))
    return Error("Invalid object index");
  LoadGameUiForProject();
  if (name == "list_components")
    return McpComponentList(i);
  if (SceneStructureLocked())
    return Error("Stop play mode before editing components");

  std::string type;
  if (!Optional(args, "type", type) || type.empty())
    return Error("type is required");
  if (std::none_of(std::begin(kComponentTypes), std::end(kComponentTypes),
                   [&](const char *t) { return type == t; }))
    return Error("Unknown component type: " + type);
  auto &mesh = sceneRenderer->meshes3d[i];

  // Script components are addressed by project-relative source or class name.
  const auto matchesScript = [](const ilmeee::NativeScriptBinding &b,
                                const std::string &key) {
    return b.source == key || b.className == key;
  };
  const auto saved = [&]() {
    json result = McpComponentList(i);
    result["saved"] = false;
    result["note"] = "Call save_scene to keep the change";
    return result;
  };

  if (name == "remove_component") {
    std::string script;
    if (!Optional(args, "script", script))
      return Error("script must be a string");
    if (type == "movement") {
      if (!HasMovement(mesh)) return Error("No movement component");
      mesh.movable = false;
      mesh.movementScript.clear();
      mesh.movementSpeed = 3.0f;
      mesh.movementRig = {};
    } else if (type == "physics") {
      if (!mesh.hasPhysics) return Error("No physics component");
      mesh.hasPhysics = false;
      mesh.velocity = glm::vec3(0.0f);
    } else if (type == "collider") {
      if (!mesh.collider.enabled) return Error("No collider component");
      mesh.collider.enabled = false;
    } else if (type == "audio_source") {
      if (!mesh.audio.enabled) return Error("No audio_source component");
      if (mesh.isAudioEmitter)
        return Error("This is an Audio Source object; delete the object instead");
      mesh.audio.enabled = false;
    } else if (type == "video_player") {
      if (!mesh.video.enabled) return Error("No video_player component");
      mesh.video.enabled = false;
    } else if (type == "dialog") {
      if (!mesh.dialog.enabled) return Error("No dialog component");
      mesh.dialog = {};
    } else if (type == "animation") {
      if (!HasAnimation(mesh)) return Error("No animation component");
      mesh.animation = {};
      mesh.loadedAnimationPath.clear();
      mesh.animationError.clear();
      mesh.animationTime = 0;
      mesh.locomotion = {};
    } else if (type == "spring_bones") {
      if (!mesh.springBones.enabled && mesh.springBones.chains.empty())
        return Error("No spring_bones component");
      mesh.springBones = {};
      mesh.springRuntime.Reset();
    } else { // script
      if (script.empty())
        return Error("script (source path or class name) is required");
      auto it = std::find_if(mesh.scripts.begin(), mesh.scripts.end(),
                             [&](const auto &b) { return matchesScript(b, script); });
      if (it == mesh.scripts.end())
        return Error("No script " + script + " on this object");
      mesh.scripts.erase(it);
    }
    RecordEditorHistory();
    return saved();
  }

  // add_component: attach, or update an attached component (merge).
  json props = json::object();
  if (auto it = args.find("properties"); it != args.end()) {
    if (!it->is_object()) return Error("properties must be an object");
    props = *it;
  }
  const auto badKey = [&](std::initializer_list<const char *> allowed) -> json {
    const std::string key = UnknownKey(props, allowed);
    if (key.empty()) return nullptr;
    std::string list;
    for (const char *k : allowed) list += (list.empty() ? "" : ", ") + std::string(k);
    return Error("Unknown " + type + " property '" + key + "' (allowed: " + list + ")");
  };
  const auto fitCollider = [&](ilmeee::ColliderComponent &collider) {
    glm::vec3 lo(-0.5f), hi(0.5f);
    sceneRenderer->GetMesh3DLocalBounds(i, lo, hi);
    ilmeee::FitColliderToBounds(collider, lo, hi);
  };

  // These already have dedicated tools; forward so validation stays in one place.
  if (type == "dialog" || type == "animation" || type == "spring_bones") {
    json forwarded = props;
    forwarded.erase("index");
    forwarded["index"] = i;
    if (type == "dialog") {
      if (!forwarded.contains("enabled")) forwarded["enabled"] = true;
      return HandleMcpTool("set_dialog", forwarded);
    }
    if (type == "spring_bones") {
      if (!forwarded.contains("auto") && !forwarded.contains("chains") &&
          mesh.springBones.chains.empty())
        forwarded["auto"] = true;
      if (!forwarded.contains("enabled")) forwarded["enabled"] = true;
      return HandleMcpTool("set_spring_bones", forwarded);
    }
    if (mesh.skeleton.empty())
      return Error("Animation needs a skinned (PMX) model with bones");
    if (!HasAnimation(mesh) && !forwarded.contains("clip") &&
        !forwarded.contains("controller"))
      return Error("animation needs properties.clip (.ilmeeeanim/.vmd) or "
                   "properties.controller (.ilmeeecontroller); create one with "
                   "create_animation or generate_animation");
    return HandleMcpTool("set_animation", forwarded);
  }

  if (type == "movement") {
    if (mesh.isLight || mesh.isCamera)
      return Error("Lights and cameras cannot take a movement component");
    if (json e = badKey({"movable", "script", "speed", "camera", "cameraStyle",
                         "distance", "height", "moveJoystick", "lookJoystick"});
        !e.is_null())
      return e;
    bool movable = true;
    std::string script = mesh.movementScript.empty() ? "WASD XZ" : mesh.movementScript;
    float speed = mesh.movementSpeed;
    ilmeee::MovementRig rig = mesh.movementRig;
    std::string style = kCameraStyles[(int)rig.style];
    if (!Optional(props, "movable", movable) || !Optional(props, "script", script) ||
        !Optional(props, "speed", speed) || !Optional(props, "cameraStyle", style) ||
        !Optional(props, "distance", rig.distance) ||
        !Optional(props, "height", rig.height) ||
        !Optional(props, "moveJoystick", rig.moveJoystick) ||
        !Optional(props, "lookJoystick", rig.lookJoystick))
      return Error("Invalid movement property type");
    const auto names = ilmeee::MovementScriptNames();
    if (std::find(names.begin(), names.end(), script) == names.end())
      return Error("Unknown movement script '" + script + "'; see movementScripts in list_components");
    if (speed < 0 || speed > 100) return Error("speed must be 0..100");
    if (rig.distance < 0 || rig.distance > 100 || rig.height < -10 || rig.height > 50)
      return Error("distance must be 0..100 and height -10..50");
    if (props.contains("camera")) {
      const json &c = props["camera"];
      if (c.is_number_integer() && c.get<std::int64_t>() == -1) {
        rig.camera = -1;
      } else {
        size_t cam;
        if (!Index(props, "camera", count, cam) || !sceneRenderer->IsMesh3DCamera(cam))
          return Error("camera must be -1 (Game view) or the index of a camera object");
        rig.camera = static_cast<int>(cam);
      }
    }
    const auto styleIt = std::find_if(std::begin(kCameraStyles), std::end(kCameraStyles),
                                      [&](const char *s) { return style == s; });
    if (styleIt == std::end(kCameraStyles))
      return Error("cameraStyle must be none, third_person, first_person, side_scroller or top_down");
    rig.style = static_cast<ilmeee::CameraStyle>(styleIt - std::begin(kCameraStyles));
    // Same pairing as the Inspector's POV Style combo.
    if (props.contains("cameraStyle") && !props.contains("script")) {
      if (rig.style == ilmeee::CameraStyle::SideScroller)
        script = "Platformer";
      else if (rig.style != ilmeee::CameraStyle::None && script == "WASD XZ")
        script = "Camera Relative";
    }
    for (const std::string *joystick : {&rig.moveJoystick, &rig.lookJoystick})
      if (!joystick->empty() &&
          std::none_of(gameUi.elements.begin(), gameUi.elements.end(), [&](const auto &e) {
            return e.kind == ilmeee::UiKind::Joystick && e.id == *joystick;
          }))
        return Error("No UI joystick named " + *joystick);
    mesh.movable = movable;
    mesh.movementScript = script;
    mesh.movementSpeed = speed;
    mesh.movementRig = rig;
  } else if (type == "physics") {
    if (json e = badKey({"useGravity", "isKinematic", "mass", "drag", "gravityY"});
        !e.is_null())
      return e;
    bool gravity = mesh.useGravity, kinematic = mesh.isKinematic;
    float mass = mesh.mass, drag = mesh.drag, gravityY = mesh.gravityY;
    if (!Optional(props, "useGravity", gravity) ||
        !Optional(props, "isKinematic", kinematic) || !Optional(props, "mass", mass) ||
        !Optional(props, "drag", drag) || !Optional(props, "gravityY", gravityY))
      return Error("Invalid physics property type");
    if (mass <= 0 || mass > 100000 || drag < 0 || drag > 1000 || std::abs(gravityY) > 1000)
      return Error("mass must be > 0, drag >= 0, |gravityY| <= 1000");
    if (mesh.isLight || mesh.isCamera)
      return Error("Lights and cameras cannot have physics");
    mesh.hasPhysics = true;
    mesh.useGravity = gravity;
    mesh.isKinematic = kinematic;
    mesh.mass = mass;
    mesh.drag = drag;
    mesh.gravityY = gravityY;
    // Same as the Inspector: a body without a collider falls through all.
    if (!mesh.collider.enabled) {
      mesh.collider.enabled = true;
      fitCollider(mesh.collider);
    }
  } else if (type == "collider") {
    if (json e = badKey({"shape", "center", "size", "radius", "height", "isTrigger",
                         "friction", "bounciness", "fit"});
        !e.is_null())
      return e;
    if (mesh.isLight || mesh.isCamera)
      return Error("Lights and cameras cannot have a collider");
    ilmeee::ColliderComponent c = mesh.collider;
    std::string shape = kColliderShapes[static_cast<int>(c.shape)];
    bool fit = !c.enabled; // a new collider starts sized to the mesh
    if (!Optional(props, "shape", shape) || !Optional(props, "fit", fit) ||
        !Vec3(props, "center", c.center) || !Vec3(props, "size", c.size) ||
        !Optional(props, "radius", c.radius) || !Optional(props, "height", c.height) ||
        !Optional(props, "isTrigger", c.isTrigger) ||
        !Optional(props, "friction", c.friction) ||
        !Optional(props, "bounciness", c.bounciness))
      return Error("Invalid collider property type");
    const auto shapeIt = std::find(std::begin(kColliderShapes), std::end(kColliderShapes), shape);
    if (shapeIt == std::end(kColliderShapes))
      return Error("shape must be box, sphere or capsule");
    if (glm::any(glm::lessThan(c.size, glm::vec3(0.0f))) || c.radius < 0 || c.height < 0 ||
        c.friction < 0 || c.friction > 1 || c.bounciness < 0 || c.bounciness > 1)
      return Error("size/radius/height must be >= 0, friction and bounciness 0..1");
    const auto newShape = static_cast<ilmeee::ColliderShape>(shapeIt - std::begin(kColliderShapes));
    if (newShape != c.shape && !props.contains("center") && !props.contains("size") &&
        !props.contains("radius") && !props.contains("height"))
      fit = true;
    c.shape = newShape;
    c.enabled = true;
    // Explicit dimensions win over the fitted ones.
    if (fit) {
      ilmeee::ColliderComponent fitted = c;
      fitCollider(fitted);
      if (!props.contains("center")) c.center = fitted.center;
      if (!props.contains("size")) c.size = fitted.size;
      if (!props.contains("radius")) c.radius = fitted.radius;
      if (!props.contains("height")) c.height = fitted.height;
    }
    c.Clamp();
    mesh.collider = c;
  } else if (type == "audio_source") {
    if (json e = badKey({"clip", "volume", "loop", "playOnStart", "spatial",
                         "minDistance", "maxDistance"});
        !e.is_null())
      return e;
    ilmeee::AudioSourceComponent a = mesh.audio;
    if (!Optional(props, "clip", a.clip) || !Optional(props, "volume", a.volume) ||
        !Optional(props, "loop", a.loop) || !Optional(props, "playOnStart", a.playOnStart) ||
        !Optional(props, "spatial", a.spatial) ||
        !Optional(props, "minDistance", a.minDistance) ||
        !Optional(props, "maxDistance", a.maxDistance))
      return Error("Invalid audio_source property type");
    if (!a.clip.empty() && !ilmeee::IsAudioClipPath(a.clip))
      return Error("clip must be .wav/.ogg/.mp3/.flac/.m4a/.aac/.opus");
    if (a.volume < 0 || a.volume > 2 || a.minDistance < 0 || a.maxDistance <= a.minDistance ||
        a.maxDistance > 1000)
      return Error("volume must be 0..2 and 0 <= minDistance < maxDistance <= 1000");
    if (std::filesystem::path(a.clip).is_absolute())
      a.clip = ProjectMediaPath(projectHandler.projectPath, a.clip);
    a.enabled = true;
    mesh.audio = std::move(a);
  } else if (type == "video_player") {
    if (json e = badKey({"clip", "loop", "playOnStart", "volume", "unlit", "surface"});
        !e.is_null())
      return e;
    ilmeee::VideoPlayerComponent v = mesh.video;
    if (!Optional(props, "clip", v.clip) || !Optional(props, "loop", v.loop) ||
        !Optional(props, "playOnStart", v.playOnStart) ||
        !Optional(props, "volume", v.volume) || !Optional(props, "unlit", v.unlit))
      return Error("Invalid video_player property type");
    if (props.contains("surface")) {
      const json &s = props["surface"];
      size_t surface;
      if (s.is_number_integer() && s.get<std::int64_t>() == -1)
        v.surface = -1;
      else if (Index(props, "surface", sceneRenderer->GetMesh3DSubmeshCount(i), surface))
        v.surface = static_cast<int>(surface);
      else
        return Error("surface must be -1 (all) or a submesh index");
    }
    if (!v.clip.empty() && !ilmeee::IsVideoClipPath(v.clip))
      return Error("clip must be .mp4/.mkv/.webm/.mov/.avi");
    if (v.volume < 0 || v.volume > 2) return Error("volume must be 0..2");
    if (std::filesystem::path(v.clip).is_absolute())
      v.clip = ProjectMediaPath(projectHandler.projectPath, v.clip);
    v.enabled = true;
    mesh.video = std::move(v);
  } else { // script
    if (json e = badKey({"source", "enabled", "values"}); !e.is_null())
      return e;
    std::string source;
    if (!Optional(props, "source", source) || source.empty())
      return Error("properties.source (project-relative .cpp or class name) is required; "
                   "see projectScripts in list_components");
    auto binding = std::find_if(mesh.scripts.begin(), mesh.scripts.end(),
                                [&](const auto &b) { return matchesScript(b, source); });
    ilmeee::NativeScriptBinding updated;
    if (binding != mesh.scripts.end()) {
      updated = *binding;
    } else {
      const auto sources = nativeScripts.Sources();
      auto it = std::find_if(sources.begin(), sources.end(), [&](const std::string &s) {
        return s == source || std::filesystem::path(s).stem().string() == source;
      });
      if (it == sources.end())
        return Error(nativeScripts.IsCompiling()
                         ? "Scripts are compiling; retry shortly"
                         : "No project script " + source + "; see projectScripts in list_components");
      if (!nativeScripts.Ready(*it))
        return Error(nativeScripts.IsCompiling()
                         ? "Script is compiling; retry shortly"
                         : "Script failed to compile; see get_game_log or Console > Output");
      updated.source = *it;
      updated.className = std::filesystem::path(*it).stem().string();
      updated.values = nativeScripts.Properties(updated);
    }
    if (!Optional(props, "enabled", updated.enabled))
      return Error("enabled must be a boolean");
    if (auto it = props.find("values"); it != props.end()) {
      if (!it->is_object()) return Error("values must be an object {field: value}");
      const ObjectResolver resolve = [&](const json &in, uint64_t &out) {
        if (in.is_null()) { out = 0; return true; }
        if (in.is_string()) {
          const std::string id = in.get<std::string>();
          for (const auto &e : gameUi.elements)
            if (e.id == id) { out = e.objectId; return true; }
          return false;
        }
        size_t target;
        if (!Index(json{{"i", in}}, "i", count, target)) return false;
        out = sceneRenderer->meshes3d[target].objectId;
        return true;
      };
      auto fields = nativeScripts.Properties(updated);
      for (const auto &[field, value] : it->items()) {
        auto f = std::find_if(fields.begin(), fields.end(),
                              [&](const auto &v) { return v.name == field; });
        if (f == fields.end())
          return Error("Script " + updated.className + " has no field " + field);
        ilmeee::ScriptValue next = *f;
        std::string error;
        if (next.type == ilmeee::ScriptValueType::List) {
          if (!value.is_array() || value.size() > ilmeee::kMaxScriptListItems)
            return Error("Field " + field + " expects an array");
          next.items.clear();
          for (const auto &item : value) {
            ilmeee::ScriptValue element;
            element.type = next.elementType;
            if (!(error = ScalarFromJson(next.elementType, item, element, resolve)).empty())
              return Error("Field " + field + " item " + error);
            next.items.push_back(std::move(element));
          }
        } else if (!(error = ScalarFromJson(next.type, value, next, resolve)).empty()) {
          return Error("Field " + field + " " + error);
        }
        auto stored = std::find_if(updated.values.begin(), updated.values.end(),
                                   [&](const auto &v) { return v.name == field; });
        if (stored == updated.values.end())
          updated.values.push_back(std::move(next));
        else
          *stored = std::move(next);
      }
    }
    if (binding != mesh.scripts.end())
      *binding = std::move(updated);
    else
      mesh.scripts.push_back(std::move(updated));
  }
  RecordEditorHistory();
  return saved();
}
