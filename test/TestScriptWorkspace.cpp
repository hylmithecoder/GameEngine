#include "../include/core_engine/ScriptWorkspace.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
static int checks = 0, failures = 0;
#define CHECK(expr)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(expr)) {                                                             \
      ++failures;                                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);     \
    }                                                                          \
  } while (0)

static std::string Read(const fs::path &path) {
  std::ifstream file(path);
  return {std::istreambuf_iterator<char>(file), {}};
}

int main(int argc, char **argv) {
  // Act as a code editor: capture the optional workspace and the file as
  // separate arguments, including spaces and literal shell syntax.
  if ((argc == 2 || argc == 3) &&
      fs::path(argv[argc - 1]).extension() == ".cpp") {
    const std::string source = argv[argc - 1];
    std::ofstream workspace(source + ".workspace");
    if (argc == 3)
      workspace << argv[1];
    workspace.close();
    std::ofstream capture(source + ".opened");
    capture << source;
    return capture ? 0 : 1;
  }
  const fs::path root =
      argc == 3 && std::string(argv[1]) == "--output-root"
          ? fs::path(argv[2])
          : fs::temp_directory_path() /
                ("ilmeee-script-test-" +
                 std::to_string(std::chrono::steady_clock::now()
                                    .time_since_epoch()
                                    .count()));
  fs::create_directories(root);
  const fs::path scripts = root / "scripts with spaces $(literal)";
  std::string error;
  CHECK(!ilmeee::CreateScriptFiles(scripts, "../Escape", error));
  CHECK(!fs::exists(root / "Escape.cpp"));
  CHECK(!ilmeee::CreateScriptFiles(scripts, "class", error));
  CHECK(!ilmeee::ValidateScriptName("Player.cpp").empty());
  CHECK(!ilmeee::ValidateScriptName("NUL").empty());
  CHECK(!ilmeee::ValidateScriptName("Component").empty());
  CHECK(!ilmeee::ValidateScriptName("TextGameObject").empty());
  CHECK(!ilmeee::ValidateScriptName("UiTextGameObject").empty());
  CHECK(!ilmeee::ValidateScriptName("UiImageGameObject").empty());
  CHECK(!ilmeee::ValidateScriptName("UiButtonGameObject").empty());
  CHECK(!ilmeee::ValidateScriptName("Player__Impl").empty());
  CHECK(ilmeee::CreateScriptFiles(scripts, "PlayerController", error));
  CHECK(error.empty());
  const fs::path source = scripts / "PlayerController.cpp";
  const fs::path header = scripts / "PlayerController.hpp";
  const auto originalSource = Read(source);
  const auto originalHeader = Read(header);
  CHECK(!ilmeee::CreateScriptFiles(scripts, "PlayerController", error));
  CHECK(Read(source) == originalSource);
  CHECK(Read(header) == originalHeader);
  // One existing partner must prevent creation of the other.
  std::ofstream(scripts / "Existing.hpp") << "user header";
  CHECK(!ilmeee::CreateScriptFiles(scripts, "Existing", error));
  CHECK(!fs::exists(scripts / "Existing.cpp"));
  CHECK(Read(scripts / "Existing.hpp") == "user header");
  std::ofstream(root / "blocked") << "not a directory";
  CHECK(!ilmeee::CreateScriptFiles(root / "blocked" / "scripts", "Player",
                                   error));
  CHECK(!error.empty());
#ifndef _WIN32
  fs::create_symlink(root / "absent", scripts / "Dangling.cpp");
  CHECK(!ilmeee::CreateScriptFiles(scripts, "Dangling", error));
  CHECK(!fs::exists(scripts / "Dangling.hpp"));
#endif
  const fs::path prefs = root / "preferences.json";
  CHECK(ilmeee::LoadCodeEditorSettings(prefs).editor == "system");
  std::ofstream(prefs) << "{\"unrelated\":true}";
  ilmeee::CodeEditorSettings settings{"custom", fs::absolute(argv[0]).string()};
  CHECK(ilmeee::SaveCodeEditorSettings(prefs, settings, error));
  CHECK(Read(prefs).find("unrelated") != std::string::npos);
  const auto restored = ilmeee::LoadCodeEditorSettings(prefs);
  CHECK(restored.editor == settings.editor &&
        restored.executable == settings.executable);
  CHECK(!ilmeee::SaveCodeEditorSettings(prefs, {"custom", ""}, error));
  CHECK(ilmeee::LoadCodeEditorSettings(prefs).executable ==
        settings.executable);
  CHECK(!ilmeee::OpenInCodeEditor(settings, root / "missing.cpp", error));
#ifndef _WIN32
  CHECK(!ilmeee::OpenInCodeEditor(
      {"custom", (root / "missing-editor").string()}, source, error));
#endif
  CHECK(ilmeee::OpenInCodeEditor(restored, source, error));
  const fs::path captured = source.string() + ".opened";
  for (int i = 0; i < 200 && Read(captured).empty(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(Read(captured) == fs::absolute(source).string());
  const fs::path capturedWorkspace = source.string() + ".workspace";
  CHECK(Read(capturedWorkspace).empty());
  CHECK(!ilmeee::OpenInCodeEditor(restored, source, error,
                                  root / "missing-project"));
  CHECK(ilmeee::OpenInCodeEditor(restored, source, error, scripts));
  for (int i = 0; i < 200 && Read(capturedWorkspace).empty(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(Read(capturedWorkspace) == fs::absolute(scripts).string());
  CHECK(Read(captured) == fs::absolute(source).string());
  // Reopening also supplies the workspace, regardless of prior launches.
  fs::remove(capturedWorkspace);
  CHECK(ilmeee::OpenInCodeEditor(restored, source, error, scripts));
  for (int i = 0; i < 200 && Read(capturedWorkspace).empty(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(Read(capturedWorkspace) == fs::absolute(scripts).string());
  std::ofstream(prefs) << "not JSON";
  CHECK(ilmeee::LoadCodeEditorSettings(prefs).editor == "system");
  std::printf("%d/%d checks passed; generated source: %s\n", checks - failures,
              checks, source.string().c_str());
  return failures ? 1 : 0;
}
