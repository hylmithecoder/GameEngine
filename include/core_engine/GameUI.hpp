#pragma once

#include <functional>
#include <string>
#include <vector>
#include <imgui.h>

namespace ilmeee {
struct IlmeeeScene;

enum class UiKind { Text, Image, Button };
enum class UiAnchor { TopLeft, TopRight, BottomLeft, BottomRight, Center };

struct UiElement {
  std::string id;
  UiKind kind = UiKind::Text;
  UiAnchor anchor = UiAnchor::TopLeft;
  float x = 24.0f;
  float y = 24.0f;
  float width = 160.0f;
  float height = 44.0f;
  float fontSize = 24.0f;
  std::string text;
  std::string imagePath;
  std::string action;
  ImVec4 color{1, 1, 1, 1};
  ImVec4 background{0.14f, 0.20f, 0.31f, 0.92f};
};

struct GameUI {
  float referenceWidth = 1280.0f;
  float referenceHeight = 720.0f;
  std::vector<UiElement> elements;
};

GameUI DefaultGameUI();
bool GameUIFromScene(const IlmeeeScene &scene, GameUI &out);
void AppendGameUIToScene(IlmeeeScene &scene, const GameUI &ui);
bool LoadGameUI(const std::string &path, GameUI &out, std::string *error = nullptr);
bool SaveGameUI(const std::string &path, const GameUI &ui,
                std::string *error = nullptr);

// Draws in the current ImGui window, over its scene image. The document and
// coordinate mapping are shared by the editor preview and the game player.
// Returns the clicked action, if any.
std::string DrawGameUI(
    const GameUI &ui, ImVec2 origin, ImVec2 size,
    const std::function<ImTextureID(const std::string &)> &imageTexture = {},
    bool interactive = true, bool paused = false);

} // namespace ilmeee
