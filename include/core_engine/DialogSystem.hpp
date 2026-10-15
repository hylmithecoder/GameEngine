#pragma once
// Runs Dialog components during Play, shared by the editor's Game tab and
// IlmeeePlayer. One dialog is open at a time. The dialog writes into the
// game Canvas (show/hide, text), so the caller restores the Canvas on Stop.

#include "DialogComponent.hpp"
#include "MovementScript.hpp"
#include <filesystem>
#include <functional>
#include <unordered_set>

class SceneRenderer;

namespace ilmeee {
struct GameUI;

class DialogSystem {
public:
  std::function<void(const std::string &)> log;
  void SetProjectRoot(const std::filesystem::path &root) { root_ = root; }

  // Play started: hides every element a dialog uses and opens the first
  // On Start dialog. Call before scripts Start so they may override it.
  void Start(SceneRenderer &renderer, GameUI &ui);
  // Interaction (E / Enter / "Interact" button, edge-triggered), typing and
  // advancing (the same keys, or a click on the box or text element).
  void Update(SceneRenderer &renderer, GameUI &ui, const MovementInput &input,
              float deltaSeconds);
  // Play stopped: forgets the open dialog and who was already talked to.
  void Stop();

  bool Begin(SceneRenderer &renderer, GameUI &ui, size_t meshIndex);
  // Completes the typing line, else shows the next line or closes.
  bool Next(SceneRenderer &renderer, GameUI &ui);
  void End(SceneRenderer &renderer, GameUI &ui);

  bool IsTalking() const { return owner_ != 0; }
  uint64_t Owner() const { return owner_; }
  int LineIndex() const { return owner_ ? line_ : -1; }
  const DialogLine *CurrentLine() const;
  // Movement input should be ignored (freezePlayer of the open dialog).
  bool FreezesPlayer() const { return owner_ != 0 && freeze_; }

private:
  void ShowLine(SceneRenderer &renderer, GameUI &ui);
  int FindOwner(const SceneRenderer &renderer) const;

  std::filesystem::path root_;
  uint64_t owner_ = 0;
  std::vector<DialogLine> lines_;
  int line_ = -1;
  float typed_ = 0.0f; // code points shown of the current line
  bool freeze_ = false;
  bool wasDown_ = false;
  std::unordered_set<uint64_t> finished_;
};

// Input with movement (WASD, jump, joysticks) cleared; camera look kept.
MovementInput FrozenMovementInput(const MovementInput &input);

} // namespace ilmeee
