#include "../../../include/ui/MainWindow.hpp"
#include <filesystem>

void MainWindow::RenderNativeScriptAddMenu(size_t meshIndex) {
  auto &mesh = sceneRenderer->meshes3d[meshIndex];
  const auto sources = nativeScripts.Sources();
  if (!sources.empty())
    TextDisabled("Project scripts");
  for (const auto &source : sources) {
    const bool attached = std::any_of(
        mesh.scripts.begin(), mesh.scripts.end(),
        [&](const auto &binding) { return binding.source == source; });
    const bool ready = nativeScripts.Ready(source);
    PushID(source.c_str());
    BeginDisabled(attached || !ready || nativeScripts.IsCompiling());
    const auto name = std::filesystem::path(source).stem().string();
    if (Selectable(name.c_str())) {
      ilmeee::NativeScriptBinding binding;
      binding.source = source;
      binding.className = name;
      binding.values = nativeScripts.Properties(binding);
      mesh.scripts.push_back(std::move(binding));
      RecordEditorHistory();
    }
    EndDisabled();
    if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      SetTooltip("%s", attached ? "Already attached"
                       : ready  ? source.c_str()
                                : "Compile failed. See Console > Output.");
    PopID();
  }
}

void MainWindow::RenderNativeScriptInspector(size_t meshIndex) {
  auto &bindings = sceneRenderer->meshes3d[meshIndex].scripts;
  for (size_t index = 0; index < bindings.size();) {
    auto &binding = bindings[index];
    PushID(static_cast<int>(index));
    bool keep = true;
    if (CollapsingHeader((binding.className + " (Script)").c_str(), &keep,
                         ImGuiTreeNodeFlags_DefaultOpen) &&
        keep) {
      if (Checkbox("Enabled", &binding.enabled))
        RecordEditorHistory();
      TextWrapped("%s", binding.source.c_str());
      if (SmallButton("Open Script"))
        projectHandler.OpenFile(
            (std::filesystem::path(projectHandler.projectPath) / binding.source)
                .string());
      SameLine();
      BeginDisabled(SceneStructureLocked() || nativeScripts.IsCompiling());
      if (SmallButton("Recompile"))
        nativeScripts.CompileAll();
      EndDisabled();
      if (!nativeScripts.Ready(binding.source))
        TextWrapped("%s", nativeScripts.IsCompiling()
                              ? "Compiling..."
                              : "Script is not ready. See compiler errors in "
                                "Console > Output.");
      auto values = nativeScripts.Properties(binding);
      for (auto &value : values) {
        PushID(value.name.c_str());
        bool changed = false, finished = false;
        if (value.type == ilmeee::ScriptValueType::Float) {
          changed = DragFloat(value.name.c_str(), &value.number, 0.05f);
          finished = IsItemDeactivatedAfterEdit();
        } else if (value.type == ilmeee::ScriptValueType::Bool) {
          changed = Checkbox(value.name.c_str(), &value.boolean);
          finished = changed;
        } else {
          const auto label = [&](uint64_t id) -> std::string {
            if (!id)
              return "None (GameObject)";
            for (const auto &mesh : sceneRenderer->meshes3d)
              if (mesh.objectId == id)
                return mesh.displayName;
            return "Missing GameObject";
          };
          if (BeginCombo(value.name.c_str(), label(value.object).c_str())) {
            if (Selectable("None", value.object == 0)) {
              value.object = 0;
              changed = true;
            }
            for (const auto &mesh : sceneRenderer->meshes3d) {
              PushID(std::to_string(mesh.objectId).c_str());
              if (Selectable(mesh.displayName.c_str(),
                             value.object == mesh.objectId)) {
                value.object = mesh.objectId;
                changed = true;
              }
              PopID();
            }
            EndCombo();
          }
          if (BeginDragDropTarget()) {
            if (const auto *payload = AcceptDragDropPayload("ILMEEE_MESH")) {
              if (payload->DataSize == sizeof(int)) {
                const int target = *static_cast<const int *>(payload->Data);
                if (target >= 0 && static_cast<size_t>(target) <
                                       sceneRenderer->meshes3d.size()) {
                  value.object = sceneRenderer->meshes3d[target].objectId;
                  changed = true;
                }
              }
            }
            EndDragDropTarget();
          }
          finished = changed;
        }
        if (changed) {
          auto it = std::find_if(
              binding.values.begin(), binding.values.end(),
              [&](const auto &stored) { return stored.name == value.name; });
          if (it == binding.values.end())
            binding.values.push_back(value);
          else
            *it = value;
        }
        if (finished)
          RecordEditorHistory();
        PopID();
      }
    }
    if (!keep) {
      bindings.erase(bindings.begin() + index);
      RecordEditorHistory();
    } else
      ++index;
    PopID();
  }
}
