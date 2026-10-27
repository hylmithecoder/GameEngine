#include "../../../include/core_engine/ScriptWorkspace.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <json.hpp>
#include <set>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
#endif

namespace ilmeee {
namespace fs = std::filesystem;
namespace {
bool WriteExclusive(const fs::path &path, const std::string &text,
                    std::string &error) {
  FILE *file = std::fopen(path.string().c_str(), "wx");
  if (!file) {
    error = "Cannot create " + path.filename().string() + ": " +
            std::strerror(errno);
    return false;
  }
  const bool written =
      std::fwrite(text.data(), 1, text.size(), file) == text.size();
  const bool closed = std::fclose(file) == 0;
  if (!written || !closed) {
    error = "Could not finish writing " + path.filename().string();
    std::error_code ec;
    fs::remove(path, ec);
    return false;
  }
  return true;
}

std::string EditorExecutable(const CodeEditorSettings &settings) {
  if (settings.editor == "code" || settings.editor == "codium" ||
      settings.editor == "cursor" || settings.editor == "antigravity")
    return settings.editor;
  if (settings.editor == "custom")
    return settings.executable;
  return {};
}
} // namespace

std::string ValidateScriptName(const std::string &name) {
  if (name.empty())
    return "Enter a script name.";
  if (name.size() > 64)
    return "Use a name of 64 characters or fewer.";
  const auto letter = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
  };
  if (!letter(name.front()))
    return "Start the name with a letter.";
  for (unsigned char c : name)
    if (!letter(c) && !(c >= '0' && c <= '9') && c != '_')
      return "Use letters, numbers and underscores; omit the file extension.";
  if (name.find("__") != std::string::npos)
    return "Double underscores are reserved by C++.";
  static const std::set<std::string> reserved = {
      "alignas",       "alignof",     "and",
      "and_eq",        "asm",         "auto",
      "bitand",        "bitor",       "bool",
      "break",         "case",        "catch",
      "char",          "char8_t",     "char16_t",
      "char32_t",      "class",       "compl",
      "concept",       "const",       "consteval",
      "constexpr",     "constinit",   "const_cast",
      "continue",      "co_await",    "co_return",
      "co_yield",      "decltype",    "default",
      "delete",        "do",          "double",
      "dynamic_cast",  "else",        "enum",
      "explicit",      "export",      "extern",
      "false",         "float",       "for",
      "friend",        "goto",        "if",
      "inline",        "int",         "long",
      "mutable",       "namespace",   "new",
      "noexcept",      "not",         "not_eq",
      "nullptr",       "operator",    "or",
      "or_eq",         "private",     "protected",
      "public",        "register",    "reinterpret_cast",
      "requires",      "return",      "short",
      "signed",        "sizeof",      "static",
      "static_assert", "static_cast", "struct",
      "switch",        "template",    "this",
      "thread_local",  "throw",       "true",
      "try",           "typedef",     "typeid",
      "typename",      "union",       "unsigned",
      "using",         "virtual",     "void",
      "volatile",      "wchar_t",     "while",
      "xor",           "xor_eq",      "Component",
      // Script SDK names (core_engine/script/SceneObjects.hpp).
      "Vec3", "Transform", "GameObject", "GameObjectRef", "ObjectRef",
      "AudioGameObject", "VideoPlayerGameObject", "LightGameObject",
      "CameraGameObject", "UiGameObject", "DialogGameObject",
      "TextGameObject", "UiTextGameObject", "UiImageGameObject", "UiButtonGameObject",
      "PhysicsGameObject", "ColliderGameObject",
      "ScriptProperties"};
  if (reserved.contains(name))
    return "Choose a name that is not a C++ keyword or engine base class.";
  std::string upper = name;
  for (char &c : upper)
    if (c >= 'a' && c <= 'z')
      c -= 'a' - 'A';
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
      (upper.size() == 4 &&
       (upper.substr(0, 3) == "COM" || upper.substr(0, 3) == "LPT") &&
       upper[3] >= '1' && upper[3] <= '9'))
    return "This name is reserved on Windows.";
  return {};
}

