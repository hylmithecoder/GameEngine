#pragma once
#include "NativeScriptTypes.hpp"
#include "core/Component.hpp"
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

namespace ilmeee {
struct ScriptCompileOptions {
  std::filesystem::path project, sdkInclude, cache;
  std::string compiler = "c++";
  // Empty: desktop. Android modules use the exact NDK recorded by the player.
  std::string androidAbi;
  int androidApi = 26;
};
struct CompiledScript {
  std::string source, className, diagnostics;
  std::filesystem::path library;
  bool ok = false;
};
ScriptCompileOptions
DefaultScriptCompileOptions(const std::filesystem::path &project);
ScriptCompileOptions AndroidScriptCompileOptions(
    const std::filesystem::path &project,
    const std::filesystem::path &androidPlayer, const std::string &abi);
std::vector<std::string>
DiscoverNativeScripts(const std::filesystem::path &project);
CompiledScript CompileNativeScript(const ScriptCompileOptions &options,
                                   const std::string &source);
bool WriteScriptCompilationDatabase(const ScriptCompileOptions &options,
                                    std::string &error);
struct ScriptOwner {
  uint64_t id;
  std::vector<NativeScriptBinding> *scripts;
};

class NativeScripts {
public:
  NativeScripts();
  ~NativeScripts();
  bool Configure(const ScriptCompileOptions &options);
  void CompileAll();
  CompiledScript CompileAndLoad(const std::string &source);
  void Poll();
  bool IsCompiling() const;
  bool Ready(const std::string &source) const;
  std::vector<std::string> Sources() const;
  std::string Diagnostics(const std::string &source) const;
  std::vector<ScriptValue> Properties(const NativeScriptBinding &binding) const;
  bool Load(const CompiledScript &script, std::string &error);
  void Start(script::HostApi host, const std::vector<ScriptOwner> &owners);
  void DispatchPhysicsEvents(
      const std::vector<std::pair<uint64_t, uint64_t>> &triggers,
      const std::vector<std::pair<uint64_t, uint64_t>> &collisions);
  void Update(float deltaTime);
  void Stop();
  std::function<void(const std::string &)> log;

private:
  struct Module;
  struct Instance;
  ScriptCompileOptions options_;
  std::future<std::vector<CompiledScript>> job_;
  std::string jobProject_;
  bool pending_ = false;
  bool active_ = false;
  std::string sourceRevision_;
  std::chrono::steady_clock::time_point lastScan_{};
  std::unordered_map<std::string, std::shared_ptr<Module>> modules_;
  std::unordered_map<std::string, std::string> errors_;
  std::vector<std::unique_ptr<Instance>> instances_;
  std::set<std::pair<uint64_t, uint64_t>> activeTriggers_;
  std::set<std::pair<uint64_t, uint64_t>> activeCollisions_;
  script::HostApi host_;
};
} // namespace ilmeee
