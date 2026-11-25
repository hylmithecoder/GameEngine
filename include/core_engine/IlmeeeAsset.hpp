#pragma once
// `.ilmeeeasset` — values of one ScriptableObject (data asset).
//
// Binary format v1.0 (little-endian, strings as uint16 length + bytes):
//
//   uint8  magic[4]      = 'I','L','M','A'
//   uint16 versionMajor  = 1
//   uint16 versionMinor  = 0
//   string source        project-relative .cpp of the ScriptableObject class
//   string className     must equal the source's file stem
//   uint32 valueCount    (<= 1024)
//   per value: the .ilmeeescene v1.14+ script value layout
//              (detail::WriteScriptValue), types up to Asset
//
// Only fields the user changed need to be stored: missing ones fall back to
// the class defaults, exactly like script values on a scene object.

#include "IlmeeeScene.hpp"
#include <filesystem>
#include <system_error>

namespace ilmeee {

inline constexpr const char *kDataAssetExtension = ".ilmeeeasset";
constexpr uint16_t kAssetFormatMinor = 0;

struct DataAssetFile {
  std::string source;    // e.g. "assets/scripts/EnemyStats.cpp"
  std::string className; // e.g. "EnemyStats"
  std::vector<ScriptValue> values;
};

inline bool SaveDataAsset(const std::string &path, const DataAssetFile &asset) {
  // Write next to the target and rename, so a crash never leaves half a file.
  const std::string temp = path + ".tmp";
  {
    std::ofstream f(temp, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
      return false;
    const char magic[4] = {'I', 'L', 'M', 'A'};
    detail::WriteBytes(f, magic, 4);
    detail::WriteU16(f, 1);
    detail::WriteU16(f, kAssetFormatMinor);
    detail::WriteString(f, asset.source);
    detail::WriteString(f, asset.className);
    const auto count = std::min<size_t>(asset.values.size(), 1024);
    detail::WriteU32(f, static_cast<uint32_t>(count));
    for (size_t i = 0; i < count; ++i)
      detail::WriteScriptValue(f, asset.values[i]);
    f.flush();
    if (!f)
      return false;
  }
  std::error_code ec;
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(temp, ec);
    return false;
  }
  return true;
}

inline bool LoadDataAsset(const std::string &path, DataAssetFile &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f.is_open())
    return false;
  char magic[4];
  uint16_t major = 0, minor = 0;
  if (!f.read(magic, 4) || std::memcmp(magic, "ILMA", 4) != 0 ||
      !detail::ReadU16(f, major) || !detail::ReadU16(f, minor) || major != 1 ||
      minor > kAssetFormatMinor)
    return false;
  DataAssetFile asset;
  uint32_t count = 0;
  if (!detail::ReadString(f, asset.source) ||
      !detail::ReadString(f, asset.className) || !detail::ReadU32(f, count) ||
      count > 1024 || asset.className.empty())
    return false;
  asset.values.resize(count);
  for (auto &value : asset.values)
    if (!detail::ReadScriptValue(f, value, ScriptValueType::Asset))
      return false;
  out = std::move(asset);
  return true;
}

// Every Asset path stored in these values (fields and List items).
inline void CollectAssetPaths(const std::vector<ScriptValue> &values,
                              std::vector<std::string> &out) {
  for (const auto &value : values) {
    if (value.type == ScriptValueType::Asset && !value.string.empty())
      out.push_back(value.string);
    if (value.type == ScriptValueType::List &&
        value.elementType == ScriptValueType::Asset)
      for (const auto &item : value.items)
        if (!item.string.empty())
          out.push_back(item.string);
  }
}

struct DataAssetEntry {
  std::string path;      // project-relative, generic separators
  std::string className;
};
// Data assets under <project>/assets. `className` empty = all classes.
inline std::vector<DataAssetEntry>
FindDataAssets(const std::filesystem::path &project,
               const std::string &className = {}) {
  namespace fs = std::filesystem;
  std::vector<DataAssetEntry> found;
  std::error_code ec;
  const fs::path root = project / "assets";
  if (!fs::is_directory(root, ec))
    return found;
  fs::recursive_directory_iterator it(
      root, fs::directory_options::skip_permission_denied, ec),
      end;
  for (; !ec && it != end; it.increment(ec)) {
    std::error_code itemError;
    if (!it->is_regular_file(itemError) ||
        it->path().extension() != kDataAssetExtension)
      continue;
    DataAssetFile asset;
    if (!LoadDataAsset(it->path().string(), asset) ||
        (!className.empty() && asset.className != className))
      continue;
    found.push_back(
        {it->path().lexically_relative(project).generic_string(),
         asset.className});
  }
  std::sort(found.begin(), found.end(),
            [](const auto &a, const auto &b) { return a.path < b.path; });
  return found;
}

} // namespace ilmeee
