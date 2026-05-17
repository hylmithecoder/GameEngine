#include "../../../../../include/core_engine/core_editor/panels/PanelManager.hpp"

#include <cstring>
#include <imgui.h>

namespace Ilmeee {

PanelManager::~PanelManager() {
  for (auto &p : m_Panels) {
    p->OnDetach();
  }
}

void PanelManager::RenderAll() {
  for (auto &p : m_Panels) {
    if (!p->IsOpen())
      continue;

    if (p->UsesDefaultWindow()) {
      bool open = p->IsOpen();
      if (ImGui::Begin(p->GetName(), &open, p->GetWindowFlags())) {
        p->OnImGuiRender();
      }
      ImGui::End();
      p->SetOpen(open);
    } else {
      p->OnImGuiRender();
    }
  }
}

void PanelManager::RenderWindowMenuItems() {
  for (auto &p : m_Panels) {
    if (!p->UsesDefaultWindow())
      continue;
    bool open = p->IsOpen();
    if (ImGui::MenuItem(p->GetName(), nullptr, &open)) {
      p->SetOpen(open);
    } else {
      p->SetOpen(open);
    }
  }
}

Panel *PanelManager::Find(const char *name) {
  for (auto &p : m_Panels) {
    if (std::strcmp(p->GetName(), name) == 0) {
      return p.get();
    }
  }
  return nullptr;
}

} // namespace Ilmeee
