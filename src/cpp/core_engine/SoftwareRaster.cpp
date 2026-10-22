#include "../../../include/core_engine/SoftwareRaster.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stb/stb_image.h>

// The editor links no other stb_image_write TU (only the player has one), so
// keep these symbols private to this file.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

namespace fs = std::filesystem;

namespace ilmeee::raster {
namespace {
std::string Lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return text;
}

glm::vec4 Sample(const Texture &tex, glm::vec2 uv) {
  uv -= glm::floor(uv); // repeat
  const int x = std::min(tex.width - 1, (int)(uv.x * tex.width));
  const int y = std::min(tex.height - 1, (int)(uv.y * tex.height));
  const unsigned char *p = &tex.rgba[((size_t)y * tex.width + x) * 4];
  return glm::vec4(p[0], p[1], p[2], p[3]) / 255.0f;
}
} // namespace

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

bool LoadTexture(const std::string &path, Texture &out, int maxSize) {
  int w = 0, h = 0, n = 0;
  unsigned char *pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (!pixels)
    return false;
  int step = 1;
  while (w / step > maxSize || h / step > maxSize)
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

View MakeView(float yawDegrees, float pitchDegrees) {
  const float yaw = glm::radians(yawDegrees), pitch = glm::radians(pitchDegrees);
  View v;
  v.toCamera = {std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                std::cos(yaw) * std::cos(pitch)};
  v.right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), v.toCamera));
  v.up = glm::cross(v.toCamera, v.right);
  v.light = glm::normalize(-0.4f * v.right + 0.7f * v.up + 0.6f * v.toCamera);
  return v;
}

Canvas::Canvas(int size, int supersample)
    : size_(size), supersample_(supersample),
      depth_((size_t)Samples() * Samples()), color_((size_t)Samples() * Samples()) {
  Clear();
}

void Canvas::Clear() {
  std::fill(depth_.begin(), depth_.end(), -INFINITY);
  std::fill(color_.begin(), color_.end(), glm::vec4(0.0f));
}

void Canvas::Draw(const std::vector<glm::vec3> &world, const std::vector<glm::vec2> &uvs,
                  const std::vector<Part> &parts, const View &view, glm::vec2 viewCenter,
                  float pixelsPerUnit,
                  std::unordered_map<std::string, Texture> &textureCache) {
  const int S = Samples();
  const float scale = pixelsPerUnit * supersample_;
  const auto toScreen = [&](const glm::vec3 &p) {
    return glm::vec3(S * 0.5f + (glm::dot(p, view.right) - viewCenter.x) * scale,
                     S * 0.5f - (glm::dot(p, view.up) - viewCenter.y) * scale,
                     glm::dot(p, view.toCamera));
  };
  for (const auto &part : parts) {
    const Texture *tex = nullptr;
    if (!part.texture.empty()) {
      auto it = textureCache.find(part.texture);
      if (it == textureCache.end()) {
        Texture loaded;
        LoadTexture(part.texture, loaded);
        it = textureCache.emplace(part.texture, std::move(loaded)).first;
      }
      if (it->second.width > 0)
        tex = &it->second;
    }
    const uint32_t end = std::min<uint32_t>(part.first + part.count, (uint32_t)world.size());
    for (uint32_t t = part.first; t + 2 < end; t += 3) {
      const glm::vec3 a = toScreen(world[t]), b = toScreen(world[t + 1]),
                      c = toScreen(world[t + 2]);
      const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
      if (std::abs(area) < 1e-12f || !std::isfinite(area))
        continue;
      // Two-sided Lambert with a flat face normal.
      glm::vec3 n = glm::cross(world[t + 1] - world[t], world[t + 2] - world[t]);
      const float len = glm::length(n);
      n = len > 0 ? n / len : view.toCamera;
      if (glm::dot(n, view.toCamera) < 0)
        n = -n;
      const float shade = 0.45f + 0.55f * std::max(0.0f, glm::dot(n, view.light));

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
          if (z <= depth_[pixel])
            continue;
          glm::vec4 base = part.color;
          if (tex) {
            const glm::vec2 uv = w0 * uvs[t] + w1 * uvs[t + 1] + w2 * uvs[t + 2];
            const glm::vec4 texel = Sample(*tex, uv);
            base = glm::vec4(glm::vec3(texel), texel.a * part.color.a);
          }
          if (base.a < 0.4f) // cut-out hair, lashes, transparent shells
            continue;
          depth_[pixel] = z;
          color_[pixel] = glm::vec4(glm::clamp(glm::vec3(base) * shade, 0.0f, 1.0f), 1.0f);
        }
    }
  }
}

std::vector<unsigned char> Canvas::Resolve(glm::vec4 background) const {
  const int S = Samples(), ss = supersample_;
  std::vector<unsigned char> out((size_t)size_ * size_ * 4);
  for (int y = 0; y < size_; ++y)
    for (int x = 0; x < size_; ++x) {
      glm::vec4 sum(0.0f); // premultiplied
      for (int sy = 0; sy < ss; ++sy)
        for (int sx = 0; sx < ss; ++sx) {
          const glm::vec4 c = color_[(size_t)(y * ss + sy) * S + x * ss + sx];
          sum += glm::vec4(glm::vec3(c) * c.a, c.a);
        }
      sum /= float(ss * ss);
      glm::vec4 px;
      if (background.a > 0) {
        px = glm::vec4(glm::vec3(sum) + glm::vec3(background) * (1 - sum.a), 1.0f);
      } else {
        px = glm::vec4(sum.a > 0 ? glm::vec3(sum) / sum.a : glm::vec3(0), sum.a);
      }
      unsigned char *o = &out[((size_t)y * size_ + x) * 4];
      for (int k = 0; k < 4; ++k)
        o[k] = (unsigned char)std::lround(std::clamp(px[k], 0.0f, 1.0f) * 255);
    }
  return out;
}

bool WritePng(const std::string &path, const std::vector<unsigned char> &rgba,
              int width, int height) {
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  const std::string temp = path + ".tmp";
  if (!stbi_write_png(temp.c_str(), width, height, 4, rgba.data(), width * 4))
    return false;
  fs::rename(temp, path, ec);
  return !ec;
}

std::vector<unsigned char> EncodePng(const std::vector<unsigned char> &rgba,
                                     int width, int height) {
  std::vector<unsigned char> out;
  stbi_write_png_to_func(
      [](void *context, void *data, int size) {
        auto *bytes = static_cast<std::vector<unsigned char> *>(context);
        const auto *p = static_cast<unsigned char *>(data);
        bytes->insert(bytes->end(), p, p + size);
      },
      &out, width, height, 4, rgba.data(), width * 4);
  return out;
}

} // namespace ilmeee::raster
