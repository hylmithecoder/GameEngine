#include "core_engine/WorldText.hpp"
#include "core_engine/GameUI.hpp"
#include "core_engine/IlmeeeScene.hpp"
#include "core_engine/script/SceneObjects.hpp"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <limits>

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
