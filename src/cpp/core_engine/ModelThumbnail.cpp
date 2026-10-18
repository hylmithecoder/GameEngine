#include "../../../include/core_engine/ModelThumbnail.hpp"
#include "../../../include/core_engine/PMXLoader.hpp"
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
#include <stb/stb_image.h>
#include <ufbx.h>
#include <vector>

// The editor links no other stb_image_write TU (only the player has one), so
// keep these symbols private to this file.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fs = std::filesystem;

namespace ilmeee {
namespace {

// Bump when the look of the thumbnails changes so old PNGs are re-rendered.
constexpr int kThumbnailVersion = 1;
constexpr int kThumbnailSize = 128;
constexpr int kSupersample = 2;
constexpr int kMaxTextureSize = 512;

struct Part {
  uint32_t first = 0, count = 0;
  glm::vec4 color{0.8f, 0.8f, 0.8f, 1.0f};
  std::string texture; // resolved path, "" = untextured
};

struct ThumbMesh {
  std::vector<glm::vec3> positions; // one per corner, 3 per triangle
  std::vector<glm::vec2> uvs;       // top-left origin
  std::vector<Part> parts;
};

struct Texture {
  int width = 0, height = 0;
  std::vector<unsigned char> rgba;
};

std::string Lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return text;
}

// Model files authored on Windows reference textures with backslashes and
// arbitrary letter case; resolve them component by component on disk.
std::string ResolveFile(const fs::path &dir, std::string rel) {
  std::replace(rel.begin(), rel.end(), '\\', '/');
  if (rel.empty())
    return {};
  std::error_code ec;
  fs::path direct = fs::path(rel).is_absolute() ? fs::path(rel) : dir / rel;
  if (fs::is_regular_file(direct, ec))
    return direct.string();
  fs::path current = dir;
  for (const auto &part : fs::path(rel)) {
    if (part == ".")
      continue;
    if (part == "..") {
      current = current.parent_path();
      continue;
    }
    const std::string wanted = Lowercase(part.string());
    bool found = false;
    for (fs::directory_iterator it(current, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (Lowercase(it->path().filename().string()) == wanted) {
        current = it->path();
        found = true;
        break;
      }
    }
    if (!found)
      break;
    if (fs::is_regular_file(current, ec))
      return current.string();
  }
  // Last resort: the bare file name next to the model.
  fs::path sibling = dir / fs::path(rel).filename();
  return fs::is_regular_file(sibling, ec) ? sibling.string() : std::string();
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

// Loads RGBA8 and shrinks large textures; a thumbnail never needs 4K texels.
bool LoadTexture(const std::string &path, Texture &out) {
  int w = 0, h = 0, n = 0;
  unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!pixels)
    return false;
  int step = 1;
  while (w / step > kMaxTextureSize || h / step > kMaxTextureSize)
    step *= 2;
  out.width = std::max(1, w / step);
  out.height = std::max(1, h / step);
  out.rgba.resize((size_t)out.width * out.height * 4);
  for (int y = 0; y < out.height; ++y)
    for (int x = 0; x < out.width; ++x)
      std::copy_n(pixels + ((size_t)(y * step) * w + x * step) * 4, 4,
                  out.rgba.data() + ((size_t)y * out.width + x) * 4);
  stbi_image_free(pixels);
  return true;
}

glm::vec4 Sample(const Texture &tex, glm::vec2 uv) {
  uv -= glm::floor(uv); // repeat
  const int x = std::min(tex.width - 1, (int)(uv.x * tex.width));
  const int y = std::min(tex.height - 1, (int)(uv.y * tex.height));
  const unsigned char *p = &tex.rgba[((size_t)y * tex.width + x) * 4];
  return glm::vec4(p[0], p[1], p[2], p[3]) / 255.0f;
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
  const float yaw = glm::radians(30.0f), pitch = glm::radians(15.0f);
  const glm::vec3 toCamera(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                           std::cos(yaw) * std::cos(pitch));
  const glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), toCamera));
  const glm::vec3 up = glm::cross(toCamera, right);
  const glm::vec3 light =
      glm::normalize(-0.4f * right + 0.7f * up + 0.6f * toCamera);

  std::vector<glm::vec3> view(mesh.positions.size());
  glm::vec2 lo(INFINITY), hi(-INFINITY);
  for (size_t i = 0; i < view.size(); ++i) {
    const glm::vec3 &p = mesh.positions[i];
    view[i] = {glm::dot(p, right), glm::dot(p, up), glm::dot(p, toCamera)};
    lo = glm::min(lo, glm::vec2(view[i]));
    hi = glm::max(hi, glm::vec2(view[i]));
  }
  const int S = size * kSupersample;
  const float extent = std::max(hi.x - lo.x, hi.y - lo.y);
  if (!(extent > 1e-9f) || !std::isfinite(extent)) {
    error = "degenerate bounds";
    return false;
  }
  const float scale = S * 0.88f / extent;
  const glm::vec2 mid = (lo + hi) * 0.5f;
  for (auto &v : view) {
    v.x = S * 0.5f + (v.x - mid.x) * scale;
    v.y = S * 0.5f - (v.y - mid.y) * scale;
  }

  std::vector<float> depth((size_t)S * S, -INFINITY);
  std::vector<glm::vec4> color((size_t)S * S, glm::vec4(0.0f));
  std::unordered_map<std::string, Texture> textures;
  for (const auto &part : mesh.parts) {
    const Texture *tex = nullptr;
    if (!part.texture.empty()) {
      auto it = textures.find(part.texture);
      if (it == textures.end()) {
        Texture loadedTex;
        LoadTexture(part.texture, loadedTex);
        it = textures.emplace(part.texture, std::move(loadedTex)).first;
      }
      if (it->second.width > 0)
        tex = &it->second;
    }
    for (uint32_t t = part.first; t + 2 < part.first + part.count; t += 3) {
      const glm::vec3 &a = view[t], &b = view[t + 1], &c = view[t + 2];
      const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
      if (std::abs(area) < 1e-12f)
        continue;
      // Two-sided Lambert with a flat face normal.
      glm::vec3 n = glm::cross(mesh.positions[t + 1] - mesh.positions[t],
                               mesh.positions[t + 2] - mesh.positions[t]);
      const float len = glm::length(n);
      n = len > 0 ? n / len : toCamera;
      if (glm::dot(n, toCamera) < 0)
        n = -n;
      const float shade = 0.45f + 0.55f * std::max(0.0f, glm::dot(n, light));

      const int x0 = std::max(0, (int)std::floor(std::min({a.x, b.x, c.x})));
      const int x1 = std::min(S - 1, (int)std::ceil(std::max({a.x, b.x, c.x})));
      const int y0 = std::max(0, (int)std::floor(std::min({a.y, b.y, c.y})));
      const int y1 = std::min(S - 1, (int)std::ceil(std::max({a.y, b.y, c.y})));
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const float px = x + 0.5f, py = y + 0.5f;
          const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
          const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
          const float w2 = 1.0f - w0 - w1;
          if (w0 < 0 || w1 < 0 || w2 < 0)
            continue;
          const float z = w0 * a.z + w1 * b.z + w2 * c.z;
          const size_t pixel = (size_t)y * S + x;
          if (z <= depth[pixel])
            continue;
          glm::vec4 base = part.color;
          if (tex) {
            const glm::vec2 uv = w0 * mesh.uvs[t] + w1 * mesh.uvs[t + 1] +
                                 w2 * mesh.uvs[t + 2];
            const glm::vec4 texel = Sample(*tex, uv);
            base = glm::vec4(glm::vec3(texel), texel.a * part.color.a);
          }
          if (base.a < 0.4f) // cut-out hair, lashes, transparent shells
            continue;
          depth[pixel] = z;
          color[pixel] = glm::vec4(glm::clamp(glm::vec3(base) * shade, 0.0f, 1.0f), 1.0f);
        }
    }
  }

  // Box-filter down to the final size (premultiplied, transparent background).
  std::vector<unsigned char> png((size_t)size * size * 4);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      glm::vec4 sum(0.0f);
      for (int sy = 0; sy < kSupersample; ++sy)
        for (int sx = 0; sx < kSupersample; ++sx) {
          const glm::vec4 c = color[(size_t)(y * kSupersample + sy) * S +
                                    x * kSupersample + sx];
          sum += glm::vec4(glm::vec3(c) * c.a, c.a);
        }
      sum /= float(kSupersample * kSupersample);
      const glm::vec3 rgb = sum.a > 0 ? glm::vec3(sum) / sum.a : glm::vec3(0);
      unsigned char *out = &png[((size_t)y * size + x) * 4];
      out[0] = (unsigned char)std::lround(rgb.r * 255);
      out[1] = (unsigned char)std::lround(rgb.g * 255);
      out[2] = (unsigned char)std::lround(rgb.b * 255);
      out[3] = (unsigned char)std::lround(sum.a * 255);
    }

  std::error_code ec;
  fs::create_directories(fs::path(pngPath).parent_path(), ec);
  const std::string temp = pngPath + ".tmp";
  if (!stbi_write_png(temp.c_str(), size, size, 4, png.data(), size * 4)) {
    error = "cannot write " + temp;
    return false;
  }
  fs::rename(temp, pngPath, ec);
  if (ec) {
    error = ec.message();
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
