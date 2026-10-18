// Lighting menu + window: scene-wide NPR (toon / cel) lighting.
#include "../../../include/ui/MainWindow.hpp"

using namespace ImGui;

namespace {

struct NprPreset {
  const char *name;
  const char *tooltip;
  ilmeee::NprSettings settings;
};

ilmeee::NprSettings AnimeCel() {
  ilmeee::NprSettings s;
  s.enabled = true;
  return s; // the defaults are tuned for this look
}

ilmeee::NprSettings SoftToon() {
  ilmeee::NprSettings s;
  s.enabled = true;
  s.bands = 3;
  s.softness = 0.2f;
  s.shadowColor = {0.70f, 0.68f, 0.78f};
  s.rimWidth = 0.3f;
  s.rimStrength = 0.35f;
  s.specular = false;
  s.outlineColor = {0.25f, 0.20f, 0.22f};
  s.outlineWidth = 1.0f;
  return s;
}

ilmeee::NprSettings ComicInk() {
  ilmeee::NprSettings s;
  s.enabled = true;
  s.softness = 0.0f;
  s.shadowColor = {0.35f, 0.35f, 0.40f};
  s.rim = false;
  s.specularSize = 0.03f;
  s.specularStrength = 0.8f;
  s.outlineColor = {0.0f, 0.0f, 0.0f};
  s.outlineWidth = 3.0f;
  return s;
}

const NprPreset kPresets[] = {
    {"Anime Cel", "Two hard bands, purple shadows, rim, thin ink lines",
     AnimeCel()},
    {"Soft Toon", "Three soft bands, no highlight, light brown lines",
     SoftToon()},
    {"Comic Ink", "Hard dark shadows and thick black outlines", ComicInk()},
};

} // namespace

void MainWindow::RenderLightingMenu() {
  if (!BeginMenu("Lighting"))
    return;
  if (!sceneRenderer) {
    TextDisabled("No scene renderer");
    EndMenu();
    return;
  }
  ilmeee::NprSettings npr = sceneRenderer->GetNpr();
  bool changed = MenuItem("NPR (Toon) Lighting", nullptr, &npr.enabled);
  if (BeginMenu("Presets")) {
    for (const auto &preset : kPresets) {
      if (MenuItem(preset.name)) {
        npr = preset.settings;
        changed = true;
      }
      if (IsItemHovered())
        SetTooltip("%s", preset.tooltip);
    }
    EndMenu();
  }
  Separator();
  MenuItem("Lighting Settings...", nullptr, &showLighting);
  if (changed)
    sceneRenderer->SetNpr(npr);
  EndMenu();
}

void MainWindow::RenderLightingWindow() {
  SetNextWindowSize(ImVec2(360, 0), ImGuiCond_FirstUseEver);
  if (!Begin("Lighting", &showLighting)) {
    End();
    return;
  }
  if (!sceneRenderer) {
    TextDisabled("No scene renderer");
    End();
    return;
  }
  ilmeee::NprSettings npr = sceneRenderer->GetNpr();
  bool changed = Checkbox("NPR (Toon) Lighting", &npr.enabled);
  SameLine();
  TextDisabled("(?)");
  if (IsItemHovered())
    SetTooltip("Non-photorealistic rendering: cel-shaded light bands, rim\n"
               "light and ink outlines, for an anime / cartoon look.\n"
               "Saved with the scene; applies in the editor and the game.");

  for (size_t i = 0; i < std::size(kPresets); ++i) {
    if (i)
      SameLine();
    if (SmallButton(kPresets[i].name)) {
      npr = kPresets[i].settings;
      changed = true;
    }
    if (IsItemHovered())
      SetTooltip("%s", kPresets[i].tooltip);
  }

  BeginDisabled(!npr.enabled);
  SeparatorText("Shading");
  changed |= SliderInt("Bands", &npr.bands, 2, 5);
  changed |= SliderFloat("Softness", &npr.softness, 0.0f, 0.5f, "%.2f");
  changed |= ColorEdit3("Shadow Color", &npr.shadowColor.x);

  SeparatorText("Rim Light");
  changed |= Checkbox("Rim##enabled", &npr.rim);
  BeginDisabled(!npr.rim);
  changed |= ColorEdit3("Rim Color", &npr.rimColor.x);
  changed |= SliderFloat("Rim Width", &npr.rimWidth, 0.0f, 1.0f, "%.2f");
  changed |= SliderFloat("Rim Strength", &npr.rimStrength, 0.0f, 2.0f, "%.2f");
  EndDisabled();

  SeparatorText("Highlight");
  changed |= Checkbox("Specular##enabled", &npr.specular);
  BeginDisabled(!npr.specular);
  changed |= SliderFloat("Size", &npr.specularSize, 0.0f, 0.5f, "%.2f");
  changed |=
      SliderFloat("Strength", &npr.specularStrength, 0.0f, 2.0f, "%.2f");
  EndDisabled();

  SeparatorText("Outline");
  changed |= Checkbox("Outline##enabled", &npr.outline);
  BeginDisabled(!npr.outline);
  changed |= ColorEdit3("Outline Color", &npr.outlineColor.x);
  changed |= SliderFloat("Width", &npr.outlineWidth, 0.0f, 10.0f, "%.1f px");
  EndDisabled();
  EndDisabled();

  Spacing();
  TextDisabled("Light direction and colour come from the scene's first "
               "Light object.");
  if (changed)
    sceneRenderer->SetNpr(npr);
  End();
}
