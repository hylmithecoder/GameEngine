#include "../../../../include/core_engine/core_editor/EditorTheme.hpp"

#include <imgui.h>

namespace Ilmeee {

namespace {

void ApplyCommonStyle(ImGuiStyle &style) {
  // Unity 6 inspired compact, crisp layout metrics
  style.WindowPadding = ImVec2(8, 8);
  style.FramePadding = ImVec2(7, 4);
  style.CellPadding = ImVec2(6, 4);
  style.ItemSpacing = ImVec2(6, 5);
  style.ItemInnerSpacing = ImVec2(5, 4);
  style.IndentSpacing = 16.0f;
  style.ScrollbarSize = 11.0f;
  style.GrabMinSize = 10.0f;

  // Crisp subtle rounding
  style.WindowRounding = 4.0f;
  style.ChildRounding = 4.0f;
  style.FrameRounding = 3.0f;
  style.PopupRounding = 5.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 3.0f;
  style.TabRounding = 4.0f;

  // Clean 1px borders for inputs, popups, and panel separation
  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.TabBorderSize = 0.0f;
  style.TabBarBorderSize = 1.0f;
  style.TabBarOverlineSize = 2.0f;
  style.PopupBorderSize = 1.0f;

  style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
  style.WindowMenuButtonPosition = ImGuiDir_Right;
  style.ColorButtonPosition = ImGuiDir_Left;

  style.DockingSeparatorSize = 2.0f;
}

void ApplyDarkColors(ImGuiStyle &style) {
  ImGui::StyleColorsDark();
  ImVec4 *c = style.Colors;

  // Unity 6 Charcoal-Slate Palette
  const ImVec4 bgGutter    = ImVec4(0.090f, 0.098f, 0.114f, 1.00f); // #17191D tab strip / menu
  const ImVec4 bgPanel     = ImVec4(0.125f, 0.137f, 0.161f, 1.00f); // #202329 main panel body
  const ImVec4 bgInput     = ImVec4(0.098f, 0.106f, 0.125f, 1.00f); // #191B20 recessed input
  const ImVec4 bgElevated  = ImVec4(0.173f, 0.188f, 0.220f, 1.00f); // #2C3038 neutral button
  const ImVec4 bgHover     = ImVec4(0.224f, 0.243f, 0.282f, 1.00f); // #393E48 hover surface
  const ImVec4 accent      = ImVec4(0.227f, 0.510f, 0.965f, 1.00f); // #3A82F6 Unity active blue
  const ImVec4 accentSoft  = ImVec4(0.196f, 0.392f, 0.686f, 0.85f); // #3264AF selection row
  const ImVec4 accentDim   = ImVec4(0.227f, 0.510f, 0.965f, 0.40f);
  const ImVec4 textMain    = ImVec4(0.886f, 0.906f, 0.933f, 1.00f); // #E2E7EE
  const ImVec4 textDim     = ImVec4(0.545f, 0.580f, 0.639f, 1.00f); // #8B94A3
  const ImVec4 borderSub   = ImVec4(0.204f, 0.224f, 0.263f, 0.85f); // #343943

  c[ImGuiCol_Text] = textMain;
  c[ImGuiCol_TextDisabled] = textDim;
  c[ImGuiCol_WindowBg] = bgPanel;
  c[ImGuiCol_ChildBg] = ImVec4(0.110f, 0.120f, 0.141f, 1.00f);
  c[ImGuiCol_PopupBg] = ImVec4(0.110f, 0.118f, 0.137f, 0.98f);
  c[ImGuiCol_Border] = borderSub;
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

  c[ImGuiCol_FrameBg] = bgInput;
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.157f, 0.184f, 1.00f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.165f, 0.220f, 0.310f, 1.00f);

  c[ImGuiCol_TitleBg] = bgGutter;
  c[ImGuiCol_TitleBgActive] = bgGutter;
  c[ImGuiCol_TitleBgCollapsed] = bgGutter;
  c[ImGuiCol_MenuBarBg] = bgGutter;

