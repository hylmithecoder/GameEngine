#include "../../../include/core_engine/NativeScripts.hpp"
#include "../../../include/core_engine/ScriptWorkspace.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <json.hpp>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#ifndef __ANDROID__
#include <spawn.h>
#endif
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace ilmeee {
namespace fs = std::filesystem;
namespace {
bool Inside(const fs::path &root, const fs::path &path) {
  const auto relative = path.lexically_relative(root);
  return !relative.empty() && !relative.is_absolute() &&
         *relative.begin() != "..";
}
std::string ReadLog(const fs::path &file) {
  std::ifstream input(file);
  std::string result(128 * 1024, '\0');
  input.read(result.data(), result.size());
  result.resize(static_cast<size_t>(input.gcount()));
  return result;
}
std::string AndroidTriple(const std::string &abi) {
  if (abi == "arm64-v8a") return "aarch64-linux-android";
  if (abi == "armeabi-v7a") return "armv7a-linux-androideabi";
  if (abi == "x86_64") return "x86_64-linux-android";
  if (abi == "x86") return "i686-linux-android";
  throw std::runtime_error("Unsupported Android script ABI: " + abi);
}
std::string ReadFile(const fs::path &file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) throw std::runtime_error("Cannot read script SDK header: " + file.string());
  return {std::istreambuf_iterator<char>(input), {}};
}
std::string ScriptRevision(const fs::path &project) {
  std::string revision;
  std::error_code ec;
  const auto scripts = project / "assets/scripts";
  if (!fs::is_directory(scripts, ec))
    return revision;
  fs::recursive_directory_iterator it(
      scripts, fs::directory_options::skip_permission_denied, ec),
      end;
  for (; !ec && it != end; it.increment(ec)) {
    const auto extension = it->path().extension();
    if (!it->is_regular_file(ec) ||
        (extension != ".cpp" && extension != ".hpp"))
      continue;
    const auto time = fs::last_write_time(it->path(), ec);
    if (!ec)
      revision +=
          it->path().string() + std::to_string(static_cast<long long>(time.time_since_epoch().count()));
  }
  return revision;
}
// One element of each Inspector type, handed to a script's ListAccess.
struct ScalarSlot {
  float number = 0;
  bool boolean = false;
  std::string text;
  script::Vec3 vec;
  GameObjectRef object;
  void *For(ScriptValueType type) {
    switch (type) {
    case ScriptValueType::Float: return &number;
    case ScriptValueType::Bool: return &boolean;
    case ScriptValueType::String: return &text;
    case ScriptValueType::Vec3: return &vec;
    case ScriptValueType::Object: return &object;
    default: return nullptr;
    }
  }
};
void ReadScalar(ScriptValueType type, const void *from, ScriptValue &value) {
  if (type == ScriptValueType::Float)
    value.number = *static_cast<const float *>(from);
  if (type == ScriptValueType::Bool)
    value.boolean = *static_cast<const bool *>(from);
  if (type == ScriptValueType::String)
    value.string = *static_cast<const std::string *>(from);
  if (type == ScriptValueType::Object)
    value.object = static_cast<const GameObjectRef *>(from)->id;
  if (type == ScriptValueType::Vec3) {
    const auto *vec = static_cast<const script::Vec3 *>(from);
    value.vec3[0] = vec->x;
    value.vec3[1] = vec->y;
    value.vec3[2] = vec->z;
  }
}
void WriteScalar(ScriptValueType type, void *to, const ScriptValue &value,
                 const script::HostApi *host) {
  if (type == ScriptValueType::Float)
    *static_cast<float *>(to) = value.number;
  if (type == ScriptValueType::Bool)
    *static_cast<bool *>(to) = value.boolean;
  if (type == ScriptValueType::String)
    *static_cast<std::string *>(to) = value.string;
  if (type == ScriptValueType::Vec3)
    *static_cast<script::Vec3 *>(to) = {value.vec3[0], value.vec3[1],
                                        value.vec3[2]};
  if (type == ScriptValueType::Object) {
    auto *ref = static_cast<GameObjectRef *>(to);
    ref->id = value.object;
    ref->Bind(host);
  }
}
bool ValidField(const ScriptProperty &field) {
  if (field.type == ScriptValueType::List)
    return field.list && field.elementType < ScriptValueType::List;
  return field.type < ScriptValueType::List;
}
ScriptValue ReadProperty(const ScriptProperty &field) {
  ScriptValue value;
  value.name = field.name;
  value.type = field.type;
  if (field.type != ScriptValueType::List) {
    ReadScalar(field.type, field.value, value);
    return value;
  }
  value.elementType = field.elementType;
  const size_t count = field.list->size(field.value);
  for (size_t i = 0; i < count; ++i) {
    ScalarSlot slot;
    field.list->get(field.value, i, slot.For(field.elementType));
    ScriptValue item;
    item.type = field.elementType;
    ReadScalar(item.type, slot.For(item.type), item);
    value.items.push_back(std::move(item));
  }
  return value;
}
bool Finite(const ScriptValue &value) {
  if (value.type == ScriptValueType::Float)
    return std::isfinite(value.number);
  if (value.type == ScriptValueType::Vec3)
    return std::isfinite(value.vec3[0]) && std::isfinite(value.vec3[1]) &&
           std::isfinite(value.vec3[2]);
  if (value.type == ScriptValueType::List)
    return value.items.size() <= kMaxScriptListItems &&
           std::all_of(value.items.begin(), value.items.end(),
                       [](const ScriptValue &item) { return Finite(item); });
  return true;
}
void ApplyProperties(Component &component, const NativeScriptBinding &binding,
                     const script::HostApi *host) {
  ScriptProperties properties;
  component.DescribeProperties(properties);
  for (const auto &field : properties.fields) {
    if (!ValidField(field))
      continue;
    ScriptValue shape;
    shape.name = field.name;
    shape.type = field.type;
    shape.elementType = field.elementType;
    for (const auto &value : binding.values) {
      if (!SameScriptShape(value, shape) || !Finite(value))
        continue;
      if (field.type != ScriptValueType::List) {
        WriteScalar(field.type, field.value, value, host);
        continue;
      }
      field.list->resize(field.value, value.items.size());
      for (size_t i = 0; i < value.items.size(); ++i) {
        ScalarSlot slot;
        WriteScalar(field.elementType, slot.For(field.elementType),
                    value.items[i], host);
        field.list->set(field.value, i, slot.For(field.elementType));
      }
    }
    // Defaults the Inspector never touched still need the host.
    if (field.type == ScriptValueType::Object)
      static_cast<GameObjectRef *>(field.value)->Bind(host);
    if (field.type == ScriptValueType::List &&
        field.elementType == ScriptValueType::Object) {
      const size_t count = field.list->size(field.value);
      for (size_t i = 0; i < count; ++i) {
        GameObjectRef ref;
        field.list->get(field.value, i, &ref);
        ref.Bind(host);
        field.list->set(field.value, i, &ref);
      }
    }
  }
}
} // namespace

