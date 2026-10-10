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

enum class BuildTarget { Linux, Android };

struct GameBuildOptions {
  BuildTarget target = BuildTarget::Linux;
  std::filesystem::path projectRoot;
  // Empty: <projectRoot>/build/<linux|android>/<SafeGameName>.
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

  // ---- Android (target == Android) ----------------------------------------
  // What scripts/android/build-player.sh produced: jniLibs/<abi>/*.so and
  // sdl-java/ (SDL3's Java glue). playerExecutable is not used.
  std::filesystem::path androidPlayerDir;
  std::filesystem::path androidTemplateDir; // the engine's android/template
  std::filesystem::path androidSdkDir;      // Empty: $ANDROID_HOME or ~/Android/Sdk
  // Empty: com.ilmeee.<lowercase game name>.
  std::string applicationId;
  std::string versionName = "1.0";
  int versionCode = 1;
  // Gradle to run in the generated project. Empty: only prepare the project
  // (no APK) — what the tests do.
  std::string gradleCommand = "gradle";
};

struct GameBuildResult {
  bool ok = false;
  std::string error;
  std::filesystem::path outputDir;
  std::filesystem::path launcher; // what to run: <outputDir>/<SafeGameName>
                                  // (Android: the .apk)
  std::string applicationId;      // Android package name
  size_t filesCopied = 0;
  std::uintmax_t bytesCopied = 0;
  size_t relocatedReferences = 0; // scene paths that pointed outside assets/
  size_t missingReferences = 0;   // ... and whose file did not exist
  bool librariesBundled = false;
};

using BuildLog = std::function<void(const std::string &line)>;

// "My First Project" -> "My_First_Project": safe as a file and folder name.
std::string SafeGameName(const std::string &name);

// "My First Project" -> "com.ilmeee.my_first_project": a valid Android
// package name (lowercase segments that start with a letter).
std::string DefaultApplicationId(const std::string &name);

// Main activity of every Ilmeee APK, for `adb shell am start -n`.
inline constexpr const char *kAndroidActivity = "com.ilmeee.player.IlmeeeActivity";

GameBuildResult BuildGame(const GameBuildOptions &options, const BuildLog &log);

} // namespace ilmeee
