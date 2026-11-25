#pragma once
// Helpers for systems that key runtime state by meshes3d index, used after
// SceneRenderer::RemoveMesh3D renumbers the meshes (objects destroyed during
// Play). `remap[old] = new index, or -1 when the mesh was removed`.

#include <cstddef>
#include <utility>
#include <vector>

namespace ilmeee {

inline int RemapMeshIndex(const std::vector<int> &remap, size_t index) {
  return index < remap.size() ? remap[index] : -1;
}

// Re-keys an index -> value map, dropping removed meshes.
template <class Map> void RemapMeshKeys(Map &map, const std::vector<int> &remap) {
  Map out;
  for (auto &[index, value] : map)
    if (const int to = RemapMeshIndex(remap, index); to >= 0)
      out.emplace(static_cast<size_t>(to), std::move(value));
  map = std::move(out);
}

// Re-indexes (a, b) pairs, dropping pairs with a removed mesh.
inline void RemapMeshPairs(std::vector<std::pair<size_t, size_t>> &pairs,
                           const std::vector<int> &remap) {
  std::vector<std::pair<size_t, size_t>> out;
  out.reserve(pairs.size());
  for (const auto &[a, b] : pairs) {
    const int na = RemapMeshIndex(remap, a), nb = RemapMeshIndex(remap, b);
    if (na >= 0 && nb >= 0)
      out.emplace_back(static_cast<size_t>(na), static_cast<size_t>(nb));
  }
  pairs = std::move(out);
}

} // namespace ilmeee