ScriptCompileOptions DefaultScriptCompileOptions(const fs::path &project) {
  ScriptCompileOptions options;
  options.project = project;
  options.cache = project / ".ilmeee/native-scripts";
#ifdef ILMEEE_SCRIPT_INCLUDE
  if (fs::exists(fs::path(ILMEEE_SCRIPT_INCLUDE) /
                 "core_engine/core/Component.hpp"))
    options.sdkInclude = ILMEEE_SCRIPT_INCLUDE;
#endif
  if (options.sdkInclude.empty()) {
    for (const auto &path : {ExecutableDir() / "../../include",
                             ExecutableDir() / "../include/ilmeeeengine"})
      if (fs::exists(path / "core_engine/core/Component.hpp")) {
        options.sdkInclude = fs::weakly_canonical(path);
        break;
      }
  }
#ifdef ILMEEE_SCRIPT_COMPILER
  if (fs::exists(ILMEEE_SCRIPT_COMPILER))
    options.compiler = ILMEEE_SCRIPT_COMPILER;
#endif
  if (const char *compiler = std::getenv("ILMEEE_SCRIPT_CXX");
      compiler && *compiler)
    options.compiler = compiler;
  return options;
}

ScriptCompileOptions AndroidScriptCompileOptions(const fs::path &project,
                                                 const fs::path &player,
                                                 const std::string &abi) {
  AndroidTriple(abi); // Validate before constructing any paths.
  auto options = DefaultScriptCompileOptions(project);
  const auto metadata = player / ("script-toolchain-" + abi + ".json");
  std::ifstream input(metadata);
  if (!input)
    throw std::runtime_error("Android player has no native-script toolchain for " + abi +
                             ". Rebuild it with scripts/android/build-player.sh.");
  const auto config = nlohmann::json::parse(input);
  if (config.at("schema").get<int>() != 1 ||
      config.at("abi").get<std::string>() != abi ||
      config.at("stl").get<std::string>() != "c++_shared")
    throw std::runtime_error("Incompatible Android script toolchain; rebuild the Android player.");
  options.compiler = config.at("compiler").get<std::string>();
  options.androidAbi = abi;
  options.androidApi = config.at("api").get<int>();
  if (options.androidApi != 26)
    throw std::runtime_error("Android script API must match the APK minSdk (26); rebuild the player.");
  if (!fs::is_regular_file(options.compiler))
    throw std::runtime_error("Player's Android NDK compiler is missing: " + options.compiler +
                             ". Restore that NDK or rebuild the Android player.");
  const auto snapshot = player / "script-sdk" / abi;
  const auto current = options.sdkInclude;
  if (!fs::is_directory(current / "core_engine/script"))
    throw std::runtime_error("Current engine script SDK not found.");
  std::vector<fs::path> headers = {"core_engine/core/Component.hpp", "core_engine/NativeScriptTypes.hpp"};
  for (const auto &entry : fs::recursive_directory_iterator(current / "core_engine/script"))
    if (entry.is_regular_file()) headers.push_back(entry.path().lexically_relative(current));
  for (const auto &header : headers)
    if (!fs::is_regular_file(snapshot / header) ||
        ReadFile(current / header) != ReadFile(snapshot / header))
      throw std::runtime_error("Android player script SDK is outdated (" + header.generic_string() +
                               "); rebuild with scripts/android/build-player.sh.");
  options.sdkInclude = snapshot;
  options.cache = project / ".ilmeee/native-scripts-android" / abi;
  return options;
}

