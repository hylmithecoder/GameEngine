#include "ui/AnimationEditor.hpp"
#include "ui/MainWindow.hpp"
#include "core_engine/UserDataDir.hpp"
#include <nfd.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace ImGui;
namespace fs = std::filesystem;
using namespace ilmeee;

AnimationEditor::~AnimationEditor() = default;

void MainWindow::RenderAnimationWindow() {
  if (!animationEditor) animationEditor = std::make_unique<AnimationEditor>();
  animationEditor->Render(*this);
}
void MainWindow::OpenAnimationEditor(size_t mesh) {
  if (!animationEditor) animationEditor = std::make_unique<AnimationEditor>();
  animationEditor->Open(*this, mesh);
}
void MainWindow::OpenMotionPreview(const std::string &path) {
  if (!animationEditor) animationEditor = std::make_unique<AnimationEditor>();
  animationEditor->OpenMotion(*this, path);
}
void MainWindow::RenderMotionLibrary() {
  if (!animationEditor) animationEditor = std::make_unique<AnimationEditor>();
  animationEditor->MotionLibrary(*this);
}
void AnimationEditor::OpenMotion(MainWindow &app, const std::string &path) {
  if (IsAnimationControllerPath(path)) { OpenController(app, path); return; }
  if (!IsAnimationPath(path)) return;
  app.showAnimation = true; focus_ = true;
  focusClip_ = true; focusController_ = false;
  queuedMotion_ = path;
}
void AnimationEditor::MotionLibrary(MainWindow &app) {
  if (!CollapsingHeader("Engine Motions")) return;
  TextWrapped("Choose a scene PMX character, then preview a motion.");
  fs::path folder;
  std::error_code ec;
  for (const auto &candidate : {ilmeee::ExecutableDir() / "../assets/motions",
                                ilmeee::ExecutableDir() / "assets/motions",
                                fs::path(getAssetPath()) / "motions"}) {
    if (fs::is_directory(candidate, ec)) { folder = candidate; break; }
    ec.clear();
  }
  std::vector<fs::path> paths;
  if (!folder.empty())
    for (fs::directory_iterator it(folder, ec), end; it != end && !ec; it.increment(ec))
      if (it->is_regular_file(ec) && IsAnimationPath(it->path().string())) paths.push_back(it->path());
  std::sort(paths.begin(), paths.end());
  for (const auto &path : paths) {
    PushID(path.string().c_str());
    if (Selectable(path.stem().string().c_str())) OpenMotion(app, path.string());
    if (BeginDragDropSource()) {
      const auto value = fs::absolute(path).string();
      SetDragDropPayload("ASSET_PATH", value.c_str(), value.size() + 1);
      TextUnformatted(path.filename().string().c_str()); EndDragDropSource();
    }
    PopID();
  }
  if (paths.empty()) TextDisabled("No bundled motions found.");
  if (Button("Browse Motion...")) {
    NFD::Guard guard;
    NFD::UniquePath path;
    nfdfilteritem_t filters[] = {{"PMX motion", "vmd,ilmeeeanim"}};
    const auto result = NFD::OpenDialog(path, filters, 1, nullptr);
    if (result == NFD_OKAY && path.get()) { OpenMotion(app, path.get()); }
    else if (result == NFD_ERROR) {
      const char *err = NFD::GetError();
      message_ = err ? err : "Failed to open file dialog.";
    }
  }
}

void AnimationEditor::Remember() {
  undo_.push_back(clip_);
  if (undo_.size() > 64) undo_.erase(undo_.begin());
  redo_.clear();
  dirty_ = true;
  poseDirty_ = true;
}

