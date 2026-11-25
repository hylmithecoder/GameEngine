#include "../../../include/core_engine/ModelThumbnail.hpp"
#include "../../../include/core_engine/PMXLoader.hpp"
#include "../../../include/core_engine/SoftwareRaster.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <ufbx.h>
#include <vector>

namespace fs = std::filesystem;

namespace ilmeee {
namespace {

// Bump when the look of the thumbnails changes so old PNGs are re-rendered.
constexpr int kThumbnailVersion = 1;
constexpr int kThumbnailSize = 128;
constexpr int kSupersample = 2;

using raster::Part;
using raster::ResolveFile;

struct ThumbMesh {
  std::vector<glm::vec3> positions; // one per corner, 3 per triangle
  std::vector<glm::vec2> uvs;       // top-left origin
  std::vector<Part> parts;
};

std::string Lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return text;
}

bool LoadPmxThumb(const std::string &path, ThumbMesh &out) {
  pmx::PMXModel model;
  if (!pmx::LoadPMX(path, model))
    return false;
  const fs::path dir = fs::path(path).parent_path();
  uint32_t offset = 0;
  for (const auto &mat : model.materials) {
    if (mat.indexCount <= 0)
      continue;
    Part part;
    part.first = (uint32_t)out.positions.size();
    part.color = mat.diffuse;
    if (mat.textureIndex >= 0 &&
        mat.textureIndex < (int)model.texturePaths.size())
      part.texture = ResolveFile(dir, model.texturePaths[mat.textureIndex]);
    const uint32_t end =
        std::min<uint32_t>(offset + mat.indexCount, model.indices.size());
    for (uint32_t i = offset; i + 2 < end; i += 3) {
      const uint32_t tri[3] = {model.indices[i], model.indices[i + 1],
                               model.indices[i + 2]};
      if (std::max({tri[0], tri[1], tri[2]}) >= model.vertices.size())
        continue; // corrupt index
      for (uint32_t index : tri) {
        const auto &v = model.vertices[index];
        // PMX is left-handed (front faces -Z); mirror Z like SceneRenderer.
        out.positions.push_back({v.position.x, v.position.y, -v.position.z});
        out.uvs.push_back(v.uv);
      }
    }
    offset += mat.indexCount;
    part.count = (uint32_t)out.positions.size() - part.first;
    if (part.count)
      out.parts.push_back(std::move(part));
  }
  return !out.positions.empty();
}

void LoadMtl(const fs::path &file, std::unordered_map<std::string, Part> &out) {
  std::ifstream in(file);
  std::string line, current;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string key;
    ss >> key;
    if (key == "newmtl") {
      ss >> current;
      out[current] = Part{};
    } else if (current.empty()) {
      continue;
    } else if (key == "Kd") {
      ss >> out[current].color.r >> out[current].color.g >>
          out[current].color.b;
    } else if (key == "d") {
      ss >> out[current].color.a;
    } else if (key == "map_Kd") {
      // Options (-s 1 1 1 ...) may precede the file name; it is last.
      std::string token, name;
      while (ss >> token)
        name = token;
      out[current].texture = ResolveFile(file.parent_path(), name);
    }
  }
}