std::vector<std::string> DiscoverNativeScripts(const fs::path &project) {
  std::vector<std::string> sources;
  std::error_code ec;
  const auto root = fs::weakly_canonical(project, ec);
  const auto scripts = root / "assets/scripts";
  if (ec || !fs::is_directory(scripts, ec))
    return sources;
  fs::recursive_directory_iterator it(
      scripts, fs::directory_options::skip_permission_denied, ec),
      end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code itemError;
    if (!it->is_regular_file(itemError) || it->path().extension() != ".cpp")
      continue;
    const auto source = fs::weakly_canonical(it->path(), itemError);
    auto header = source;
    header.replace_extension(".hpp");
    header = fs::weakly_canonical(header, itemError);
    if (!itemError && Inside(root, source) && Inside(root, header) &&
        fs::is_regular_file(header, itemError))
      sources.push_back(source.lexically_relative(root).generic_string());
  }
  std::sort(sources.begin(), sources.end());
  return sources;
}

bool WriteScriptCompilationDatabase(const ScriptCompileOptions &options,
                                    std::string &error) {
  try {
    const auto file = options.project / "compile_commands.json";
    nlohmann::json entries = nlohmann::json::array();
    std::ifstream old(file);
    if (old) {
      entries = nlohmann::json::parse(old, nullptr, false);
      if (!entries.is_array()) {
        error = "Existing compile_commands.json is invalid; left unchanged.";
        return false;
      }
    }
    const auto sources = DiscoverNativeScripts(options.project);
    std::set<std::string> current;
    for (const auto &source : sources)
      current.insert(fs::absolute(options.project / source).string());
    nlohmann::json merged = nlohmann::json::array();
    for (const auto &entry : entries)
      if (entry.is_object() && !current.contains(entry.value("file", "")))
        merged.push_back(entry);
    for (const auto &source : sources) {
      const auto path = fs::absolute(options.project / source).string();
      merged.push_back({{"directory", fs::absolute(options.project).string()},
                        {"file", path},
                        {"arguments",
                         {options.compiler, "-std=c++20", "-fPIC",
                          "-DILMEEE_SCRIPT_BUILD", "-I",
                          options.sdkInclude.string(), "-c", path}}});
    }
    old.close();
    std::ofstream output(file);
    output << merged.dump(2) << '\n';
    output.close();
    if (!output) {
      error = "Cannot write compile_commands.json";
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  }
}

CompiledScript CompileNativeScript(const ScriptCompileOptions &options,
                                   const std::string &source) {
  CompiledScript result;
  result.source = source;
  result.className = fs::path(source).stem().string();
#ifdef __ANDROID__
  result.diagnostics = "Compile project scripts on the desktop with the Android NDK, not on the device.";
  return result;
#else
  try {
    if (!options.androidAbi.empty()) AndroidTriple(options.androidAbi);
    result.diagnostics = ValidateScriptName(result.className);
    if (!result.diagnostics.empty())
      return result;
    const auto root = fs::canonical(options.project);
    const auto path = fs::canonical(root / source);
    auto header = path;
    header.replace_extension(".hpp");
    if (!Inside(root, path) || !Inside(root, fs::canonical(header))) {
      result.diagnostics = "Script files must be inside the project.";
      return result;
    }
    if (!fs::exists(options.sdkInclude / "core_engine/core/Component.hpp")) {
      result.diagnostics =
          "Engine script SDK not found. Install the public engine headers.";
      return result;
    }
    const auto build = options.cache /
                       (result.className + "-" + std::to_string(NewObjectId()));
    fs::create_directories(build);
    const auto wrapper = build / "factory.cpp";
    std::ofstream output(wrapper);
    output << "#include \"" << result.className
           << ".hpp\"\n#include <type_traits>\n"
           << "static_assert(std::is_base_of_v<Component, " << result.className
           << ">, \"Script class must derive from Component\");\n"
           << "static_assert(std::is_default_constructible_v<"
           << result.className
           << ">, \"Script class needs a public default constructor\");\n"
           << "static_assert(!std::is_abstract_v<" << result.className
           << ">, \"Script class cannot be abstract\");\n"
           << "extern \"C\" __attribute__((visibility(\"default\"))) unsigned "
              "ilmeee_script_api() { return ilmeee::script::kApiVersion; }\n"
           << "extern \"C\" __attribute__((visibility(\"default\"))) uint64_t "
              "ilmeee_script_abi() { return ilmeee::script::kComponentAbi; }\n"
           << "extern \"C\" __attribute__((visibility(\"default\"))) "
              "Component* ilmeee_script_create() { return new "
           << result.className << "(); }\n"
           << "extern \"C\" __attribute__((visibility(\"default\"))) void "
              "ilmeee_script_destroy(Component* value) { delete value; }\n";
    output.close();
    if (!output) {
      result.diagnostics = "Cannot write the script build wrapper.";
      return result;
    }
#if defined(_WIN32)
    result.diagnostics = "Native project script compilation currently requires "
                         "the Linux desktop toolchain.";
    return result;
#else
    result.library = build / "component.so";
    const auto log = build / "compiler.log";
    std::vector<std::string> args = {options.compiler,
                                     "-Wall",
                                     "-Wextra",
                                     "-Werror=overloaded-virtual",
                                     "-std=c++20",
                                     "-DILMEEE_SCRIPT_BUILD",
                                     "-shared",
                                     "-fPIC",
                                     "-fvisibility=hidden",
                                     "-g",
                                     "-Wl,-z,defs",
                                     "-Wl,-rpath,$ORIGIN",
                                     "-I",
                                     options.sdkInclude.string(),
                                     "-I",
                                     path.parent_path().string(),
                                     path.string(),
                                     wrapper.string(),
                                     "-o",
                                     result.library.string()};
    if (!options.androidAbi.empty()) {
      if (options.androidApi < 26 || options.androidApi > 100)
        throw std::runtime_error("Invalid Android script API level.");
      args.insert(args.begin() + 1,
                  {"--target=" + AndroidTriple(options.androidAbi) + std::to_string(options.androidApi),
                   "-O2", "-stdlib=libc++", "-nostdlib++", "-Wl,-z,max-page-size=16384"});
      // One shared libc++ for the player and all modules (no static STL copies).
      args.push_back("-lc++_shared");
    }
    std::vector<char *> argv;
    for (auto &arg : args)
      argv.push_back(arg.data());
    argv.push_back(nullptr);
    const int descriptor =
        open(log.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (descriptor < 0) {
      result.diagnostics = std::strerror(errno);
      return result;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, descriptor, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, descriptor, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, descriptor);
    pid_t pid;
    const int status = posix_spawnp(&pid, options.compiler.c_str(), &actions,
                                    nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(descriptor);
    if (status) {
      result.diagnostics =
          "Cannot start C++ compiler: " + std::string(std::strerror(status));
      return result;
    }
    int exitStatus = 0;
    pid_t waited;
    do {
      waited = waitpid(pid, &exitStatus, 0);
    } while (waited < 0 && errno == EINTR);
    result.diagnostics = ReadLog(log);
    result.ok =
        waited == pid && WIFEXITED(exitStatus) && WEXITSTATUS(exitStatus) == 0;
    if (!result.ok && result.diagnostics.empty())
      result.diagnostics = "C++ compiler did not finish successfully.";
#endif
  } catch (const std::exception &e) {
    result.diagnostics = e.what();
  }
  return result;
#endif
}

struct NativeScripts::Module {
  void *library = nullptr;
  Component *(*create)() = nullptr;
  void (*destroy)(Component *) = nullptr;
  Component *prototype = nullptr;
  std::vector<ScriptValue> defaults;
  ~Module() {
    if (prototype && destroy)
      destroy(prototype);
#ifndef _WIN32
    if (library)
      dlclose(library);
#endif
  }
};
struct NativeScripts::Instance {
  std::shared_ptr<Module> module;
  Component *component = nullptr;
  bool failed = false;
  ~Instance() {
    if (component)
      module->destroy(component);
  }
};
NativeScripts::NativeScripts() = default;
NativeScripts::~NativeScripts() {
  Stop();
  if (job_.valid())
    job_.wait();
}
bool NativeScripts::Configure(const ScriptCompileOptions &options) {
  if (options.project == options_.project &&
      options.sdkInclude == options_.sdkInclude)
    return false;
  Stop();
  modules_.clear();
  errors_.clear();
  options_ = options;
  sourceRevision_ = ScriptRevision(options.project);
  if (!options.project.empty() &&
      !DiscoverNativeScripts(options.project).empty())
    CompileAll();
  return true;
}
bool NativeScripts::IsCompiling() const { return job_.valid(); }
void NativeScripts::CompileAll() {
  if (options_.project.empty())
    return;
  if (active_) {
    if (log)
      log("[WARNING] [Scripts] Stop Play before compiling scripts.");
    return;
  }
  if (job_.valid()) {
    pending_ = true;
    return;
  }
  const auto options = options_;
  sourceRevision_ = ScriptRevision(options.project);
  jobProject_ = options.project.string();
  job_ = std::async(std::launch::async, [options] {
    std::vector<CompiledScript> results;
    std::string error;
    WriteScriptCompilationDatabase(options, error);
    for (const auto &source : DiscoverNativeScripts(options.project))
      results.push_back(CompileNativeScript(options, source));
    if (!error.empty())
      results.push_back({"compile_commands.json", "", error, {}, false});
    return results;
  });
  if (log)
    log("[INFO] [Scripts] Compiling project scripts...");
}
void NativeScripts::Poll() {
  if (active_)
    return;
  const auto now = std::chrono::steady_clock::now();
  if (!options_.project.empty() && now - lastScan_ > std::chrono::seconds(1)) {
    lastScan_ = now;
    if (ScriptRevision(options_.project) != sourceRevision_)
      CompileAll();
  }
  if (!job_.valid() ||
      job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    return;
  try {
    const auto results = job_.get();
    if (jobProject_ == options_.project.string()) {
      std::set<std::string> found;
      for (const auto &result : results) {
        found.insert(result.source);
        std::string error = result.diagnostics;
        const bool ok = result.ok && Load(result, error);
        errors_[result.source] = ok ? "" : error;
        if (log)
          log(std::string(ok ? "[INFO] [Scripts] " : "[ERROR] [Scripts] ") +
              result.source +
              (ok ? ": compiled and validated" : ": compile failed\n" + error));
      }
      std::erase_if(modules_, [&](const auto &entry) {
        return !found.contains(entry.first);
      });
      std::erase_if(errors_, [&](const auto &entry) {
        return !found.contains(entry.first);
      });
    }
  } catch (const std::exception &e) {
    if (log)
      log(std::string("[ERROR] [Scripts] ") + e.what());
  }
  if (pending_) {
    pending_ = false;
    CompileAll();
  }
}
bool NativeScripts::Load(const CompiledScript &result, std::string &error) {
  if (!result.ok) {
    error = result.diagnostics;
    return false;
  }
#ifdef _WIN32
  error = "Native project script loading currently requires Linux.";
  return false;
#else
  auto module = std::make_shared<Module>();
  module->library = dlopen(result.library.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!module->library) {
    error = dlerror();
    return false;
  }
  auto api = reinterpret_cast<unsigned (*)()>(
      dlsym(module->library, "ilmeee_script_api"));
  auto abi = reinterpret_cast<uint64_t (*)()>(
      dlsym(module->library, "ilmeee_script_abi"));
  module->create = reinterpret_cast<Component *(*)()>(
      dlsym(module->library, "ilmeee_script_create"));
  module->destroy = reinterpret_cast<void (*)(Component *)>(
      dlsym(module->library, "ilmeee_script_destroy"));
  if (!api || api() != script::kApiVersion || !abi ||
      abi() != script::kComponentAbi || !module->create || !module->destroy) {
    error = "Script module has an incompatible engine API or compiler ABI. "
            "Recompile using the engine toolchain.";
    return false;
  }
  try {
    module->prototype = module->create();
    if (!module->prototype) {
      error = "Script factory returned no component.";
      return false;
    }
    ScriptProperties properties;
    module->prototype->DescribeProperties(properties);
    if (properties.fields.size() > 1024) {
      error = "A component may expose at most 1024 fields.";
      return false;
    }
    std::set<std::string> names;
    for (const auto &field : properties.fields) {
      if (!field.value || field.name.empty() || field.name.size() > 256 ||
          !names.insert(field.name).second) {
        error = "Exposed script fields must have unique non-empty names.";
        return false;
      }
      if (!ValidField(field)) {
        error = "Field '" + field.name + "' has an unsupported type.";
        return false;
      }
      const auto value = ReadProperty(field);
      if (!Finite(value)) {
        error = "Exposed float and Vec3 defaults must be finite, and lists "
                "may hold at most " +
                std::to_string(kMaxScriptListItems) + " items.";
        return false;
      }
      module->defaults.push_back(value);
    }
  } catch (const std::exception &e) {
    error = e.what();
    return false;
  } catch (...) {
    error =
        "Unknown exception while constructing or inspecting the script class.";
    return false;
  }
  modules_[result.source] = std::move(module);
  errors_[result.source].clear();
  return true;
#endif
}
bool NativeScripts::Ready(const std::string &source) const {
  return modules_.contains(source) && Diagnostics(source).empty();
}
std::vector<std::string> NativeScripts::Sources() const {
  std::set<std::string> sources;
  for (const auto &[source, module] : modules_)
    sources.insert(source);
  for (const auto &[source, error] : errors_)
    if (fs::path(source).extension() == ".cpp")
      sources.insert(source);
  return {sources.begin(), sources.end()};
}
std::string NativeScripts::Diagnostics(const std::string &source) const {
  const auto it = errors_.find(source);
  return it == errors_.end() ? "" : it->second;
}
std::vector<ScriptValue>
NativeScripts::Properties(const NativeScriptBinding &binding) const {
  const auto it = modules_.find(binding.source);
  if (it == modules_.end())
    return binding.values;
  auto values = it->second->defaults;
  for (auto &value : values)
    for (const auto &stored : binding.values)
      if (SameScriptShape(value, stored))
        value = stored;
  return values;
}
void NativeScripts::Start(script::HostApi host,
                          const std::vector<ScriptOwner> &owners) {
  Stop();
  host_ = host;
  active_ = true;
  for (const auto &owner : owners)
    for (const auto &binding : *owner.scripts) {
      if (!binding.enabled)
        continue;
      const auto module = modules_.find(binding.source);
      if (module == modules_.end() || !Ready(binding.source)) {
        if (log)
          log("[WARNING] [Scripts] " + binding.className +
              ": not running; compile this script first.");
        continue;
      }
      auto instance = std::make_unique<Instance>();
      instance->module = module->second;
      try {
        instance->component = instance->module->create();
        if (!instance->component)
          throw std::runtime_error("Script factory returned no instance");
        instance->component->Bind(&host_, owner.id);
        ApplyProperties(*instance->component, binding, &host_);
        instance->component->Awake();
        instance->component->OnEnable();
        instance->component->Start();
        instances_.push_back(std::move(instance));
      } catch (const std::exception &e) {
        if (log)
          log("[ERROR] [Scripts] " + binding.className + ": " + e.what());
      } catch (...) {
        if (log)
          log("[ERROR] [Scripts] " + binding.className +
              ": unknown exception during initialization");
      }
    }
}
void NativeScripts::Update(float deltaTime) {
  if (!std::isfinite(deltaTime) || deltaTime <= 0)
    return;
  for (auto &instance : instances_) {
    if (instance->failed)
      continue;
    try {
      const float step = std::min(deltaTime, 0.25f);
      instance->component->Update(step);
      instance->component->TickCoroutines(step);
    } catch (const std::exception &e) {
      instance->failed = true;
      if (log)
        log(std::string("[ERROR] [Scripts] Update stopped: ") + e.what());
    } catch (...) {
      instance->failed = true;
      if (log)
        log("[ERROR] [Scripts] Update stopped: unknown exception");
    }
  }
}
void NativeScripts::Stop() {
  active_ = false;
  for (auto &instance : instances_) {
    try {
      instance->component->OnDisable();
    } catch (...) {
    }
  }
  instances_.clear();
}
} // namespace ilmeee
