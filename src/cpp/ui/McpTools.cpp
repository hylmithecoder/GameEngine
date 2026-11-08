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
#include <spawn.h>
#include <sstream>
#include <sys/wait.h>
#include <type_traits>
#include <unistd.h>

extern char **environ;

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

const char *UiAnchorName(ilmeee::UiAnchor anchor) {
  switch (anchor) {
  case ilmeee::UiAnchor::TopLeft: return "top_left";
  case ilmeee::UiAnchor::TopRight: return "top_right";
  case ilmeee::UiAnchor::BottomLeft: return "bottom_left";
  case ilmeee::UiAnchor::BottomRight: return "bottom_right";
  case ilmeee::UiAnchor::Center: return "center";
  case ilmeee::UiAnchor::TopCenter: return "top_center";
  case ilmeee::UiAnchor::BottomCenter: return "bottom_center";
  case ilmeee::UiAnchor::CenterLeft: return "center_left";
  case ilmeee::UiAnchor::CenterRight: return "center_right";
  case ilmeee::UiAnchor::StretchTop: return "stretch_top";
  case ilmeee::UiAnchor::StretchBottom: return "stretch_bottom";
  case ilmeee::UiAnchor::StretchCenter: return "stretch_center";
  case ilmeee::UiAnchor::StretchFull: return "stretch_full";
  }
  return "top_left";
}

bool ParseUiAnchor(const std::string &s, ilmeee::UiAnchor &out) {
  if (s == "top_left") { out = ilmeee::UiAnchor::TopLeft; return true; }
  if (s == "top_right") { out = ilmeee::UiAnchor::TopRight; return true; }
  if (s == "bottom_left") { out = ilmeee::UiAnchor::BottomLeft; return true; }
  if (s == "bottom_right") { out = ilmeee::UiAnchor::BottomRight; return true; }
  if (s == "center") { out = ilmeee::UiAnchor::Center; return true; }
  if (s == "top_center") { out = ilmeee::UiAnchor::TopCenter; return true; }
  if (s == "bottom_center") { out = ilmeee::UiAnchor::BottomCenter; return true; }
  if (s == "center_left") { out = ilmeee::UiAnchor::CenterLeft; return true; }
  if (s == "center_right") { out = ilmeee::UiAnchor::CenterRight; return true; }
  if (s == "stretch_top") { out = ilmeee::UiAnchor::StretchTop; return true; }
  if (s == "stretch_bottom") { out = ilmeee::UiAnchor::StretchBottom; return true; }
  if (s == "stretch_center") { out = ilmeee::UiAnchor::StretchCenter; return true; }
  if (s == "stretch_full" || s == "stretch") { out = ilmeee::UiAnchor::StretchFull; return true; }
  return false;
}

bool Color4(const json &args, const char *key, ImVec4 &out) {
  auto it = args.find(key);
  if (it == args.end())
    return true;
  if (!it->is_array() || (it->size() != 3 && it->size() != 4))
    return false;
  float v[4] = {out.x, out.y, out.z, 1.0f};
  for (size_t k = 0; k < it->size(); ++k) {
    if (!(*it)[k].is_number())
      return false;
    const double d = (*it)[k].get<double>();
    if (!std::isfinite(d))
      return false;
    v[k] = std::clamp(static_cast<float>(d), 0.0f, 1.0f);
  }
  out = ImVec4(v[0], v[1], v[2], v[3]);
  return true;
}

bool RunProcessCapture(const std::vector<std::string> &argv, std::string &output,
                       int &exitCode) {
  output.clear();
  exitCode = -1;
  if (argv.empty())
    return false;
  int pipefd[2];
  if (pipe(pipefd) != 0)
    return false;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_addclose(&actions, pipefd[0]);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, pipefd[1]);
  std::vector<char *> cargs;
  cargs.reserve(argv.size() + 1);
  for (const auto &s : argv)
    cargs.push_back(const_cast<char *>(s.c_str()));
  cargs.push_back(nullptr);
  pid_t pid = -1;
  const int rc = posix_spawnp(&pid, argv[0].c_str(), &actions, nullptr,
                              cargs.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  close(pipefd[1]);
  if (rc != 0) {
    close(pipefd[0]);
    return false;
  }
  char buf[4096];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
    if (output.size() < 512 * 1024)
      output.append(buf, static_cast<size_t>(n));
  }
  close(pipefd[0]);
  int status = 0;
  if (waitpid(pid, &status, 0) < 0)
    return false;
  if (WIFEXITED(status)) {
    exitCode = WEXITSTATUS(status);
    return exitCode == 0;
  }
  return false;
}