bool AnimationEditor::Save() {
  if (project_.empty()) { message_ = "Open a project before saving a clip."; return false; }
  std::error_code ec;
  const fs::path root = fs::weakly_canonical(project_, ec);
  fs::path path(path_);
  if (path.is_relative()) path = root / path;
  path = fs::weakly_canonical(path, ec);
  const fs::path relative = path.lexically_relative(root);
  if (ec || relative.empty() || *relative.begin() == ".." ||
      path.extension() != ".ilmeeeanim") {
    message_ = "Choose an .ilmeeeanim path inside this project.";
    return false;
  }
  if (!SaveAnimationClip(path.string(), clip_, message_)) return false;
  std::snprintf(path_, sizeof(path_), "%s", relative.generic_string().c_str());
  dirty_ = false;
  savedPath_ = path_;
  message_ = "Clip saved. It can be reopened and edited.";
  return true;
}

void AnimationEditor::BuildPreview(MainWindow &app, size_t index) {
  preview_.reset();
  if (!app.sceneRenderer || index >= app.sceneRenderer->meshes3d.size()) return;
  const auto &source = app.sceneRenderer->meshes3d[index];
  if (source.bones.empty()) return;
  preview_ = std::make_unique<SceneRenderer>(600, 600);
  preview_->SetVulkanContext(app.ctx.device, app.ctx.physicalDevice,
      app.ctx.graphicsQueue, app.ctx.commandPool, app.ctx.descriptorPool);
  if (!preview_->LoadPMXMesh(source.path)) { preview_.reset(); message_ = "Cannot preview this PMX."; return; }
  for (size_t i = 0; i < source.submeshes.size(); ++i)
    if (!source.submeshes[i].texturePath.empty())
      preview_->BindMesh3DSubmeshTexture(0, uint32_t(i), source.submeshes[i].texturePath);
  preview_->SetBackgroundColor(0.055f, 0.075f, 0.10f, 1);
  preview_->SetGrid3DVisible(false);
  preview_->SetSunVisible(false);
  preview_->camera3d.fovDeg = 40;
  modelPath_ = source.path;
  controllerPlayer_.Reset();
  bone_ = 0;
  yaw_ = -90; pitch_ = 0; distance_ = 2.4f;
  poseDirty_ = true;
}

void AnimationEditor::Open(MainWindow &app, size_t mesh) {
  app.showAnimation = true;
  focusClip_ = true; focusController_ = false;
  focus_ = true;
  if (!app.sceneRenderer || mesh >= app.sceneRenderer->meshes3d.size()) return;
  pendingTarget_ = app.sceneRenderer->meshes3d[mesh].objectId;
  if (pendingTarget_ != target_) Request(app, Action::Target);
}

void AnimationEditor::Request(MainWindow &app, Action action) {
  playing_ = false;
  if (dirty_) {
    pending_ = action;
    if (action == Action::Open) {
      pendingPath_ = path_;
      if (!savedPath_.empty()) std::snprintf(path_, sizeof(path_), "%s", savedPath_.c_str());
    }
  }
  else Change(app, action);
}

