#include "../../include/ui/Application.hpp"
#include "../../include/core_engine/UserDataDir.hpp"
#include "../../include/core_engine/net/Protocol.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstring>
#include <thread>

extern char **environ;

ApplicationManager::ApplicationManager() {
  environment = std::make_unique<Environment>();
  discordRich = std::make_unique<DiscordRichPresence>();

  // Register cleanup tasks in reverse order of initialization
  RegisterCleanupTask([this]() { CleanupWindow(); });
  RegisterCleanupTask([this]() {
    if (discordRich)
      discordRich->Shutdown();
  });
  RegisterCleanupTask([this]() { CleanupNetwork(); });
  RegisterCleanupTask([this]() { CleanupEngine(); });
  RegisterCleanupTask([this]() { CleanupSDL(); });
}

ApplicationManager::~ApplicationManager() { Shutdown(); }

bool ApplicationManager::Initialize() {
  try {
    ::Log("Initializing Application Manager...");

    environment->detectDriveInfo();
    environment->printEnvironment();
    environment->printDriveInfo();

    av_log_set_level(AV_LOG_ERROR);

    // Create main window (powered by VulkanBase)
    window = new MainWindow("Ilmeee Editor", 1280, 720);
    if (!window || !window->Init("Ilmeee Editor", 1280, 720)) {
      ::Log("Failed to initialize main window", Debug::LogLevel::CRASH);
      return false;
    }

    // Propagate the standalone-debug fallback selector before opening
    // any project. Only consumed when projectPath is empty.
    window->debug2D = debug2D;

    // The window's frame loop is what drives the IPC bus from here on.
    window->ipcBus = ipcBus.get();

    // If a project path was passed in (typically from IlmeeeHub via
    // --project), open it now so the editor starts directly in-context.
    // When unset, the editor keeps its hardcoded debug behavior — useful
    // when running GameEngineSDL directly without going through Hub.
    if (!projectPath.empty()) {
      ::Log("Auto-opening project: " + projectPath, Debug::LogLevel::SUCCESS);
      window->projectHandler.OpenProject(projectPath.c_str());
    }

    if (discordRich) {
      discordRich->Init();
    }

    ::Log("Application Manager initialized successfully");
    return true;

  } catch (const std::exception &e) {
    ::Log("Exception during initialization: " + std::string(e.what()),
          Debug::LogLevel::CRASH);
    return false;
  }
}

bool ApplicationManager::StartIpcServer() {
  using namespace ilmeee::net;

  ipcToken = proto::GenerateToken();
  ipcTransport = std::make_unique<WsServerTransport>();
  BusConfig config;
  config.role = BusRole::Server;
  config.token = ipcToken;
  ipcBus = std::make_unique<MessageBus>(*ipcTransport, config);

  ipcBus->SetLogSink([](BusLogLevel level, const std::string &text) {
    ::Log("[IPC] " + text, level == BusLogLevel::Error ? Debug::LogLevel::ERROR
                           : level == BusLogLevel::Warning
                               ? Debug::LogLevel::WARNING
                               : Debug::LogLevel::INFO);
  });

  ipcBus->OnPeerJoined([this](const PeerInfo &peer) {
    if (peer.role != proto::kRoleEngine)
      return;
    ::Log("[IPC] Engine connected (pid " + std::to_string(peer.pid) + ")",
          Debug::LogLevel::SUCCESS);
    ipcBus->Emit(peer.connection, proto::kSessionInit,
                 {{"project", projectPath}});
  });

  ipcBus->OnPeerLeft([this](const PeerInfo &peer, const std::string &reason) {
    if (peer.role != proto::kRoleEngine)
      return;
    ::Log("[IPC] Engine disconnected: " + reason,
          isRunning ? Debug::LogLevel::WARNING : Debug::LogLevel::INFO);
    ReapEngineIfExited();
  });

  if (!ipcBus->Start())
    return false;
  ::Log("[IPC] Core listening on " + ipcTransport->Url(),
        Debug::LogLevel::SUCCESS);
  return true;
}

