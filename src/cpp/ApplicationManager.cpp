#include "../../include/ui/Application.hpp"
#include <SDL3/SDL.h>

ApplicationManager::ApplicationManager() {
  networkManager = std::make_unique<NetworkManager>();
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

bool ApplicationManager::LaunchEngine() {
  try {
    ::Log("[IlmeeeEditor] Starting network server...");
    isRunning = true;
    if (!networkManager->startServer()) {
      ::Log("Failed to start network server", Debug::LogLevel::CRASH);
      return false;
    }

    ::Log("[IlmeeeEditor] Launching engine process...");

    pid_t pid = fork();
    if (pid == -1) {
      ::Log("Failed to fork process: " + std::string(strerror(errno)),
            Debug::LogLevel::CRASH);
      return false;
    } else if (pid == 0) {
      execl("./HandlerIlmeeeEngine", "HandlerIlmeeeEngine", "-project",
            "MyGameProject", nullptr);
      exit(1);
    } else {
      engineProcessId = pid;
    }

    // Wait for connection asynchronously
    std::future<bool> connectionFuture = std::async(
        std::launch::async, [this]() { return WaitForServerConnection(30); });

    while (connectionFuture.wait_for(std::chrono::milliseconds(100)) !=
           std::future_status::ready) {
      // Waiting
    }

    if (!connectionFuture.get()) {
      ::Log("Failed to establish connection", Debug::LogLevel::CRASH);
      return false;
    }

    networkManager->sendMessage("init:MyGameProject");
    ::Log("[IlmeeeEditor] Engine launched successfully");

    StartNetworkThread();
    return true;

  } catch (const std::exception &e) {
    ::Log("Exception during engine launch: " + std::string(e.what()),
          Debug::LogLevel::CRASH);
    return false;
  }
}

void ApplicationManager::StartNetworkThread() {
  networkThreadRunning = true;
  networkThread = std::thread([this]() {
    ::Log("[IlmeeeEditor] Network thread started");
    while (networkThreadRunning && isRunning) {
      try {
        std::string message = networkManager->receiveMessage();
        if (!message.empty()) {
          ProcessNetworkMessage(message);
          messagesFrom27015.push(message);
          lastMessageFrom27015 = message;
        }

        if (engineProcessId > 0) {
          int status;
          pid_t result = waitpid(engineProcessId, &status, WNOHANG);
          if (result == engineProcessId) {
            shouldExit = true;
            break;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
      } catch (...) {
        break;
      }
    }
  });
}

void ApplicationManager::ProcessNetworkMessage(const std::string &message) {
  if (message == "shutdown" || message == "exit") {
    shouldExit = true;
  }
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

  if (networkManager && isRunning) {
    try {
      networkManager->sendMessage("Stop");
    } catch (...) {
      // sendMessage bisa throw kalau peer sudah disconnect; jangan
      // halangi shutdown.
    }
  }
  isRunning = false;
  shouldExit = true;

  // Discord update thread harus di-join sebelum unique_ptr destroy
  // DiscordRichPresence, kalau tidak destruktor std::thread bawaan
  // panggil std::terminate.
  if (discordRich) {
    discordRich->Shutdown();
  }

  CleanupNetwork();
  CleanupEngine();
  CleanupWindow();
  CleanupSDL();
  ::Log("Application shutdown complete");
}

void ApplicationManager::CleanupNetwork() {
  networkThreadRunning = false;
  if (networkThread.joinable())
    networkThread.join();

  if (networkManager) {
    // Penting: panggil stop() supaya listenThread_ & receiveThread_
    // internal NetworkManager di-join. Tanpa ini, ~NetworkManager
    // (default-generated) menghancurkan std::thread yang masih
    // joinable → std::terminate → crash di shutdown.
    try {
      networkManager->stop();
    } catch (...) {
      // best-effort; lanjut destroy walaupun gagal
    }
    networkManager.reset();
  }
}

void ApplicationManager::CleanupEngine() {
  if (engineProcessId > 0) {
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