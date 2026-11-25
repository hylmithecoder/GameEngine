#pragma once
// `.ilmeeeprefab`: a reusable object subtree (root + children + all of their
// components), spawned any number of times from the editor or from scripts.
//
// The file is an `.ilmeeescene` body with magic 'ILMP' instead of 'ILMS'
// (same versioning, see IlmeeeScene.hpp). Rules on top of that:
//   - entities[0] is the root and the only entity with parent -1;
//   - parent and movementRig.camera are indices into this file;
//   - only world objects: Canvas/UI elements are never part of a prefab;
//   - backgroundColor / npr / bloom are written but ignored.
//
// Prefab v1 is "unpacked": a spawned copy has no link back to the file.
//
// Everything here is pure data (no renderer), so it is unit-testable. The
// renderer side (capture from / spawn into a SceneRenderer) lives in
// SceneLoader.hpp.

#include "IlmeeeScene.hpp"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace ilmeee {

inline constexpr const char *kPrefabExtension = ".ilmeeeprefab";

inline bool IsPrefabPath(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  for (char &c : ext)
    c = (char)std::tolower((unsigned char)c);
  return ext == kPrefabExtension;
}

// A usable prefab: non-empty, root first, every other parent before its
// child (so a single forward pass can link the hierarchy), no UI.
inline bool ValidPrefab(const IlmeeeScene &prefab) {
  if (prefab.entities.empty() || prefab.entities[0].parent != -1)
    return false;
  for (size_t i = 0; i < prefab.entities.size(); ++i) {
    const auto &e = prefab.entities[i];
    if (e.kind == PrimitiveKind::Canvas || IsUiElementKind(e.kind))
      return false;
    if (i > 0 && (e.parent < 0 || (size_t)e.parent >= i))
      return false;
  }
  return true;
}

