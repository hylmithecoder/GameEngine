#include "../../include/ui/Application.hpp"
#include <SDL3/SDL.h>

ApplicationManager::ApplicationManager() {
  networkManager = std::make_unique<NetworkManager>();
  environment = std::make_unique<Environment>();
  discordRich = std::make_unique<DiscordRichPresence>();

  // Register cleanup tasks in reverse order of initialization
  RegisterCleanupTask([this]() { CleanupWindow(); });
  RegisterCleanupTask([this]() { 
    if (discordRich) discordRich->Shutdown(); 
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
  if (!isRunning)
    return;

  networkManager->sendMessage("Stop");
  ::Log("Starting application shutdown...");
  isRunning = false;
  shouldExit = true;

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
  if (networkManager)
    networkManager.reset();
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