#include "core_engine/WorldText.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace ilmeee {
namespace {
struct FontData {
  ImFontAtlas atlas;
  ImFont *font = nullptr;
  unsigned char *pixels = nullptr;
  int width = 0, height = 0;
  FontData() {
    ImFontConfig config;
    config.SizePixels = 64;
    config.OversampleH = config.OversampleV = 2;
    const char *path = "assets/fonts/MiSansLatin-Regular.ttf";
    if (std::filesystem::is_regular_file(path))
      font = atlas.AddFontFromFileTTF(path, 64, &config);
    if (!font) font = atlas.AddFontDefault(&config);
    atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
  }
};
FontData &Font() { static FontData data; return data; }
} // namespace
WorldTextFontImage GetWorldTextFontImage() {
  auto &f = Font();
  return {f.width, f.height, f.pixels};
}
WorldTextGeometry BuildWorldTextGeometry(const WorldTextComponent &text) {
  WorldTextGeometry out;
  if (!std::isfinite(text.size) || text.size <= 0) return out;
  auto *font = Font().font;
  const float scale = text.size / font->FontSize;
  // Limit generated geometry, including hostile script-supplied strings.
  const char *begin = text.text.data();
  const char *end = begin + std::min<size_t>(text.text.size(), 16384);
  float y = 0;
  while (begin < end) {
    const char *lineEnd = std::find(begin, end, '\n');
    float width = 0;
    for (const char *p = begin; p < lineEnd;) {
      unsigned int c = 0;
      const int count = ImTextCharFromUtf8(&c, p, lineEnd);
      if (count <= 0) break;
      p += count;
      if (c == '\t') width += font->GetCharAdvance(' ') * 4 * scale;
      else if (c >= 32) width += (c > IM_UNICODE_CODEPOINT_MAX ? font->FallbackAdvanceX
          : font->GetCharAdvance(static_cast<ImWchar>(c))) * scale;
    }
    float x = text.alignment == 2 ? -width : text.alignment == 1 ? -width * 0.5f : 0;
    while (begin < lineEnd) {
      unsigned int c = 0;
      int count = ImTextCharFromUtf8(&c, begin, lineEnd);
      if (count <= 0) break;
      begin += count;
      if (c == '\t') { x += font->GetCharAdvance(' ') * 4 * scale; continue; }
      if (c < 32) continue;
      const ImFontGlyph *glyph = c > IM_UNICODE_CODEPOINT_MAX ? font->FallbackGlyph
          : font->FindGlyph(static_cast<ImWchar>(c));
      if (!glyph) continue;
      if (glyph->Visible) {
        const float x0 = x + glyph->X0 * scale, x1 = x + glyph->X1 * scale;
        const float y0 = y - glyph->Y0 * scale, y1 = y - glyph->Y1 * scale;
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        out.vertices.insert(out.vertices.end(), {
            {{x0, y1, 0}, {glyph->U0, glyph->V1}},
            {{x1, y1, 0}, {glyph->U1, glyph->V1}},
            {{x1, y0, 0}, {glyph->U1, glyph->V0}},
            {{x0, y0, 0}, {glyph->U0, glyph->V0}}});
        out.indices.insert(out.indices.end(), {base, base+1, base+2, base, base+2, base+3});
      }
      x += glyph->AdvanceX * scale;
    }
    begin = lineEnd < end ? lineEnd + 1 : end;
    y -= text.size;
  }
  if (!out.vertices.empty()) {
    out.min = out.max = out.vertices.front().position;
    for (const auto &v : out.vertices) {
      out.min = glm::min(out.min, v.position);
      out.max = glm::max(out.max, v.position);
    }
  }
  return out;
}
} // namespace ilmeee
