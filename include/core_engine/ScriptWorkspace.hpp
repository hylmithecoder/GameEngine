#pragma once

#include <filesystem>
#include <string>

namespace ilmeee {

// Create source assets; NativeScripts compiles and validates their components.
std::string ValidateScriptName(const std::string &name);
std::string ValidateScriptDestination(const std::filesystem::path &folder,
                                      const std::string &name);
bool CreateScriptFiles(const std::filesystem::path &folder,
                       const std::string &name, std::string &error);

struct CodeEditorSettings {
  std::string editor = "system";
  std::string executable;
};

CodeEditorSettings LoadCodeEditorSettings(const std::filesystem::path &file);
bool SaveCodeEditorSettings(const std::filesystem::path &file,
                            const CodeEditorSettings &settings,
                            std::string &error);
bool OpenInCodeEditor(const CodeEditorSettings &settings,
                      const std::filesystem::path &file, std::string &error,
                      const std::filesystem::path &projectFolder = {});

} // namespace ilmeee