bool SafeArchiveEntries(const std::string &listing, std::string &badEntry) {
  std::istringstream ss(listing);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    if (line[0] == '/' || line[0] == '\\' || line.find('\\') != std::string::npos) {
      badEntry = line;
      return false;
    }
    std::filesystem::path p(line);
    for (const auto &part : p) {
      if (part == "..") {
        badEntry = line;
        return false;
      }
    }
  }
  return true;
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
  if (name == "list_assets") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    std::string category = "all", contains;
    if (!Optional(args, "category", category) ||
        !Optional(args, "contains", contains))
      return Error("Invalid argument type");
    if (category != "all" && category != "model" && category != "audio" &&
        category != "video" && category != "image" &&
        category != "animation" && category != "script")
      return Error("category must be all, model, audio, video, image, animation or script");
    std::string containsLower = contains;
    std::transform(containsLower.begin(), containsLower.end(),
                   containsLower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    const std::filesystem::path root(projectHandler.projectPath);
    std::error_code ec;
    json assets = json::array();
    auto classify = [](const std::string &ext) -> const char * {
      if (ext == ".pmx" || ext == ".fbx" || ext == ".obj")
        return "model";
      if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".flac" ||
          ext == ".m4a" || ext == ".aac" || ext == ".opus")
        return "audio";
      if (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".mov" ||
          ext == ".avi" || ext == ".m3u8")
        return "video";
      if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
          ext == ".tga" || ext == ".gif" || ext == ".psd")
        return "image";
      if (ext == ".ilmeeeanim" || ext == ".vmd" || ext == ".ilmeeecontroller")
        return "animation";
      if (ext == ".cpp" || ext == ".hpp")
        return "script";
      return nullptr;
    };
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied,
             ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
      const auto fname = it->path().filename().string();
      if (it->is_directory(ec)) {
        if (fname == "build" || fname == ".git" ||
            fname == ".native-script-build")
          it.disable_recursion_pending();
        continue;
      }
      if (!it->is_regular_file(ec))
        continue;
      std::string ext = it->path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      const char *cat = classify(ext);
      if (!cat)
        continue;
      if (category != "all" && category != cat)
        continue;
      const std::string rel =
          it->path().lexically_relative(root).generic_string();
      if (!containsLower.empty()) {
        std::string relLower = rel;
        std::transform(relLower.begin(), relLower.end(), relLower.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        if (relLower.find(containsLower) == std::string::npos)
          continue;
      }
      assets.push_back(
          {{"path", rel}, {"category", cat}, {"extension", ext}});
    }
    return {{"projectPath", projectHandler.projectPath},
            {"assets", std::move(assets)}};
  }
  if (name == "download_asset") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    if (const std::string bad = UnknownKey(
            args, {"url", "destination", "extract", "overwrite", "spawn",
                   "name", "position", "rotation", "scale", "parent"});
        !bad.empty())
      return Error("Unknown argument '" + bad + "'");
    std::string url, destination, spawnName;
    bool extract = true, overwrite = false, spawn = false;
    if (!Optional(args, "url", url) ||
        !Optional(args, "destination", destination) ||
        !Optional(args, "extract", extract) ||
        !Optional(args, "overwrite", overwrite) ||
        !Optional(args, "spawn", spawn) ||
        !Optional(args, "name", spawnName))
      return Error("Invalid argument type");
    if (!ilmeee::IsMediaUrl(url))
      return Error("url must start with http:// or https://");
    for (unsigned char c : url) {
      if (c <= 0x20 || c == 0x7f)
        return Error("url must not contain spaces or control characters");
    }
    if (spawn) {
      if (!sceneRenderer)
        return Error("Scene is not ready");
      if (SceneStructureLocked())
        return Error("Stop play mode before spawning downloaded models into the scene");
    }

    auto urlFilename = [](const std::string &u) -> std::string {
      const auto schemePos = u.find("://");
      const auto pathStart = u.find('/', schemePos == std::string::npos ? 0 : schemePos + 3);
      if (pathStart == std::string::npos)
        return "asset.bin";
      const auto endPos = u.find_first_of("?#", pathStart);
      std::string urlPath = u.substr(
          pathStart, endPos == std::string::npos ? std::string::npos : endPos - pathStart);
      std::string fname = std::filesystem::path(urlPath).filename().string();
      // Sanitize characters outside safe filename set.
      std::string clean;
      for (unsigned char c : fname) {
        if (std::isalnum(c) || c == '.' || c == '_' || c == '-')
          clean.push_back((char)c);
        else
          clean.push_back('_');
      }
      while (!clean.empty() && clean.front() == '.')
        clean.erase(clean.begin());
      return clean.empty() ? "asset.bin" : clean;
    };

    const std::string defaultFile = urlFilename(url);
    auto lowerStr = [](std::string s) {
      std::transform(s.begin(), s.end(), s.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      return s;
    };
    const std::string lowerFile = lowerStr(defaultFile);
    auto isArchiveName = [&](const std::string &n) {
      const std::string l = lowerStr(n);
      auto endsWith = [&](const char *suf) {
        const size_t len = std::strlen(suf);
        return l.size() >= len && l.compare(l.size() - len, len, suf) == 0;
      };
      return endsWith(".zip") || endsWith(".tar.gz") || endsWith(".tgz") ||
             endsWith(".tar.xz") || endsWith(".tar");
    };

    const std::filesystem::path root =
        std::filesystem::path(projectHandler.projectPath).lexically_normal();
    const std::filesystem::path assetsRoot = (root / "assets").lexically_normal();
    std::filesystem::path targetPath;
    if (destination.empty()) {
      const std::string ext = lowerStr(std::filesystem::path(defaultFile).extension().string());
      std::string subdir = "downloads";
      if (ext == ".pmx" || ext == ".fbx" || ext == ".obj" || isArchiveName(defaultFile))
        subdir = "models";
      else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
               ext == ".tga" || ext == ".gif" || ext == ".psd")
        subdir = "textures";
      else if (ilmeee::IsAudioClipPath(defaultFile))
        subdir = "audio";
      else if (ilmeee::IsVideoClipPath(defaultFile))
        subdir = "video";
      else if (ext == ".ilmeeeanim" || ext == ".vmd" || ext == ".ilmeeecontroller")
        subdir = "animations";
      targetPath = assetsRoot / subdir / defaultFile;
    } else {
      std::filesystem::path rel(destination);
      if (rel.is_absolute())
        return Error("destination must be a project-relative path under assets/");
      // Prepend assets/ if caller gave e.g. "models/city.obj".
      std::string generic = rel.generic_string();
      if (generic != "assets" && generic.rfind("assets/", 0) != 0)
        rel = std::filesystem::path("assets") / rel;
      targetPath = (root / rel).lexically_normal();
      std::error_code ec;
      if ((!generic.empty() && generic.back() == '/') ||
          std::filesystem::is_directory(targetPath, ec) ||
          targetPath.extension().empty()) {
        targetPath = (targetPath / defaultFile).lexically_normal();
      }
    }

    // Verify targetPath stays strictly under <project>/assets/.
    const std::string relToAssets =
        targetPath.lexically_relative(assetsRoot).generic_string();
    if (relToAssets.empty() || relToAssets == "." ||
        relToAssets.rfind("..", 0) == 0)
      return Error("destination must stay inside the project's assets/ directory");

    std::error_code ec;
    if (std::filesystem::exists(targetPath, ec) && !overwrite)
      return Error("Destination already exists: " +
                   targetPath.lexically_relative(root).generic_string() +
                   " (pass overwrite=true to replace)");
    std::filesystem::create_directories(targetPath.parent_path(), ec);
    if (ec || std::filesystem::is_symlink(targetPath.parent_path(), ec))
      return Error("Could not create destination directory");

    std::string curlOut;
    int curlCode = -1;
    if (!RunProcessCapture({"curl", "-fL", "-sS", "--max-time", "90",
                            "--max-filesize", "536870912", "-o",
                            targetPath.string(), "--", url},
                           curlOut, curlCode)) {
      std::filesystem::remove(targetPath, ec);
      return Error("Download failed (curl exit " + std::to_string(curlCode) +
                   "): " + (curlOut.empty() ? url : curlOut));
    }

    const auto downloadedBytes = std::filesystem::file_size(targetPath, ec);
    const std::string relDownloaded =
        targetPath.lexically_relative(root).generic_string();

    json extractedFiles = json::array();
    json discoveredModels = json::array();
    json discoveredTextures = json::array();
    bool wasExtracted = false;

    const std::string targetName = targetPath.filename().string();
    if (extract && isArchiveName(targetName)) {
      const bool isZip = lowerStr(targetPath.extension().string()) == ".zip";
      std::string listOut;
      int listCode = -1;
      const bool listed =
          isZip ? RunProcessCapture({"unzip", "-Z1", "--", targetPath.string()},
                                    listOut, listCode)
                : RunProcessCapture({"tar", "-tf", targetPath.string()},
                                    listOut, listCode);
      if (!listed)
        return Error("Failed to inspect downloaded archive: " + listOut);
      std::string badEntry;
      if (!SafeArchiveEntries(listOut, badEntry)) {
        std::filesystem::remove(targetPath, ec);
        return Error("Refusing to extract archive with unsafe path entry: " +
                     badEntry);
      }
      // Extract into a subdirectory named after the archive stem (or parent if destination was specific).
      std::string stem = targetPath.stem().string();
      if (lowerStr(std::filesystem::path(stem).extension().string()) == ".tar")
        stem = std::filesystem::path(stem).stem().string();
      const std::filesystem::path extractDir =
          (targetPath.parent_path() / stem).lexically_normal();
      std::filesystem::create_directories(extractDir, ec);
      std::string extOut;
      int extCode = -1;
      const bool extracted =
          isZip ? RunProcessCapture({"unzip", "-q", "-o", "--",
                                     targetPath.string(), "-d",
                                     extractDir.string()},
                                    extOut, extCode)
                : RunProcessCapture({"tar", "-xf", targetPath.string(), "-C",
                                     extractDir.string()},
                                    extOut, extCode);
      if (!extracted)
        return Error("Archive extraction failed: " + extOut);
      wasExtracted = true;
      for (auto it = std::filesystem::recursive_directory_iterator(
               extractDir,
               std::filesystem::directory_options::skip_permission_denied, ec);
           !ec && it != std::filesystem::recursive_directory_iterator();
           it.increment(ec)) {
        if (!it->is_regular_file(ec))
          continue;
        const std::string rel =
            it->path().lexically_relative(root).generic_string();
        extractedFiles.push_back(rel);
        const std::string ext = lowerStr(it->path().extension().string());
        if (ext == ".pmx" || ext == ".fbx" || ext == ".obj")
          discoveredModels.push_back(rel);
        else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                 ext == ".bmp" || ext == ".tga" || ext == ".gif" ||
                 ext == ".psd")
          discoveredTextures.push_back(rel);
      }
    } else {
      const std::string ext = lowerStr(targetPath.extension().string());
      if (ext == ".pmx" || ext == ".fbx" || ext == ".obj")
        discoveredModels.push_back(relDownloaded);
      else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp" || ext == ".tga" || ext == ".gif" ||
               ext == ".psd")
        discoveredTextures.push_back(relDownloaded);
    }

    // Refresh Explorer asset tree so newly downloaded files appear immediately.
    projectHandler.rootAsset =
        projectHandler.BuildAssetTree(projectHandler.projectPath);

    json out = {{"ok", true},
                {"url", url},
                {"path", relDownloaded},
                {"bytes", ec ? 0 : downloadedBytes},
                {"extracted", wasExtracted},
                {"extractedFiles", std::move(extractedFiles)},
                {"models", discoveredModels},
                {"textures", std::move(discoveredTextures)}};

    if (spawn) {
      if (discoveredModels.empty())
        return Error("Asset downloaded to " + relDownloaded +
                     ", but no .pmx/.fbx/.obj 3D model was found to spawn");
      json spawnArgs = {{"kind", "model"},
                        {"path", discoveredModels.front().get<std::string>()}};
      if (!spawnName.empty())
        spawnArgs["name"] = spawnName;
      for (const char *k : {"position", "rotation", "scale", "parent"})
        if (args.contains(k))
          spawnArgs[k] = args[k];
      json spawned = HandleMcpTool("create_object", spawnArgs);
      if (spawned.contains("error"))
        return spawned;
      out["spawned"] = std::move(spawned);
    }
    return out;
  }
  if (name == "list_ui_elements") {
    LoadGameUiForProject();
    json elements = json::array();
    for (size_t i = 0; i < gameUi.elements.size(); ++i) {
      const auto &e = gameUi.elements[i];
      elements.push_back({{"index", i},
                          {"name", e.id},
                          {"type", UiTypeName(e.kind)},
                          {"anchor", UiAnchorName(e.anchor)},
                          {"visible", e.visible},
                          {"position", json::array({e.x, e.y})},
                          {"size", json::array({e.width, e.height})},
                          {"fontSize", e.fontSize},
                          {"color", json::array({e.color.x, e.color.y, e.color.z, e.color.w})},
                          {"background", json::array({e.background.x, e.background.y, e.background.z, e.background.w})},
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
    std::string modelPath;
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
      else if (!path.empty())
        modelPath = ProjectMediaPath(projectHandler.projectPath, path);
    }
    json surfaces = json::array();
    const uint32_t subCount = sceneRenderer->GetMesh3DSubmeshCount(i);
    for (uint32_t s = 0; s < subCount; ++s) {
      std::string tex = sceneRenderer->GetMesh3DSubmeshTexture(i, s);
      if (!tex.empty() && std::filesystem::path(tex).is_absolute())
        tex = ProjectMediaPath(projectHandler.projectPath, tex);
      surfaces.push_back({{"index", s},
                          {"name", sceneRenderer->GetMesh3DSubmeshName(i, s)},
                          {"texture", std::move(tex)}});
    }
    glm::vec3 bMin(-0.5f), bMax(0.5f);
    sceneRenderer->GetMesh3DLocalBounds(i, bMin, bMax);
    json out = {{"index", i},
                {"name", sceneRenderer->GetMesh3DName(i)},
                {"kind", kind},
                {"parent", sceneRenderer->GetMesh3DParent(i)},
                {"position", Array(sceneRenderer->GetMesh3DPosition(i))},
                {"rotation", Array(sceneRenderer->GetMesh3DRotation(i))},
                {"scale", Array(sceneRenderer->GetMesh3DScale(i))},
                {"bounds", {{"min", Array(bMin)},
                            {"max", Array(bMax)},
                            {"size", Array(bMax - bMin)}}},
                {"hasDialog", sceneRenderer->meshes3d[i].dialog.enabled},
                {"bones", sceneRenderer->meshes3d[i].skeleton.size()},
                {"surfaces", std::move(surfaces)},
                {"animation", {{"enabled", sceneRenderer->meshes3d[i].animation.enabled},
                               {"clip", sceneRenderer->meshes3d[i].animation.clip},
                               {"controller", sceneRenderer->meshes3d[i].animation.controller},
                               {"loop", sceneRenderer->meshes3d[i].animation.loop},
                               {"speed", sceneRenderer->meshes3d[i].animation.speed},
                               {"time", sceneRenderer->meshes3d[i].animationTime},
                               {"error", sceneRenderer->meshes3d[i].animationError}}},
                {"springBones", sceneRenderer->meshes3d[i].springBones.enabled},
                {"components", AttachedComponents(sceneRenderer->meshes3d[i])}};
    if (!modelPath.empty())
      out["path"] = std::move(modelPath);
    if (sceneRenderer->IsMesh3DLight(i)) {
      const int lt = sceneRenderer->GetMesh3DLightType(i);
      const char *ltName = (lt == 0) ? "directional" : (lt == 2) ? "spot" : "point";
      out["light"] = {{"type", ltName},
                      {"color", Array(sceneRenderer->GetMesh3DLightColor(i))},
                      {"intensity", sceneRenderer->GetMesh3DLightIntensity(i)},
                      {"range", sceneRenderer->GetMesh3DLightRange(i)},
                      {"spotAngle", sceneRenderer->GetMesh3DLightSpotAngle(i)},
                      {"gamma", sceneRenderer->GetMesh3DLightGamma(i)}};
    } else if (sceneRenderer->IsMesh3DCamera(i)) {
      const int cp = sceneRenderer->GetMesh3DCameraProjection(i);
      out["camera"] = {{"projection", cp == 1 ? "orthographic" : "perspective"},
                       {"fov", sceneRenderer->GetMesh3DCameraFov(i)},
                       {"orthoSize", sceneRenderer->GetMesh3DCameraOrthoSize(i)},
                       {"near", sceneRenderer->GetMesh3DCameraNear(i)},
                       {"far", sceneRenderer->GetMesh3DCameraFar(i)}};
    } else if (sceneRenderer->meshes3d[i].isWorldText) {
      const auto &wt = sceneRenderer->meshes3d[i].worldText;
      const char *align = (wt.alignment == 0) ? "left" : (wt.alignment == 2) ? "right" : "center";
      out["text3d"] = {{"text", wt.text},
                       {"size", wt.size},
                       {"color", json::array({wt.color.r, wt.color.g, wt.color.b, wt.color.a})},
                       {"alignment", align},
                       {"visible", wt.visible}};
    }
    return out;
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
  const auto envJson = [&]() -> json {
    const glm::vec4 bg = sceneRenderer->GetBackgroundColor();
    const auto &npr = sceneRenderer->GetNpr();
    return {{"backgroundColor", json::array({bg.r, bg.g, bg.b, bg.a})},
            {"sunDirection", Array(sceneRenderer->sunLight.direction)},
            {"sunColor", Array(sceneRenderer->sunLight.color)},
            {"sunIntensity", sceneRenderer->sunLight.intensity},
            {"sunVisible", sceneRenderer->IsSunVisible()},
            {"gridVisible", sceneRenderer->IsGrid3DVisible()},
            {"npr",
             {{"enabled", npr.enabled},
              {"bands", npr.bands},
              {"softness", npr.softness},
              {"shadowColor", Array(npr.shadowColor)},
              {"rim", npr.rim},
              {"rimColor", Array(npr.rimColor)},
              {"rimWidth", npr.rimWidth},
              {"rimStrength", npr.rimStrength},
              {"specular", npr.specular},
              {"specularSize", npr.specularSize},
              {"specularStrength", npr.specularStrength},
              {"outline", npr.outline},
              {"outlineColor", Array(npr.outlineColor)},
              {"outlineWidth", npr.outlineWidth}}}};
  };
  if (name == "get_scene_environment")
    return envJson();
  if (name == "set_play_mode") {
    std::string state;
    if (!Optional(args, "state", state) || state.empty())
      return Error("state is required (play, pause or stop)");
    if (state == "play") {
      if (builder.IsStopped()) {
        if (nativeScripts.IsCompiling())
          return Error("Scripts are still compiling; wait and retry");
        builder.Play();
        focusGameTab = true;
        showConsole = true;
        focusOutputTab = true;
        PushMessage("[INFO] Game started via MCP");
      } else if (builder.IsPaused()) {
        builder.Resume();
        focusGameTab = true;
        PushMessage("[INFO] Game resumed via MCP");
      }
    } else if (state == "pause") {
      if (builder.IsPlaying()) {
        builder.Pause();
        PushMessage("[INFO] Game paused via MCP");
      } else if (builder.IsStopped()) {
        return Error("Game is stopped; start Play mode first");
      }
    } else if (state == "stop") {
      if (!builder.IsStopped()) {
        nativeScripts.Stop();
        builder.Stop();
        focusSceneTab = true;
        PushMessage("[INFO] Game stopped via MCP");
      }
    } else {
      return Error("state must be play, pause or stop");
    }
    const char *cur = builder.IsPlaying()  ? "play"
                      : builder.IsPaused() ? "pause"
                                           : "stop";
    return {{"state", cur},
            {"playing", builder.IsPlaying()},
            {"paused", builder.IsPaused()},
            {"stopped", builder.IsStopped()}};
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
  if (name == "create_ui_element") {
    LoadGameUiForProject();
    std::string typeStr, customName;
    if (!Optional(args, "type", typeStr) || typeStr.empty() ||
        !Optional(args, "name", customName))
      return Error("type is required (text, image, button or joystick)");
    if (!hasGameUiCanvas) {
      hasGameUiCanvas = true;
      gameUi.elements.clear();
    }
    ilmeee::UiElement e;
    std::string baseName = "Text";
    if (typeStr == "text") {
      e.kind = ilmeee::UiKind::Text;
      e.text = "New Text";
      e.width = 240.0f;
      e.height = 44.0f;
      baseName = "Text";
    } else if (typeStr == "image") {
      e.kind = ilmeee::UiKind::Image;
      e.width = 160.0f;
      e.height = 160.0f;
      baseName = "Image";
    } else if (typeStr == "button") {
      e.kind = ilmeee::UiKind::Button;
      e.text = "Button";
      e.width = 180.0f;
      e.height = 48.0f;
      baseName = "Button";
    } else if (typeStr == "joystick") {
      e.kind = ilmeee::UiKind::Joystick;
      e.anchor = ilmeee::UiAnchor::BottomLeft;
      e.x = 40.0f;
      e.y = -40.0f;
      e.width = 160.0f;
      e.height = 160.0f;
      baseName = "Joystick";
    } else {
      return Error("type must be text, image, button or joystick");
    }
    const std::string desired = customName.empty() ? baseName : customName;
    auto taken = [&](const std::string &id) {
      for (const auto &item : gameUi.elements)
        if (item.id == id)
          return true;
      return false;
    };
    std::string uniqueId = desired;
    for (int n = 1; taken(uniqueId); ++n)
      uniqueId = desired + " " + std::to_string(n);
    e.id = uniqueId;
    std::string anchorStr;
    float fontSize = e.fontSize;
    if (!Optional(args, "text", e.text) ||
        !Optional(args, "action", e.action) ||
        !Optional(args, "image", e.imagePath) ||
        !Optional(args, "anchor", anchorStr) ||
        !Optional(args, "visible", e.visible) ||
        !Optional(args, "fontSize", fontSize) ||
        !Vec2(args, "position", e.x, e.y) ||
        !Vec2(args, "size", e.width, e.height) ||
        !Color4(args, "color", e.color) ||
        !Color4(args, "background", e.background))
      return Error("Invalid argument type");
    if (!anchorStr.empty() && !ParseUiAnchor(anchorStr, e.anchor))
      return Error("Invalid anchor value");
    if (e.width < 1 || e.height < 1 || fontSize < 1 || fontSize > 512)
      return Error("size must be >= 1 and fontSize 1..512");
    e.fontSize = fontSize;
    if (!e.imagePath.empty() && std::filesystem::path(e.imagePath).is_absolute())
      e.imagePath = ProjectMediaPath(projectHandler.projectPath, e.imagePath);
    gameUi.elements.push_back(e);
    selectedUiElement = static_cast<int>(gameUi.elements.size()) - 1;
    RecordEditorHistory();
    return {{"index", gameUi.elements.size() - 1},
            {"name", e.id},
            {"type", UiTypeName(e.kind)},
            {"anchor", UiAnchorName(e.anchor)},
            {"visible", e.visible},
            {"position", json::array({e.x, e.y})},
            {"size", json::array({e.width, e.height})},
            {"fontSize", e.fontSize},
            {"color", json::array({e.color.x, e.color.y, e.color.z, e.color.w})},
            {"background", json::array({e.background.x, e.background.y, e.background.z, e.background.w})},
            {"text", e.text},
            {"action", e.action},
            {"image", e.imagePath},
            {"saved", false},
            {"note", "Call save_scene to keep the change"}};
  }
  if (name == "delete_ui_element") {
    LoadGameUiForProject();
    bool clearCanvas = false;
    std::string elementName;
    if (!Optional(args, "clearCanvas", clearCanvas) ||
        !Optional(args, "name", elementName))
      return Error("Invalid argument type");
    if (clearCanvas) {
      gameUi.elements.clear();
      hasGameUiCanvas = false;
      selectedUiElement = -1;
      for (auto &mesh : sceneRenderer->meshes3d) {
        mesh.dialog.box = 0;
        mesh.dialog.text = 0;
        mesh.dialog.speaker = 0;
        mesh.dialog.prompt = 0;
      }
      RecordEditorHistory();
      return {{"clearedCanvas", true}, {"saved", false}};
    }
    if (elementName.empty())
      return Error("Provide name or clearCanvas=true");
    auto it = std::find_if(gameUi.elements.begin(), gameUi.elements.end(),
                           [&](const auto &e) { return e.id == elementName; });
    if (it == gameUi.elements.end())
      return Error("No UI element named " + elementName);
    const uint64_t removedId = it->objectId;
    gameUi.elements.erase(it);
    selectedUiElement = -1;
    for (auto &mesh : sceneRenderer->meshes3d) {
      if (mesh.dialog.box == removedId) mesh.dialog.box = 0;
      if (mesh.dialog.text == removedId) mesh.dialog.text = 0;
      if (mesh.dialog.speaker == removedId) mesh.dialog.speaker = 0;
      if (mesh.dialog.prompt == removedId) mesh.dialog.prompt = 0;
    }
    RecordEditorHistory();
    return {{"deleted", elementName},
            {"remainingElements", gameUi.elements.size()},
            {"saved", false}};
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
    std::string anchorStr;
    if (!Optional(args, "text", e.text) || !Optional(args, "action", e.action) ||
        !Optional(args, "image", e.imagePath) ||
        !Optional(args, "anchor", anchorStr) ||
        !Optional(args, "visible", e.visible) ||
        !Optional(args, "fontSize", fontSize) ||
        !Vec2(args, "position", e.x, e.y) ||
        !Vec2(args, "size", e.width, e.height) ||
        !Color4(args, "color", e.color) ||
        !Color4(args, "background", e.background))
      return Error("Invalid argument type");
    if (!anchorStr.empty()) {
      ilmeee::UiAnchor nextAnchor = e.anchor;
      if (!ParseUiAnchor(anchorStr, nextAnchor))
        return Error("Invalid anchor value");
      if (!args.contains("position"))
        ilmeee::SetUiElementAnchor(gameUi, e, nextAnchor);
      else
        e.anchor = nextAnchor;
    }
    if (e.width < 1 || e.height < 1 || fontSize < 1 || fontSize > 512)
      return Error("size must be >= 1 and fontSize 1..512");
    e.fontSize = fontSize;
    // Relative paths are already project-relative; absolute ones inside the
    // project are made relative so built games find them.
    if (std::filesystem::path(e.imagePath).is_absolute())
      e.imagePath = ProjectMediaPath(projectHandler.projectPath, e.imagePath);
    *it = std::move(e);
    RecordEditorHistory();
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
    std::string rawPath, customName;
    if (!Optional(args, "path", rawPath) || !Optional(args, "name", customName))
      return Error("Invalid argument type");
    glm::vec3 position(0.0f), rotation(0.0f), scale(1.0f);
    if (!Vec3(args, "position", position) ||
        !Vec3(args, "rotation", rotation) || !Vec3(args, "scale", scale))
      return Error("Invalid transform vector");
    if (scale.x <= 0 || scale.y <= 0 || scale.z <= 0)
      return Error("Scale values must be positive");
    int parent = -1;
    if (args.contains("parent")) {
      size_t p;
      if (!Index(args, "parent", count, p) ||
          p > static_cast<size_t>(std::numeric_limits<int>::max()))
        return Error("Invalid parent index");
      parent = static_cast<int>(p);
    }
    auto ensureUniqueName = [&](std::string desired) {
      if (desired.empty())
        return;
      auto taken = [&](const std::string &s, size_t skip) {
        for (size_t k = 0; k < sceneRenderer->GetMesh3DCount(); ++k)
          if (k != skip && sceneRenderer->GetMesh3DName(k) == s)
            return true;
        return false;
      };
      const size_t idx = sceneRenderer->GetMesh3DCount() - 1;
      std::string unique = desired;
      for (int n = 1; taken(unique, idx); ++n)
        unique = desired + " " + std::to_string(n);
      sceneRenderer->meshes3d[idx].displayName = unique;
      std::snprintf(objectName, sizeof(objectName), "%s", unique.c_str());
    };

    if (kind == "model") {
      if (rawPath.empty())
        return Error("path (.pmx/.fbx/.obj) is required when kind is \"model\"");
      std::filesystem::path resolved(rawPath);
      if (resolved.is_relative() && !projectHandler.projectPath.empty())
        resolved = std::filesystem::path(projectHandler.projectPath) / resolved;
      std::error_code ec;
      if (!std::filesystem::is_regular_file(resolved, ec))
        return Error("Model file not found: " + rawPath);
      std::string ext = resolved.extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return (char)std::tolower(c); });
      if (ext != ".pmx" && ext != ".fbx" && ext != ".obj")
        return Error("Model path must be .pmx, .fbx, or .obj");
      const bool ok =
          (ext == ".pmx")   ? sceneRenderer->LoadPMXMesh(resolved.string())
          : (ext == ".fbx") ? sceneRenderer->LoadFbxMesh(resolved.string())
                            : sceneRenderer->LoadObjMesh(resolved.string());
      if (!ok || sceneRenderer->GetMesh3DCount() <= count)
        return Error("Failed to load model: " + rawPath);
      const size_t createdIdx = sceneRenderer->GetMesh3DCount() - 1;
      sceneRenderer->SetMesh3DTransform(createdIdx, position, rotation, scale);
      if (parent >= 0)
        sceneRenderer->SetMesh3DParent(createdIdx, parent, false);
      sceneRenderer->SetMesh3DDebugSource(createdIdx, __FILE__, __LINE__);
      if (!customName.empty())
        ensureUniqueName(customName);
      else
        ensureUniqueName(sceneRenderer->GetMesh3DName(createdIdx));
      selectedUiElement = -1;
      RecordEditorHistory();
      return object(createdIdx);
    }

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
    else if (kind == "audio_source")
      kindIndex = 5;
    else if (kind == "screen")
      kindIndex = 6;
    else if (kind == "text_3d")
      kindIndex = 7;
    if (kindIndex < 0)
      return Error("Unsupported object kind");
    if (!rawPath.empty()) {
      if (kind == "audio_source" && !ilmeee::IsAudioClipPath(rawPath))
        return Error("path for audio_source must be a supported audio file or http(s):// stream URL");
      if (kind == "screen" && !ilmeee::IsVideoClipPath(rawPath))
        return Error("path for screen must be a supported video file or http(s):// stream URL");
    }
    const int created = SpawnSceneObject(kindIndex, parent, position);
    if (created < 0)
      return Error("Could not create object");
    const size_t createdIdx = static_cast<size_t>(created);
    if (args.contains("rotation") || args.contains("scale")) {
      const glm::vec3 pos = sceneRenderer->GetMesh3DPosition(createdIdx);
      const glm::vec3 rot = args.contains("rotation")
                                ? rotation
                                : sceneRenderer->GetMesh3DRotation(createdIdx);
      sceneRenderer->SetMesh3DTransform(createdIdx, pos, rot, scale);
    }
    if (!customName.empty())
      ensureUniqueName(customName);
    if (!rawPath.empty()) {
      const std::string clip =
          ilmeee::IsMediaUrl(rawPath)
              ? rawPath
              : ProjectMediaPath(projectHandler.projectPath, rawPath);
      if (kind == "audio_source") {
        sceneRenderer->meshes3d[createdIdx].audio.enabled = true;
        sceneRenderer->meshes3d[createdIdx].audio.clip = clip;
      } else if (kind == "screen") {
        sceneRenderer->meshes3d[createdIdx].video.enabled = true;
        sceneRenderer->meshes3d[createdIdx].video.clip = clip;
      }
    }
    if (args.contains("rotation") || args.contains("scale") ||
        !customName.empty() || !rawPath.empty())
      RecordEditorHistory();
    return object(createdIdx);
  }
  if (name == "set_surface_texture") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    const uint32_t subCount = sceneRenderer->GetMesh3DSubmeshCount(i);
    if (subCount == 0)
      return Error("This object has no textureable surfaces");
    size_t surface;
    if (!Index(args, "surface", subCount, surface))
      return Error("Invalid surface index");
    std::string texPath;
    if (!Optional(args, "texture", texPath) || !args.contains("texture"))
      return Error("texture is required");
    if (texPath.empty()) {
      sceneRenderer->ClearMesh3DSubmeshTexture(i, (uint32_t)surface);
      RecordEditorHistory();
      return object(i);
    }
    std::filesystem::path resolved(texPath);
    if (resolved.is_relative() && !projectHandler.projectPath.empty())
      resolved = std::filesystem::path(projectHandler.projectPath) / resolved;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(resolved, ec))
      return Error("Texture file not found: " + texPath);
    if (!sceneRenderer->BindMesh3DSubmeshTexture(i, (uint32_t)surface,
                                                 resolved.string()))
      return Error("Failed to bind texture: " + texPath);
    RecordEditorHistory();
    return object(i);
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
  if (name == "delete_object") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    if (DeletingRemovesLastCamera(i))
      return Error("Scene must keep at least one camera.");
    const std::string deletedName = sceneRenderer->GetMesh3DName(i);
    DeleteSceneObject(i);
    return {{"deleted", true},
            {"index", i},
            {"name", deletedName},
            {"remainingObjects", sceneRenderer->GetMesh3DCount()},
            {"saved", false}};
  }
  if (name == "set_object_properties") {
    size_t i;
    if (!Index(args, "index", count, i))
      return Error("Invalid object index");
    if (const std::string bad = UnknownKey(
            args, {"index", "name", "parent", "keepWorld", "lightType", "color",
                   "intensity", "range", "spotAngle", "gamma", "projection",
                   "fov", "orthoSize", "near", "far", "text", "textSize",
                   "textColor", "alignment", "visible"});
        !bad.empty())
      return Error("Unknown argument '" + bad + "'");
    bool changed = false;
    if (args.contains("name")) {
      std::string newName;
      if (!Optional(args, "name", newName) || newName.empty())
        return Error("name must be a non-empty string");
      if (RenameMesh(i, newName))
        changed = true;
    }
    if (args.contains("parent")) {
      if (!args["parent"].is_number_integer())
        return Error("parent must be an integer (-1 or object index)");
      const auto p = args["parent"].get<std::int64_t>();
      if (p < -1 || p >= static_cast<std::int64_t>(count))
        return Error("parent must be -1 (root) or a valid object index");
      bool keepWorld = true;
      if (!Optional(args, "keepWorld", keepWorld))
        return Error("keepWorld must be a boolean");
      if (!sceneRenderer->SetMesh3DParent(i, static_cast<int>(p), keepWorld))
        return Error("Invalid parent assignment (cannot parent an object to itself or its descendant)");
      changed = true;
    }

    if (args.contains("lightType") || args.contains("color") ||
        args.contains("intensity") || args.contains("range") ||
        args.contains("spotAngle") || args.contains("gamma")) {
      if (!sceneRenderer->IsMesh3DLight(i))
        return Error("Light properties require a Light object");
      std::string lightType;
      glm::vec3 color = sceneRenderer->GetMesh3DLightColor(i);
      float intensity = sceneRenderer->GetMesh3DLightIntensity(i);
      float range = sceneRenderer->GetMesh3DLightRange(i);
      float spotAngle = sceneRenderer->GetMesh3DLightSpotAngle(i);
      float gamma = sceneRenderer->GetMesh3DLightGamma(i);
      if (!Optional(args, "lightType", lightType) ||
          !Vec3(args, "color", color) ||
          !Optional(args, "intensity", intensity) ||
          !Optional(args, "range", range) ||
          !Optional(args, "spotAngle", spotAngle) ||
          !Optional(args, "gamma", gamma))
        return Error("Invalid light property value");
      if (!lightType.empty()) {
        if (lightType == "directional")
          sceneRenderer->SetMesh3DLightType(i, 0);
        else if (lightType == "point")
          sceneRenderer->SetMesh3DLightType(i, 1);
        else if (lightType == "spot")
          sceneRenderer->SetMesh3DLightType(i, 2);
        else
          return Error("lightType must be directional, point or spot");
      }
      sceneRenderer->SetMesh3DLightColor(i, glm::clamp(color, 0.0f, 10.0f));
      sceneRenderer->SetMesh3DLightIntensity(i, std::clamp(intensity, 0.0f, 100.0f));
      sceneRenderer->SetMesh3DLightRange(i, std::clamp(range, 0.1f, 1000.0f));
      sceneRenderer->SetMesh3DLightSpotAngle(i, std::clamp(spotAngle, 1.0f, 89.0f));
      sceneRenderer->SetMesh3DLightGamma(i, std::clamp(gamma, 0.1f, 5.0f));
      changed = true;
    }

    if (args.contains("projection") || args.contains("fov") ||
        args.contains("orthoSize") || args.contains("near") ||
        args.contains("far")) {
      if (!sceneRenderer->IsMesh3DCamera(i))
        return Error("Camera properties require a Camera object");
      std::string projection;
      float fov = sceneRenderer->GetMesh3DCameraFov(i);
      float orthoSize = sceneRenderer->GetMesh3DCameraOrthoSize(i);
      float nearPlane = sceneRenderer->GetMesh3DCameraNear(i);
      float farPlane = sceneRenderer->GetMesh3DCameraFar(i);
      if (!Optional(args, "projection", projection) ||
          !Optional(args, "fov", fov) ||
          !Optional(args, "orthoSize", orthoSize) ||
          !Optional(args, "near", nearPlane) ||
          !Optional(args, "far", farPlane))
        return Error("Invalid camera property value");
      if (!projection.empty()) {
        if (projection == "perspective")
          sceneRenderer->SetMesh3DCameraProjection(i, 0);
        else if (projection == "orthographic")
          sceneRenderer->SetMesh3DCameraProjection(i, 1);
        else
          return Error("projection must be perspective or orthographic");
      }
      if (nearPlane <= 0.0f || farPlane <= nearPlane)
        return Error("require 0 < near < far");
      sceneRenderer->SetMesh3DCameraFov(i, std::clamp(fov, 10.0f, 170.0f));
      sceneRenderer->SetMesh3DCameraOrthoSize(i, std::clamp(orthoSize, 0.1f, 1000.0f));
      sceneRenderer->SetMesh3DCameraNear(i, nearPlane);
      sceneRenderer->SetMesh3DCameraFar(i, farPlane);
      changed = true;
    }

    if (args.contains("text") || args.contains("textSize") ||
        args.contains("textColor") || args.contains("alignment") ||
        args.contains("visible")) {
      if (!sceneRenderer->meshes3d[i].isWorldText)
        return Error("3D text properties require a text_3d object");
      auto &wt = sceneRenderer->meshes3d[i].worldText;
      std::string alignStr;
      ImVec4 col(wt.color.r, wt.color.g, wt.color.b, wt.color.a);
      if (!Optional(args, "text", wt.text) ||
          !Optional(args, "textSize", wt.size) ||
          !Color4(args, "textColor", col) ||
          !Optional(args, "alignment", alignStr) ||
          !Optional(args, "visible", wt.visible))
        return Error("Invalid 3D text property value");
      if (!alignStr.empty()) {
        if (alignStr == "left")
          wt.alignment = 0;
        else if (alignStr == "center")
          wt.alignment = 1;
        else if (alignStr == "right")
          wt.alignment = 2;
        else
          return Error("alignment must be left, center or right");
      }
      wt.size = std::clamp(wt.size, 0.05f, 50.0f);
      wt.color = glm::vec4(col.x, col.y, col.z, col.w);
      changed = true;
    }

    if (changed)
      RecordEditorHistory();
    return object(i);
  }
  if (name == "set_scene_environment") {
    if (const std::string bad = UnknownKey(
            args, {"backgroundColor", "sunDirection", "sunColor", "sunIntensity",
                   "sunVisible", "gridVisible", "nprPreset", "npr"});
        !bad.empty())
      return Error("Unknown argument '" + bad + "'");
    if (args.contains("backgroundColor")) {
      const glm::vec4 cur = sceneRenderer->GetBackgroundColor();
      ImVec4 bg(cur.r, cur.g, cur.b, cur.a);
      if (!Color4(args, "backgroundColor", bg))
        return Error("backgroundColor must be [r, g, b] or [r, g, b, a] in 0..1");
      sceneRenderer->SetBackgroundColor(bg.x, bg.y, bg.z, bg.w);
    }
    if (args.contains("sunDirection")) {
      glm::vec3 dir = sceneRenderer->sunLight.direction;
      if (!Vec3(args, "sunDirection", dir))
        return Error("Invalid sunDirection");
      sceneRenderer->sunLight.direction = dir;
    }
    if (args.contains("sunColor")) {
      glm::vec3 col = sceneRenderer->sunLight.color;
      if (!Vec3(args, "sunColor", col))
        return Error("Invalid sunColor");
      sceneRenderer->sunLight.color = glm::clamp(col, 0.0f, 10.0f);
    }
    if (args.contains("sunIntensity")) {
      float intensity = sceneRenderer->sunLight.intensity;
      if (!Optional(args, "sunIntensity", intensity) || intensity < 0.0f ||
          intensity > 20.0f)
        return Error("sunIntensity must be 0..20");
      sceneRenderer->sunLight.intensity = intensity;
    }
    if (args.contains("sunVisible")) {
      bool vis = sceneRenderer->IsSunVisible();
      if (!Optional(args, "sunVisible", vis))
        return Error("sunVisible must be a boolean");
      sceneRenderer->SetSunVisible(vis);
    }
    if (args.contains("gridVisible")) {
      bool grid = sceneRenderer->IsGrid3DVisible();
      if (!Optional(args, "gridVisible", grid))
        return Error("gridVisible must be a boolean");
      sceneRenderer->SetGrid3DVisible(grid);
    }
    ilmeee::NprSettings npr = sceneRenderer->GetNpr();
    bool nprChanged = false;
    if (args.contains("nprPreset")) {
      std::string preset;
      if (!Optional(args, "nprPreset", preset))
        return Error("nprPreset must be a string");
      if (preset == "anime_cel") {
        npr = ilmeee::NprSettings{};
        npr.enabled = true;
      } else if (preset == "soft_toon") {
        npr = ilmeee::NprSettings{};
        npr.enabled = true;
        npr.bands = 3;
        npr.softness = 0.2f;
        npr.shadowColor = {0.70f, 0.68f, 0.78f};
        npr.rimWidth = 0.3f;
        npr.rimStrength = 0.35f;
        npr.specular = false;
        npr.outlineColor = {0.25f, 0.20f, 0.22f};
        npr.outlineWidth = 1.0f;
      } else if (preset == "comic_ink") {
        npr = ilmeee::NprSettings{};
        npr.enabled = true;
        npr.softness = 0.0f;
        npr.shadowColor = {0.35f, 0.35f, 0.40f};
        npr.rim = false;
        npr.specularSize = 0.03f;
        npr.specularStrength = 0.8f;
        npr.outlineColor = {0.0f, 0.0f, 0.0f};
        npr.outlineWidth = 3.0f;
      } else {
        return Error("nprPreset must be anime_cel, soft_toon or comic_ink");
      }
      nprChanged = true;
    }
    if (auto it = args.find("npr"); it != args.end()) {
      if (!it->is_object())
        return Error("npr must be an object");
      const json &n = *it;
      int bands = npr.bands;
      if (!Optional(n, "enabled", npr.enabled) ||
          !Optional(n, "bands", bands) ||
          !Optional(n, "softness", npr.softness) ||
          !Vec3(n, "shadowColor", npr.shadowColor) ||
          !Optional(n, "rim", npr.rim) ||
          !Vec3(n, "rimColor", npr.rimColor) ||
          !Optional(n, "rimWidth", npr.rimWidth) ||
          !Optional(n, "rimStrength", npr.rimStrength) ||
          !Optional(n, "specular", npr.specular) ||
          !Optional(n, "specularSize", npr.specularSize) ||
          !Optional(n, "specularStrength", npr.specularStrength) ||
          !Optional(n, "outline", npr.outline) ||
          !Vec3(n, "outlineColor", npr.outlineColor) ||
          !Optional(n, "outlineWidth", npr.outlineWidth))
        return Error("Invalid npr field type");
      npr.bands = bands;
      npr.Clamp();
      nprChanged = true;
    }
    if (nprChanged)
      sceneRenderer->SetNpr(npr);
    RecordEditorHistory();
    return envJson();
  }
  if (name == "create_animation_controller") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    if (const std::string bad = UnknownKey(
            args, {"path", "idle", "walk", "run", "walkSpeed", "runSpeed",
                   "idleThreshold", "transition", "inPlace", "rootBone",
                   "overwrite", "attachTo"});
        !bad.empty())
      return Error("Unknown argument '" + bad + "'");
    std::string rawPath;
    bool overwrite = false;
    if (!Optional(args, "path", rawPath) || rawPath.empty() ||
        !Optional(args, "overwrite", overwrite))
      return Error("path is required");
    if (std::filesystem::path(rawPath).extension().empty())
      rawPath += ".ilmeeecontroller";
    std::string absPath, relPath, err;
    if (!ilmeee::ResolveAnimationAsset(projectHandler.projectPath, rawPath, true,
                                       absPath, relPath, err))
      return Error(err);
    if (!ilmeee::IsAnimationControllerPath(absPath))
      return Error("Controller path must end with .ilmeeecontroller");
    std::error_code ec;
    if (std::filesystem::exists(absPath, ec) && !overwrite)
      return Error("Controller already exists: " + relPath +
                   " (pass overwrite=true to replace)");
    size_t attachIdx = 0;
    const bool hasAttach = args.contains("attachTo");
    if (hasAttach) {
      if (!Index(args, "attachTo", count, attachIdx))
        return Error("Invalid attachTo object index");
      if (sceneRenderer->meshes3d[attachIdx].skeleton.empty())
        return Error("attachTo target must be a skinned (PMX) model with bones");
    }
    ilmeee::AnimationController ctrl;
    if (!Optional(args, "idle", ctrl.idle) ||
        !Optional(args, "walk", ctrl.walk) ||
        !Optional(args, "run", ctrl.run) ||
        !Optional(args, "walkSpeed", ctrl.walkSpeed) ||
        !Optional(args, "runSpeed", ctrl.runSpeed) ||
        !Optional(args, "idleThreshold", ctrl.idleThreshold) ||
        !Optional(args, "transition", ctrl.transition) ||
        !Optional(args, "inPlace", ctrl.inPlace) ||
        !Optional(args, "rootBone", ctrl.rootBone))
      return Error("Invalid animation controller field");
    for (std::string *clip : {&ctrl.idle, &ctrl.walk, &ctrl.run}) {
      if (clip->empty())
        continue;
      std::string cAbs, cRel, cErr;
      if (!ilmeee::ResolveAnimationAsset(projectHandler.projectPath, *clip,
                                         false, cAbs, cRel, cErr))
        return Error(cErr);
      if (!ilmeee::IsAnimationPath(cAbs))
        return Error("Controller clip must be .ilmeeeanim or .vmd: " + *clip);
      *clip = cRel;
    }
    if (!ilmeee::SaveAnimationController(absPath, ctrl, err))
      return Error(err.empty() ? "Could not save animation controller" : err);
    projectHandler.rootAsset =
        projectHandler.BuildAssetTree(projectHandler.projectPath);
    if (hasAttach) {
      auto &mesh = sceneRenderer->meshes3d[attachIdx];
      mesh.animation.controller = relPath;
      mesh.animation.enabled = true;
      mesh.loadedAnimationPath.clear();
      mesh.animationError.clear();
      mesh.animationTime = 0;
      mesh.locomotion = {};
      RecordEditorHistory();
    }
    return {{"ok", true},
            {"path", relPath},
            {"idle", ctrl.idle},
            {"walk", ctrl.walk},
            {"run", ctrl.run},
            {"walkSpeed", ctrl.walkSpeed},
            {"runSpeed", ctrl.runSpeed},
            {"attachedTo", hasAttach ? json(attachIdx) : json(nullptr)}};
  }
  if (name == "build_game") {
    if (projectHandler.projectPath.empty())
      return Error("No project is open");
    if (gameBuildRunning.load())
      return Error("A game build is already running");
    std::string target =
        (gameBuildTarget == ilmeee::BuildTarget::Android) ? "android" : "linux";
    bool runWhenDone = false;
    bool portable = gameBuildPortable;
    bool includeUnused = gameBuildIncludeUnused;
    int maxVideoHeight = gameBuildAndroidVideoHeight;
    if (!Optional(args, "target", target) ||
        !Optional(args, "run", runWhenDone) ||
        !Optional(args, "portable", portable) ||
        !Optional(args, "includeUnusedAssets", includeUnused) ||
        !Optional(args, "androidMaxVideoHeight", maxVideoHeight))
      return Error("Invalid build_game argument");
    if (target == "linux")
      gameBuildTarget = ilmeee::BuildTarget::Linux;
    else if (target == "android")
      gameBuildTarget = ilmeee::BuildTarget::Android;
    else
      return Error("target must be linux or android");
    if (maxVideoHeight != 0 && maxVideoHeight != 720 && maxVideoHeight != 1080)
      return Error("androidMaxVideoHeight must be 0, 720 or 1080");
    gameBuildPortable = portable;
    gameBuildIncludeUnused = includeUnused;
    gameBuildAndroidVideoHeight = maxVideoHeight;
    StartGameBuild(runWhenDone);
    return {{"started", gameBuildRunning.load()},
            {"target", target},
            {"runWhenDone", runWhenDone},
            {"portable", gameBuildPortable},
            {"includeUnusedAssets", gameBuildIncludeUnused},
            {"androidMaxVideoHeight", gameBuildAndroidVideoHeight}};
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
      return Error("clip must be .wav/.ogg/.mp3/.flac/.m4a/.aac/.opus or an http(s):// stream URL");
    if (a.volume < 0 || a.volume > 2 || a.minDistance < 0 || a.maxDistance <= a.minDistance ||
        a.maxDistance > 1000)
      return Error("volume must be 0..2 and 0 <= minDistance < maxDistance <= 1000");
    if (!ilmeee::IsMediaUrl(a.clip) && std::filesystem::path(a.clip).is_absolute())
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
      return Error("clip must be .mp4/.mkv/.webm/.mov/.avi/.m3u8 or an http(s):// stream URL");
    if (v.volume < 0 || v.volume > 2) return Error("volume must be 0..2");
    if (!ilmeee::IsMediaUrl(v.clip) && std::filesystem::path(v.clip).is_absolute())
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
