#include "../header/SecondaryWindow.hpp"
#include <imgui.h>

void ShowSecondaryWindow(bool* isOpen) {
    if (!*isOpen) return;

    ImGui::Begin("Tools", isOpen, ImGuiWindowFlags_AlwaysAutoResize);

    static bool toggleFeature = false;
    ImGui::Checkbox("Enable Feature X", &toggleFeature);
    
    static float value = 0.5f;
    ImGui::SliderFloat("Adjustment", &value, 0.0f, 1.0f);

    ImGui::Text("This is a tools/debug window.");

    ImGui::End();
}
