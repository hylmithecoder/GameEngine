#include "core_engine/WorldText.hpp"
#include "core_engine/GameUI.hpp"
#include "core_engine/IlmeeeScene.hpp"
#include "core_engine/script/SceneObjects.hpp"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <filesystem>
#include <unistd.h>

static int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)
int main() {
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  using namespace ilmeee;
  WorldTextComponent t;
  t.text = "Wise";
  const auto font = GetWorldTextFontImage();
  CHECK(font.rgba && font.width > 0 && font.height > 0);
  const auto centered = BuildWorldTextGeometry(t);
  CHECK(centered.vertices.size() == 16 && centered.indices.size() == 24);
  CHECK(centered.min.x < 0 && centered.max.x > 0);
  CHECK(centered.min.y < centered.max.y && centered.max.z == 0);
  t.alignment = 0;
  const auto left = BuildWorldTextGeometry(t);
  CHECK(left.min.x >= 0 && left.max.x > centered.max.x);
  t.alignment = 2;
  const auto right = BuildWorldTextGeometry(t);
  CHECK(right.max.x <= 0);
  t.size *= 2;
  const auto bigger = BuildWorldTextGeometry(t);
  CHECK(std::abs(bigger.min.x - right.min.x * 2) < 1e-5f);
  CHECK(std::abs(bigger.min.y - right.min.y * 2) < 1e-5f);
  t.text = "Wise\nWise";
  const auto lines = BuildWorldTextGeometry(t);
  CHECK(lines.vertices.size() == 32 && lines.indices.size() == 48);
  CHECK(lines.min.y < bigger.min.y - t.size * 0.9f);
  t.text = " \r\n\t";
  CHECK(BuildWorldTextGeometry(t).vertices.empty());
  t.text = "";
  CHECK(BuildWorldTextGeometry(t).vertices.empty());
  t.text = "Café";
  const auto utf8 = BuildWorldTextGeometry(t);
  CHECK(utf8.vertices.size() == 16);
  for (const auto &v : utf8.vertices) {
    CHECK(std::isfinite(v.position.x) && std::isfinite(v.position.y));
    CHECK(v.uv.x >= 0 && v.uv.x <= 1 && v.uv.y >= 0 && v.uv.y <= 1);
  }
  t.size = std::numeric_limits<float>::quiet_NaN();
  CHECK(BuildWorldTextGeometry(t).vertices.empty());
  t.size = 0;
  CHECK(BuildWorldTextGeometry(t).vertices.empty());
  t.size = 1;
  t.text = std::string(20000, 'A');
  CHECK(BuildWorldTextGeometry(t).vertices.size() == 16384 * 4);

  // UI bindings survive the same conversion used by Save Scene / player.
  GameUI ui;
  UiElement e;
  e.id = "Play";
  e.kind = UiKind::Button;
  NativeScriptBinding binding{"assets/scripts/Play.cpp", "Play", true, {}};
  binding.values.push_back({"label", ScriptValueType::Object, 0, false, 42});
  e.scripts.push_back(binding);
  ui.elements.push_back(e);
  IlmeeeScene scene;
  AppendGameUIToScene(scene, ui);
  CHECK(scene.entities.size() == 2 && scene.entities[1].scripts.size() == 1);
  GameUI loaded;
  CHECK(GameUIFromScene(scene, loaded));
  CHECK(loaded.elements[0].objectId == e.objectId);
  CHECK(loaded.elements[0].scripts[0].values[0].object == 42);

  const auto near = [](float a, float b) { return std::abs(a - b) < 0.01f; };
  // Stretch panels keep both side margins on phones/tablets, landscape and
  // portrait; controls retain their aspect ratio. Nonzero origin also models
  // a docked preview or the player's safe area.
  for (ImVec2 size : {ImVec2(1280, 720), ImVec2(1600, 720), ImVec2(2340, 1080),
                     ImVec2(1024, 768), ImVec2(720, 1600), ImVec2(2560, 1600)}) {
    const float scale = std::min(size.x / 1280, size.y / 720);
    UiElement panel;
    panel.anchor = UiAnchor::StretchBottom;
    panel.x = 40; panel.y = 30; panel.width = 1200; panel.height = 190;
    auto rect = GameUIElementRect(ui, panel, {57, 29}, size);
    CHECK(near(rect.min.x, 57 + 40 * scale));
    CHECK(near(rect.max.x, 57 + size.x - 40 * scale));
    CHECK(near(rect.max.y, 29 + size.y - 30 * scale));
    panel.anchor = UiAnchor::BottomRight; panel.width = panel.height = 180;
    rect = GameUIElementRect(ui, panel, {57, 29}, size);
    CHECK(near(rect.max.x - rect.min.x, rect.max.y - rect.min.y));
    CHECK(near(rect.max.x, 57 + size.x - 40 * scale));
    panel.anchor = UiAnchor::BottomCenter; panel.x = 0;
    rect = GameUIElementRect(ui, panel, {57, 29}, size);
    CHECK(near((rect.min.x + rect.max.x) * 0.5f, 57 + size.x * 0.5f));
    panel.anchor = UiAnchor::StretchFull; panel.x = 40; panel.y = 30;
    panel.width = 1200; panel.height = 660;
    rect = GameUIElementRect(ui, panel, {57, 29}, size);
    CHECK(near(rect.max.x, 57 + size.x - 40 * scale));
    CHECK(near(rect.max.y, 29 + size.y - 30 * scale));
  }

  // Anchor changes never jump in the reference canvas, and every new layout
  // survives both standalone UI JSON and the binary scene/export path.
  char uiDirectory[] = "/tmp/ilmeee-ui-layout-XXXXXX";
  CHECK(mkdtemp(uiDirectory) != nullptr);
  const auto uiFile = std::filesystem::path(uiDirectory) / "layout.json";
  const auto sceneFile = std::filesystem::path(uiDirectory) / "layout.ilmeeescene";
  for (int anchor = 0; anchor <= int(UiAnchor::StretchFull); ++anchor) {
    auto element = e;
    const auto before = GameUIElementRect(ui, element, {0, 0}, {1280, 720});
    SetUiElementAnchor(ui, element, UiAnchor(anchor));
    const auto after = GameUIElementRect(ui, element, {0, 0}, {1280, 720});
    CHECK(near(before.min.x, after.min.x) && near(before.min.y, after.min.y));
    GameUI layouts; layouts.elements = {element};
    std::string error;
    CHECK(SaveGameUI(uiFile.string(), layouts, &error));
    GameUI fromJson;
    CHECK(LoadGameUI(uiFile.string(), fromJson, &error));
    CHECK(fromJson.elements[0].anchor == UiAnchor(anchor));
    IlmeeeScene saved; AppendGameUIToScene(saved, layouts);
    CHECK(SaveScene(sceneFile.string(), saved));
    IlmeeeScene restored; CHECK(LoadScene(sceneFile.string(), restored));
    GameUI fromScene; CHECK(GameUIFromScene(restored, fromScene));
    CHECK(fromScene.elements[0].anchor == UiAnchor(anchor));
  }
  std::filesystem::remove_all(uiDirectory);

  using namespace ilmeee::script;
  HostApi host;
  host.valid = [](void *, uint64_t id) { return id >= 1 && id <= 4; };
  host.kinds = [](void *, uint64_t id) -> uint32_t {
    if (id == 1) return static_cast<uint32_t>(ObjectKind::WorldText);
    return static_cast<uint32_t>(ObjectKind::Ui) |
        static_cast<uint32_t>(id == 2 ? ObjectKind::UiText : id == 3 ? ObjectKind::UiImage : ObjectKind::UiButton);
  };
  script::GameObject world(1, &host), label(2, &host), image(3, &host), button(4, &host);
  CHECK(world.As<TextGameObject>().IsValid());
  CHECK(!world.As<UiGameObject>().IsValid());
  CHECK(label.As<UiTextGameObject>().IsValid());
  CHECK(image.As<UiImageGameObject>().IsValid());
  CHECK(button.As<UiButtonGameObject>().IsValid());
  CHECK(!image.As<UiTextGameObject>().IsValid());
  CHECK(!button.As<TextGameObject>().IsValid());
  CHECK(button.As<UiGameObject>().IsValid());
  CHECK(!TextGameObject{}.IsValid());

  // Image scripting colour/alpha must reach the actual draw vertices.
  auto &io = ImGui::GetIO();
  io.DisplaySize = {500, 500};
  io.DeltaTime = 1.0f / 60;
  unsigned char *uiPixels = nullptr;
  int uiWidth = 0, uiHeight = 0;
  io.Fonts->GetTexDataAsRGBA32(&uiPixels, &uiWidth, &uiHeight);
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize({500, 500});
  ImGui::Begin("UI tint test");
  GameUI imageUi;
  UiElement tinted;
  tinted.kind = UiKind::Image;
  tinted.color = {0.2f, 0.5f, 0.8f, 0.4f};
  imageUi.elements.push_back(tinted);
  const auto texture = [](const std::string &) { return ImTextureID{1}; };
  DrawGameUI(imageUi, {30, 40}, {500, 500}, texture, true);
  CHECK(ImGui::GetWindowDrawList()->VtxBuffer.back().col ==
        ImGui::ColorConvertFloat4ToU32(tinted.color));
  // A tap in the added width of a stretched button hits the same rectangle
  // that is drawn, including an inset safe-area origin.
  GameUI touchUi;
  UiElement buttonUi; buttonUi.id = "talk"; buttonUi.kind = UiKind::Button;
  buttonUi.anchor = UiAnchor::StretchBottom;
  buttonUi.x = 40; buttonUi.y = 30; buttonUi.width = 1200; buttonUi.height = 190;
  touchUi.elements.push_back(buttonUi);
  UiFingers fingers; fingers[1].pos = {1500, 650};
  std::vector<uint64_t> pressed;
  DrawGameUI(touchUi, {57, 29}, {1600, 720}, {}, true, false, nullptr, &fingers, &pressed);
  CHECK(pressed.size() == 1 && pressed[0] == buttonUi.objectId);
  CHECK(fingers[1].element == "talk");
  imageUi.elements[0].visible = false;
  DrawGameUI(imageUi, {30, 40}, {500, 500}, texture, false);
  tinted.color.w *= 0.35f;
  CHECK(ImGui::GetWindowDrawList()->VtxBuffer.back().col ==
        ImGui::ColorConvertFloat4ToU32(tinted.color));
  ImGui::End();
  ImGui::EndFrame();
  ImGui::DestroyContext();
  std::printf("%d/%d checks passed\n", checks-failures, checks);
  return failures ? 1 : 0;
}
