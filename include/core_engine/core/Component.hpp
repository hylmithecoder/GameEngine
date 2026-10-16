#pragma once
#include "../NativeScriptTypes.hpp"
#include "../script/Coroutine.hpp"
#include "../script/SceneObjects.hpp"
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

namespace ilmeee::script {
// Maps a C++ field type to its Inspector type. Storage is what the host reads
// and writes (ObjectRef<T> is stored through its GameObjectRef base).
template <class T, class = void> struct FieldType {};
template <> struct FieldType<float> {
  static constexpr auto type = ScriptValueType::Float;
  using Storage = float;
};
template <> struct FieldType<bool> {
  static constexpr auto type = ScriptValueType::Bool;
  using Storage = bool;
};
template <> struct FieldType<std::string> {
  static constexpr auto type = ScriptValueType::String;
  using Storage = std::string;
};
template <> struct FieldType<Vec3> {
  static constexpr auto type = ScriptValueType::Vec3;
  using Storage = Vec3;
};
template <class T>
struct FieldType<T, std::enable_if_t<std::is_base_of_v<GameObjectRef, T>>> {
  static constexpr auto type = ScriptValueType::Object;
  using Storage = GameObjectRef;
};
template <class T>
concept InspectorScalar = requires { FieldType<T>::type; };

// Type-erased std::vector<T>, built inside the script so the host never
// needs T. Elements travel as FieldType<T>::Storage.
struct ListAccess {
  size_t (*size)(const void *list);
  void (*resize)(void *list, size_t count);
  void (*get)(const void *list, size_t index, void *out);
  void (*set)(void *list, size_t index, const void *in);
};
template <InspectorScalar T> inline constexpr ListAccess kListAccess{
    [](const void *list) { return static_cast<const std::vector<T> *>(list)->size(); },
    [](void *list, size_t count) { static_cast<std::vector<T> *>(list)->resize(count); },
    [](const void *list, size_t index, void *out) {
      using Storage = typename FieldType<T>::Storage;
      *static_cast<Storage *>(out) = (*static_cast<const std::vector<T> *>(list))[index];
    },
    [](void *list, size_t index, const void *in) {
      using Storage = typename FieldType<T>::Storage;
      auto &&item = (*static_cast<std::vector<T> *>(list))[index];
      if constexpr (std::is_same_v<T, bool>) // vector<bool> hands out proxies
        item = *static_cast<const bool *>(in);
      else
        static_cast<Storage &>(item) = *static_cast<const Storage *>(in);
    }};
} // namespace ilmeee::script

struct ScriptProperty {
  std::string name;
  ilmeee::ScriptValueType type;
  void *value;
  // List only.
  ilmeee::ScriptValueType elementType = ilmeee::ScriptValueType::Float;
  const ilmeee::script::ListAccess *list = nullptr;
};
// Field(name, member) exposes a member in the Inspector: float, bool,
// std::string, Vec3, GameObjectRef, ObjectRef<T>, or a std::vector of any of
// those.
class ScriptProperties {
public:
  std::vector<ScriptProperty> fields;
  template <ilmeee::script::InspectorScalar T>
  void Field(const char *name, T &value) {
    using Traits = ilmeee::script::FieldType<T>;
    fields.push_back({name, Traits::type,
                      static_cast<typename Traits::Storage *>(&value)});
  }
  template <ilmeee::script::InspectorScalar T>
  void Field(const char *name, std::vector<T> &list) {
    fields.push_back({name, ilmeee::ScriptValueType::List, &list,
                      ilmeee::script::FieldType<T>::type,
                      &ilmeee::script::kListAccess<T>});
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
  // Only on the first frame the key is held (menus, dialog, interaction).
  bool IsKeyPressed(const char *key) const {
    return host_ && host_->keyPressed && host_->keyPressed(host_->context, key);
  }
  void Log(const std::string &message) const {
    if (host_ && host_->log)
      host_->log(host_->context, message.c_str());
  }
  void LogWarning(const std::string &message) const {
    Log("[WARNING] " + message);
  }
  // Also brings the Console to the front in the editor.
  void LogError(const std::string &message) const {
    Log("[ERROR] " + message);
  }
  virtual ~Component() = default;
  virtual void Awake() {}
  virtual void Start() {}
  virtual void Update(float deltaTime) {}
  virtual void OnEnable() {}
  virtual void OnDisable() {}
  virtual void DescribeProperties(ScriptProperties &) {}

  // Coroutines (see script/Coroutine.hpp): run until the first co_await now,
  // then advance once per frame after Update.
  ilmeee::script::CoroutineId StartCoroutine(ilmeee::script::Coroutine coroutine) {
    return coroutines_.Start(std::move(coroutine));
  }
  void StopCoroutine(ilmeee::script::CoroutineId id) { coroutines_.Stop(id); }
  void StopAllCoroutines() { coroutines_.StopAll(); }
  bool IsCoroutineRunning(ilmeee::script::CoroutineId id) const {
    return coroutines_.IsRunning(id);
  }
  // Called by the engine after Update; virtual so it runs in the script
  // module. Do not override.
  virtual void TickCoroutines(float deltaTime) { coroutines_.Tick(deltaTime); }

private:
  const ilmeee::script::HostApi *host_ = nullptr;
  ilmeee::script::CoroutineRunner coroutines_;
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
    (static_cast<uint64_t>(sizeof(std::vector<ScriptProperty>)) << 8) |
    static_cast<uint64_t>(sizeof(ScriptProperty));
} // namespace ilmeee::script
