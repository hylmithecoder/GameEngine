#pragma once
#include "../core_engine/SceneRenderer.hpp"
#include <memory>

class MainWindow;
class AnimationEditor {
public:
  ~AnimationEditor();
  void Render(MainWindow &app);
  void Open(MainWindow &app, size_t mesh);
private:
  enum class Action { None, Target, New, Idle, Open };
  std::unique_ptr<SceneRenderer> preview_;
  uint64_t target_ = 0, pendingTarget_ = 0;
  std::string project_, modelPath_, message_, savedPath_, pendingPath_;
  ilmeee::AnimationClip clip_;
  std::vector<ilmeee::AnimationClip> undo_, redo_;
  char path_[1024] = "assets/animations/idle.ilmeeeanim";
  char filter_[128] = {};
  int bone_ = 0;
  float time_ = 0, speed_ = 1, yaw_ = -90, pitch_ = 0, distance_ = 2.4f;
  bool playing_ = false, loop_ = true, dirty_ = false, focus_ = false;
  bool poseDirty_ = true, showBones_ = false, editing_ = false;
  Action pending_ = Action::None;
  void Remember();
  bool Save();
  void Change(MainWindow &app, Action action);
  void Request(MainWindow &app, Action action);
  void BuildPreview(MainWindow &app, size_t mesh);
  void Timeline();
  void BoneControls();
  void Viewport();
};
