#include "../../../include/ui/MainWindow.hpp"
#include <filesystem>

namespace {
int ResizeStringCallback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto *text = static_cast<std::string *>(data->UserData);
    text->resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = text->data();
  }
  return 0;
}
bool InputString(const char *label, std::string &text) {
  return ImGui::InputText(label, text.data(), text.capacity() + 1,
                          ImGuiInputTextFlags_CallbackResize,
                          ResizeStringCallback, &text);
}
const char *ScriptTypeName(ilmeee::ScriptValueType type) {
  switch (type) {
  case ilmeee::ScriptValueType::Float: return "float";
  case ilmeee::ScriptValueType::Bool: return "bool";
  case ilmeee::ScriptValueType::Object: return "GameObject";
  case ilmeee::ScriptValueType::Vec3: return "Vec3";
  case ilmeee::ScriptValueType::String: return "string";
  default: return "List";
  }
}
} // namespace

void MainWindow::RenderNativeScriptAddMenu(size_t meshIndex) {
  RenderNativeScriptChoices(sceneRenderer->meshes3d[meshIndex].scripts);
}
void MainWindow::RenderNativeScriptChoices(std::vector<ilmeee::NativeScriptBinding> &bindings) {
  const auto sources = nativeScripts.Sources();
  if (!sources.empty())
    TextDisabled("Project scripts");
  for (const auto &source : sources) {
    const bool attached = std::any_of(
        bindings.begin(), bindings.end(),
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
      bindings.push_back(std::move(binding));
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
  RenderNativeScriptBindings(sceneRenderer->meshes3d[meshIndex].scripts);
}
void MainWindow::RenderNativeScriptBindings(std::vector<ilmeee::NativeScriptBinding> &bindings) {
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
        bool finished = false;
        const bool changed =
            RenderScriptValueField(value, value.name.c_str(), finished);
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

bool MainWindow::RenderScriptValueField(ilmeee::ScriptValue &value,
                                        const char *label, bool &finished) {
  using Type = ilmeee::ScriptValueType;
  bool changed = false;
  if (value.type == Type::Float) {
    changed = DragFloat(label, &value.number, 0.05f);
    finished |= IsItemDeactivatedAfterEdit();
  } else if (value.type == Type::Vec3) {
    changed = DragFloat3(label, value.vec3, 0.05f);
    finished |= IsItemDeactivatedAfterEdit();
  } else if (value.type == Type::Bool) {
    changed = Checkbox(label, &value.boolean);
    finished |= changed;
  } else if (value.type == Type::String) {
    changed = InputString(label, value.string);
    finished |= IsItemDeactivatedAfterEdit();
  } else if (value.type == Type::Object) {
    changed = RenderScriptObjectField(value.object, label);
    finished |= changed;
  } else if (value.type == Type::List) {
    const bool open = TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen,
                                 "%s (List<%s>, %zu)", label,
                                 ScriptTypeName(value.elementType),
                                 value.items.size());
    if (!open)
      return false;
    size_t remove = value.items.size();
    for (size_t i = 0; i < value.items.size(); ++i) {
      PushID(static_cast<int>(i));
      if (SmallButton("X"))
        remove = i;
      SameLine();
      const auto itemLabel = "[" + std::to_string(i) + "]";
      changed |= RenderScriptValueField(value.items[i], itemLabel.c_str(),
                                        finished);
      PopID();
    }
    if (remove < value.items.size()) {
      value.items.erase(value.items.begin() + static_cast<long>(remove));
      changed = finished = true;
    }
    BeginDisabled(value.items.size() >= ilmeee::kMaxScriptListItems);
    if (SmallButton("+ Add")) {
      ilmeee::ScriptValue item;
      item.type = value.elementType;
      // New items copy the last one, like Unity's list "+".
      if (!value.items.empty())
        item = value.items.back();
      value.items.push_back(std::move(item));
      changed = finished = true;
    }
    EndDisabled();
    if (!value.items.empty()) {
      SameLine();
      if (SmallButton("Clear")) {
        value.items.clear();
        changed = finished = true;
      }
    }
    TreePop();
  }
  return changed;
}

bool MainWindow::RenderScriptObjectField(uint64_t &object, const char *label) {
  bool changed = false;
  const auto name = [&](uint64_t id) -> std::string {
    if (!id)
      return "None (GameObject)";
    for (const auto &mesh : sceneRenderer->meshes3d)
      if (mesh.objectId == id)
        return mesh.displayName;
    if (const auto *element = ilmeee::FindUiElement(gameUi, id))
      return element->id + " (UI)";
    return "Missing GameObject";
  };
  if (BeginCombo(label, name(object).c_str())) {
    if (Selectable("None", object == 0)) {
      object = 0;
      changed = true;
    }
    for (const auto &mesh : sceneRenderer->meshes3d) {
      PushID(std::to_string(mesh.objectId).c_str());
      if (Selectable(mesh.displayName.c_str(), object == mesh.objectId)) {
        object = mesh.objectId;
        changed = true;
      }
      PopID();
    }
    // Canvas elements, for UiGameObject fields.
    if (hasGameUiCanvas && !gameUi.elements.empty()) {
      SeparatorText("UI");
      for (const auto &element : gameUi.elements) {
        PushID(std::to_string(element.objectId).c_str());
        if (Selectable((element.id + " (UI)").c_str(),
                       object == element.objectId)) {
          object = element.objectId;
          changed = true;
        }
        PopID();
      }
    }
    EndCombo();
  }
  if (BeginDragDropTarget()) {
    if (const auto *payload = AcceptDragDropPayload("ILMEEE_UI")) {
      if (payload->DataSize == sizeof(int)) {
        const int target = *static_cast<const int *>(payload->Data);
        if (target >= 0 && static_cast<size_t>(target) < gameUi.elements.size()) {
          object = gameUi.elements[target].objectId;
          changed = true;
        }
      }
    }
    if (const auto *payload = AcceptDragDropPayload("ILMEEE_MESH")) {
      if (payload->DataSize == sizeof(int)) {
        const int target = *static_cast<const int *>(payload->Data);
        if (target >= 0 &&
            static_cast<size_t>(target) < sceneRenderer->meshes3d.size()) {
          object = sceneRenderer->meshes3d[target].objectId;
          changed = true;
        }
      }
    }
    EndDragDropTarget();
  }
  return changed;
}