void AnimationEditor::Change(MainWindow &app, Action action) {
  pending_ = Action::None;
  message_.clear();
  if (action == Action::Target) {
    if (!app.sceneRenderer) return;
    const auto &meshes = app.sceneRenderer->meshes3d;
    const auto found = std::find_if(meshes.begin(), meshes.end(),
        [&](const auto &m) { return m.objectId == pendingTarget_; });
    if (found == meshes.end() || found->bones.empty()) {
      message_ = "Select a PMX character with bones."; return;
    }
    target_ = pendingTarget_;
    project_ = app.projectHandler.projectPath;
    BuildPreview(app, size_t(found - meshes.begin()));
    if (!preview_) return;
    clip_ = AnimationClip{};
    clip_.name = found->displayName + " Animation";
    const auto defaultPath = "assets/animations/" + fs::path(found->path).stem().string() + ".ilmeeeanim";
    std::snprintf(path_, sizeof(path_), "%s", defaultPath.c_str());
    if (!found->animation.clip.empty()) {
      std::snprintf(path_, sizeof(path_), "%s", found->animation.clip.c_str());
      fs::path path(path_);
      if (path.is_relative()) path = fs::path(project_) / path;
      LoadAnimationClip(path.string(), clip_, message_);
      speed_ = found->animation.speed;
      loop_ = found->animation.loop;
    }
  } else if (action == Action::Open) {
    if (!pendingPath_.empty()) {
      std::snprintf(path_, sizeof(path_), "%s", pendingPath_.c_str());
      pendingPath_.clear();
    }
    fs::path path(path_);
    if (path.is_relative()) path = fs::path(project_) / path;
    AnimationClip loaded;
    auto restorePath = [&] { std::snprintf(path_, sizeof(path_), "%s", savedPath_.c_str()); };
    if (!LoadAnimationClip(path.string(), loaded, message_)) { restorePath(); return; }
    if (preview_) {
      size_t matched = 0;
      for (const auto &track : loaded.tracks)
        if (std::any_of(preview_->meshes3d[0].skeleton.begin(), preview_->meshes3d[0].skeleton.end(),
            [&](const auto &bone) { return bone.name == track.bone; })) ++matched;
      message_ = "Matched " + std::to_string(matched) + "/" + std::to_string(loaded.tracks.size()) + " bone tracks.";
      if (!matched && !loaded.tracks.empty()) { message_ += " Choose a PMX with matching bone names."; restorePath(); return; }
    }
    clip_ = std::move(loaded);
    // Imported files stay intact; save an editable project copy for Game Play.
    const auto output = "assets/animations/" + path.stem().string() + ".ilmeeeanim";
    std::snprintf(path_, sizeof(path_), "%s", output.c_str());
    if (path.extension() == ".ilmeeeanim") {
      const auto rel = path.lexically_relative(fs::path(project_));
      if (!rel.empty() && *rel.begin() != "..")
        std::snprintf(path_, sizeof(path_), "%s", rel.generic_string().c_str());
    }
  } else if (action == Action::Idle) {
    if (!preview_) return;
    const auto &mesh = preview_->meshes3d[0];
    clip_ = MakeBreathingClip(mesh.skeleton, mesh.aabbMax.y - mesh.aabbMin.y);
    if (clip_.tracks.empty()) message_ = "No standard chest/head bone names found. Select bones and add keys manually.";
  } else if (action == Action::New) clip_ = AnimationClip{};
  undo_.clear(); redo_.clear();
  dirty_ = action == Action::Idle || action == Action::New;
  savedPath_ = path_;
  time_ = 0; playing_ = action == Action::Open; poseDirty_ = true;
}

void AnimationEditor::BoneControls() {
  if (!preview_) return;
  auto &mesh = preview_->meshes3d[0];
  if (bone_ < 0 || size_t(bone_) >= mesh.skeleton.size()) return;
  const auto &bone = mesh.skeleton[bone_];
  TextUnformatted(bone.name.c_str());
  if (IsItemHovered()) SetTooltip("%s", mesh.bones[bone_].nameEN.c_str());
  BoneTransform value;
  for (const auto &track : clip_.tracks)
    if (track.bone == bone.name) value = SampleBone(track, time_);
  glm::vec3 rotation = glm::degrees(glm::eulerAngles(value.rotation));
  auto edit = [&](const char *label, glm::vec3 &v, float step) {
    TextUnformatted(label);
    SetNextItemWidth(-1);
    PushID(label);
    const bool changed = DragFloat3("##value", &v.x, step);
    PopID();
    if (changed && !editing_) { Remember(); editing_ = true; }
    return changed;
  };
  const bool translate = edit("Position", value.translation, 0.01f);
  const bool rotate = edit("Rotation", rotation, 0.25f);
  if (translate || rotate) {
    value.rotation = glm::quat(glm::radians(rotation));
    SetBoneKey(clip_, bone.name, {time_, value});
    playing_ = false; poseDirty_ = true; dirty_ = true;
  }
  if (!IsAnyItemActive()) editing_ = false;
  if (Button("Set Key")) { Remember(); SetBoneKey(clip_, bone.name, {time_, value}); }
  SameLine();
  if (Button("Delete Key")) {
    const auto before = clip_;
    if (DeleteBoneKey(clip_, bone.name, time_)) {
      const auto after = clip_; clip_ = before; Remember(); clip_ = after;
    }
  }
  SameLine();
  if (Button("Rest Key")) { Remember(); SetBoneKey(clip_, bone.name, {time_, {}}); }
  TextWrapped("Edits add a key at the playhead. Rotation is in degrees.");
}

