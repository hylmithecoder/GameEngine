#include "../../../include/core_engine/GameUI.hpp"
#include "../../../include/core_engine/IlmeeeScene.hpp"
#include "../../../vendor/nlohmann/json.hpp"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace ilmeee {
namespace {
using json = nlohmann::json;

const char *KindName(UiKind kind) {
  switch (kind) {
  case UiKind::Text: return "text";
  case UiKind::Image: return "image";
  case UiKind::Button: return "button";
  }
  return "text";
}

const char *AnchorName(UiAnchor anchor) {
  switch (anchor) {
  case UiAnchor::TopLeft: return "topLeft";
  case UiAnchor::TopRight: return "topRight";
  case UiAnchor::BottomLeft: return "bottomLeft";
  case UiAnchor::BottomRight: return "bottomRight";
  case UiAnchor::Center: return "center";
  }
  return "topLeft";
}

UiKind ParseKind(const std::string &name) {
  if (name == "image") return UiKind::Image;
  if (name == "button") return UiKind::Button;
  return UiKind::Text;
}

UiAnchor ParseAnchor(const std::string &name) {
  if (name == "topRight") return UiAnchor::TopRight;
  if (name == "bottomLeft") return UiAnchor::BottomLeft;
  if (name == "bottomRight") return UiAnchor::BottomRight;
  if (name == "center") return UiAnchor::Center;
  return UiAnchor::TopLeft;
}

ImVec4 ReadColor(const json &value, ImVec4 fallback) {
  if (!value.is_array() || value.size() != 4)
    return fallback;
  try {
    return ImVec4(value[0].get<float>(), value[1].get<float>(),
                  value[2].get<float>(), value[3].get<float>());
  } catch (const json::exception &) {
    return fallback;
  }
}

json WriteColor(ImVec4 color) {
  return json::array({color.x, color.y, color.z, color.w});
}
} // namespace

GameUI DefaultGameUI() {
  GameUI ui;
  UiElement hp;
  hp.id = "hp";
  hp.text = "HP: 100";
  hp.x = 28;
  hp.y = 26;
  hp.fontSize = 28;
  ui.elements.push_back(hp);

  UiElement pause;
  pause.id = "pause";
  pause.kind = UiKind::Button;
  pause.anchor = UiAnchor::TopRight;
  pause.x = 28;
  pause.y = 22;
  pause.width = 120;
  pause.height = 44;
  pause.fontSize = 20;
  pause.text = "Pause";
  pause.action = "TogglePause";
  ui.elements.push_back(pause);
  return ui;
}

bool GameUIFromScene(const IlmeeeScene &scene, GameUI &out) {
  size_t canvasIndex = scene.entities.size();
  for (size_t i = 0; i < scene.entities.size(); ++i)
    if (scene.entities[i].kind == PrimitiveKind::Canvas) {
      canvasIndex = i;
      break;
    }
  if (canvasIndex == scene.entities.size())
    return false;

  GameUI loaded;
  const SceneEntity &canvas = scene.entities[canvasIndex];
  loaded.referenceWidth = canvas.uiWidth;
  loaded.referenceHeight = canvas.uiHeight;
  for (const SceneEntity &e : scene.entities) {
    if (e.parent != static_cast<int32_t>(canvasIndex))
      continue;
    UiElement element;
    switch (e.kind) {
    case PrimitiveKind::UiText: element.kind = UiKind::Text; break;
    case PrimitiveKind::UiImage: element.kind = UiKind::Image; break;
    case PrimitiveKind::UiButton: element.kind = UiKind::Button; break;
    default: continue;
    }
    element.id = e.name;
    element.anchor = static_cast<UiAnchor>(e.uiAnchor);
    element.x = e.uiX;
    element.y = e.uiY;
    element.width = e.uiWidth;
    element.height = e.uiHeight;
    element.fontSize = e.uiFontSize;
    element.text = e.uiText;
    element.imagePath = e.uiImagePath;
    element.action = e.uiAction;
    element.color = ImVec4(e.uiColor.r, e.uiColor.g,
                           e.uiColor.b, e.uiColor.a);
    element.background = ImVec4(e.uiBackground.r, e.uiBackground.g,
                                e.uiBackground.b, e.uiBackground.a);
    loaded.elements.push_back(std::move(element));
  }
  out = std::move(loaded);
  return true;
}

