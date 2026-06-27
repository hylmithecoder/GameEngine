#include "../../../../include/core_engine/core_editor/EditorDockSpace.hpp"

#include <imgui_internal.h>

namespace Ilmeee {

bool EditorDockSpace::s_LayoutInitialized = false;
bool EditorDockSpace::s_LayoutResetRequested = false;

void EditorDockSpace::ResetLayout() { s_LayoutResetRequested = true; }

void EditorDockSpace::Begin(const char *dockspaceId, float topOffset) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImVec2 pos = viewport->WorkPos;
  pos.y += topOffset;
  ImVec2 size = viewport->WorkSize;
  size.y -= topOffset;
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##IlmeeeEditorRoot", nullptr, hostFlags);
  ImGui::PopStyleVar(3);

  ImGuiID id = ImGui::GetID(dockspaceId);
  ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

  if (!s_LayoutInitialized || s_LayoutResetRequested) {
    BuildDefaultLayout(id);
    s_LayoutInitialized = true;
    s_LayoutResetRequested = false;
  }
}

void EditorDockSpace::End() { ImGui::End(); }

void EditorDockSpace::BuildDefaultLayout(ImGuiID dockspaceId) {
  // Only rebuild if no persisted layout exists for this dockspace,
  // unless the user explicitly asked for a reset.
  ImGuiDockNode *existing = ImGui::DockBuilderGetNode(dockspaceId);
  if (existing && !s_LayoutResetRequested) {
    return;
  }

  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId,
                                ImGui::GetMainViewport()->WorkSize);

  // Symmetric side panels — same ratio left/right keeps the center
  // viewport balanced. Bottom row split into 60/40 so video player /
  // preview lives beside the asset+console tab group instead of
  // overlapping with it.
  ImGuiID dockMain = dockspaceId;
  ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.20f,
                                                 nullptr, &dockMain);
  ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right,
                                                  0.20f, nullptr, &dockMain);
  ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down,
                                                   0.30f, nullptr, &dockMain);
  ImGuiID dockBottomRight = ImGui::DockBuilderSplitNode(
      dockBottom, ImGuiDir_Right, 0.40f, nullptr, &dockBottom);

  ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
  ImGui::DockBuilderDockWindow("Inspector", dockRight);
  ImGui::DockBuilderDockWindow("Scene", dockMain);
  ImGui::DockBuilderDockWindow("Game", dockMain);
  ImGui::DockBuilderDockWindow("Main View", dockMain);
  ImGui::DockBuilderDockWindow("Explorer", dockBottom);
  ImGui::DockBuilderDockWindow("Console", dockBottom);
  ImGui::DockBuilderDockWindow("Video Player", dockBottomRight);

  ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace Ilmeee