std::string ValidateScriptDestination(const fs::path &folder,
                                      const std::string &name) {
  if (const auto error = ValidateScriptName(name); !error.empty())
    return error;
  if (folder.empty())
    return "Open a project before creating a script.";
  std::error_code ec;
  for (const char *extension : {".cpp", ".hpp"}) {
    const auto status = fs::symlink_status(folder / (name + extension), ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
      return "Cannot access the script folder: " + ec.message();
    if (fs::exists(status))
      return name + extension + " already exists. Choose another name.";
  }
  return {};
}

bool CreateScriptFiles(const fs::path &folder, const std::string &name,
                       std::string &error) {
  error = ValidateScriptDestination(folder, name);
  if (!error.empty())
    return false;
  std::error_code ec;
  fs::create_directories(folder, ec);
  if (ec) {
    error = "Cannot create the script folder: " + ec.message();
    return false;
  }
  const fs::path header = folder / (name + ".hpp");
  const std::string headerText =
      "#pragma once\n\n#include <core_engine/core/Component.hpp>\n\nclass " +
      name +
      " : public Component {\npublic:\n    float speed = 3.0f;\n    GameObjectRef target;\n"
      "    void DescribeProperties(ScriptProperties &properties) override;\n"
      "    void Start() override;\n"
      "    void Update(float deltaTime) override;\n};\n";
  const std::string sourceText =
      "#include \"" + name + ".hpp\"\n\nvoid " + name +
      "::DescribeProperties(ScriptProperties &properties) {\n"
      "    properties.Field(\"Speed\", speed);\n"
      "    properties.Field(\"Target\", target);\n}\n\nvoid " + name +
      "::Start() {\n    // Initialize this component.\n}\n\nvoid " + name +
      "::Update(float deltaTime) {\n    (void)deltaTime;\n    // Update this "
      "component.\n    // Example: if (IsKeyDown(\"W\")) gameObject.Translate({0, 0, -speed * deltaTime});\n"
      "    // target.Get() resolves the object assigned in Inspector.\n}\n";
  if (!WriteExclusive(header, headerText, error))
    return false;
  if (!WriteExclusive(folder / (name + ".cpp"), sourceText, error)) {
    fs::remove(header,
               ec); // Roll back only the header created by this operation.
    return false;
  }
  return true;
}

CodeEditorSettings LoadCodeEditorSettings(const fs::path &file) {
  CodeEditorSettings settings;
  try {
    std::ifstream input(file);
    if (!input)
      return settings;
    const auto json = nlohmann::json::parse(input);
    settings.editor = json.value("codeEditor", "system");
    settings.executable = json.value("codeEditorExecutable", "");
    if (settings.editor != "system" && settings.editor != "custom" &&
        EditorExecutable(settings).empty())
      settings.editor = "system";
  } catch (...) {
    return {};
  }
  return settings;
}

bool SaveCodeEditorSettings(const fs::path &file,
                            const CodeEditorSettings &settings,
                            std::string &error) {
  error.clear();
  if (settings.editor == "custom" && settings.executable.empty()) {
    error = "Choose an editor executable.";
    return false;
  }
  try {
    if (!file.parent_path().empty())
      fs::create_directories(file.parent_path());
    nlohmann::json json = nlohmann::json::object();
    std::ifstream input(file);
    if (input) {
      json = nlohmann::json::parse(input, nullptr, false);
      if (!json.is_object())
        json = nlohmann::json::object();
    }
    json["codeEditor"] = settings.editor;
    json["codeEditorExecutable"] = settings.executable;
    std::ofstream output(file);
    output << json.dump(2) << '\n';
    output.close();
    if (!output) {
      error = "Could not save code editor settings.";
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}

bool OpenInCodeEditor(const CodeEditorSettings &settings, const fs::path &file,
                      std::string &error, const fs::path &projectFolder) {
  error.clear();
  std::error_code ec;
  const auto absolute = fs::absolute(file, ec);
  if (ec || !fs::exists(absolute, ec)) {
    error = "File does not exist: " + file.string();
    return false;
  }
  const std::string executable = EditorExecutable(settings);
  if (settings.editor == "custom" && executable.empty()) {
    error = "Choose an editor executable in Edit > Code Editor.";
    return false;
  }
  std::vector<std::string> targets;
  // Always send the workspace along with the file. Persisting "already
  // opened" would become stale when the user closes the external editor.
  // OS file associations accept a file, not a code-editor workspace.
  if (!executable.empty() && !projectFolder.empty()) {
    const auto workspace = fs::absolute(projectFolder, ec);
    if (ec || !fs::is_directory(workspace, ec)) {
      error = "Project folder does not exist: " + projectFolder.string();
      return false;
    }
    targets.push_back(workspace.lexically_normal().string());
  }
  targets.push_back(absolute.lexically_normal().string());
#ifdef _WIN32
  // Quote one Windows argument, including embedded quotes and trailing slashes.
  const auto quote = [](const std::string &target) {
    std::string argument = "\"";
    size_t slashes = 0;
    for (char c : target) {
      if (c == '\\') {
        ++slashes;
        continue;
      }
      argument.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
      argument += c;
      slashes = 0;
    }
    argument.append(slashes * 2, '\\');
    argument += '"';
    return argument;
  };
  std::string arguments;
  for (const auto &target : targets) {
    if (!arguments.empty())
      arguments += ' ';
    arguments += quote(target);
  }
  const auto result = ShellExecuteA(
      nullptr, "open",
      executable.empty() ? absolute.string().c_str() : executable.c_str(),
      executable.empty() ? nullptr : arguments.c_str(), nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(result) <= 32) {
    error = "Could not launch the code editor. Check Edit > Code Editor.";
    return false;
  }
#else
#ifdef __APPLE__
  const std::string launcher = executable.empty() ? "open" : executable;
#else
  const std::string launcher = executable.empty() ? "xdg-open" : executable;
#endif
  std::vector<char *> argv{const_cast<char *>(launcher.c_str())};
  for (auto &target : targets)
    argv.push_back(target.data());
  argv.push_back(nullptr);
  pid_t pid;
  const int status = posix_spawnp(&pid, launcher.c_str(), nullptr, nullptr,
                                  argv.data(), environ);
  if (status != 0) {
    error = "Cannot start " + launcher + ": " + std::strerror(status) +
            ". Choose an installed editor in Edit > Code Editor.";
    return false;
  }
  std::thread([pid] {
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
  }).detach();
#endif
  return true;
}
} // namespace ilmeee
