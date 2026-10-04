#pragma once
// Turns a project folder into a runnable game folder: the IlmeeePlayer
// runtime + the project's scenes and assets + the engine shaders, laid out as
// documented in src/cpp/player/PlayerMain.cpp. No GPU involved, so it can be
// run (and tested) anywhere.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ilmeee {

struct GameBuildOptions {
  std::filesystem::path projectRoot;
  // Empty: <projectRoot>/build/linux/<SafeGameName>.
  std::filesystem::path outputDir;
  // Empty: the project folder's name.
  std::string gameName;
  std::filesystem::path playerExecutable; // the built IlmeeePlayer
  // The engine's assets/shaders/vulkan (what the editor itself loads).
  std::filesystem::path engineShaderDir;
  // scripts/bundle-deps.sh. Empty: don't bundle libraries — the game then
  // runs on this machine only.
  std::filesystem::path bundleScript;
  int windowWidth = 1280;
  int windowHeight = 720;
  bool fullscreen = false;
};

struct GameBuildResult {
  bool ok = false;
  std::string error;
  std::filesystem::path outputDir;
  std::filesystem::path launcher; // what to run: <outputDir>/<SafeGameName>
  size_t filesCopied = 0;
  std::uintmax_t bytesCopied = 0;
  size_t relocatedReferences = 0; // scene paths that pointed outside assets/
  size_t missingReferences = 0;   // ... and whose file did not exist
  bool librariesBundled = false;
};

using BuildLog = std::function<void(const std::string &line)>;

// "My First Project" -> "My_First_Project": safe as a file and folder name.
std::string SafeGameName(const std::string &name);

GameBuildResult BuildGame(const GameBuildOptions &options, const BuildLog &log);

} // namespace ilmeee
