#pragma once
// Interactive Debug Overlay ("Inspect Mode") for Ilmeee Engine.
//
// Launch the editor with --debug to enable. Press F2 in the editor to
// toggle at runtime. While active, hovering any traced UI panel/widget
// or scene object draws a cyan outline and a tooltip with the source
// location (file:line) that produced it. Ctrl+C copies "full/path:line"
// to the clipboard for one-click IDE navigation.
//
// Annotating UI sites:
//   void MainWindow::RenderInspectorWindow() {
//       Begin("Inspector");
//       DEBUG_TRACE_PANEL("Inspector");        // panel-level, after Begin()
//       ...
//       DragFloat3("Position", position, 0.02f);
//       DEBUG_TRACE_ITEM("Position drag");     // item-level, after widget
//   }
//
// Annotating scene objects:
//   sceneRenderer->LoadCube("Cube");
//   sceneRenderer->SetMesh3DDebugSource(
//       sceneRenderer->GetMesh3DCount() - 1, __FILE__, __LINE__);
//
// Cost when Inspect Mode is off: a single bool branch per macro.

#include "Debugger.hpp"
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

namespace Debug {
namespace Inspect {

struct TraceRegion {
  std::string label;
  std::string file;
  int line = 0;
  ImVec2 rectMin{0.0f, 0.0f};
  ImVec2 rectMax{0.0f, 0.0f};
  bool isItem = false; // true = single widget rect, false = whole panel
};

inline std::vector<TraceRegion> &GetTraceRegions() {
  static std::vector<TraceRegion> regions;
  return regions;
}

// Stripped-down basename so the tooltip stays narrow. The full path is
// still available via the Ctrl+C clipboard copy.
inline const char *BasenameOf(const char *path) {
  if (!path)
    return "";
  const char *out = path;
  for (const char *p = path; *p; ++p) {
    if (*p == '/' || *p == '\\')
      out = p + 1;
  }
  return out;
}

// Must be called once per frame, before any DEBUG_TRACE_* macros fire.
// Wiring is in MainWindow::OnRender(). Cheap no-op when Inspect Mode is off.
inline void BeginFrame() {
  if (!g_InspectModeActive)
    return;
  GetTraceRegions().clear();
}

// Record an arbitrary rect. UI code should prefer the macros below.
inline void RecordRegion(const char *label, const char *file, int line,
                         ImVec2 rectMin, ImVec2 rectMax, bool isItem) {
  if (!g_InspectModeActive)
    return;
  GetTraceRegions().push_back(
      {label ? label : "", file ? file : "", line, rectMin, rectMax, isItem});
}

// Records the current ImGui window (whatever was opened by the most recent
// Begin() / BeginChild() at this point in the call stack). Place right after
// Begin(...) inside a Render*Window() method.
inline void TraceCurrentWindow(const char *label, const char *file, int line) {
  if (!g_InspectModeActive)
    return;
  ImVec2 pos = ImGui::GetWindowPos();
  ImVec2 size = ImGui::GetWindowSize();
  RecordRegion(label, file, line, pos, ImVec2(pos.x + size.x, pos.y + size.y),
               false);
}

// Records the bounding rect of the most-recently-submitted ImGui item.
// Place RIGHT AFTER a widget call (Button, Drag*, Selectable, etc.).
inline void TraceLastItem(const char *label, const char *file, int line) {
  if (!g_InspectModeActive)
    return;
  RecordRegion(label, file, line, ImGui::GetItemRectMin(),
               ImGui::GetItemRectMax(), true);
}

// Drawn once per frame at the end of OnRender (after ImGui::End on the
// dockspace). Walks the trace registry, finds the smallest rect containing
// the mouse cursor, and draws an outline + tooltip on the foreground draw
// list so it stays above every panel.
inline void RenderHoverOverlay() {
  if (!g_InspectModeActive)
    return;

  ImDrawList *fg = ImGui::GetForegroundDrawList();
  const ImGuiIO &io = ImGui::GetIO();
  const ImVec2 mouse = io.MousePos;

  // Persistent badge so users never wonder whether the overlay is on.
  // Placed below the OS menubar (~22 px) so it doesn't collide with the
  // FPS counter on the right edge of the main menu.
  {
    const char *badge = "INSPECT [F2] ON  (Ctrl+C = copy file:line)";
    ImVec2 ts = ImGui::CalcTextSize(badge);
    ImVec2 vsz = io.DisplaySize;
    ImVec2 p(vsz.x - ts.x - 16.0f, 28.0f);
    fg->AddRectFilled(ImVec2(p.x - 8, p.y - 4),
                      ImVec2(p.x + ts.x + 8, p.y + ts.y + 4),
                      IM_COL32(220, 64, 64, 220), 4.0f);
    fg->AddText(p, IM_COL32(255, 255, 255, 255), badge);
  }

  auto &regs = GetTraceRegions();
  if (regs.empty())
    return;

  // Pick the smallest region that contains the cursor: item beats panel,
  // most-specific rect wins when nested.
  int best = -1;
  float bestArea = 0.0f;
  for (size_t i = 0; i < regs.size(); ++i) {
    const auto &r = regs[i];
    if (mouse.x < r.rectMin.x || mouse.x > r.rectMax.x ||
        mouse.y < r.rectMin.y || mouse.y > r.rectMax.y)
      continue;
    float w = r.rectMax.x - r.rectMin.x;
    float h = r.rectMax.y - r.rectMin.y;
    if (w <= 0.0f || h <= 0.0f)
      continue;
    float a = w * h;
    if (best < 0 || a < bestArea) {
      best = (int)i;
      bestArea = a;
    }
  }
  if (best < 0)
    return;

  const TraceRegion &r = regs[best];

  // Cyan outline + faint fill so the hovered area pops without obscuring it.
  const ImU32 outline =
      r.isItem ? IM_COL32(255, 200, 80, 255) : IM_COL32(80, 200, 255, 255);
  const ImU32 fill =
      r.isItem ? IM_COL32(255, 200, 80, 40) : IM_COL32(80, 200, 255, 32);
  fg->AddRectFilled(r.rectMin, r.rectMax, fill);
  fg->AddRect(r.rectMin, r.rectMax, outline, 2.0f, 0, 2.0f);

  char tip[640];
  std::snprintf(tip, sizeof(tip), "%s\n%s:%d", r.label.c_str(),
                BasenameOf(r.file.c_str()), r.line);
  ImVec2 ts = ImGui::CalcTextSize(tip);
  ImVec2 tp(mouse.x + 18.0f, mouse.y + 18.0f);
  // Keep tooltip on-screen.
  ImVec2 vsz = io.DisplaySize;
  if (tp.x + ts.x + 12.0f > vsz.x)
    tp.x = vsz.x - ts.x - 12.0f;
  if (tp.y + ts.y + 12.0f > vsz.y)
    tp.y = vsz.y - ts.y - 12.0f;
  fg->AddRectFilled(ImVec2(tp.x - 8, tp.y - 6),
                    ImVec2(tp.x + ts.x + 8, tp.y + ts.y + 6),
                    IM_COL32(20, 22, 28, 235), 4.0f);
  fg->AddRect(ImVec2(tp.x - 8, tp.y - 6),
              ImVec2(tp.x + ts.x + 8, tp.y + ts.y + 6), outline, 4.0f);
  fg->AddText(tp, IM_COL32(255, 255, 255, 255), tip);

  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
    char clip[1024];
    std::snprintf(clip, sizeof(clip), "%s:%d", r.file.c_str(), r.line);
    ImGui::SetClipboardText(clip);
  }
}

} // namespace Inspect
} // namespace Debug

// Public macros — preferred over calling Inspect:: directly so __FILE__ /
// __LINE__ resolve at the actual call site, not inside this header.
#define DEBUG_TRACE_PANEL(label)                                               \
  ::Debug::Inspect::TraceCurrentWindow((label), __FILE__, __LINE__)
#define DEBUG_TRACE_ITEM(label)                                                \
  ::Debug::Inspect::TraceLastItem((label), __FILE__, __LINE__)
// Free-form rect (use when the area you want to highlight isn't a current
// window or last-item — e.g. an offscreen viewport hit).
#define DEBUG_TRACE_RECT(label, minXY, maxXY)                                  \
  ::Debug::Inspect::RecordRegion((label), __FILE__, __LINE__, (minXY),         \
                                 (maxXY), true)
