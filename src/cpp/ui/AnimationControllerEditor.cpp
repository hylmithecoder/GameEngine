#include "ui/AnimationEditor.hpp"
#include "ui/MainWindow.hpp"
#include "core_engine/UserDataDir.hpp"
#include <nfd.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>

using namespace ImGui;
using namespace ilmeee;
namespace fs = std::filesystem;
namespace {
fs::path Resolve(const std::string &base, const std::string &path) {
  fs::path p(path); return p.is_absolute() ? p : fs::path(base) / p;
}
bool Inside(const fs::path &root, const fs::path &path) {
  const auto rel = path.lexically_relative(root);
  return !rel.empty() && *rel.begin() != "..";
}
bool EditString(const char *label, std::string &value) {
  char text[4097]; std::snprintf(text, sizeof(text), "%s", value.c_str());
  SetNextItemWidth(-1);
  if (!InputText(label, text, sizeof(text))) return false;
  value = text; return true;
}
std::string Browse(const char *title, const char *extensions) {
  NFD::Guard guard;
  NFD::UniquePath path;
  nfdfilteritem_t filter[] = {{title, extensions}};
  if (NFD::OpenDialog(path, filter, 1, nullptr) != NFD_OKAY || !path.get()) return {};
  return std::string(path.get());
}
}

