#pragma once

#include "Scene.hpp"
#include "SceneRenderer.hpp"
#include "GameUI.hpp"
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Temporary, per-engine-process editor history. The live snapshots stay in
// memory for fast Ctrl+Z/Ctrl+Y operations while the same history is written
// as readable YAML so a session can be inspected or recovered after a crash.
// The YAML is written by a background thread: rewriting the whole history on
// the editor thread after every edit/undo caused visible stutter.
class EditorSessionHistory {
public:
  EditorSessionHistory() = default;
  ~EditorSessionHistory();
  EditorSessionHistory(const EditorSessionHistory &) = delete;
  EditorSessionHistory &operator=(const EditorSessionHistory &) = delete;

  struct Snapshot {
    SceneRenderer::EditorSnapshot renderer;
    std::string selectedName;
    int selectedSurface = -1;
    std::string sceneName;
    std::vector<GameObject> sceneObjects;
    ilmeee::GameUI ui;
    bool hasCanvas = false;
    int selectedUiElement = -1;
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

  // Everything the writer thread needs; snapshots are shared, not copied.
  struct PersistJob {
    std::string projectPath;
    std::filesystem::path sessionDirectory;
    std::filesystem::path sessionFilePath;
    std::size_t cursor = 0;
    std::vector<std::shared_ptr<const Snapshot>> history;
  };

private:
  struct Entry {
    std::shared_ptr<const Snapshot> snapshot;
    std::string signature; // cached SnapshotSignature, for Push's dedup
  };
  void Persist(); // queue a background write of the current history
  void WriterLoop();

  std::string projectPath_;
  std::filesystem::path sessionDirectory_;
  std::filesystem::path sessionFilePath_;
  std::vector<Entry> history_;
  std::size_t cursor_ = 0;
  bool active_ = false;

  std::mutex writerMutex_;
  std::condition_variable writerWake_;
  std::optional<PersistJob> pendingJob_; // newest wins; older ones are skipped
  bool writerStop_ = false;
  std::thread writer_;
};
