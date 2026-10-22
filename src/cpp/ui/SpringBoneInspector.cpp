#include "ui/MainWindow.hpp"
#include "core_engine/SpringBone.hpp"
#include <cstdio>

using namespace ImGui;
using namespace ilmeee;
namespace {
// Searchable bone picker; PMX rigs easily carry several hundred bones.
bool BoneCombo(const char *label, std::string &value,
               const std::vector<SkeletonBone> &skeleton) {
  bool changed = false;
  SetNextItemWidth(-1);
  if (!BeginCombo(label, value.empty() ? "(none)" : value.c_str(),
                  ImGuiComboFlags_HeightLarge))
    return false;
  static char filter[64] = "";
  if (IsWindowAppearing()) { filter[0] = 0; SetKeyboardFocusHere(); }
  SetNextItemWidth(-1);
  InputTextWithHint("##BoneFilter", "Search bone...", filter, sizeof(filter));
  for (const auto &bone : skeleton) {
    if (filter[0] && bone.name.find(filter) == std::string::npos) continue;
    if (Selectable(bone.name.c_str(), bone.name == value)) { value = bone.name; changed = true; }
  }
  EndCombo();
  return changed;
}
bool HasBone(const std::vector<SkeletonBone> &skeleton, const std::string &name) {
  for (const auto &bone : skeleton) if (bone.name == name) return true;
  return false;
}
} // namespace

void MainWindow::RenderSpringBoneInspector(size_t index) {
  auto &mesh = sceneRenderer->meshes3d[index];
  auto &spring = mesh.springBones;
  if (!TreeNodeEx("Spring Bones (Hair / Cloth Physics)", ImGuiTreeNodeFlags_DefaultOpen)) return;
  bool record = false, rebuild = false;
  const bool locked = SceneStructureLocked();

  record |= Checkbox("Spring Bones Enabled", &spring.enabled);
  BeginDisabled(locked);
  if (Button("Auto Setup (Hair / Breast / Skirt)")) {
    spring = AutoSetupSpringBones(mesh.skeleton);
    record = rebuild = true;
  }
  EndDisabled();
  if (IsItemHovered())
    SetTooltip("Finds hair, breast, skirt, ribbon and tail bones by name and\n"
               "adds head, chest and thigh colliders. Replaces current chains.");
  SameLine();
  if (Button("Reset Simulation")) rebuild = true;
  DragFloat3("Wind", &spring.wind.x, 0.05f, -20, 20, "%.2f");
  record |= IsItemDeactivatedAfterEdit();
  Text("Simulated bones: %zu", CountSpringJoints(mesh.skeleton, spring));
  if (spring.enabled && !mesh.animation.enabled)
    TextDisabled("Runs on the rest pose in Play mode.");

  SeparatorText("Chains");
  for (size_t c = 0; c < spring.chains.size(); ++c) {
    auto &chain = spring.chains[c];
    PushID(int(c));
    const bool missing = !HasBone(mesh.skeleton, chain.rootBone);
    char title[192];
    std::snprintf(title, sizeof(title), "%s%s###chain", chain.rootBone.empty() ? "(no root)" : chain.rootBone.c_str(),
                  missing ? "  [bone not found]" : "");
    const bool open = TreeNode(title);
    SameLine(GetContentRegionAvail().x + GetCursorPosX() - 60);
    BeginDisabled(locked);
    const bool remove = SmallButton("Remove");
    EndDisabled();
    if (open) {
      TextUnformatted("Root Bone");
      if (BoneCombo("##Root", chain.rootBone, mesh.skeleton)) record = rebuild = true;
      DragFloat("Stiffness", &chain.stiffness, 0.1f, 0, 100, "%.1f /s", ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      if (IsItemHovered()) SetTooltip("Pull back to the animated pose. Low = floppy hair, high = firm.");
      SliderFloat("Drag", &chain.drag, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      if (IsItemHovered()) SetTooltip("Velocity lost per step. Low = bouncy jiggle, high = calm.");
      DragFloat("Gravity", &chain.gravity, 0.05f, -10, 10, "%.2f");
      record |= IsItemDeactivatedAfterEdit();
      DragFloat("Hit Radius", &chain.hitRadius, 0.005f, 0, 10, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      record |= Checkbox("Use Colliders", &chain.useColliders);
      TreePop();
    }
    PopID();
    if (remove) {
      spring.chains.erase(spring.chains.begin() + c);
      record = rebuild = true;
      break;
    }
  }
  BeginDisabled(locked);
  if (Button("Add Chain")) {
    spring.chains.push_back({});
    record = rebuild = true;
  }
  EndDisabled();

  SeparatorText("Colliders");
  for (size_t c = 0; c < spring.colliders.size(); ++c) {
    auto &collider = spring.colliders[c];
    PushID(int(1000 + c));
    char title[192];
    std::snprintf(title, sizeof(title), "%s (%s)###collider",
                  collider.bone.empty() ? "(no bone)" : collider.bone.c_str(),
                  collider.offset == collider.tailOffset ? "sphere" : "capsule");
    const bool open = TreeNode(title);
    SameLine(GetContentRegionAvail().x + GetCursorPosX() - 60);
    BeginDisabled(locked);
    const bool remove = SmallButton("Remove");
    EndDisabled();
    if (open) {
      TextUnformatted("Bone");
      record |= BoneCombo("##Bone", collider.bone, mesh.skeleton);
      DragFloat3("Offset", &collider.offset.x, 0.01f, -100, 100, "%.3f");
      record |= IsItemDeactivatedAfterEdit();
      DragFloat3("Tail Offset", &collider.tailOffset.x, 0.01f, -100, 100, "%.3f");
      record |= IsItemDeactivatedAfterEdit();
      if (IsItemHovered()) SetTooltip("Equal to Offset = sphere, otherwise a capsule.");
      DragFloat("Radius", &collider.radius, 0.01f, 0, 100, "%.3f", ImGuiSliderFlags_AlwaysClamp);
      record |= IsItemDeactivatedAfterEdit();
      TreePop();
    }
    PopID();
    if (remove) {
      spring.colliders.erase(spring.colliders.begin() + c);
      record = true;
      break;
    }
  }
  BeginDisabled(locked);
  if (Button("Add Collider")) {
    spring.colliders.push_back({});
    record = true;
  }
  EndDisabled();

  if (rebuild) mesh.springRuntime.Reset();
  if (record) RecordEditorHistory();
  TreePop();
}