void AnimationEditor::Timeline() {
  if (Button(playing_ ? "Pause" : "Play")) {
    if (!playing_ && time_ >= clip_.duration) time_ = 0;
    playing_ = !playing_;
    if (!playing_) time_ = std::clamp(std::round(time_ * clip_.fps) / clip_.fps, 0.0f, clip_.duration);
    poseDirty_ = true;
  }
  SameLine();
  if (Button("Stop")) { playing_ = false; time_ = 0; poseDirty_ = true; }
  SameLine(); Checkbox("Loop", &loop_);
  SameLine(); SetNextItemWidth(90); DragFloat("Speed", &speed_, 0.05f, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
  SameLine(); Text("Frame %d / %d", int(std::round(time_ * clip_.fps)), int(std::round(clip_.duration * clip_.fps)));
  SetNextItemWidth(-1);
  if (SliderFloat("##playhead", &time_, 0, clip_.duration, "%.3f s")) {
    playing_ = false;
    time_ = std::clamp(std::round(time_ * clip_.fps) / clip_.fps, 0.0f, clip_.duration);
    poseDirty_ = true;
  }
  const ImVec2 origin = GetCursorScreenPos();
  const ImVec2 size(std::max(100.0f, GetContentRegionAvail().x), 65);
  InvisibleButton("##keyframeTimeline", size);
  auto *draw = GetWindowDrawList();
  draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(22, 26, 32, 255), 4);
  for (int tick = 0; tick <= 8; ++tick) {
    const float x = origin.x + size.x * float(tick) / 8;
    draw->AddLine(ImVec2(x, origin.y + 20), ImVec2(x, origin.y + size.y), IM_COL32(65, 74, 86, 255));
    char label[24]; std::snprintf(label, sizeof(label), "%.1f", clip_.duration * tick / 8);
    draw->AddText(ImVec2(x + 3, origin.y + 3), IM_COL32(160, 170, 184, 255), label);
  }
  if (preview_ && bone_ >= 0 && size_t(bone_) < preview_->meshes3d[0].skeleton.size()) {
    const auto &name = preview_->meshes3d[0].skeleton[bone_].name;
    for (const auto &track : clip_.tracks) if (track.bone == name)
      for (const auto &key : track.keys) {
        const float x = origin.x + size.x * key.time / clip_.duration, y = origin.y + 42;
        draw->AddQuadFilled({x, y - 6}, {x + 6, y}, {x, y + 6}, {x - 6, y}, IM_COL32(242, 188, 86, 255));
      }
  }
  const float x = origin.x + size.x * time_ / clip_.duration;
  draw->AddLine({x, origin.y}, {x, origin.y + size.y}, IM_COL32(246, 104, 104, 255), 2);
  if (IsItemActive() && IsMouseDown(ImGuiMouseButton_Left)) {
    time_ = std::clamp((GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f) * clip_.duration;
    time_ = std::clamp(std::round(time_ * clip_.fps) / clip_.fps, 0.0f, clip_.duration);
    playing_ = false; poseDirty_ = true;
  }
}

void AnimationEditor::Viewport() {
  if (!preview_) return;
  const ImVec2 size(std::max(100.0f, GetContentRegionAvail().x),
                     std::max(100.0f, GetContentRegionAvail().y - (controllerMode_ ? 0 : 140)));
  preview_->SetViewportSize(int(size.x), int(size.y));
  const float yaw = glm::radians(yaw_), pitch = glm::radians(pitch_);
  const glm::vec3 forward(std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                          std::sin(yaw) * std::cos(pitch));
  preview_->camera3d.position = -forward * distance_;
  preview_->camera3d.yaw = yaw_; preview_->camera3d.pitch = pitch_;
  if (poseDirty_) {
    if (!preview_->SetMesh3DAnimationPose(0, controllerMode_ ? controllerPose_ : clip_, controllerMode_ ? 0 : time_))
      message_ = "Cannot preview this pose: " + preview_->meshes3d[0].animationError;
    poseDirty_ = false;
  }
  preview_->RenderSceneToTexture(Scene{});
  const ImVec2 origin = GetCursorScreenPos();
  Image((ImTextureID)preview_->GetViewportDescriptorSet(), size, {0, 1}, {1, 0});
  const bool hovered = IsItemHovered();
  if (hovered && IsMouseDragging(ImGuiMouseButton_Right)) {
    yaw_ += GetIO().MouseDelta.x * 0.4f;
    pitch_ = std::clamp(pitch_ - GetIO().MouseDelta.y * 0.4f, -80.0f, 80.0f);
  }
  if (hovered) distance_ = std::clamp(distance_ * std::exp(-GetIO().MouseWheel * 0.12f), 0.4f, 10.0f);
  if (showBones_) {
    const auto &mesh = preview_->meshes3d[0];
    const glm::mat4 mvp = preview_->GetCamera3DProj() * preview_->GetCamera3DView() * mesh.ComputeModel();
    const auto project = [&](size_t i, ImVec2 &point) {
      const glm::vec4 p = mvp * glm::vec4(mesh.posedBones[i], 1);
      if (p.w <= 0) return false;
      point = {origin.x + (p.x / p.w + 1) * size.x / 2,
                origin.y + (1 - p.y / p.w) * size.y / 2};
      return true;
    };
    auto *draw = GetWindowDrawList();
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    float nearest = 100; int hit = -1;
    for (size_t i = 0; i < mesh.skeleton.size(); ++i) {
      ImVec2 point;
      if (!project(i, point)) continue;
      const auto color = int(i) == bone_ ? IM_COL32(255, 198, 75, 255) : IM_COL32(87, 201, 224, 170);
      const int parent = mesh.skeleton[i].parent;
      ImVec2 parentPoint;
      if (parent >= 0 && size_t(parent) < mesh.posedBones.size() && project(parent, parentPoint))
        draw->AddLine(point, parentPoint, color, int(i) == bone_ ? 2.5f : 1);
      draw->AddCircleFilled(point, int(i) == bone_ ? 4 : 2, color);
      const float dx = point.x - GetIO().MousePos.x, dy = point.y - GetIO().MousePos.y;
      const float d = dx * dx + dy * dy;
      if (d < nearest) { nearest = d; hit = int(i); }
    }
    draw->PopClipRect();
    if (hovered && IsMouseClicked(ImGuiMouseButton_Left) && hit >= 0) bone_ = hit;
  }
  auto *draw = GetWindowDrawList();
  draw->AddText({origin.x + 12, origin.y + 10}, IM_COL32(190, 202, 218, 255), "RMB orbit  |  Wheel zoom");
}

void AnimationEditor::Render(MainWindow &app) {
  if (auto *scene = FindWindowByName("Scene"); scene && scene->DockId)
    SetNextWindowDockID(scene->DockId, ImGuiCond_FirstUseEver);
  SetNextWindowSize({1000, 720}, ImGuiCond_FirstUseEver);
  if (focus_) { SetNextWindowFocus(); focus_ = false; }
  if (!Begin("Animation", &app.showAnimation)) { End(); return; }
  MotionLibrary(app);
  const bool wasController = controllerMode_;
  if (BeginTabBar("AnimationTools")) {
    if (BeginTabItem("Clip Editor", nullptr, focusClip_ ? ImGuiTabItemFlags_SetSelected : 0)) { controllerMode_ = false; EndTabItem(); }
    if (BeginTabItem("Locomotion Controller", nullptr, focusController_ ? ImGuiTabItemFlags_SetSelected : 0)) {
      controllerMode_ = true; EndTabItem();
    }
    EndTabBar();
  }
  focusController_ = false;
  focusClip_ = false;
  if (wasController != controllerMode_) poseDirty_ = true;
  if (!app.sceneRenderer) { TextWrapped("Open a scene and select a PMX character to animate."); End(); return; }
  if (project_ != app.projectHandler.projectPath) {
    preview_.reset(); target_ = 0; modelPath_.clear();
    // Keep an unsaved clip in memory so switching projects cannot destroy it.
    project_ = app.projectHandler.projectPath;
  }
  int targetIndex = -1;
  for (size_t i = 0; i < app.sceneRenderer->meshes3d.size(); ++i)
    if (app.sceneRenderer->meshes3d[i].objectId == target_) targetIndex = int(i);
  if (!target_ && !dirty_) {
    int selected = app.SelectedMeshIndex();
    if (selected < 0 || app.sceneRenderer->meshes3d[selected].bones.empty()) {
      selected = -1;
      for (size_t i = 0; i < app.sceneRenderer->meshes3d.size(); ++i)
        if (!app.sceneRenderer->meshes3d[i].bones.empty()) { selected = int(i); break; }
    }
    if (selected >= 0) {
      pendingTarget_ = app.sceneRenderer->meshes3d[selected].objectId;
      Change(app, Action::Target); targetIndex = selected;
    }
  }
  if (!queuedMotion_.empty() && preview_ && targetIndex >= 0 && pending_ == Action::None) {
    std::snprintf(path_, sizeof(path_), "%s", queuedMotion_.c_str());
    queuedMotion_.clear(); Request(app, Action::Open);
  }
  if (!queuedMotion_.empty()) TextWrapped("Motion selected. Choose a PMX character to preview it.");
  const char *targetName = targetIndex >= 0 ? app.sceneRenderer->meshes3d[targetIndex].displayName.c_str() : "Select a PMX character";
  SetNextItemWidth(240);
  if (BeginCombo("Character", targetName)) {
    for (const auto &mesh : app.sceneRenderer->meshes3d) {
      if (mesh.bones.empty()) continue;
      PushID(reinterpret_cast<void *>(static_cast<uintptr_t>(mesh.objectId)));
      if (Selectable(mesh.displayName.c_str(), target_ == mesh.objectId)) {
        pendingTarget_ = mesh.objectId; Request(app, Action::Target);
      }
      PopID();
    }
    EndCombo();
  }
  SameLine(); Checkbox("Show Bones", &showBones_);
  SameLine(); if (Button("Reset View")) { yaw_ = -90; pitch_ = 0; distance_ = 2.4f; }
  if (pending_ != Action::None && dirty_) OpenPopup("Unsaved animation");
  if (BeginPopupModal("Unsaved animation", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    TextUnformatted("Save your edited clip before replacing it?");
    if (Button("Save and continue") && Save()) { const auto action = pending_; Change(app, action); CloseCurrentPopup(); }
    SameLine();
    if (Button("Discard edits")) { const auto action = pending_; dirty_ = false; Change(app, action); CloseCurrentPopup(); }
    SameLine(); if (Button("Cancel")) { pending_ = Action::None; pendingPath_.clear(); CloseCurrentPopup(); }
    EndPopup();
  }
  if (controllerMode_) { ControllerPanel(app, targetIndex); End(); return; }
  SetNextItemWidth(-180);
  InputText("##clipPath", path_, sizeof(path_));
  if (BeginDragDropTarget()) {
    if (const auto *payload = AcceptDragDropPayload("ASSET_PATH")) {
      const char *value = static_cast<const char *>(payload->Data);
      if (IsAnimationPath(value)) OpenMotion(app, value);
    }
    EndDragDropTarget();
  }
  SameLine(); if (Button("Open Clip")) Request(app, Action::Open);
  SameLine(); if (Button(dirty_ ? "Save *" : "Save")) Save();
  if (Button("New Clip")) Request(app, Action::New);
  SameLine(); BeginDisabled(!preview_);
  if (Button("Create Breathing Idle")) Request(app, Action::Idle);
  EndDisabled();
  SameLine(); BeginDisabled(undo_.empty());
  if (Button("Undo")) {
    redo_.push_back(clip_); clip_ = undo_.back(); undo_.pop_back(); dirty_ = poseDirty_ = true;
    time_ = std::min(time_, clip_.duration);
  }
  EndDisabled(); SameLine(); BeginDisabled(redo_.empty());
  if (Button("Redo")) {
    undo_.push_back(clip_); clip_ = redo_.back(); redo_.pop_back(); dirty_ = poseDirty_ = true;
  }
  EndDisabled();
  if (GetWindowWidth() >= 720) SameLine();
  BeginDisabled(targetIndex < 0 || app.SceneStructureLocked() || !preview_);
  if (Button("Save + Assign to Character") && Save()) {
    auto &mesh = app.sceneRenderer->meshes3d[targetIndex];
    mesh.animation = {true, path_, speed_, loop_};
    mesh.loadedAnimationPath.clear();
    app.RecordEditorHistory();
    if (app.Save3DScene()) message_ = "Assigned and scene saved. Press Game Play to use this clip.";
  }
  EndDisabled();
  if (!message_.empty()) TextWrapped("%s", message_.c_str());
  TextDisabled("VMD: bone motion + Bezier curves. IK, morphs, camera and physics are not evaluated.");
  if (!preview_ || targetIndex < 0) {
    TextWrapped("Choose a PMX character from the scene. Clips contain editable bone keyframes.");
    End(); return;
  }
  if (playing_) {
    time_ = AnimationTime(time_ + std::min(GetIO().DeltaTime, 0.25f) * speed_, clip_.duration, loop_);
    if (!loop_ && time_ >= clip_.duration) playing_ = false;
    poseDirty_ = true;
  }
  char name[256]; std::snprintf(name, sizeof(name), "%s", clip_.name.c_str());
  SetNextItemWidth(220);
  if (InputText("Clip Name", name, sizeof(name))) { Remember(); clip_.name = name; }
  if (GetWindowWidth() > 680) SameLine();
  SetNextItemWidth(85);
  float duration = clip_.duration, minimum = 1.0f / clip_.fps;
  for (const auto &track : clip_.tracks)
    if (!track.keys.empty()) minimum = std::max(minimum, track.keys.back().time);
  if (DragFloat("Duration", &duration, 0.05f, minimum, 3600, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
    Remember(); clip_.duration = duration; time_ = std::min(time_, duration);
  }
  SameLine(); SetNextItemWidth(65);
  float fps = clip_.fps;
  if (DragFloat("FPS", &fps, 1, 1, 240, "%.0f", ImGuiSliderFlags_AlwaysClamp)) { Remember(); clip_.fps = fps; }
  Separator();
  BeginChild("##bones", {250, 0}, ImGuiChildFlags_Borders);
  Text("Bones (%zu)", preview_->meshes3d[0].skeleton.size());
  if (IsItemHovered()) SetTooltip("FK bone poses. IK, facial morphs and physics are not evaluated yet.\nSDEF/QDEF vertices currently use linear skinning.");
  SetNextItemWidth(-1); InputTextWithHint("##boneFilter", "Search bones...", filter_, sizeof(filter_));
  const auto &mesh = preview_->meshes3d[0];
  BeginChild("##boneList", {0, std::max(75.0f, GetContentRegionAvail().y - 205)});
  for (size_t i = 0; i < mesh.skeleton.size(); ++i) {
    const auto &bone = mesh.skeleton[i];
    if (filter_[0] && bone.name.find(filter_) == std::string::npos && mesh.bones[i].nameEN.find(filter_) == std::string::npos) continue;
    PushID(int(i));
    const bool keyed = std::any_of(clip_.tracks.begin(), clip_.tracks.end(), [&](const auto &t) { return t.bone == bone.name; });
    if (Selectable((std::string(keyed ? "* " : "") + bone.name).c_str(), bone_ == int(i))) bone_ = int(i);
    if (IsItemHovered()) SetTooltip("%s\n%s", bone.name.c_str(), mesh.bones[i].nameEN.c_str());
    PopID();
  }
  EndChild();
  Separator();
  BoneControls();
  EndChild(); SameLine();
  BeginChild("##animationWorkspace", {0, 0});
  Viewport();
  Timeline();
  EndChild();
  End();
}
