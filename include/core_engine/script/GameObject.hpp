#pragma once
#include <cstdint>
#include <string>

namespace ilmeee::script {
inline constexpr uint32_t kApiVersion = 1;
struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3 operator+(Vec3 other) const {
    return {x + other.x, y + other.y, z + other.z};
  }
  Vec3 operator*(float value) const {
    return {x * value, y * value, z * value};
  }
};
struct Transform {
  Vec3 position, rotation, scale{1, 1, 1};
};
// Only the editor/player calls these callbacks, on its scene thread.
struct HostApi {
  void *context = nullptr;
  bool (*valid)(void *, uint64_t) = nullptr;
  bool (*getTransform)(void *, uint64_t, Transform *) = nullptr;
  bool (*setTransform)(void *, uint64_t, const Transform *) = nullptr;
  uint64_t (*find)(void *, const char *) = nullptr;
  const char *(*name)(void *, uint64_t) = nullptr;
  bool (*keyDown)(void *, const char *) = nullptr;
  void (*log)(void *, const char *) = nullptr;
};
class GameObject {
public:
  GameObject() = default;
  GameObject(uint64_t id, const HostApi *host) : id_(id), host_(host) {}
  uint64_t Id() const { return id_; }
  bool IsValid() const {
    return host_ && host_->valid && host_->valid(host_->context, id_);
  }
  std::string Name() const {
    if (!IsValid() || !host_->name)
      return {};
    const char *value = host_->name(host_->context, id_);
    return value ? value : "";
  }
  Transform GetTransform() const {
    Transform value;
    if (host_ && host_->getTransform)
      host_->getTransform(host_->context, id_, &value);
    return value;
  }
  bool SetTransform(const Transform &value) const {
    return host_ && host_->setTransform &&
           host_->setTransform(host_->context, id_, &value);
  }
  Vec3 GetPosition() const { return GetTransform().position; }
  bool SetPosition(Vec3 position) const {
    auto value = GetTransform();
    value.position = position;
    return SetTransform(value);
  }
  bool Translate(Vec3 delta) const {
    return SetPosition(GetPosition() + delta);
  }
  GameObject Find(const std::string &name) const {
    return {host_ && host_->find ? host_->find(host_->context, name.c_str())
                                 : 0,
            host_};
  }

private:
  uint64_t id_ = 0;
  const HostApi *host_ = nullptr;
};
class GameObjectRef {
public:
  uint64_t id = 0; // serialized identity, never a mesh index or raw pointer
  GameObject Get() const { return {id, host_}; }
  bool IsValid() const { return Get().IsValid(); }
  void Bind(const HostApi *host) { host_ = host; }

private:
  const HostApi *host_ = nullptr;
};
} // namespace ilmeee::script
using GameObjectRef = ilmeee::script::GameObjectRef;
