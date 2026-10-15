#include "../../../include/ui/MainWindow.hpp"

namespace {
const char *UiKindName(ilmeee::UiKind kind) {
  switch (kind) {
  case ilmeee::UiKind::Text: return "Text";
  case ilmeee::UiKind::Image: return "Image";
  case ilmeee::UiKind::Button: return "Button";
  case ilmeee::UiKind::Joystick: return "Joystick";
  }
  return "UI";
}

int ResizeString(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
    auto *text = static_cast<std::string *>(data->UserData);
    text->resize(static_cast<size_t>(data->BufTextLen));
    data->Buf = text->data();
  }
  return 0;
}

bool InputString(const char *label, std::string &text, bool multiline,
                 ImVec2 size = ImVec2(0, 0)) {
  const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
  return multiline ? InputTextMultiline(label, text.data(), text.capacity() + 1,
                                        size, flags, ResizeString, &text)
                   : InputText(label, text.data(), text.capacity() + 1, flags,
                               ResizeString, &text);
}
} // namespace

bool MainWindow::UiElementCombo(const char *label, uint64_t &objectId) {
  LoadGameUiForProject();
  const ilmeee::UiElement *current = ilmeee::FindUiElement(gameUi, objectId);
  const std::string preview = !objectId ? "None"
                              : current ? current->id + " (" +
                                              UiKindName(current->kind) + ")"
                                        : "Missing UI element";
  bool changed = false;
  if (BeginCombo(label, preview.c_str())) {
    if (Selectable("None", objectId == 0)) {
      objectId = 0;
      changed = true;
    }
    for (const auto &element : gameUi.elements) {
      PushID(std::to_string(element.objectId).c_str());
      const std::string name =
          element.id + " (" + UiKindName(element.kind) + ")";
      if (Selectable(name.c_str(), objectId == element.objectId)) {
        objectId = element.objectId;
        changed = true;
      }
      PopID();
    }
    if (gameUi.elements.empty())
      TextDisabled("No UI elements. Create a Canvas child first.");
    EndCombo();
  }
  return changed;
}

void MainWindow::CreateDialogUi(ilmeee::DialogComponent &d) {
  // A box along the bottom with the name above it and, for Interact, a
  // "Talk" button in the middle as the prompt: it only shows near the
  // character and works by click, tap (phones) or E. Existing links are
  // kept, so this only fills the gaps.
  LoadGameUiForProject();
  if (!hasGameUiCanvas) {
    gameUi = ilmeee::GameUI{};
    hasGameUiCanvas = true;
  }
  const float w = gameUi.referenceWidth;
  const auto add = [&](uint64_t &link, const char *base, ilmeee::UiKind kind,
                       ilmeee::UiAnchor anchor, float x, float y, float width,
                       float height, float fontSize,
                       ImVec4 background) -> ilmeee::UiElement * {
    if (ilmeee::FindUiElement(gameUi, link))
      return nullptr;
    ilmeee::UiElement e;
    e.id = base;
    for (int n = 2; std::any_of(gameUi.elements.begin(), gameUi.elements.end(),
                                [&](const auto &o) { return o.id == e.id; });
         ++n)
      e.id = std::string(base) + " " + std::to_string(n);
    e.kind = kind;
    e.anchor = anchor;
    e.x = x;
    e.y = y;
    e.width = width;
    e.height = height;
    e.fontSize = fontSize;
    e.background = background;
    link = e.objectId;
    gameUi.elements.push_back(std::move(e));
    return &gameUi.elements.back();
  };
  const float boxW = std::max(200.0f, w - 80.0f);
  add(d.box, "Dialog Box", ilmeee::UiKind::Image, ilmeee::UiAnchor::BottomLeft,
      40, 30, boxW, 190, 24, ImVec4(0.05f, 0.06f, 0.10f, 0.85f));
  add(d.text, "Dialog Text", ilmeee::UiKind::Text, ilmeee::UiAnchor::BottomLeft,
      64, 46, boxW - 48, 150, 28, ImVec4(0, 0, 0, 0));
  add(d.speaker, "Dialog Name", ilmeee::UiKind::Text,
      ilmeee::UiAnchor::BottomLeft, 40, 228, 260, 48, 26,
      ImVec4(0.20f, 0.32f, 0.55f, 0.95f));
  if (d.trigger == ilmeee::DialogTrigger::Interact) {
    if (auto *prompt = add(d.prompt, "Talk Button", ilmeee::UiKind::Button,
                           ilmeee::UiAnchor::Center, 0, 140, 220, 56, 24,
                           ImVec4(0.05f, 0.06f, 0.10f, 0.80f))) {
      prompt->text = "[E] Talk";
      prompt->action = "Interact";
    }
  }
}

