#include "../../../../include/core_engine/core_editor/EditorTheme.hpp"

#include <imgui.h>

namespace Ilmeee {

namespace {

void ApplyCommonStyle(ImGuiStyle &style) {
  // Spacing — slightly tighter than the old defaults so dense panels
  // (inspector, hierarchy) don't waste vertical space.
  style.WindowPadding = ImVec2(10, 10);
  style.FramePadding = ImVec2(8, 5);
  style.CellPadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(8, 5);
  style.ItemInnerSpacing = ImVec2(6, 4);
  style.IndentSpacing = 18.0f;
  style.ScrollbarSize = 14.0f;
  style.GrabMinSize = 12.0f;

  // Rounding — softer corners but not so round that things look toy-like.
  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 9.0f;
  style.GrabRounding = 3.0f;
  style.TabRounding = 4.0f;

  // Borders — thin border helps panel separation inside dockspace.
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.TabBorderSize = 0.0f;
  style.PopupBorderSize = 1.0f;

  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_Right;
  style.ColorButtonPosition = ImGuiDir_Left;

  // Docking — visible tab bar makes layout discoverable.
  style.DockingSeparatorSize = 2.0f;
}

void ApplyDarkColors(ImGuiStyle &style) {
  ImGui::StyleColorsDark();
  ImVec4 *c = style.Colors;

  const ImVec4 bg0 = ImVec4(0.10f, 0.11f, 0.13f, 1.00f); // deepest
  const ImVec4 bg1 = ImVec4(0.13f, 0.14f, 0.17f, 1.00f); // window bg
  const ImVec4 bg2 = ImVec4(0.17f, 0.18f, 0.22f, 1.00f); // frame bg
  const ImVec4 bg3 = ImVec4(0.21f, 0.23f, 0.28f, 1.00f); // hovered/header
  const ImVec4 accent = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
  const ImVec4 accentDim = ImVec4(0.28f, 0.56f, 1.00f, 0.50f);
  const ImVec4 text = ImVec4(0.90f, 0.91f, 0.93f, 1.00f);
  const ImVec4 textDim = ImVec4(0.55f, 0.57f, 0.62f, 1.00f);
  const ImVec4 border = ImVec4(0.27f, 0.29f, 0.34f, 0.55f);

  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = textDim;
  c[ImGuiCol_WindowBg] = bg1;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = bg0;
  c[ImGuiCol_Border] = border;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

  c[ImGuiCol_FrameBg] = bg2;
  c[ImGuiCol_FrameBgHovered] = bg3;
  c[ImGuiCol_FrameBgActive] = ImVec4(accent.x, accent.y, accent.z, 0.35f);

  c[ImGuiCol_TitleBg] = bg0;
  c[ImGuiCol_TitleBgActive] = bg1;
  c[ImGuiCol_TitleBgCollapsed] = bg0;
  c[ImGuiCol_MenuBarBg] = bg0;

  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = bg3;
  c[ImGuiCol_ScrollbarGrabHovered] = accentDim;
  c[ImGuiCol_ScrollbarGrabActive] = accent;

  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent;

  c[ImGuiCol_Button] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
  c[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
  c[ImGuiCol_ButtonActive] = accent;

  c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
  c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
  c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.85f);

  c[ImGuiCol_Separator] = border;
  c[ImGuiCol_SeparatorHovered] = accentDim;
  c[ImGuiCol_SeparatorActive] = accent;

  c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.20f);
  c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
  c[ImGuiCol_ResizeGripActive] = accent;

  c[ImGuiCol_Tab] = bg0;
  c[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
  c[ImGuiCol_TabActive] =
      ImVec4(accent.x * 0.7f, accent.y * 0.7f, accent.z * 0.9f, 1.00f);
  c[ImGuiCol_TabUnfocused] = bg0;
  c[ImGuiCol_TabUnfocusedActive] = bg1;

  c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
  c[ImGuiCol_DockingEmptyBg] = bg0;

  c[ImGuiCol_NavHighlight] = accent;
  c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
  c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.05f, 0.06f, 0.08f, 0.50f);
}

void ApplyLightColors(ImGuiStyle &style) {
  ImGui::StyleColorsLight();
  ImVec4 *c = style.Colors;

  const ImVec4 bg0 = ImVec4(0.92f, 0.93f, 0.95f, 1.00f);
  const ImVec4 bg1 = ImVec4(0.97f, 0.97f, 0.98f, 1.00f);
  const ImVec4 bg2 = ImVec4(0.88f, 0.89f, 0.91f, 1.00f);
  const ImVec4 accent = ImVec4(0.10f, 0.45f, 0.85f, 1.00f);

  c[ImGuiCol_WindowBg] = bg1;
  c[ImGuiCol_MenuBarBg] = bg0;
  c[ImGuiCol_TitleBg] = bg2;
  c[ImGuiCol_TitleBgActive] = bg0;
  c[ImGuiCol_FrameBg] = bg2;
  c[ImGuiCol_Button] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
  c[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
  c[ImGuiCol_ButtonActive] = accent;
  c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.25f);
  c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
  c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
  c[ImGuiCol_Tab] = bg2;
  c[ImGuiCol_TabActive] = ImVec4(accent.x, accent.y, accent.z, 0.60f);
  c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.40f);
  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accent;
}

} // namespace

void EditorTheme::Apply(Variant v) {
  ImGuiStyle &style = ImGui::GetStyle();
  ApplyCommonStyle(style);
  if (v == Variant::Dark) {
    ApplyDarkColors(style);
  } else {
    ApplyLightColors(style);
  }
}

} // namespace Ilmeee