void AppendGameUIToScene(IlmeeeScene &scene, const GameUI &ui) {
  SceneEntity canvas;
  canvas.name = "Canvas";
  canvas.kind = PrimitiveKind::Canvas;
  canvas.uiWidth = ui.referenceWidth;
  canvas.uiHeight = ui.referenceHeight;
  const int32_t parent = static_cast<int32_t>(scene.entities.size());
  scene.entities.push_back(std::move(canvas));
  for (const UiElement &element : ui.elements) {
    SceneEntity e;
    e.name = element.id;
    e.parent = parent;
    switch (element.kind) {
    case UiKind::Text: e.kind = PrimitiveKind::UiText; break;
    case UiKind::Image: e.kind = PrimitiveKind::UiImage; break;
    case UiKind::Button: e.kind = PrimitiveKind::UiButton; break;
    }
    e.uiAnchor = static_cast<uint8_t>(element.anchor);
    e.uiX = element.x;
    e.uiY = element.y;
    e.uiWidth = element.width;
    e.uiHeight = element.height;
    e.uiFontSize = element.fontSize;
    e.uiText = element.text;
    e.uiImagePath = element.imagePath;
    e.uiAction = element.action;
    e.uiColor = {element.color.x, element.color.y,
                 element.color.z, element.color.w};
    e.uiBackground = {element.background.x, element.background.y,
                      element.background.z, element.background.w};
    scene.entities.push_back(std::move(e));
  }
}

bool LoadGameUI(const std::string &path, GameUI &out, std::string *error) {
  std::ifstream input(path);
  if (!input) {
    if (error) *error = "Cannot open " + path;
    return false;
  }
  const json root = json::parse(input, nullptr, false);
  if (!root.is_object() || !root.contains("version") ||
      !root["version"].is_number_integer() || root["version"] != 1 ||
      !root.contains("elements") || !root["elements"].is_array()) {
    if (error) *error = "Invalid Game UI document: " + path;
    return false;
  }
  try {
    GameUI loaded;
    loaded.referenceWidth = root.value("referenceWidth", 1280.0f);
    loaded.referenceHeight = root.value("referenceHeight", 720.0f);
    if (!std::isfinite(loaded.referenceWidth) || loaded.referenceWidth <= 0 ||
        !std::isfinite(loaded.referenceHeight) || loaded.referenceHeight <= 0)
      throw std::runtime_error("Invalid reference size");
    std::unordered_set<std::string> ids;
    for (const auto &item : root["elements"]) {
      UiElement e;
      e.id = item.at("id").get<std::string>();
      e.kind = ParseKind(item.value("type", "text"));
      e.anchor = ParseAnchor(item.value("anchor", "topLeft"));
      e.x = item.value("x", e.x);
      e.y = item.value("y", e.y);
      e.width = item.value("width", e.width);
      e.height = item.value("height", e.height);
      e.fontSize = item.value("fontSize", e.fontSize);
      e.text = item.value("text", "");
      e.imagePath = item.value("imagePath", "");
      e.action = item.value("action", "");
      if (item.contains("color"))
        e.color = ReadColor(item["color"], e.color);
      if (item.contains("background"))
        e.background = ReadColor(item["background"], e.background);
      if (e.id.empty() || !ids.insert(e.id).second ||
          !std::isfinite(e.x) || !std::isfinite(e.y) ||
          !std::isfinite(e.width) || e.width <= 0 ||
          !std::isfinite(e.height) || e.height <= 0 ||
          !std::isfinite(e.fontSize) || e.fontSize <= 0)
        throw std::runtime_error("Invalid UI element");
      loaded.elements.push_back(std::move(e));
    }
    out = std::move(loaded);
    return true;
  } catch (const std::exception &e) {
    if (error) *error = e.what();
    return false;
  }
}

