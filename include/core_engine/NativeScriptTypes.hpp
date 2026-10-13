#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace ilmeee {
inline uint64_t NewObjectId() {
  static std::atomic<uint64_t> next{
      static_cast<uint64_t>(
          std::chrono::system_clock::now().time_since_epoch().count()) |
      1u};
  return next.fetch_add(1);
}
enum class ScriptValueType : uint32_t { Float, Bool, Object, Vec3 };
struct ScriptValue {
  std::string name;
  ScriptValueType type = ScriptValueType::Float;
  float number = 0;
  bool boolean = false;
  uint64_t object = 0;
  float vec3[3] = {0, 0, 0};
};
struct NativeScriptBinding {
  std::string source; // project-relative .cpp
  std::string className;
  bool enabled = true;
  std::vector<ScriptValue> values;
};
} // namespace ilmeee