  c[ImGuiCol_ScrollbarBg] = ImVec4(0.08f, 0.09f, 0.10f, 0.35f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.26f, 0.28f, 0.33f, 0.80f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.38f, 0.44f, 0.95f);
  c[ImGuiCol_ScrollbarGrabActive] = accent;

  c[ImGuiCol_CheckMark] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = ImVec4(0.35f, 0.62f, 1.00f, 1.00f);

  // Neutral dark buttons like Unity 6 (only active/primary actions light up blue)
  c[ImGuiCol_Button] = bgElevated;
  c[ImGuiCol_ButtonHovered] = bgHover;
  c[ImGuiCol_ButtonActive] = accentSoft;

  // Headers (CollapsingHeader, Selectable, TreeNode selected)
  c[ImGuiCol_Header] = ImVec4(0.180f, 0.325f, 0.545f, 0.72f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.220f, 0.390f, 0.640f, 0.80f);
  c[ImGuiCol_HeaderActive] = accent;

  c[ImGuiCol_Separator] = borderSub;
  c[ImGuiCol_SeparatorHovered] = accentDim;
  c[ImGuiCol_SeparatorActive] = accent;

  c[ImGuiCol_ResizeGrip] = ImVec4(accent.x, accent.y, accent.z, 0.15f);
  c[ImGuiCol_ResizeGripHovered] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
  c[ImGuiCol_ResizeGripActive] = accent;

  // Unity 6 Tab Bar: seamless panel-connected active tab + 2px blue top overline
  c[ImGuiCol_Tab] = bgGutter;
  c[ImGuiCol_TabHovered] = bgHover;
  c[ImGuiCol_TabSelected] = bgPanel;
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_TabDimmed] = bgGutter;
  c[ImGuiCol_TabDimmedSelected] = bgPanel;
  c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.38f);

  c[ImGuiCol_TableHeaderBg] = bgGutter;
  c[ImGuiCol_TableBorderStrong] = borderSub;
  c[ImGuiCol_TableBorderLight] = ImVec4(0.18f, 0.20f, 0.24f, 0.65f);
  c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);

  c[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
  c[ImGuiCol_DockingEmptyBg] = bgGutter;

  c[ImGuiCol_NavHighlight] = accent;
  c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
  c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.05f, 0.07f, 0.60f);
}

void ApplyLightColors(ImGuiStyle &style) {
  ImGui::StyleColorsLight();
  ImVec4 *c = style.Colors;

  const ImVec4 bgGutter = ImVec4(0.85f, 0.87f, 0.90f, 1.00f);
  const ImVec4 bgPanel  = ImVec4(0.93f, 0.94f, 0.96f, 1.00f);
  const ImVec4 bgInput  = ImVec4(0.99f, 0.99f, 1.00f, 1.00f);
  const ImVec4 bgBtn    = ImVec4(0.87f, 0.89f, 0.92f, 1.00f);
  const ImVec4 accent   = ImVec4(0.13f, 0.44f, 0.86f, 1.00f);

  c[ImGuiCol_WindowBg] = bgPanel;
  c[ImGuiCol_ChildBg] = ImVec4(0.90f, 0.91f, 0.94f, 1.00f);
  c[ImGuiCol_MenuBarBg] = bgGutter;
  c[ImGuiCol_TitleBg] = bgGutter;
  c[ImGuiCol_TitleBgActive] = bgGutter;
  c[ImGuiCol_FrameBg] = bgInput;
  c[ImGuiCol_Border] = ImVec4(0.72f, 0.75f, 0.80f, 0.90f);
  c[ImGuiCol_Button] = bgBtn;
  c[ImGuiCol_ButtonHovered] = ImVec4(0.80f, 0.83f, 0.88f, 1.00f);
  c[ImGuiCol_ButtonActive] = ImVec4(accent.x, accent.y, accent.z, 0.35f);
  c[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.22f);
  c[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.36f);
  c[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
  c[ImGuiCol_Tab] = bgGutter;
  c[ImGuiCol_TabHovered] = ImVec4(0.80f, 0.84f, 0.90f, 1.00f);
  c[ImGuiCol_TabSelected] = bgPanel;
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_TabDimmed] = bgGutter;
  c[ImGuiCol_TabDimmedSelected] = bgPanel;
  c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
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
