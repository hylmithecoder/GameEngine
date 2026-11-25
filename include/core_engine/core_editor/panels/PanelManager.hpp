#pragma once

#include "Panel.hpp"
#include <memory>
#include <utility>
#include <vector>

namespace Ilmeee {

// Owns the editor's panels. Construct panels via Register<T>(args...) so
// the manager has ownership and lifecycle (OnAttach/OnDetach) control.
class PanelManager {
public:
  PanelManager() = default;
  ~PanelManager();

  PanelManager(const PanelManager &) = delete;
  PanelManager &operator=(const PanelManager &) = delete;

  template <typename T, typename... Args> T *Register(Args &&...args) {
    static_assert(std::is_base_of<Panel, T>::value,
                  "T must derive from Ilmeee::Panel");
    auto panel = std::make_unique<T>(std::forward<Args>(args)...);
    T *raw = panel.get();
    raw->OnAttach();
    m_Panels.push_back(std::move(panel));
    return raw;
  }

  // Render all open panels in registration order.
  void RenderAll();

  // Render a list of "Window > <PanelName>" toggle entries, suitable
  // to drop inside an existing ImGui::BeginMenu("Window") block.
  void RenderWindowMenuItems();

  Panel *Find(const char *name);

  const std::vector<std::unique_ptr<Panel>> &Panels() const { return m_Panels; }

private:
  std::vector<std::unique_ptr<Panel>> m_Panels;
};

} // namespace Ilmeee
