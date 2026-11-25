#pragma once
// The one place both ends of the engine IPC agree on names: how the core
// hands its address to a child process, which roles exist, and the message
// catalog. Payload shapes are documented next to each type; keep this file
// and docs in `.implementation/claude/06-*` in sync when adding one.

#include <random>
#include <string>

namespace ilmeee::net::proto {

// ---- process wiring ------------------------------------------------------
// Core → child: the URL goes on the command line (not secret), the token in
// the environment (argv is world-readable through ps; environ is not).
inline constexpr const char *kIpcUrlArg = "--ipc-url";
inline constexpr const char *kIpcTokenEnv = "ILMEEE_IPC_TOKEN";

// ---- roles (BusConfig::clientRole) ---------------------------------------
inline constexpr const char *kRoleEngine = "engine"; // HandlerIlmeeeEngine

// ---- message catalog -----------------------------------------------------
// core → engine, event   {project: string}   sent once the engine has joined.
// `project` is the editor's project path; empty in standalone debug mode.
inline constexpr const char *kSessionInit = "session.init";

// core → engine, request {}  →  {path, json, sceneName, objects}
// Opens the engine-side scene picker. Fails with "cancelled" if the user
// closes the dialog. Interactive, so callers use a long timeout.
inline constexpr const char *kSceneLoad = "scene.load";

// core → engine, event   {}   the editor is closing; stop the engine loop.
inline constexpr const char *kEngineStop = "engine.stop";

// Local only, never on the wire: raised inside the engine process when its
// connection to the core drops, so it can shut down instead of lingering.
inline constexpr const char *kCoreLost = "core.lost";

// 128-bit random token, hex encoded.
inline std::string GenerateToken() {
  std::random_device rd;
  static const char *hex = "0123456789abcdef";
  std::string out;
  for (int i = 0; i < 4; ++i) {
    unsigned v = rd();
    for (int n = 0; n < 8; ++n, v >>= 4)
      out += hex[v & 0xF];
  }
  return out;
}

} // namespace ilmeee::net::proto
