// Creates a disposable runtime fixture. Run IlmeeePlayer --game <folder>
// --screenshot <png> --frames 15; the UI-owned native script exercises the
// real HostApi bridge and prints WORLD_TEXT_UI_BRIDGE_OK on success.
#include "core_engine/IlmeeeScene.hpp"
#include "core_engine/NativeScripts.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
static void Write(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path) << text;
}
int main(int argc, char **argv) {
  if (argc != 2) return 2;
  const fs::path root = fs::absolute(argv[1]);
  fs::create_directories(root);
  if (!fs::is_empty(root)) { std::cerr << "Fixture folder must be empty\n"; return 2; }
  Write(root / "game.json", R"({"name":"World Text and UI","startScene":"scenes/main.ilmeeescene","window":{"width":960,"height":640}})");
  const fs::path engine = ILMEEE_ENGINE_ROOT;
  fs::create_directories(root / "assets/shaders");
  fs::copy(engine / "assets/shaders/vulkan", root / "assets/shaders/vulkan", fs::copy_options::recursive);
  fs::create_directories(root / "assets/fonts");
  fs::copy_file(engine / "assets/fonts/MiSansLatin-Regular.ttf", root / "assets/fonts/MiSansLatin-Regular.ttf");
  Write(root / "assets/scripts/UiDemo.hpp", R"cpp(
#pragma once
#include <core_engine/core/Component.hpp>
#include <cmath>
class UiDemo : public Component {
public:
  ObjectRef<TextGameObject> worldLabel;
  ObjectRef<UiTextGameObject> status;
  ObjectRef<UiImageGameObject> image;
  void DescribeProperties(ScriptProperties &p) override {
    p.Field("worldLabel", worldLabel); p.Field("status", status); p.Field("image", image);
  }
  void Start() override {
    auto button = gameObject.As<UiButtonGameObject>();
    auto text = worldLabel.Get(); auto ui = status.Get(); auto pic = image.Get();
    bool ok = button.IsValid() && text.IsValid() && ui.IsValid() && pic.IsValid();
    ok &= !gameObject.As<TextGameObject>().IsValid();
    ok &= !worldLabel.Get().As<UiGameObject>().IsValid();
    ok &= text.SetText("WISE / NPC\nWorld-space Text");
    ok &= text.SetFontSize(0.55f) && std::abs(text.GetFontSize()-0.55f) < 0.001f;
    ok &= text.SetColor({0.3f, 0.85f, 1});
    ok &= text.SetAlpha(0.9f) && std::abs(text.GetAlpha()-0.9f) < 0.001f;
    ok &= text.SetAlignment(TextGameObject::Alignment::Center);
    ok &= text.GetAlignment() == TextGameObject::Alignment::Center;
    ok &= text.Hide() && !text.IsVisible() && text.Show() && text.IsVisible();
    ok &= text.GetText() == "WISE / NPC\nWorld-space Text";
    ok &= std::abs(text.GetColor().y-0.85f) < 0.001f;
    ok &= ui.SetText("UI-owned script: ready") && ui.SetFontSize(28);
    ok &= ui.SetColor({0.5f, 1, 0.7f}) && ui.SetSize({600, 50, 0});
    ok &= button.SetText("Click to change world Text") && button.SetAction("CustomText");
    ok &= pic.SetImage("assets/portrait.png") && pic.GetImage() == "assets/portrait.png";
    ok &= pic.Hide(); // no image asset required for this bridge check
    if (!ok) { LogError("WORLD_TEXT_UI_BRIDGE_FAILED"); return; }
    Log("WORLD_TEXT_UI_BRIDGE_OK");
  }
  void Update(float) override {
    if (gameObject.As<UiButtonGameObject>().WasClicked()) {
      worldLabel.Get().SetText("Button clicked!\nChanged by UI script");
      status.Get().SetText("Click received");
      Log("UI_CLICK_OK");
    }
  }
};
)cpp");
  Write(root / "assets/scripts/UiDemo.cpp", "#include \"UiDemo.hpp\"\n");
  using namespace ilmeee;
  IlmeeeScene scene;
  scene.backgroundColor = {0.035f, 0.055f, 0.085f, 1};
  SceneEntity camera;
  camera.kind = PrimitiveKind::Camera; camera.name = "Camera";
  camera.objectId = 10; camera.position = {0, 1.5f, 6};
  scene.entities.push_back(camera);
  SceneEntity label;
  label.kind = PrimitiveKind::WorldText; label.name = "NPC Name";
  label.objectId = 20; label.position = {0, 2, 0};
  label.worldText.text = "Before script";
  scene.entities.push_back(label);
  SceneEntity cube;
  cube.kind = PrimitiveKind::Cube; cube.name = "Occluder";
  cube.objectId = 30; cube.position = {0.4f, 1.2f, 0.6f}; cube.scale = {0.35f, 0.8f, 0.3f};
  scene.entities.push_back(cube);
  SceneEntity canvas;
  canvas.kind = PrimitiveKind::Canvas; canvas.name = "Canvas";
  canvas.objectId = 40; canvas.uiWidth = 960; canvas.uiHeight = 640;
  scene.entities.push_back(canvas);
  SceneEntity button;
  button.kind = PrimitiveKind::UiButton; button.name = "Change Text";
  button.objectId = 42; button.parent = 3; button.uiX = 30; button.uiY = 550;
  button.uiWidth = 350; button.uiHeight = 56; button.uiFontSize = 24;
  NativeScriptBinding binding{"assets/scripts/UiDemo.cpp", "UiDemo", true, {}};
  binding.values = {{"worldLabel", ScriptValueType::Object, 0, false, 20},
                    {"status", ScriptValueType::Object, 0, false, 44},
                    {"image", ScriptValueType::Object, 0, false, 46}};
  button.scripts.push_back(binding);
  scene.entities.push_back(button);
  SceneEntity status;
  status.kind = PrimitiveKind::UiText; status.name = "Status";
  status.objectId = 44; status.parent = 3; status.uiX = 30; status.uiY = 25;
  scene.entities.push_back(status);
  SceneEntity image;
  image.kind = PrimitiveKind::UiImage; image.name = "Portrait";
  image.objectId = 46; image.parent = 3;
  scene.entities.push_back(image);
  fs::create_directories(root / "scenes");
  if (!SaveScene((root / "scenes/main.ilmeeescene").string(), scene)) return 1;
  auto options = DefaultScriptCompileOptions(root);
  options.sdkInclude = engine / "include";
  const auto module = CompileNativeScript(options, binding.source);
  if (!module.ok) { std::cerr << module.diagnostics; return 1; }
  fs::create_directories(root / "lib");
  fs::copy_file(module.library, root / "lib/UiDemo.so");
  Write(root / "native-scripts.json", R"([{"source":"assets/scripts/UiDemo.cpp","className":"UiDemo","library":"lib/UiDemo.so"}])");
  std::cout << root << '\n';
}
