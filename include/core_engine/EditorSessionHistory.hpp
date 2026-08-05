#pragma once

#include "Scene.hpp"
#include "SceneRenderer.hpp"
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// Temporary, per-engine-process editor history. The live snapshots stay in
// memory for fast Ctrl+Z/Ctrl+Y operations while the same history is written
// as readable YAML so a session can be inspected or recovered after a crash.
class EditorSessionHistory {
public:
  struct Snapshot {
    SceneRenderer::EditorSnapshot renderer;
    std::string selectedName;
    int selectedSurface = -1;
    std::string sceneName;
    std::vector<GameObject> sceneObjects;
  };

  void Start(const std::string &projectPath, const Snapshot &initial);
  void Push(const Snapshot &snapshot);
  bool Undo(Snapshot &out);
  bool Redo(Snapshot &out);

  bool CanUndo() const;
  bool CanRedo() const;
  bool IsActive() const { return active_; }
  const std::filesystem::path &FilePath() const { return sessionFilePath_; }
  std::size_t Cursor() const { return cursor_; }
  std::size_t Size() const { return history_.size(); }

  void Clear();

private:
  void Persist() const;

  std::string projectPath_;
  std::filesystem::path sessionDirectory_;
  std::filesystem::path sessionFilePath_;
  std::vector<Snapshot> history_;
  std::size_t cursor_ = 0;
  bool active_ = false;
};