void MainWindow::OpenAnimationController(size_t mesh) {
  if (!animationEditor) animationEditor = std::make_unique<AnimationEditor>();
  animationEditor->Open(*this, mesh);
  animationEditor->OpenController(*this);
}
void AnimationEditor::OpenController(MainWindow &app, const std::string &path) {
  app.showAnimation = true; focus_ = true; focusController_ = true;
  focusClip_ = false;
  if (!path.empty()) queuedController_ = path;
}
void MainWindow::RenderAnimationInspector(size_t index) {
  auto &mesh = sceneRenderer->meshes3d[index];
  auto &animation = mesh.animation;
  if (Button("Edit Clip")) OpenAnimationEditor(index);
  if (Button("Locomotion Controller")) OpenAnimationController(index);
  BeginDisabled(SceneStructureLocked());
  if (Button("Choose Controller...")) {
    const auto path = Browse("Animation Controller", "ilmeeecontroller");
    if (!path.empty()) {
      std::error_code ec;
      const auto root = fs::weakly_canonical(projectHandler.projectPath, ec);
      const auto file = fs::weakly_canonical(path, ec);
      animation.controller = !ec && Inside(root, file) ? file.lexically_relative(root).generic_string() : path;
      animation.enabled = true; mesh.locomotion = {}; RecordEditorHistory();
    }
  }
  if (!animation.controller.empty()) {
    TextWrapped("Controller: %s", animation.controller.c_str());
    if (Button("Use Single Clip")) { animation.controller.clear(); mesh.locomotion = {}; RecordEditorHistory(); }
  } else if (!animation.clip.empty()) TextWrapped("Clip: %s", animation.clip.c_str());
  if (!animation.clip.empty() || !animation.controller.empty()) {
    if (Checkbox("Animation Enabled", &animation.enabled)) RecordEditorHistory();
    if (animation.controller.empty() && Checkbox("Loop Animation", &animation.loop)) RecordEditorHistory();
    DragFloat("Playback Speed", &animation.speed, 0.05f, 0, 4, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (IsItemDeactivatedAfterEdit()) RecordEditorHistory();
  }
  EndDisabled();
  if (!animation.controller.empty()) {
    Text("State: %s", LocomotionName(mesh.locomotion.player.State()));
    Text("Movement: %.2f units/s", mesh.locomotion.player.Speed());
  }
  if (!mesh.animationError.empty()) TextWrapped("%s", mesh.animationError.c_str());
}
bool AnimationEditor::LoadControllerDraft(const std::string &path) {
  const auto absolute = Resolve(project_, path);
  AnimationController loaded;
  if (!LoadAnimationController(absolute.string(), loaded, controllerMessage_)) return false;
  controller_ = std::move(loaded);
  controllerBase_ = absolute.parent_path().string(); controllerProject_ = project_;
  std::snprintf(controllerPath_, sizeof(controllerPath_), "%s", path.c_str());
  controllerSavedPath_ = path;
  controllerDirty_ = false;
  return ReloadController();
}
bool AnimationEditor::ReloadController() {
  controllerReady_ = LoadControllerClips((fs::path(controllerBase_) / "controller.ilmeeecontroller").string(),
      controller_, controllerClips_, controllerMessage_);
  controllerPlayer_.Reset(); controllerPose_ = {}; poseDirty_ = true;
  return controllerReady_;
}
void AnimationEditor::StarterController(MainWindow &) {
  fs::path folder;
  std::error_code ec;
  for (const auto &candidate : {ExecutableDir() / "../assets/motions", ExecutableDir() / "assets/motions",
                                fs::path(getAssetPath()) / "motions"}) {
    if (fs::is_directory(candidate, ec)) { folder = fs::absolute(candidate); break; }
    ec.clear();
  }
  if (folder.empty()) { controllerMessage_ = "Engine starter motions not found."; return; }
  controller_ = {};
  controller_.idle = (folder / "Breathing.vmd").string();
  controller_.walk = (folder / "Walk.vmd").string(); controller_.run = (folder / "Run.vmd").string();
  controllerBase_ = (fs::path(project_) / "assets/controllers").string(); controllerProject_ = project_;
  const std::string name = fs::path(modelPath_).stem().string() + "Locomotion";
  fs::path relative = fs::path("assets/controllers") / (name + ".ilmeeecontroller");
  for (int i = 1; fs::exists(fs::path(project_) / relative, ec) && !ec; ++i)
    relative = fs::path("assets/controllers") / (name + std::to_string(i) + ".ilmeeecontroller");
  std::snprintf(controllerPath_, sizeof(controllerPath_), "%s", relative.generic_string().c_str());
  controllerSavedPath_ = controllerPath_;
  controllerDirty_ = true; previewSpeed_ = 0;
  ReloadController();
}
bool AnimationEditor::SaveController(MainWindow &app, int targetIndex, bool assign) {
  if (project_.empty() || controllerProject_ != project_) { controllerMessage_ = "Open or create a controller for the current project."; return false; }
  if (assign && (app.SceneStructureLocked() || targetIndex < 0)) return false;
  std::error_code ec;
  const auto root = fs::weakly_canonical(project_, ec);
  const auto target = fs::weakly_canonical(Resolve(project_, controllerPath_), ec);
  if (ec || !Inside(root, target) || !IsAnimationControllerPath(target.string())) {
    controllerMessage_ = "Save an .ilmeeecontroller inside this project."; return false;
  }
  if (!ReloadController()) return false;
  if (assign && preview_) {
    for (int i = 0; i < 3; ++i) {
      bool matched = controllerClips_[i].tracks.empty();
      for (const auto &track : controllerClips_[i].tracks)
        for (const auto &bone : preview_->meshes3d[0].skeleton) if (bone.name == track.bone) matched = true;
      if (!matched) {
        controllerMessage_ = std::string(LocomotionName(static_cast<LocomotionState>(i))) + " has no matching PMX bones.";
        return false;
      }
    }
  }
  AnimationController saved = controller_;
  const std::array<std::string *, 3> slots{&saved.idle, &saved.walk, &saved.run};
  // Gather imported/outside clips into the project. VMD is converted so players
  // need no platform CP932 converter to play a saved controller.
  for (int i = 0; i < 3; ++i) {
    auto &slot = *slots[i]; if (slot.empty()) continue;
    const auto source = fs::weakly_canonical(Resolve(controllerBase_, slot), ec);
    if (ec) { controllerMessage_ = "Cannot resolve clip: " + slot; return false; }
    fs::path destination = source;
    if (!Inside(root, source) || source.extension() != ".ilmeeeanim") {
      const auto stem = source.stem().string();
      destination = root / "assets/animations" / (stem + ".ilmeeeanim");
      for (int n = 1; fs::exists(destination, ec) && !ec; ++n)
        destination = root / "assets/animations" / (stem + std::to_string(n) + ".ilmeeeanim");
      destination = fs::weakly_canonical(destination, ec);
      if (ec || !Inside(root, destination)) { controllerMessage_ = "Animation folder must remain inside the project."; return false; }
      if (!SaveAnimationClip(destination.string(), controllerClips_[i], controllerMessage_)) return false;
    }
    slot = destination.lexically_relative(target.parent_path()).generic_string();
  }
  if (!SaveAnimationController(target.string(), saved, controllerMessage_)) return false;
  controller_ = std::move(saved); controllerBase_ = target.parent_path().string(); controllerDirty_ = false;
  const auto relative = target.lexically_relative(root).generic_string();
  std::snprintf(controllerPath_, sizeof(controllerPath_), "%s", relative.c_str());
  controllerSavedPath_ = relative;
  controllerMessage_ = "Controller saved. Clips are included in this project.";
  if (assign) {
    auto &mesh = app.sceneRenderer->meshes3d[targetIndex];
    mesh.animation.controller = relative; mesh.animation.enabled = true; mesh.locomotion = {};
    mesh.animationError.clear(); app.RecordEditorHistory();
    if (app.Save3DScene()) controllerMessage_ = "Controller assigned and scene saved. Press Game Play.";
  } else if (app.sceneRenderer) {
    for (auto &mesh : app.sceneRenderer->meshes3d)
      if (mesh.animation.controller == relative) mesh.locomotion = {};
  }
  return true;
}
void AnimationEditor::ControllerPanel(MainWindow &app, int targetIndex) {
  if (controllerTarget_ != target_) {
    controllerTarget_ = target_; controllerPlayer_.Reset();
    if (!controllerDirty_ && queuedController_.empty() && targetIndex >= 0) {
      const auto &assigned = app.sceneRenderer->meshes3d[targetIndex].animation.controller;
      if (!assigned.empty()) queuedController_ = assigned;
    }
  }
  if (!queuedController_.empty() || controllerNew_) {
    if (controllerDirty_) {
      // Opening a different asset must not make Save overwrite that asset.
      if (!controllerSavedPath_.empty()) std::snprintf(controllerPath_, sizeof(controllerPath_), "%s", controllerSavedPath_.c_str());
      OpenPopup("Unsaved controller");
    }
    else {
      if (controllerNew_) StarterController(app); else LoadControllerDraft(queuedController_);
      queuedController_.clear(); controllerNew_ = false;
    }
  }
  if (BeginPopupModal("Unsaved controller", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    TextUnformatted("Save controller edits before replacing this draft?");
    auto proceed = [&] {
      if (controllerNew_) StarterController(app); else LoadControllerDraft(queuedController_);
      queuedController_.clear(); controllerNew_ = false; CloseCurrentPopup();
    };
    if (Button("Save and continue") && SaveController(app, targetIndex, false)) proceed();
    SameLine(); if (Button("Discard edits")) { controllerDirty_ = false; proceed(); }
    SameLine(); if (Button("Cancel")) { queuedController_.clear(); controllerNew_ = false; CloseCurrentPopup(); }
    EndPopup();
  }
  SetNextItemWidth(-160);
  InputText("##controllerPath", controllerPath_, sizeof(controllerPath_));
  if (BeginDragDropTarget()) {
    if (const auto *payload = AcceptDragDropPayload("ASSET_PATH")) {
      const char *path = static_cast<const char *>(payload->Data);
      if (IsAnimationControllerPath(path)) queuedController_ = path;
    }
    EndDragDropTarget();
  }
  SameLine(); if (Button("Open")) queuedController_ = controllerPath_;
  SameLine(); if (Button("Browse...")) {
    const auto path = Browse("Animation Controller", "ilmeeecontroller"); if (!path.empty()) queuedController_ = path;
  }
  if (!controllerMessage_.empty()) TextWrapped("%s", controllerMessage_.c_str());
  BeginChild("ControllerSettings", {300, 0}, ImGuiChildFlags_Borders);
  BeginDisabled(project_.empty());
  if (Button("Use Starter Motions")) controllerNew_ = true;
  EndDisabled();
  TextWrapped("Assign Idle, Walk and Run. Clip paths are relative to the controller folder.");
  bool reload = false;
  const std::array<std::string *, 3> slots{&controller_.idle, &controller_.walk, &controller_.run};
  for (int i = 0; i < 3; ++i) {
    PushID(i); TextUnformatted(LocomotionName(static_cast<LocomotionState>(i)));
    if (EditString("##clip", *slots[i])) controllerDirty_ = true;
    reload |= IsItemDeactivatedAfterEdit();
    if (BeginDragDropTarget()) {
      if (const auto *payload = AcceptDragDropPayload("ASSET_PATH")) {
        const char *path = static_cast<const char *>(payload->Data);
        if (IsAnimationPath(path)) { *slots[i] = path; controllerDirty_ = reload = true; }
      }
      EndDragDropTarget();
    }
    if (SmallButton("Choose Clip...")) {
      const auto path = Browse("Motion clip", "vmd,ilmeeeanim");
      if (!path.empty()) { *slots[i] = path; controllerDirty_ = reload = true; }
    }
    PopID();
  }
  Separator();
  SetNextItemWidth(100); controllerDirty_ |= DragFloat("Walk Speed", &controller_.walkSpeed, 0.05f, 0.01f, 10000, "%.2f");
  SetNextItemWidth(100); controllerDirty_ |= DragFloat("Run Speed", &controller_.runSpeed, 0.05f, 0.02f, 10000, "%.2f");
  SetNextItemWidth(100); controllerDirty_ |= DragFloat("Idle Threshold", &controller_.idleThreshold, 0.01f, 0, 100, "%.2f");
  SetNextItemWidth(100); controllerDirty_ |= DragFloat("Transition (s)", &controller_.transition, 0.01f, 0, 10, "%.2f");
  controllerDirty_ |= Checkbox("In-place Motion", &controller_.inPlace);
  if (controller_.inPlace) {
    TextUnformatted("Additional Root Bone"); controllerDirty_ |= EditString("##root", controller_.rootBone);
    TextWrapped("Keeps vertical bounce. Removes horizontal motion on Center, Groove, All Parent and this bone.");
  }
  if (reload) { if (controllerBase_.empty()) { controllerBase_ = Resolve(project_, controllerPath_).parent_path().string(); controllerProject_ = project_; } ReloadController(); }
  if (Button("Reload Preview")) ReloadController();
  BeginDisabled(app.SceneStructureLocked() || project_.empty());
  if (Button(controllerDirty_ ? "Save Controller *" : "Save Controller")) SaveController(app, targetIndex, false);
  BeginDisabled(targetIndex < 0 || !preview_);
  if (Button("Save + Assign to Character")) SaveController(app, targetIndex, true);
  EndDisabled(); EndDisabled();
  EndChild(); SameLine();
  BeginChild("ControllerPreview", {0, 0});
  TextWrapped("Preview only. Speed here does not move the scene character.");
  SetNextItemWidth(-1);
  SliderFloat("##previewSpeed", &previewSpeed_, 0, std::max(1.0f, controller_.runSpeed * 1.5f), "Speed %.2f units/s");
  Checkbox("Play Preview", &controllerPlaying_); SameLine();
  if (Button("Reset Preview")) { controllerPlayer_.Reset(); previewSpeed_ = 0; }
  if (preview_ && controllerReady_) {
    std::string invalid;
    if (ValidateController(controller_, invalid)) {
      if (CollapsingHeader("Rig Compatibility")) {
        for (int i = 0; i < 3; ++i) {
          size_t matches = 0;
          for (const auto &track : controllerClips_[i].tracks)
            if (std::any_of(preview_->meshes3d[0].skeleton.begin(), preview_->meshes3d[0].skeleton.end(),
                [&](const auto &bone) { return bone.name == track.bone; })) ++matches;
          Text("%s: %zu/%zu bone tracks", LocomotionName(static_cast<LocomotionState>(i)), matches, controllerClips_[i].tracks.size());
        }
        TextWrapped("FK bone motion. IK, morphs and physics are not evaluated.");
      }
      controllerPose_ = controllerPlayer_.Update(preview_->meshes3d[0].skeleton, controller_, controllerClips_,
          previewSpeed_, controllerPlaying_ ? GetIO().DeltaTime : 0);
      poseDirty_ = true;
      Text("State: %s | Transition: %.0f%%", LocomotionName(controllerPlayer_.State()), controllerPlayer_.Blend() * 100);
      Viewport();
    } else TextWrapped("%s", invalid.c_str());
  } else TextWrapped("Choose a PMX character and load a controller or use starter motions.");
  EndChild();
}
