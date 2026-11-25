#pragma once
// Android side of IlmeeePlayer. A built game's files live inside the APK
// (assets/game/...), where std::ifstream can't see them. Instead of teaching
// every loader about AAssetManager, the player copies them once to internal
// storage and then runs exactly like the desktop player from that folder.

#include <filesystem>
#include <string>

namespace ilmeee::android {

// Name of the index GameBuilder writes into the APK's assets:
//   line 1: "ilmeee-files 1 <buildId>"
//   then:   one path per line, relative to the game folder ('/' separated)
inline constexpr const char *kFileIndex = "game/ilmeee_files.txt";

// Extracts the game to <internal storage>/game unless that copy already has
// this build id, points HOME at internal storage (so user-data paths work)
// and returns the game folder. Empty path and `error` set on failure.
std::filesystem::path PrepareGameFiles(std::string &error);

// Sends std::cout / std::cerr to logcat (tag "Ilmeee").
void RedirectStdioToLogcat();

} // namespace ilmeee::android
