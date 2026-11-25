#include "../../../include/core_engine/UserDataDir.hpp"
#include "../../../include/ui/HandlerProject.hpp"
#include <cstdio>

namespace {
constexpr const char *kEditorIds[] = {"system", "code",        "codium",
                                      "cursor", "antigravity", "custom"};
constexpr const char *kEditorLabels[] = {"System default", "Visual Studio Code",
                                         "VSCodium",       "Cursor",
                                         "Antigravity",    "Custom executable"};

int EditorIndex(const std::string &id) {
  for (int i = 0; i < 6; ++i)
    if (id == kEditorIds[i])
      return i;
  return 0;
}

void PrepareDialog(float preferredWidth) {
  const auto *viewport = ImGui::GetMainViewport();
  const float width =
      std::min(preferredWidth, std::max(240.0f, viewport->WorkSize.x - 32));
  ImGui::SetNextWindowSize(ImVec2(width, 0), ImGuiCond_Always);
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
             viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
      ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}
constexpr ImGuiWindowFlags kDialogFlags = ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoResize |
                                          ImGuiWindowFlags_NoSavedSettings;
} // namespace

void HandlerProject::RequestCreateScript(const string &folder) {
  requestCreateScript = true;
  scriptDestination =
      folder.empty()
          ? (projectPath.empty()
                 ? ""
                 : (fs::path(projectPath) / "assets/scripts").string())
          : folder;
  scriptNameInput[0] = '\0';
  scriptCreationError.clear();
}

void HandlerProject::RequestCodeEditorSettings() {
  requestCodeEditorSettings = true;
  codeEditorDraft = ilmeee::LoadCodeEditorSettings(ilmeee::UserDataRoot() /
                                                   "editor-preferences.json");
  std::snprintf(codeEditorExecutableInput, sizeof(codeEditorExecutableInput),
                "%s", codeEditorDraft.executable.c_str());
  codeEditorSettingsError.clear();
}

void HandlerProject::RenderScriptDialogs() {
  using namespace ImGui;
  if (requestCreateScript) {
    OpenPopup("Create C++ Script");
    requestCreateScript = false;
  }
  PrepareDialog(500);
  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
  bool keepOpen = true;
  if (BeginPopupModal("Create C++ Script", &keepOpen, kDialogFlags)) {
    TextWrapped("Create a C++ component source and matching header.");
    Spacing();
    TextUnformatted("Script name");
    if (IsWindowAppearing())
      SetKeyboardFocusHere();
    SetNextItemWidth(-1);
    const bool enter = InputTextWithHint(
        "##ScriptName", "PlayerController", scriptNameInput,
        sizeof(scriptNameInput), ImGuiInputTextFlags_EnterReturnsTrue);
    if (IsItemEdited())
      scriptCreationError.clear();
    const string validation =
        ilmeee::ValidateScriptDestination(scriptDestination, scriptNameInput);
    Spacing();
    TextDisabled("Files");
    const string name =
        scriptNameInput[0] ? scriptNameInput : "PlayerController";
    TextWrapped(
        "%s", (fs::path(scriptDestination) / (name + ".cpp")).string().c_str());
    TextWrapped(
        "%s", (fs::path(scriptDestination) / (name + ".hpp")).string().c_str());
    Spacing();
    TextWrapped("The engine compiles and validates the class after creation. "
                "Once ready, select it in Add Component to attach it to an object.");
    Checkbox("Open project and script after creating",
             &openScriptAfterCreation);
    const auto editor = ilmeee::LoadCodeEditorSettings(
        ilmeee::UserDataRoot() / "editor-preferences.json");
    TextDisabled("Code editor: %s", kEditorLabels[EditorIndex(editor.editor)]);
    const string error =
        scriptCreationError.empty() ? validation : scriptCreationError;
    if (!error.empty()) {
      PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.66f, 0.43f, 1));
      TextWrapped("%s", error.c_str());
      PopStyleColor();
    }
    Separator();
    BeginDisabled(!validation.empty());
    const bool create = Button("Create Script", ImVec2(130, 0));
    EndDisabled();
    SameLine();
    const bool cancel =
        Button("Cancel", ImVec2(90, 0)) || IsKeyPressed(ImGuiKey_Escape);
    if ((create || enter) && validation.empty()) {
      if (ilmeee::CreateScriptFiles(scriptDestination, scriptNameInput,
                                    scriptCreationError)) {
        const string source =
            (fs::path(scriptDestination) / (name + ".cpp")).string();
        // Dialogs render after Explorer, so its current item references are no
        // longer in use. Refresh immediately without reopening the project.
        try {
          if (!projectPath.empty()) {
            selectedAsset = nullptr;
            rootAsset = BuildAssetTree(projectPath);
          }
        } catch (const fs::filesystem_error &e) {
          ::Log(e.what(), Debug::LogLevel::ERROR);
        }
        ShowNotification("Script Created", name + ".cpp and " + name + ".hpp",
                         greenColor);
        if (onScriptCreated) onScriptCreated();
        if (openScriptAfterCreation)
          OpenFile(source);
        CloseCurrentPopup();
      }
    } else if (cancel) {
      CloseCurrentPopup();
    }
    EndPopup();
  }
  PopStyleVar();

  if (requestCodeEditorSettings) {
    OpenPopup("Code Editor");
    requestCodeEditorSettings = false;
  }
  PrepareDialog(460);
  PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
  keepOpen = true;
  if (BeginPopupModal("Code Editor", &keepOpen, kDialogFlags)) {
    TextWrapped(
        "Choose the application used to open scripts and other code files.");
    Spacing();
    TextUnformatted("Default code editor");
    SetNextItemWidth(-1);
    int index = EditorIndex(codeEditorDraft.editor);
    if (Combo("##CodeEditor", &index, kEditorLabels, 6)) {
      codeEditorDraft.editor = kEditorIds[index];
      codeEditorSettingsError.clear();
    }
    if (codeEditorDraft.editor == "custom") {
      TextUnformatted("Executable path or command");
      SetNextItemWidth(-1);
      InputTextWithHint("##EditorExecutable", "/path/to/editor",
                        codeEditorExecutableInput,
                        sizeof(codeEditorExecutableInput));
      if (Button("Browse...")) {
        NFD::Guard guard;
        NFD::UniquePath path;
        if (NFD::OpenDialog(path) == NFD_OKAY)
          std::snprintf(codeEditorExecutableInput,
                        sizeof(codeEditorExecutableInput), "%s", path.get());
      }
      TextWrapped("Choose an editor that accepts a project folder followed by "
                  "the selected file as arguments.");
    } else if (codeEditorDraft.editor == "system") {
      TextWrapped(
          "Uses your operating system's file association for code files. "
          "Choose a code editor to open the full project workspace.");
    } else {
      TextWrapped(
          "Opens the project folder as a workspace and the selected file. "
          "The editor command must be installed and available in PATH.");
    }
    Spacing();
    TextDisabled("Saved for this user, across projects.");
    if (!codeEditorSettingsError.empty())
      TextWrapped("%s", codeEditorSettingsError.c_str());
    Separator();
    if (Button("Save", ImVec2(100, 0))) {
      codeEditorDraft.executable = codeEditorExecutableInput;
      if (ilmeee::SaveCodeEditorSettings(
              ilmeee::UserDataRoot() / "editor-preferences.json",
              codeEditorDraft, codeEditorSettingsError))
        CloseCurrentPopup();
    }
    SameLine();
    if (Button("Cancel", ImVec2(90, 0)) || IsKeyPressed(ImGuiKey_Escape))
      CloseCurrentPopup();
    EndPopup();
  }
  PopStyleVar();
}
