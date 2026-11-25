#pragma once
#include "../core_engine/SceneRenderer.hpp"
#include <memory>

class MainWindow;
class AnimationEditor {
public:
  ~AnimationEditor();
  void Render(MainWindow &app);
  void Open(MainWindow &app, size_t mesh);
  void OpenMotion(MainWindow &app, const std::string &path);
  void MotionLibrary(MainWindow &app);
  void OpenController(MainWindow &app, const std::string &path = {});
private:
  enum class Action { None, Target, New, Idle, Open };
  std::unique_ptr<SceneRenderer> preview_;
  uint64_t target_ = 0, pendingTarget_ = 0;
  std::string project_, modelPath_, message_, savedPath_, pendingPath_;
  std::string queuedMotion_;
  ilmeee::AnimationClip clip_;
  std::vector<ilmeee::AnimationClip> undo_, redo_;
  char path_[1024] = "assets/animations/idle.ilmeeeanim";
  char filter_[128] = {};
  int bone_ = 0;
  float time_ = 0, speed_ = 1, yaw_ = -90, pitch_ = 0, distance_ = 2.4f;
  bool playing_ = false, loop_ = true, dirty_ = false, focus_ = false;
  bool poseDirty_ = true, showBones_ = false, editing_ = false;
  Action pending_ = Action::None;
  bool controllerMode_ = false, focusController_ = false, focusClip_ = false, controllerReady_ = false;
  bool controllerDirty_ = false, controllerPlaying_ = true, controllerNew_ = false;
  uint64_t controllerTarget_ = 0;
  char controllerPath_[1024] = "assets/controllers/Locomotion.ilmeeecontroller";
  float previewSpeed_ = 0;
  std::string controllerBase_, controllerProject_, controllerMessage_, queuedController_, controllerSavedPath_;
  ilmeee::AnimationController controller_;
  std::array<ilmeee::AnimationClip, 3> controllerClips_;
  ilmeee::LocomotionPlayer controllerPlayer_;
  ilmeee::AnimationClip controllerPose_;
  void ControllerPanel(MainWindow &app, int targetIndex);
  bool LoadControllerDraft(const std::string &path);
  bool ReloadController();
  bool SaveController(MainWindow &app, int targetIndex, bool assign);
  void StarterController(MainWindow &app);
  void Remember();
  bool Save();
  void Change(MainWindow &app, Action action);
  void Request(MainWindow &app, Action action);
  void BuildPreview(MainWindow &app, size_t mesh);
  void Timeline();
  void BoneControls();
  void Viewport();
};
