#pragma once
// ScriptableObject: a data-only class whose values live in a .ilmeeeasset
// file instead of on a scene object (Unity's ScriptableObject). Many objects
// can point at the same asset through AssetRef<T> fields; during Play it is
// loaded once, shared, and read-only.
//
//   // assets/scripts/EnemyStats.hpp
//   class EnemyStats : public ScriptableObject {
//     ILMEEE_SCRIPTABLE_OBJECT(EnemyStats)
//   public:
//     float maxHp = 100;
//     std::string displayName = "Slime";
//     void DescribeProperties(ScriptProperties &p) override {
//       p.Field("Max HP", maxHp);
//       p.Field("Name", displayName);
//     }
//   };
//
// Like components, the class name must match the file name and the class
// needs both EnemyStats.hpp and EnemyStats.cpp in assets/scripts/. Scripts
// that use it include EnemyStats.hpp; keep the methods they call inline in
// that header (each script is its own shared library).
#include "../core/Component.hpp"

class ScriptableObject {
public:
  virtual ~ScriptableObject() = default;
  // Fields shown in the asset's Inspector and saved in the .ilmeeeasset.
  virtual void DescribeProperties(ScriptProperties &) {}
  // After the stored values were applied, once per Play session.
  virtual void OnLoad() {}
  // Messages go to Console > Output like Component::Log.
  void Log(const std::string &message) const {
    if (host_ && host_->log)
      host_->log(host_->context, message.c_str());
  }
  // Called by the engine before OnLoad. Do not call.
  void BindHost(const ilmeee::script::HostApi *host) { host_ = host; }

private:
  const ilmeee::script::HostApi *host_ = nullptr;
};

// Names the class for AssetRef<T> and the editor's class check. Put it at
// the top of the class body; it leaves the access at `private`.
#define ILMEEE_SCRIPTABLE_OBJECT(Name)                                       \
public:                                                                      \
  static constexpr const char *kAssetClass = #Name;                          \
                                                                             \
private:
