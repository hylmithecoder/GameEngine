#include "../core_engine/Check_Environment.hpp"
#include "../core_engine/Debugger.hpp"
#include "../core_engine/Discordrich.hpp"
#include "../core_engine/net/MessageBus.hpp"
#include "../core_engine/net/WsTransport.hpp"
#include "MainWindow.hpp"
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <signal.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

using namespace Debug;
using namespace core_engine;
using namespace Ilmeee;

class ApplicationManager {
private:
  // Engine IPC: this process is the only server. HandlerIlmeeeEngine (and
  // any later tool) connects to it; see core_engine/net/Protocol.hpp.
  std::unique_ptr<ilmeee::net::WsServerTransport> ipcTransport;
  std::unique_ptr<ilmeee::net::MessageBus> ipcBus;
  std::string ipcToken;

  MainWindow *window;
  std::unique_ptr<Environment> environment;
  std::unique_ptr<DiscordRichPresence> discordRich;
  pid_t engineProcessId = -1;
  std::atomic<bool> isRunning{false};
  std::atomic<bool> shouldExit{false};
  std::vector<std::function<void()>> cleanupTasks;

  bool engineStopRequested = false;

  bool StartIpcServer();
  void ReapEngineIfExited();
  void CleanupNetwork();
  void CleanupEngine();
  void CleanupWindow();
  void CleanupSDL();

public:
  ApplicationManager();
  ~ApplicationManager();
  void Run();
  void RegisterCleanupTask(std::function<void()> task) {
    cleanupTasks.insert(cleanupTasks.begin(), task);
  }
  bool Initialize();
  bool LaunchEngine();
  void Shutdown();

  // Set the project path before Initialize() so the editor opens
  // straight into the given project. When empty (no --project arg),
  // the editor falls back to its hardcoded debug scene.
  void SetProjectPath(const std::string &path) { projectPath = path; }
  const std::string &GetProjectPath() const { return projectPath; }

  // When true and no --project was supplied, the standalone debug
  // fallback boots a 2D sprite scene instead of the 3D OBJ. Has no
  // effect when a project is loaded.
  void SetDebug2D(bool v) { debug2D = v; }
  bool GetDebug2D() const { return debug2D; }

private:
  std::string projectPath;
  bool debug2D = false;
};
