#include "../../../include/core_engine/ModelDependencies.hpp"
#include "../../../include/core_engine/PMXLoader.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <ufbx.h>

namespace ilmeee {

namespace fs = std::filesystem;

namespace {

std::string LowerExt(const fs::path &p) {
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return ext;
}

std::string Slashes(std::string s) {
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}

bool IsFile(const fs::path &p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

// Collects unique dependencies (several materials often share a texture).
struct Collector {
  std::vector<ModelDependency> &out;
  std::set<std::string> seen;
  void Add(const fs::path &source, const fs::path &relative) {
    if (!IsFile(source))
      return;
    const fs::path rel = relative.lexically_normal();
    if (seen.insert(rel.generic_string()).second)
      out.push_back({source, rel});
  }
};

bool PmxDeps(const fs::path &model, Collector &c, std::string *error) {
  pmx::PMXModel pmxModel;
  if (!pmx::LoadPMX(model.string(), pmxModel)) {
    if (error)
      *error = "cannot read PMX";
    return false;
  }
  const fs::path dir = model.parent_path();
  for (const auto &mat : pmxModel.materials) {
    const int t = mat.textureIndex;
    if (t < 0 || t >= (int)pmxModel.texturePaths.size())
      continue;
    const std::string rel = Slashes(pmxModel.texturePaths[t]);
    if (!rel.empty())
      c.Add(dir / rel, rel);
  }
  return true;
}

bool FbxDeps(const fs::path &model, Collector &c, std::string *error) {
  ufbx_load_opts opts{};
  ufbx_error err;
  ufbx_scene *scene = ufbx_load_file(model.string().c_str(), &opts, &err);
  if (!scene) {
    if (error)
      *error = std::string(err.description.data, err.description.length);
    return false;
  }
  const fs::path dir = model.parent_path();
  auto str = [](ufbx_string s) {
    return std::string(s.data ? s.data : "", s.length);
  };
  for (size_t i = 0; i < scene->materials.count; ++i) {
    ufbx_material *mat = scene->materials.data[i];
    ufbx_texture *tex = mat->fbx.diffuse_color.texture
                            ? mat->fbx.diffuse_color.texture
                            : mat->pbr.base_color.texture;
    if (!tex)
      continue;
    std::string rel = Slashes(str(tex->relative_filename));
    if (rel.empty())
      rel = Slashes(str(tex->filename));
    const fs::path abs = str(tex->absolute_filename);
    const fs::path base = fs::path(rel.empty() ? abs.string() : rel).filename();
    // The game has no absolute paths: keep the texture where the renderer's
    // relative / bare-name fallbacks look for it.
    if (!rel.empty() && IsFile(dir / rel))
      c.Add(dir / rel, rel);
    else if (!base.empty() && IsFile(dir / base))
      c.Add(dir / base, base);
    else if (!abs.empty() && IsFile(abs))
      c.Add(abs, base);
  }
  ufbx_free_scene(scene);
  return true;
}

bool ObjDeps(const fs::path &model, Collector &c, std::string *error) {
  std::ifstream in(model);
  if (!in) {
    if (error)
      *error = "cannot read OBJ";
    return false;
  }
  const fs::path dir = model.parent_path();
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (key != "mtllib")
      continue;
    std::string name;
    std::getline(ls >> std::ws, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == ' '))
      name.pop_back();
    name = Slashes(name);
    if (name.empty())
      continue;
    c.Add(dir / name, name);
    // Textures named by the material library, relative to the .obj folder
    // (the last token of a map_* line is the file; options come first).
    std::ifstream mtl(dir / name);
    std::string mline;
    while (std::getline(mtl, mline)) {
      std::istringstream ms(mline);
      std::string mkey;
      ms >> mkey;
      std::transform(mkey.begin(), mkey.end(), mkey.begin(),
                     [](unsigned char ch) { return (char)std::tolower(ch); });
      if (mkey.rfind("map_", 0) != 0 && mkey != "bump" && mkey != "disp" &&
          mkey != "decal" && mkey != "refl")
        continue;
      std::string tok, last;
      while (ms >> tok)
        last = tok;
      last = Slashes(last);
      if (!last.empty())
        c.Add(dir / last, last);
    }
  }
  return true;
}

} // namespace

bool ListModelDependencies(const fs::path &model,
                           std::vector<ModelDependency> &out,
                           std::string *error) {
  Collector c{out, {}};
  const std::string ext = LowerExt(model);
  if (ext == ".pmx")
    return PmxDeps(model, c, error);
  if (ext == ".fbx")
    return FbxDeps(model, c, error);
  return ObjDeps(model, c, error);
}

} // namespace ilmeee