void MainWindow::RenderDialogInspector(size_t meshIndex) {
  auto &mesh = sceneRenderer->meshes3d[meshIndex];
  auto &d = mesh.dialog;
  bool keep = true;
  if (CollapsingHeader("Dialog", &keep, ImGuiTreeNodeFlags_DefaultOpen)) {
    PushID("dialog");
    bool record = false;
    int trigger = static_cast<int>(d.trigger);
    if (Combo("Trigger", &trigger, ilmeee::kDialogTriggerNames)) {
      d.trigger = static_cast<ilmeee::DialogTrigger>(trigger);
      record = true;
    }
    if (d.trigger == ilmeee::DialogTrigger::Interact) {
      DragFloat("Range", &d.range, 0.05f, 0.0f, 1000.0f, "%.2f");
      record |= IsItemDeactivatedAfterEdit();
    }

    SeparatorText("UI");
    record |= UiElementCombo("Dialog Box", d.box);
    record |= UiElementCombo("Text", d.text);
    record |= UiElementCombo("Speaker Name", d.speaker);
    if (d.trigger == ilmeee::DialogTrigger::Interact)
      record |= UiElementCombo("Prompt", d.prompt);
    if (d.trigger == ilmeee::DialogTrigger::Interact && IsItemHovered())
      SetTooltip("Shown only while the player is in range. Pick a Button "
                 "with action \"Interact\" to make it the talk button.");
    BeginDisabled(SceneStructureLocked());
    if (Button("Create Dialog UI")) {
      CreateDialogUi(d);
      record = true;
      PushMessage("[INFO] Dialog UI added to the Canvas. Save with Ctrl+S.");
    }
    EndDisabled();
    if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      SetTooltip("Adds the missing Box / Text / Name elements and a Talk "
                 "button (action Interact) as the Prompt");

    SeparatorText("Lines");
    InputString("##lines", d.lines, true,
                ImVec2(-1, GetTextLineHeight() * 8));
    record |= IsItemDeactivatedAfterEdit();
    InputString("Dialog File", d.file, false);
    record |= IsItemDeactivatedAfterEdit();
    if (BeginDragDropTarget()) {
      if (const ImGuiPayload *pl = AcceptDragDropPayload("ASSET_PATH")) {
        d.file = ProjectMediaPath(projectHandler.projectPath,
                                  (const char *)pl->Data);
        record = true;
      }
      EndDragDropTarget();
    }
    if (!d.file.empty())
      TextDisabled("The file replaces the lines above.");
    else
      TextDisabled("%zu line(s)",
                   ilmeee::ParseDialogScript(d.lines, mesh.displayName).size());
    TextDisabled("\"Name: text\"   \": narration\"   \"# comment\"");

    SeparatorText("Behaviour");
    DragFloat("Typing Speed", &d.charsPerSecond, 1.0f, 0.0f, 1000.0f,
              d.charsPerSecond <= 0.0f ? "instant" : "%.0f chars/s");
    record |= IsItemDeactivatedAfterEdit();
    record |= Checkbox("Repeatable", &d.repeatable);
    SameLine();
    record |= Checkbox("Freeze Player", &d.freezePlayer);
    TextDisabled("Play: E / Enter or a button with action");
    TextDisabled("\"Interact\" talks; tap the box to continue.");
    PopID();
    if (record)
      RecordEditorHistory();
  }
  if (!keep) {
    d = {};
    RecordEditorHistory();
  }
}