bool LoadObjThumb(const std::string &path, ThumbMesh &out) {
  std::ifstream in(path);
  if (!in)
    return false;
  const fs::path dir = fs::path(path).parent_path();
  std::vector<glm::vec3> positions;
  std::vector<glm::vec2> uvs;
  std::unordered_map<std::string, Part> materials;
  Part part;
  auto flush = [&] {
    part.count = (uint32_t)out.positions.size() - part.first;
    if (part.count)
      out.parts.push_back(part);
    part.first = (uint32_t)out.positions.size();
  };
  std::string line;
  while (std::getline(in, line)) {
    if (line.size() < 2)
      continue;
    std::istringstream ss(line);
    std::string key;
    ss >> key;
    if (key == "v") {
      glm::vec3 p;
      ss >> p.x >> p.y >> p.z;
      positions.push_back(p);
    } else if (key == "vt") {
      glm::vec2 t;
      ss >> t.x >> t.y;
      uvs.push_back({t.x, 1.0f - t.y}); // OBJ V origin is bottom-left
    } else if (key == "mtllib") {
      std::string name;
      std::getline(ss >> std::ws, name);
      if (!name.empty() && name.back() == '\r')
        name.pop_back();
      if (auto file = ResolveFile(dir, name); !file.empty())
        LoadMtl(file, materials);
    } else if (key == "usemtl") {
      std::string name;
      ss >> name;
      flush();
      const auto it = materials.find(name);
      const uint32_t first = part.first;
      part = it != materials.end() ? it->second : Part{};
      part.first = first;
    } else if (key == "f") {
      std::vector<std::pair<int, int>> face; // position, uv (0 = none)
      std::string token;
      while (ss >> token) {
        int vi = 0, ti = 0;
        if (std::sscanf(token.c_str(), "%d/%d", &vi, &ti) < 1)
          continue;
        if (vi < 0)
          vi += (int)positions.size() + 1;
        if (ti < 0)
          ti += (int)uvs.size() + 1;
        if (vi < 1 || vi > (int)positions.size())
          continue;
        face.push_back({vi, (ti >= 1 && ti <= (int)uvs.size()) ? ti : 0});
      }
      for (size_t i = 2; i < face.size(); ++i)
        for (const auto &corner : {face[0], face[i - 1], face[i]}) {
          out.positions.push_back(positions[corner.first - 1]);
          out.uvs.push_back(corner.second ? uvs[corner.second - 1]
                                          : glm::vec2(0.0f));
        }
    }
  }
  flush();
  return !out.positions.empty();
}

bool LoadFbxThumb(const std::string &path, ThumbMesh &out) {
  ufbx_load_opts opts{};
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  ufbx_error err;
  ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &err);
  if (!scene)
    return false;
  const fs::path dir = fs::path(path).parent_path();
  auto text = [](ufbx_string s) {
    return std::string(s.data ? s.data : "", s.length);
  };
  for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
    ufbx_mesh *mesh = scene->meshes.data[mi];
    if (mesh->faces.count == 0)
      continue;
    const bool hasXform = mesh->instances.count > 0;
    const ufbx_matrix xform =
        hasXform ? mesh->instances.data[0]->geometry_to_world : ufbx_matrix{};
    std::vector<uint32_t> tri(
        (mesh->max_face_triangles ? mesh->max_face_triangles : 1) * 3);
    auto emit = [&](ufbx_material *mat, const uint32_t *faces, size_t count) {
      Part part;
      part.first = (uint32_t)out.positions.size();
      if (mat) {
        const ufbx_material_map &diffuse = mat->fbx.diffuse_color;
        if (diffuse.value_components >= 3)
          part.color = {(float)diffuse.value_vec3.x, (float)diffuse.value_vec3.y,
                        (float)diffuse.value_vec3.z, 1.0f};
        ufbx_texture *tex =
            diffuse.texture ? diffuse.texture : mat->pbr.base_color.texture;
        if (tex) {
          std::error_code ec;
          const std::string abs = text(tex->absolute_filename);
          part.texture = !abs.empty() && fs::is_regular_file(abs, ec)
                             ? abs
                             : ResolveFile(dir, text(tex->relative_filename));
        }
      }
      for (size_t f = 0; f < count; ++f) {
        const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh,
                                                 mesh->faces.data[faces[f]]);
        for (uint32_t c = 0; c < n * 3; ++c) {
          ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, tri[c]);
          if (hasXform)
            p = ufbx_transform_position(&xform, p);
          out.positions.push_back({(float)p.x, (float)p.y, (float)p.z});
          glm::vec2 uv(0.0f);
          if (mesh->vertex_uv.exists) {
            ufbx_vec2 t = ufbx_get_vertex_vec2(&mesh->vertex_uv, tri[c]);
            uv = {(float)t.x, 1.0f - (float)t.y};
          }
          out.uvs.push_back(uv);
        }
      }
      part.count = (uint32_t)out.positions.size() - part.first;
      if (part.count)
        out.parts.push_back(std::move(part));
    };
    if (mesh->material_parts.count > 0) {
      for (size_t pi = 0; pi < mesh->material_parts.count; ++pi) {
        const ufbx_mesh_part &part = mesh->material_parts.data[pi];
        emit(pi < mesh->materials.count ? mesh->materials.data[pi] : nullptr,
             part.face_indices.data, part.face_indices.count);
      }
    } else {
      std::vector<uint32_t> all(mesh->faces.count);
      for (size_t i = 0; i < all.size(); ++i)
        all[i] = (uint32_t)i;
      emit(mesh->materials.count ? mesh->materials.data[0] : nullptr,
           all.data(), all.size());
    }
  }
  ufbx_free_scene(scene);
  return !out.positions.empty();
}

} // namespace

