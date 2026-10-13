#pragma once
#include "../NativeScriptTypes.hpp"
#include "../script/GameObject.hpp"

struct ScriptProperty {
  std::string name;
  ilmeee::ScriptValueType type;
  void *value;
};
class ScriptProperties {
public:
  std::vector<ScriptProperty> fields;
  void Field(const char *name, float &value) {
    fields.push_back({name, ilmeee::ScriptValueType::Float, &value});
  }
  void Field(const char *name, bool &value) {
    fields.push_back({name, ilmeee::ScriptValueType::Bool, &value});
  }
  void Field(const char *name, GameObjectRef &value) {
    fields.push_back({name, ilmeee::ScriptValueType::Object, &value});
  }
};

class Component {
public:
  ilmeee::script::GameObject gameObject;
  void Bind(const ilmeee::script::HostApi *host, uint64_t owner) {
    host_ = host;
    gameObject = {owner, host};
  }
  bool IsKeyDown(const char *key) const {
    return host_ && host_->keyDown && host_->keyDown(host_->context, key);
  }
  void Log(const std::string &message) const {
    if (host_ && host_->log)
      host_->log(host_->context, message.c_str());
  }
  virtual ~Component() = default;
  virtual void Awake() {}
  virtual void Start() {}
  virtual void Update(float deltaTime) {}
  virtual void OnEnable() {}
  virtual void OnDisable() {}
  virtual void DescribeProperties(ScriptProperties &) {}

private:
  const ilmeee::script::HostApi *host_ = nullptr;
};

namespace ilmeee::script {
// Check the standard-library and SDK layouts before exchanging properties.
inline constexpr uint64_t kComponentAbi =
#if defined(_LIBCPP_VERSION)
    (uint64_t{2} << 56) |
#elif defined(__GLIBCXX__)
    (uint64_t{1} << 56) |
#if defined(_GLIBCXX_USE_CXX11_ABI)
    (static_cast<uint64_t>(_GLIBCXX_USE_CXX11_ABI) << 55) |
#endif
#endif
    (static_cast<uint64_t>(sizeof(Component)) << 32) |
    (static_cast<uint64_t>(sizeof(HostApi)) << 24) |
    (static_cast<uint64_t>(sizeof(std::string)) << 16) |
    (static_cast<uint64_t>(sizeof(std::vector<ScriptProperty>)) << 8);
} // namespace ilmeee::script
