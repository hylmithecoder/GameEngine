#pragma once

#include <string>

namespace Ilmeee {

// Hint where a panel prefers to live inside the editor dockspace
// on first launch. After that, the layout is persisted by ImGui in
// imgui.ini and the user's manual rearrangement wins.
enum class DockHint {
  None,   // Floating / no preference
  Left,   // Typically hierarchy, scene tree
  Right,  // Typically inspector, properties
  Bottom, // Typically console, log, asset explorer
  Center  // Typically viewport (scene, game)
};

// Abstract editor panel. Inherit and override OnImGuiRender().
// The PanelManager calls Begin()/End() for you only if you opt in via
// UsesDefaultWindow() = true; otherwise OnImGuiRender() is fully manual
// (use this for special things like fullscreen overlays or menubar).
class Panel {
public:
  virtual ~Panel() = default;

  virtual void OnAttach() {}
  virtual void OnDetach() {}

  // Called every frame between ImGui::NewFrame() and ImGui::Render(),
  // only if IsOpen() == true. When UsesDefaultWindow() is true, the
  // PanelManager wraps this call with Begin(name)/End(); otherwise the
  // panel is responsible for its own ImGui::Begin/End.
  virtual void OnImGuiRender() = 0;

  virtual const char *GetName() const = 0;
  virtual DockHint GetDockHint() const { return DockHint::None; }
  virtual bool UsesDefaultWindow() const { return true; }

  bool IsOpen() const { return m_Open; }
  bool *GetOpenRef() { return &m_Open; }
  void SetOpen(bool v) { m_Open = v; }

  // Window flags for the default window wrapper (only used if
  // UsesDefaultWindow() == true).
  virtual int GetWindowFlags() const { return 0; }

protected:
  bool m_Open = true;
};

} // namespace Ilmeee