// Writes to `<path>.tmp` then renames, so a crash never leaves half a file.
inline bool SavePrefab(const std::string &path, const IlmeeeScene &prefab) {
  if (!ValidPrefab(prefab))
    return false;
  const std::string tmp = path + ".tmp";
  if (!detail::SaveSceneContainer(tmp, prefab, "ILMP"))
    return false;
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

inline bool LoadPrefab(const std::string &path, IlmeeeScene &out) {
  IlmeeeScene loaded;
  if (!detail::LoadSceneContainer(path, loaded, "ILMP") ||
      !ValidPrefab(loaded))
    return false;
  out = std::move(loaded);
  return true;
}

// The subtree under `root` (root first, then its descendants in scene
// order) with parent / rig-camera indices rebased onto the result. The root
// keeps its local transform and becomes parentless. A rig camera outside
// the subtree becomes -1 (the Game view camera). Empty when `root` is out
// of range or a UI entity.
inline IlmeeeScene ExtractSubtree(const IlmeeeScene &scene, int root) {
  IlmeeeScene out;
  out.backgroundColor = scene.backgroundColor;
  out.npr = scene.npr;
  out.bloom = scene.bloom;
  const int n = (int)scene.entities.size();
  if (root < 0 || root >= n)
    return out;
  const auto inSubtree = [&](int i) {
    for (int guard = 0; i >= 0 && guard <= n; ++guard) {
      if (i == root)
        return true;
      i = scene.entities[(size_t)i].parent;
      if (i >= n)
        return false;
    }
    return false;
  };
  std::vector<int> remap((size_t)n, -1);
  std::vector<int> order{root};
  for (int i = 0; i < n; ++i)
    if (i != root && inSubtree(i))
      order.push_back(i);
  // Scene files list parents before children; keep that true for the copy
  // even if the source had a child before its parent.
  std::vector<int> sorted;
  std::vector<bool> placed((size_t)n, false);
  while (sorted.size() < order.size()) {
    bool progress = false;
    for (int i : order) {
      if (placed[(size_t)i])
        continue;
      const int p = scene.entities[(size_t)i].parent;
      if (i == root || (p >= 0 && placed[(size_t)p])) {
        placed[(size_t)i] = true;
        remap[(size_t)i] = (int)sorted.size();
        sorted.push_back(i);
        progress = true;
      }
    }
    if (!progress)
      break; // cycle: LoadScene already breaks these, so unreachable
  }
  for (int i : sorted) {
    SceneEntity e = scene.entities[(size_t)i];
    if (IsUiElementKind(e.kind) || e.kind == PrimitiveKind::Canvas) {
      if (i == root)
        return IlmeeeScene{};
      continue;
    }
    e.parent = i == root ? -1 : remap[(size_t)e.parent];
    const int cam = e.movementRig.camera;
    e.movementRig.camera = cam >= 0 && cam < n ? remap[(size_t)cam] : -1;
    out.entities.push_back(std::move(e));
  }
  return out;
}

namespace detail {
template <class Fn> void ForEachObjectReference(SceneEntity &e, Fn &&fn) {
  for (auto &binding : e.scripts)
    for (auto &value : binding.values) {
      if (value.type == ScriptValueType::Object)
        fn(value.object);
      if (value.type == ScriptValueType::List &&
          value.elementType == ScriptValueType::Object)
        for (auto &item : value.items)
          fn(item.object);
    }
  fn(e.dialog.box);
  fn(e.dialog.text);
  fn(e.dialog.speaker);
  fn(e.dialog.prompt);
}
} // namespace detail

// Gives every entity a new objectId, and points references between them
// (script Object fields and list items, Dialog element ids) at the new ids,
// so two copies never reference each other. References to objects outside
// the prefab keep their id: they work again when the copy lands in the
// scene that owns that object, and are simply invalid elsewhere.
// Returns old id -> new id.
inline std::unordered_map<uint64_t, uint64_t>
AssignFreshObjectIds(IlmeeeScene &prefab) {
  std::unordered_map<uint64_t, uint64_t> ids;
  for (auto &e : prefab.entities) {
    const uint64_t fresh = NewObjectId();
    if (e.objectId)
      ids[e.objectId] = fresh;
    e.objectId = fresh;
  }
  for (auto &e : prefab.entities)
    detail::ForEachObjectReference(e, [&](uint64_t &ref) {
      if (const auto it = ids.find(ref); ref && it != ids.end())
        ref = it->second;
    });
  return ids;
}

// "Object/Script.field" for every non-empty reference that points outside
// the prefab; the editor warns about these when a prefab is created.
inline std::vector<std::string> ExternalReferences(const IlmeeeScene &prefab) {
  std::unordered_map<uint64_t, bool> inside;
  for (const auto &e : prefab.entities)
    inside[e.objectId] = true;
  std::vector<std::string> out;
  for (const auto &e : prefab.entities)
    for (const auto &binding : e.scripts)
      for (const auto &value : binding.values) {
        bool external = false;
        if (value.type == ScriptValueType::Object)
          external = value.object && !inside.contains(value.object);
        if (value.type == ScriptValueType::List &&
            value.elementType == ScriptValueType::Object)
          for (const auto &item : value.items)
            external |= item.object && !inside.contains(item.object);
        if (external)
          out.push_back(e.name + "/" + binding.className + "." + value.name);
      }
  return out;
}

// Project-relative paths of every .ilmeeeprefab under <project>/assets,
// sorted (Inspector PrefabRef combo, MCP).
inline std::vector<std::string>
FindPrefabs(const std::filesystem::path &project) {
  namespace fs = std::filesystem;
  std::vector<std::string> found;
  std::error_code ec;
  const fs::path root = project / "assets";
  if (!fs::is_directory(root, ec))
    return found;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec),
      end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code itemError;
    if (it->is_regular_file(itemError) && IsPrefabPath(it->path()))
      found.push_back(it->path().lexically_relative(project).generic_string());
  }
  std::sort(found.begin(), found.end());
  return found;
}

} // namespace ilmeee
