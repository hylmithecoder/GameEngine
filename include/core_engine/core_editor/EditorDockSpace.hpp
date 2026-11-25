#pragma once

#include <imgui.h>

namespace Ilmeee {

// Fullscreen dockspace that hosts the editor's panels.
//
// Usage in the editor's per-frame ImGui pass:
//
//   EditorDockSpace::Begin();   // creates root host window + dockspace
//   panelManager.RenderAll();   // panels dock into it automatically
//   EditorDockSpace::End();
//
// On the first run (no imgui.ini layout) a default layout is built:
//
//   ┌────────────┬───────────────────────┬──────────────┐
//   │            │                       │              │
//   │ Hierarchy  │     Scene  /  Game    │  Inspector   │
//   │            │                       │              │
//   ├────────────┴───────────────────────┴──────────────┤
//   │            Console / Explorer (tabbed)            │
//   └───────────────────────────────────────────────────┘
//
// Panel names referenced for default docking match what the editor
// registers (see DefaultDockLayout(): "Hierarchy", "Inspector",
// "Scene", "Game", "Console", "Explorer"). Any panel not in the
// default map floats and the user can dock it manually.
class EditorDockSpace {
public:
  static void Begin(const char *dockspaceId = "IlmeeeDockSpace",
                    float topOffset = 0.0f);
  static void End();

  // Force-rebuild the default layout next frame (e.g. "Reset Layout"
  // menu item). Safe to call any time.
  static void ResetLayout();

private:
  static void BuildDefaultLayout(ImGuiID dockspaceId);
  static bool s_LayoutInitialized;
  static bool s_LayoutResetRequested;
};

} // namespace Ilmeee
