#include "../../../include/core_engine/DialogSystem.hpp"
#include "../../../include/core_engine/GameUI.hpp"
#include "../../../include/core_engine/SceneRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

namespace ilmeee {
namespace {
void SetVisible(GameUI &ui, uint64_t id, bool visible) {
  if (UiElement *e = FindUiElement(ui, id))
    e->visible = visible;
}
void SetText(GameUI &ui, uint64_t id, const std::string &text) {
  if (UiElement *e = FindUiElement(ui, id))
    e->text = text;
}
void HideAll(GameUI &ui, const DialogComponent &d) {
  for (uint64_t id : {d.box, d.text, d.speaker, d.prompt})
    SetVisible(ui, id, false);
}
bool Pressed(const MovementInput &input, uint64_t id) {
  return id && std::find(input.uiPressed.begin(), input.uiPressed.end(), id) !=
                   input.uiPressed.end();
}
// Where interaction distance is measured from: the first movable object
// (the player), else the game camera.
bool PlayerPosition(const SceneRenderer &renderer, glm::vec3 &out,
                    int &player) {
  player = -1;
  for (size_t i = 0; i < renderer.meshes3d.size(); ++i)
    if (renderer.meshes3d[i].movable &&
        !renderer.meshes3d[i].movementScript.empty()) {
      out = renderer.GetMesh3DWorldPosition(i);
      player = static_cast<int>(i);
      return true;
    }
  glm::vec3 right, forward;
  return renderer.GetPlayerCameraPose(out, right, forward);
}
} // namespace

MovementInput FrozenMovementInput(const MovementInput &input) {
  MovementInput frozen;
  frozen.look = input.look;
  frozen.lookDelta = input.lookDelta;
  return frozen;
}

int DialogSystem::FindOwner(const SceneRenderer &renderer) const {
  for (size_t i = 0; i < renderer.meshes3d.size(); ++i)
    if (renderer.meshes3d[i].objectId == owner_)
      return static_cast<int>(i);
  return -1;
}

const DialogLine *DialogSystem::CurrentLine() const {
  return owner_ && line_ >= 0 && line_ < static_cast<int>(lines_.size())
             ? &lines_[static_cast<size_t>(line_)]
             : nullptr;
}

void DialogSystem::Start(SceneRenderer &renderer, GameUI &ui) {
  Stop();
  for (const auto &mesh : renderer.meshes3d)
    if (mesh.dialog.enabled)
      HideAll(ui, mesh.dialog);
  for (size_t i = 0; i < renderer.meshes3d.size(); ++i)
    if (renderer.meshes3d[i].dialog.enabled &&
        renderer.meshes3d[i].dialog.trigger == DialogTrigger::OnStart &&
        Begin(renderer, ui, i))
      break;
}

void DialogSystem::Stop() {
  owner_ = 0;
  lines_.clear();
  line_ = -1;
  typed_ = 0.0f;
  freeze_ = false;
  wasDown_ = false;
  finished_.clear();
}

bool DialogSystem::Begin(SceneRenderer &renderer, GameUI &ui, size_t index) {
  if (owner_ || index >= renderer.meshes3d.size())
    return false;
  const auto &mesh = renderer.meshes3d[index];
  const DialogComponent &d = mesh.dialog;
  if (!d.enabled)
    return false;
  std::string source = d.lines;
  if (!d.file.empty()) {
    const std::filesystem::path path(d.file);
    std::ifstream file(path.is_absolute() ? path : root_ / path,
                       std::ios::binary);
    if (!file) {
      if (log)
        log("[ERROR] [Dialog] " + mesh.displayName + ": cannot open " + d.file);
      return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    source = text.str();
  }
  lines_ = ParseDialogScript(source, mesh.displayName);
  if (lines_.empty()) {
    if (log)
      log("[WARNING] [Dialog] " + mesh.displayName + " has no dialog lines");
    return false;
  }
  if (!FindUiElement(ui, d.text) && log)
    log("[WARNING] [Dialog] " + mesh.displayName +
        ": no Text element selected, lines are not shown");
  owner_ = mesh.objectId;
  freeze_ = d.freezePlayer;
  line_ = 0;
  typed_ = 0.0f;
  SetVisible(ui, d.prompt, false);
  SetVisible(ui, d.box, true);
  SetVisible(ui, d.text, true);
  ShowLine(renderer, ui);
  return true;
}

void DialogSystem::ShowLine(SceneRenderer &renderer, GameUI &ui) {
  const int index = FindOwner(renderer);
  const DialogLine *line = CurrentLine();
  if (index < 0 || !line)
    return;
  const DialogComponent &d = renderer.meshes3d[static_cast<size_t>(index)].dialog;
  if (d.charsPerSecond <= 0.0f)
    typed_ = static_cast<float>(Utf8Length(line->text));
  SetText(ui, d.text, Utf8Prefix(line->text, static_cast<size_t>(typed_)));
  SetText(ui, d.speaker, line->speaker);
  // The name plate is hidden for narration lines.
  SetVisible(ui, d.speaker, !line->speaker.empty());
}

bool DialogSystem::Next(SceneRenderer &renderer, GameUI &ui) {
  const DialogLine *line = CurrentLine();
  if (!line)
    return false;
  const float length = static_cast<float>(Utf8Length(line->text));
  if (typed_ < length) {
    typed_ = length;
  } else if (++line_ >= static_cast<int>(lines_.size())) {
    End(renderer, ui);
    return true;
  } else {
    typed_ = 0.0f;
  }
  ShowLine(renderer, ui);
  return true;
}

void DialogSystem::End(SceneRenderer &renderer, GameUI &ui) {
  if (!owner_)
    return;
  const int index = FindOwner(renderer);
  if (index >= 0)
    HideAll(ui, renderer.meshes3d[static_cast<size_t>(index)].dialog);
  finished_.insert(owner_);
  owner_ = 0;
  lines_.clear();
  line_ = -1;
  typed_ = 0.0f;
  freeze_ = false;
}

void DialogSystem::Update(SceneRenderer &renderer, GameUI &ui,
                          const MovementInput &input, float deltaSeconds) {
  const bool down = input.interact || input.submit;
  const bool pressed = down && !wasDown_;
  wasDown_ = down;

  if (owner_) {
    const int index = FindOwner(renderer);
    if (index < 0 || !renderer.meshes3d[static_cast<size_t>(index)].dialog.enabled) {
      // The character was removed or switched off mid-dialog.
      if (index >= 0)
        End(renderer, ui);
      else
        owner_ = 0, line_ = -1, freeze_ = false;
      return;
    }
    const DialogComponent &d = renderer.meshes3d[static_cast<size_t>(index)].dialog;
    if (pressed || Pressed(input, d.box) || Pressed(input, d.text)) {
      Next(renderer, ui);
      return;
    }
    const DialogLine *line = CurrentLine();
    const float length = line ? static_cast<float>(Utf8Length(line->text)) : 0.0f;
    if (line && typed_ < length && std::isfinite(deltaSeconds) && deltaSeconds > 0) {
      const size_t before = static_cast<size_t>(typed_);
      typed_ = std::min(length, typed_ + d.charsPerSecond * deltaSeconds);
      if (static_cast<size_t>(typed_) != before)
        SetText(ui, d.text, Utf8Prefix(line->text, static_cast<size_t>(typed_)));
    }
    return;
  }

  // Closest character in range that can be talked to.
  glm::vec3 player(0.0f);
  int playerIndex = -1;
  const bool hasPlayer = PlayerPosition(renderer, player, playerIndex);
  int nearest = -1;
  float best = std::numeric_limits<float>::max();
  for (size_t i = 0; i < renderer.meshes3d.size(); ++i) {
    const auto &mesh = renderer.meshes3d[i];
    const DialogComponent &d = mesh.dialog;
    if (!d.enabled || d.trigger != DialogTrigger::Interact ||
        static_cast<int>(i) == playerIndex ||
        (!d.repeatable && finished_.count(mesh.objectId)))
      continue;
    const float distance =
        hasPlayer ? glm::length(renderer.GetMesh3DWorldPosition(i) - player)
                  : 0.0f;
    if (distance <= d.range && distance < best) {
      best = distance;
      nearest = static_cast<int>(i);
    }
  }
  // Prompts: only the nearest character's hint shows. Characters may share a
  // prompt element, so hide them all first.
  for (const auto &mesh : renderer.meshes3d)
    if (mesh.dialog.enabled)
      SetVisible(ui, mesh.dialog.prompt, false);
  if (nearest >= 0)
    SetVisible(ui, renderer.meshes3d[static_cast<size_t>(nearest)].dialog.prompt,
               true);
  if (pressed && nearest >= 0)
    Begin(renderer, ui, static_cast<size_t>(nearest));
}

} // namespace ilmeee