bool IsPreviewableModel(const std::string &path) {
  const std::string ext = Lowercase(fs::path(path).extension().string());
  return ext == ".pmx" || ext == ".obj" || ext == ".fbx";
}

bool RenderModelThumbnail(const std::string &modelPath,
                          const std::string &pngPath, int size,
                          std::string &error) {
  ThumbMesh mesh;
  const std::string ext = Lowercase(fs::path(modelPath).extension().string());
  const bool loaded = ext == ".pmx"   ? LoadPmxThumb(modelPath, mesh)
                      : ext == ".obj" ? LoadObjThumb(modelPath, mesh)
                      : ext == ".fbx" ? LoadFbxThumb(modelPath, mesh)
                                      : false;
  if (!loaded) {
    error = "no geometry";
    return false;
  }

  // Three-quarter view from the front (+Z), slightly above, orthographic.
  const raster::View view = raster::MakeView(30.0f, 15.0f);
  glm::vec2 lo(INFINITY), hi(-INFINITY);
  for (const auto &p : mesh.positions) {
    const glm::vec2 v(glm::dot(p, view.right), glm::dot(p, view.up));
    lo = glm::min(lo, v);
    hi = glm::max(hi, v);
  }
  const float extent = std::max(hi.x - lo.x, hi.y - lo.y);
  if (!(extent > 1e-9f) || !std::isfinite(extent)) {
    error = "degenerate bounds";
    return false;
  }
  raster::Canvas canvas(size, kSupersample);
  std::unordered_map<std::string, raster::Texture> textures;
  canvas.Draw(mesh.positions, mesh.uvs, mesh.parts, view, (lo + hi) * 0.5f,
              size * 0.88f / extent, textures);
  if (!raster::WritePng(pngPath, canvas.Resolve(glm::vec4(0.0f)), size, size)) {
    error = "cannot write " + pngPath;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------

ModelThumbnailCache::~ModelThumbnailCache() {
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
    queue_.clear();
  }
  wake_.notify_all();
  if (worker_.joinable())
    worker_.join();
}

std::string ModelThumbnailCache::Request(const std::string &modelPath) {
  std::lock_guard lock(mutex_);
  if (auto it = entries_.find(modelPath); it != entries_.end())
    return it->second.state == State::Ready ? it->second.png : std::string();

  Entry &entry = entries_[modelPath];
  std::error_code ec;
  const auto size = fs::file_size(modelPath, ec);
  const auto mtime = fs::last_write_time(modelPath, ec);
  if (ec) {
    entry.state = State::Failed;
    return {};
  }
  // FNV-1a over everything that should invalidate the picture.
  const std::string key = modelPath + '|' + std::to_string(size) + '|' +
                          std::to_string(mtime.time_since_epoch().count()) +
                          '|' + std::to_string(kThumbnailVersion) + '|' +
                          std::to_string(kThumbnailSize);
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char c : key) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  char name[32];
  std::snprintf(name, sizeof(name), "%016llx.png", (unsigned long long)hash);
  entry.png = (UserDataRoot() / "thumbnails" / name).string();
  if (fs::is_regular_file(entry.png, ec)) {
    entry.state = State::Ready;
    return entry.png;
  }
  queue_.push_back(modelPath);
  if (!worker_.joinable())
    worker_ = std::thread(&ModelThumbnailCache::Worker, this);
  wake_.notify_one();
  return {};
}

void ModelThumbnailCache::Worker() {
  for (;;) {
    std::string model, png;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (stopping_)
        return;
      model = queue_.front();
      queue_.pop_front();
      png = entries_[model].png;
    }
    std::string error;
    bool ok = false;
    try {
      ok = RenderModelThumbnail(model, png, kThumbnailSize, error);
    } catch (const std::exception &e) {
      error = e.what();
    }
    if (!ok)
      std::cout << "[Thumbnail] " << model << ": " << error << std::endl;
    std::lock_guard lock(mutex_);
    entries_[model].state = ok ? State::Ready : State::Failed;
  }
}

} // namespace ilmeee
