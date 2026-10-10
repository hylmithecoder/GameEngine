#pragma once

#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <imgui.h>

namespace ilmeee {
struct IlmeeeScene;

enum class UiKind { Text, Image, Button, Joystick };
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

// Screen rectangle of an element after reference-size scaling and anchoring.
struct UiRect { ImVec2 min, max; };
UiRect GameUIElementRect(const GameUI &ui, const UiElement &element,
                         ImVec2 origin, ImVec2 size);

GameUI DefaultGameUI();
bool GameUIFromScene(const IlmeeeScene &scene, GameUI &out);
void AppendGameUIToScene(IlmeeeScene &scene, const GameUI &ui);
bool LoadGameUI(const std::string &path, GameUI &out, std::string *error = nullptr);
bool SaveGameUI(const std::string &path, const GameUI &ui,
                std::string *error = nullptr);

// Analog joystick values keyed by element id: x = right, y = up/forward,
// length <= 1. Same map type as MovementInput::axes.
using UiAxisMap = std::unordered_map<std::string, glm::vec2>;

// Fingers on the game view, kept by the caller across frames: add one on
// SDL_EVENT_FINGER_DOWN, move it on MOTION, erase it on UP. A finger that
// lands on a joystick or button belongs to it until lifted, so two sticks
// work at once; `element` stays empty for fingers on the open view.
struct UiFinger {
  ImVec2 pos;        // window coordinates, like ImGui's mouse
  bool fresh = true; // went down since the last DrawGameUI
  std::string element;
};
using UiFingers = std::unordered_map<int64_t, UiFinger>;

// Draws in the current ImGui window, over its scene image. The document and
// coordinate mapping are shared by the editor preview and the game player.
// Joystick knobs are drawn at their value in `axes` (e.g. keyboard echo);
// a held joystick writes its drag value there. Returns the clicked action.
std::string DrawGameUI(
    const GameUI &ui, ImVec2 origin, ImVec2 size,
    const std::function<ImTextureID(const std::string &)> &imageTexture = {},
    bool interactive = true, bool paused = false, UiAxisMap *axes = nullptr,
    UiFingers *fingers = nullptr);

} // namespace ilmeee