bool SaveGameUI(const std::string &path, const GameUI &ui, std::string *error) {
  if (!std::isfinite(ui.referenceWidth) || ui.referenceWidth <= 0 ||
      !std::isfinite(ui.referenceHeight) || ui.referenceHeight <= 0) {
    if (error) *error = "Reference size must be positive";
    return false;
  }
  std::unordered_set<std::string> ids;
  for (const UiElement &e : ui.elements) {
    if (e.id.empty() || !ids.insert(e.id).second ||
        !std::isfinite(e.x) || !std::isfinite(e.y) ||
        !std::isfinite(e.width) || e.width <= 0 ||
        !std::isfinite(e.height) || e.height <= 0 ||
        !std::isfinite(e.fontSize) || e.fontSize <= 0) {
      if (error) *error = "UI elements need unique IDs and valid sizes";
      return false;
    }
  }
  json root;
  root["version"] = 1;
  root["referenceWidth"] = ui.referenceWidth;
  root["referenceHeight"] = ui.referenceHeight;
  root["elements"] = json::array();
  for (const UiElement &e : ui.elements) {
    root["elements"].push_back({
        {"id", e.id}, {"type", KindName(e.kind)},
        {"anchor", AnchorName(e.anchor)}, {"x", e.x}, {"y", e.y},
        {"width", e.width}, {"height", e.height},
        {"fontSize", e.fontSize}, {"text", e.text},
        {"imagePath", e.imagePath}, {"action", e.action},
        {"color", WriteColor(e.color)},
        {"background", WriteColor(e.background)}});
  }
  std::error_code ec;
  std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
  if (ec) {
    if (error) *error = ec.message();
    return false;
  }
  std::ofstream output(path);
  if (!output || !(output << root.dump(2) << '\n')) {
    if (error) *error = "Cannot write " + path;
    return false;
  }
  return true;
}

std::string DrawGameUI(
    const GameUI &ui, ImVec2 origin, ImVec2 size,
    const std::function<ImTextureID(const std::string &)> &imageTexture,
    bool interactive, bool paused) {
  if (size.x <= 0 || size.y <= 0)
    return {};
  const float scale = std::min(size.x / ui.referenceWidth,
                               size.y / ui.referenceHeight);
  if (!std::isfinite(scale) || scale <= 0)
    return {};
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 oldCursor = ImGui::GetCursorScreenPos();
  std::string clicked;
  draw->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  for (const UiElement &e : ui.elements) {
    const float w = std::max(1.0f, e.width * scale);
    const float h = std::max(1.0f, e.height * scale);
    const float x = e.x * scale, y = e.y * scale;
    ImVec2 pos = origin;
    switch (e.anchor) {
    case UiAnchor::TopLeft: pos.x += x; pos.y += y; break;
    case UiAnchor::TopRight: pos.x += size.x - x - w; pos.y += y; break;
    case UiAnchor::BottomLeft: pos.x += x; pos.y += size.y - y - h; break;
    case UiAnchor::BottomRight:
      pos.x += size.x - x - w; pos.y += size.y - y - h; break;
    case UiAnchor::Center:
      pos.x += (size.x - w) * 0.5f + x;
      pos.y += (size.y - h) * 0.5f + y;
      break;
    }
    const ImVec2 max(pos.x + w, pos.y + h);
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(e.color);
    const ImU32 bg = ImGui::ColorConvertFloat4ToU32(e.background);
    if (e.kind == UiKind::Image) {
      const ImTextureID texture = imageTexture ? imageTexture(e.imagePath) : ImTextureID{};
      if (texture)
        draw->AddImage(texture, pos, max);
      else
        draw->AddRectFilled(pos, max, bg);
    } else {
      if (e.background.w > 0.0f)
        draw->AddRectFilled(pos, max, bg, 6.0f * scale);
      const std::string &label = paused && e.action == "TogglePause"
          ? std::string("Resume") : e.text;
      if (!label.empty()) {
        const float fontSize = std::max(1.0f, e.fontSize * scale);
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(
            fontSize, FLT_MAX, 0.0f, label.c_str());
        const ImVec2 textPos = e.kind == UiKind::Button
            ? ImVec2(pos.x + (w - textSize.x) * 0.5f,
                     pos.y + (h - textSize.y) * 0.5f)
            : ImVec2(pos.x + 8.0f * scale,
                     pos.y + (h - textSize.y) * 0.5f);
        draw->AddText(ImGui::GetFont(), fontSize, textPos, color, label.c_str());
      }
    }
    if (interactive && e.kind == UiKind::Button) {
      ImGui::PushID(e.id.c_str());
      ImGui::SetCursorScreenPos(pos);
      if (ImGui::InvisibleButton("##game-ui-button", ImVec2(w, h)))
        clicked = e.action;
      ImGui::PopID();
    }
  }
  draw->PopClipRect();
  ImGui::SetCursorScreenPos(oldCursor);
  return clicked;
}

} // namespace ilmeee