bool ApplicationManager::LaunchEngine() {
  using namespace ilmeee::net;
  try {
    ::Log("[IlmeeeEditor] Starting IPC server...");
    isRunning = true;
    if (!StartIpcServer()) {
      ::Log("Failed to start IPC server", Debug::LogLevel::CRASH);
      return false;
    }

    ::Log("[IlmeeeEditor] Launching engine process...");

    // Resolve the handler next to this binary rather than relative to the
    // working directory: the editor is launched from Hub, from a shell in the
    // repo root, or from an installed bin/, and "./HandlerIlmeeeEngine" only
    // happens to work in the last of those.
    const std::string handler = ilmeee::SiblingExecutable("HandlerIlmeeeEngine");

    // Everything the child needs is built before fork(): between fork and
    // exec only async-signal-safe calls are allowed, which rules out
    // allocating. The URL travels in argv, the token in the environment.
    std::string url = ipcTransport->Url();
    std::string programName = "HandlerIlmeeeEngine";
    std::string urlArg = proto::kIpcUrlArg;
    std::vector<char *> childArgv = {programName.data(), urlArg.data(),
                                     url.data(), nullptr};

    const std::string tokenPrefix = std::string(proto::kIpcTokenEnv) + "=";
    std::vector<std::string> envStorage;
    for (char **e = environ; e && *e; ++e)
      if (std::strncmp(*e, tokenPrefix.c_str(), tokenPrefix.size()) != 0)
        envStorage.emplace_back(*e);
    envStorage.push_back(tokenPrefix + ipcToken);
    std::vector<char *> childEnv;
    for (auto &entry : envStorage)
      childEnv.push_back(entry.data());
    childEnv.push_back(nullptr);

    pid_t pid = fork();
    if (pid == -1) {
      ::Log("Failed to fork process: " + std::string(strerror(errno)),
            Debug::LogLevel::CRASH);
      return false;
    } else if (pid == 0) {
      execve(handler.c_str(), childArgv.data(), childEnv.data());
      // exec only returns on failure. Leave with _exit, not exit: this child
      // is a fork of the editor and still owns copies of its atexit handlers,
      // static destructors and open sockets. Running them here would tear down
      // the *parent's* state — the Discord session and the listening socket the
      // engine is about to connect to.
      _exit(1);
    }

    engineProcessId = pid;
    // No waiting here: the engine dials in on its own (retrying until we
    // listen, which we already do) and announces itself with a handshake.
    // OnPeerJoined sends it the session once that happens.
    ::Log("[IlmeeeEditor] Engine process started (pid " + std::to_string(pid) +
              ")",
          Debug::LogLevel::SUCCESS);
    return true;

  } catch (const std::exception &e) {
    ::Log("Exception during engine launch: " + std::string(e.what()),
          Debug::LogLevel::CRASH);
    return false;
  }
}

void ApplicationManager::ReapEngineIfExited() {
  if (engineProcessId <= 0)
    return;
  int status = 0;
  if (waitpid(engineProcessId, &status, WNOHANG) != engineProcessId)
    return;
  if (WIFEXITED(status))
    ::Log("[IlmeeeEditor] Engine exited with code " +
          std::to_string(WEXITSTATUS(status)));
  else if (WIFSIGNALED(status))
    ::Log("[IlmeeeEditor] Engine killed by signal " +
              std::to_string(WTERMSIG(status)),
          Debug::LogLevel::WARNING);
  engineProcessId = -1;
}

void ApplicationManager::Run() {
  if (!window) {
    ::Log("Cannot run - window not initialized", Debug::LogLevel::CRASH);
    return;
  }

  ::Log("Starting main application via VulkanBase...",
        Debug::LogLevel::SUCCESS);

  // The main loop is now handled by VulkanBase::Run()
  window->Run();

  ::Log("Main application loop ended");
}

void ApplicationManager::Shutdown() {
  // Dipanggil dua kali — sekali eksplisit (dari main / akhir Run),
  // sekali implisit dari ~ApplicationManager. Idempoten via static
  // flag (asumsi: hanya satu ApplicationManager per proses, yang
  // dijaga oleh g_app unique_ptr di main.cpp).
  // Tetap memproses cleanup walau isRunning==false, supaya thread &
  // resource yang sempat dibuat saat init gagal di tengah jalan tetap
  // di-tear-down dengan benar.
  static bool shutdownDone = false;
  if (shutdownDone)
    return;
  shutdownDone = true;

  ::Log("Starting application shutdown...");

  if (ipcBus && isRunning) {
    using namespace ilmeee::net;
    ConnectionId engine = ipcBus->PeerByRole(proto::kRoleEngine);
    if (engine != kInvalidConnection)
      engineStopRequested = ipcBus->Emit(engine, proto::kEngineStop);
  }
  isRunning = false;
  shouldExit = true;

  // Discord update thread harus di-join sebelum unique_ptr destroy
  // DiscordRichPresence, kalau tidak destruktor std::thread bawaan
  // panggil std::terminate.
  if (discordRich) {
    discordRich->Shutdown();
  }

  // Engine before network: it is still connected and acting on engine.stop,
  // and it may need a moment to leave its loop.
  CleanupEngine();
  CleanupNetwork();
  CleanupWindow();
  CleanupSDL();
  ::Log("Application shutdown complete");
}

void ApplicationManager::CleanupNetwork() {
  if (window)
    window->ipcBus = nullptr;
  if (ipcBus) {
    // Joins the transport's IO threads; nothing may Poll() after this.
    ipcBus->Stop();
    ipcBus.reset();
  }
  ipcTransport.reset();
}

void ApplicationManager::CleanupEngine() {
  if (engineProcessId <= 0)
    return;

  // Give an engine that was told to stop the chance to do it cleanly;
  // SIGTERM is the fallback, not the plan.
  if (engineStopRequested) {
    for (int i = 0; i < 40 && engineProcessId > 0; ++i) {
      ReapEngineIfExited();
      if (engineProcessId > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  if (engineProcessId > 0) {
    if (engineStopRequested)
      ::Log("[IlmeeeEditor] Engine did not stop within 2 s; sending SIGTERM",
            Debug::LogLevel::WARNING);
    kill(engineProcessId, SIGTERM);
    waitpid(engineProcessId, nullptr, 0);
    engineProcessId = -1;
  }
}

void ApplicationManager::CleanupWindow() {
  // Window cleanup is handled by VulkanBase
  window = nullptr;
}

void ApplicationManager::CleanupSDL() {
  // SDL_Quit is handled by VulkanBase
}
