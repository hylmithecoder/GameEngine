#include "../../../include/core_engine/Prefab.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include "../../../include/core_engine/IlmeeeAsset.hpp"
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
  case ilmeee::ScriptValueType::Asset: return "Asset";
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
    if (nativeScripts.IsAsset(source))
      continue; // ScriptableObjects are data assets, not components
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
  } else if (value.type == Type::Asset) {
    changed = RenderScriptAssetField(value.string, value.assetClass, label);
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
      value.items[i].assetClass = value.assetClass; // Asset lists
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

namespace {
namespace fs = std::filesystem;
// Project-relative generic path, or empty when `path` is outside the project.
std::string ProjectRelative(const fs::path &project, const std::string &path) {
  std::error_code ec;
  fs::path p(path);
  if (p.is_relative())
    p = project / p;
  const auto rel = fs::weakly_canonical(p, ec)
                       .lexically_relative(fs::weakly_canonical(project, ec));
  if (ec || rel.empty() || *rel.begin() == "..")
    return {};
  return rel.generic_string();
}
} // namespace

bool MainWindow::RenderScriptAssetField(std::string &path,
                                        const std::string &assetClass,
                                        const char *label) {
  const fs::path project(projectHandler.projectPath);
  if (assetClass == ilmeee::script::kPrefabAssetClass)
    return RenderScriptPrefabField(path, label);
  const std::string kind = assetClass.empty() ? "Data Asset" : assetClass;
  std::string preview = "None (" + kind + ")";
  if (!path.empty()) {
    std::error_code ec;
    preview = fs::path(path).stem().string();
    if (!fs::is_regular_file(project / path, ec))
      preview = "Missing: " + preview;
  }
  bool changed = false;
  if (BeginCombo(label, preview.c_str())) {
    if (Selectable("None", path.empty())) {
      path.clear();
      changed = true;
    }
    const auto assets = ilmeee::FindDataAssets(project, assetClass);
    if (assets.empty())
      TextDisabled("No %s assets in assets/ (Explorer > Create > Data Asset)",
                   kind.c_str());
    for (const auto &entry : assets) {
      PushID(entry.path.c_str());
      if (Selectable(entry.path.c_str(), entry.path == path)) {
        path = entry.path;
        changed = true;
      }
      PopID();
    }
    EndCombo();
  }
  if (!path.empty() && IsItemHovered())
    SetTooltip("%s", path.c_str());
  if (BeginDragDropTarget()) {
    if (const auto *payload = AcceptDragDropPayload("ASSET_PATH")) {
      const std::string dropped(static_cast<const char *>(payload->Data));
      const auto rel = ProjectRelative(project, dropped);
      ilmeee::DataAssetFile asset;
      if (rel.empty() || fs::path(rel).extension() != ilmeee::kDataAssetExtension ||
          !ilmeee::LoadDataAsset((project / rel).string(), asset)) {
        projectHandler.ShowNotification("Not a data asset",
                                        "Drop a .ilmeeeasset from the project.",
                                        ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
      } else if (!assetClass.empty() && asset.className != assetClass) {
        projectHandler.ShowNotification(
            "Wrong asset class",
            "This field needs a " + assetClass + ", not a " + asset.className,
            ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
      } else {
        path = rel;
        changed = true;
      }
    }
    EndDragDropTarget();
  }
  return changed;
}

// PrefabRef field: a project-relative .ilmeeeprefab path.
bool MainWindow::RenderScriptPrefabField(std::string &path, const char *label) {
  const fs::path project(projectHandler.projectPath);
  std::string preview = "None (Prefab)";
  if (!path.empty()) {
    std::error_code ec;
    preview = fs::path(path).stem().string();
    if (!fs::is_regular_file(project / path, ec))
      preview = "Missing: " + preview;
  }
  bool changed = false;
  if (BeginCombo(label, preview.c_str())) {
    if (Selectable("None", path.empty())) {
      path.clear();
      changed = true;
    }
    const auto prefabs = ilmeee::FindPrefabs(project);
    if (prefabs.empty())
      TextDisabled("No prefabs in assets/ (Hierarchy > right-click > "
                   "Create Prefab)");
    for (const auto &entry : prefabs) {
      PushID(entry.c_str());
      if (Selectable(entry.c_str(), entry == path)) {
        path = entry;
        changed = true;
      }
      PopID();
    }
    EndCombo();
  }
  if (!path.empty() && IsItemHovered())
    SetTooltip("%s", path.c_str());
  if (BeginDragDropTarget()) {
    if (const auto *payload = AcceptDragDropPayload("ASSET_PATH")) {
      const std::string dropped(static_cast<const char *>(payload->Data));
      const auto rel = ProjectRelative(project, dropped);
      ilmeee::IlmeeeScene prefab;
      if (rel.empty() || !ilmeee::IsPrefabPath(rel) ||
          !ilmeee::LoadPrefab((project / rel).string(), prefab)) {
        projectHandler.ShowNotification("Not a prefab",
                                        "Drop a .ilmeeeprefab from the project.",
                                        ImVec4(1.0f, 0.6f, 0.3f, 1.0f));
      } else {
        path = rel;
        changed = true;
      }
    }
    EndDragDropTarget();
  }
  return changed;
}

void MainWindow::RenderCreateDataAssetMenu(const std::string &folder) {
  const auto classes = nativeScripts.AssetClasses();
  if (classes.empty()) {
    TextDisabled("No compiled ScriptableObject classes.");
    TextDisabled("Create a C++ Script deriving from ScriptableObject.");
    return;
  }
  for (const auto &source : classes) {
    const auto className = fs::path(source).stem().string();
    if (!MenuItem(className.c_str()))
      continue;
    fs::path file = fs::path(folder) / ("New" + className + ilmeee::kDataAssetExtension);
    for (int n = 2; fs::exists(file); ++n)
      file = fs::path(folder) /
             ("New" + className + " " + std::to_string(n) + ilmeee::kDataAssetExtension);
    // No stored values: every field starts at the class default.
    if (!ilmeee::SaveDataAsset(file.string(), {source, className, {}})) {
      projectHandler.ShowNotification("Create failed", file.string(),
                                      ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
      continue;
    }
    projectHandler.fileExplorerSelectedPaths = {file.string()};
    projectHandler.ShowNotification("Data asset created",
                                    file.filename().string(),
                                    ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
  }
}

bool MainWindow::UpdateInspectedDataAsset() {
  const auto &selection = projectHandler.fileExplorerSelectedPaths;
  if (selection != lastExplorerSelection) {
    lastExplorerSelection = selection;
    inspectedDataAsset.clear();
    if (selection.size() == 1 &&
        fs::path(*selection.begin()).extension() == ilmeee::kDataAssetExtension) {
      inspectedDataAsset = *selection.begin();
      dataAssetMeshSelection = SelectedMeshIndex();
      dataAssetUiSelection = selectedUiElement;
    }
  }
  // Picking an object or UI element afterwards brings its Inspector back.
  if (!inspectedDataAsset.empty() &&
      (SelectedMeshIndex() != dataAssetMeshSelection ||
       selectedUiElement != dataAssetUiSelection))
    inspectedDataAsset.clear();
  return !inspectedDataAsset.empty();
}

void MainWindow::RenderDataAssetInspector() {
  struct Cache {
    std::string path;
    fs::file_time_type stamp{};
    bool ok = false;
    ilmeee::DataAssetFile data;
  };
  static Cache cache;
  std::error_code ec;
  const auto stamp = fs::last_write_time(inspectedDataAsset, ec);
  if (cache.path != inspectedDataAsset || (!ec && stamp != cache.stamp)) {
    cache.path = inspectedDataAsset;
    cache.stamp = stamp;
    cache.ok = ilmeee::LoadDataAsset(inspectedDataAsset, cache.data);
  }
  svgIcons.DrawIcon("assets/icons/svg/ui_inspector.svg", 18);
  TextUnformatted(fs::path(inspectedDataAsset).stem().string().c_str());
  SameLine();
  TextDisabled("Data Asset");
  Separator();
  if (!cache.ok) {
    TextWrapped("Not a valid .ilmeeeasset file (or made by a newer engine).");
    return;
  }
  auto &data = cache.data;
  Text("Class: %s", data.className.c_str());
  TextDisabled("%s", data.source.c_str());
  if (SmallButton("Open Script"))
    projectHandler.OpenFile(
        (fs::path(projectHandler.projectPath) / data.source).string());
  SameLine();
  BeginDisabled(SceneStructureLocked() || nativeScripts.IsCompiling());
  if (SmallButton("Recompile"))
    nativeScripts.CompileAll();
  EndDisabled();
  if (!nativeScripts.Ready(data.source) || !nativeScripts.IsAsset(data.source))
    TextWrapped("%s", nativeScripts.IsCompiling()
                          ? "Compiling..."
                          : "Class is not a compiled ScriptableObject. See "
                            "Console > Output.");
  if (SceneStructureLocked())
    TextWrapped("Changes apply from the next Play (assets are read-only "
                "while playing).");
  Separator();
  ilmeee::NativeScriptBinding binding{data.source, data.className, true,
                                      data.values};
  auto values = nativeScripts.Properties(binding);
  bool save = false;
  for (auto &value : values) {
    PushID(value.name.c_str());
    bool finished = false;
    if (RenderScriptValueField(value, value.name.c_str(), finished)) {
      value.assetClass.clear();
      auto it = std::find_if(data.values.begin(), data.values.end(),
                             [&](const auto &stored) {
                               return stored.name == value.name;
                             });
      if (it == data.values.end())
        data.values.push_back(value);
      else
        *it = value;
    }
    save |= finished;
    PopID();
  }
  if (values.empty())
    TextDisabled("This class exposes no fields (DescribeProperties).");
  if (save) {
    if (ilmeee::SaveDataAsset(inspectedDataAsset, data))
      cache.stamp = fs::last_write_time(inspectedDataAsset, ec);
    else
      projectHandler.ShowNotification("Save failed", inspectedDataAsset,
                                      ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
  }
}
