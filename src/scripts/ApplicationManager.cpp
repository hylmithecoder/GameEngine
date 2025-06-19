#include <Application.hpp>

ApplicationManager::ApplicationManager() {
    networkManager = std::make_unique<NetworkManager>();
    environment = std::make_unique<Environment>();
        
    // Register cleanup tasks in reverse order of initialization
    RegisterCleanupTask([this]() { CleanupWindow(); });
    RegisterCleanupTask([this]() { CleanupNetwork(); });
    RegisterCleanupTask([this]() { CleanupEngine(); });
    RegisterCleanupTask([this]() { CleanupSDL(); });
}
    
ApplicationManager::~ApplicationManager() {
    Shutdown();
}

bool ApplicationManager::Initialize() {
    try {
        Debug::Logger::Log("Initializing Application Manager...");
            
        // Initialize environment check
        environment->detectDriveInfo();
        environment->printEnvironment();
        environment->printDriveInfo();
            
        // Set FFmpeg log level
        av_log_set_level(AV_LOG_ERROR);
            
        // Initialize SDL
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
            Debug::Logger::Log("SDL initialization failed: " + std::string(SDL_GetError()), Debug::LogLevel::CRASH);
            return false;
        }
            
        // Initialize TTF
        if (TTF_Init() == -1) {
            Debug::Logger::Log("TTF initialization failed: " + std::string(TTF_GetError()), Debug::LogLevel::CRASH);
            return false;
        }
            
        // Create main window
        window = new MainWindow("Ilmeee Editor", 1280, 720);
        if (!window) {
            Debug::Logger::Log("Failed to create main window", Debug::LogLevel::CRASH);
            return false;
        }
            
        // Print working directory
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd))) {
            Debug::Logger::Log("Working Directory: " + std::string(cwd));
        }
            
        // isRunning = true;
        Debug::Logger::Log("Application Manager initialized successfully");
        return true;
            
    } catch (const std::exception& e) {
        Debug::Logger::Log("Exception during initialization: " + std::string(e.what()), Debug::LogLevel::CRASH);
        return false;
    }
}
    
bool ApplicationManager::LaunchEngine() {
    try {
        Debug::Logger::Log("[IlmeeeEditor] Starting network server...");
        isRunning = true;
        if (!networkManager->startServer()) {
            Debug::Logger::Log("Failed to start network server", Debug::LogLevel::CRASH);
            return false;
        }
        
        Debug::Logger::Log("[IlmeeeEditor] Launching engine process...");
        
        pid_t pid = fork();
        if (pid == -1) {
            // Fork failed
            Debug::Logger::Log("Failed to fork process: " + std::string(strerror(errno)), Debug::LogLevel::CRASH);
            return false;
        }
        else if (pid == 0) {
            // Child process
            execl("./HandlerIlmeeeEngine", "HandlerIlmeeeEngine", "-project", "MyGameProject", nullptr);
            // If execl returns, it failed
            exit(1);
        }
        else {
            // Parent process
            engineProcessId = pid;
        }

        // Wait for connection asynchronously
        std::future<bool> connectionFuture = std::async(std::launch::async, 
            [this]() { return WaitForServerConnection(30); });
        
        // Show connection status
        while (connectionFuture.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
            // Waiting for connection
        }
        
        if (!connectionFuture.get()) {
            Debug::Logger::Log("Failed to establish connection", Debug::LogLevel::CRASH);
            return false;
        }
        
        networkManager->sendMessage("init:MyGameProject");
        Debug::Logger::Log("[IlmeeeEditor] Engine launched successfully");
        
        StartNetworkThread();
        
        return true;
        
    } catch (const std::exception& e) {
        Debug::Logger::Log("Exception during engine launch: " + std::string(e.what()), Debug::LogLevel::CRASH);
        return false;
    }
}
    
    void ApplicationManager::StartNetworkThread() {
        networkThreadRunning = true;
        networkThread = std::thread([this]() {
            Debug::Logger::Log("[IlmeeeEditor] Network thread started");
            
            while (networkThreadRunning && isRunning) {
            try {
                // Debug::Logger::Log("Wait for network message...");
                std::string message = networkManager->receiveMessage();
                if (!message.empty()) {
                    // Debug::Logger::Log("Received: " + message, Debug::LogLevel::SUCCESS);
                    ProcessNetworkMessage(message);
                    messagesFrom27015.push(message);
                    // Debug::Logger::Log("Count Message From 27015: " + to_string(messagesFrom27015.size()), Debug::LogLevel::SUCCESS);
                    // Debug::Logger::Log("Message From 27015: " + messagesFrom27015.back(), Debug::LogLevel::WARNING);
                    lastMessageFrom27015 = message;
                    // Push message to UI with thread safety
                    // std::lock_guard<std::mutex> lock(messagesMutex);
                    // window->PushMessage(message);
                    // window->currentMessageFrom27015 = message;
                    // Debug::Logger::Log("Pushed message to UI: " + window->currentMessageFrom27015, Debug::LogLevel::SUCCESS);
                }
                
                // Check engine process
                if (engineProcessId > 0) {
                    int status;
                    pid_t result = waitpid(engineProcessId, &status, WNOHANG);
                    
                    if (result == engineProcessId) {
                        // Process has terminated
                        if (WIFEXITED(status)) {
                            Debug::Logger::Log("Engine process has terminated with exit code: " + 
                                std::to_string(WEXITSTATUS(status)), Debug::LogLevel::CRASH);
                        } else if (WIFSIGNALED(status)) {
                            Debug::Logger::Log("Engine process was terminated by signal: " + 
                                std::to_string(WTERMSIG(status)), Debug::LogLevel::CRASH);
                        }
                        shouldExit = true;
                        break;
                    } else if (result == -1) {
                        Debug::Logger::Log("Error checking engine process status: " + 
                            std::string(strerror(errno)), Debug::LogLevel::CRASH);
                        shouldExit = true;
                        break;
                    }
                }
                
                // Heartbeat logic
                static auto lastHeartbeat = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastHeartbeat).count() >= 5) {
                    networkManager->sendMessage("heartbeat");
                    lastHeartbeat = now;
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            } catch (const std::exception& e) {
                Debug::Logger::Log("Network thread exception: " + std::string(e.what()), Debug::LogLevel::WARNING);
                break;
            }
        }
            
            Debug::Logger::Log("Network thread ended");
        });
    }
    
    void ApplicationManager::ProcessNetworkMessage(const std::string& message) {
        if (message == "shutdown" || message == "exit") {
            Debug::Logger::Log("Received shutdown command from engine");
            shouldExit = true;
        }
        // Add more message processing as needed
    }
    
    void ApplicationManager::Run() {
        if (!isRunning || !window) {
            Debug::Logger::Log("Cannot run - application not properly initialized", Debug::LogLevel::CRASH);
            return;
        }

        string message = networkManager->receiveMessage();
        if (!message.empty()) {
            Debug::Logger::Log("Received initial message: " + message);
        } else {
            Debug::Logger::Log("No initial message received from network");
        }
        
        Debug::Logger::Log("Starting main application loop...", Debug::LogLevel::SUCCESS);
        
        // This is the main application loop
        while (window->running() && isRunning && !shouldExit) {
            try {
                // Handle window events
                window->handleEvents();
                
                // Check if window was closed
                if (!window->running()) {
                    Debug::Logger::Log("Window close requested");
                    break;
                }

                if (!lastMessageFrom27015.empty())
                {
                    window->PushMessage(lastMessageFrom27015);
                    lastMessageFrom27015.clear();
                }
                
                // Update and render
                window->update();
                window->render();
                
                // Check engine process status
                if (engineProcessId > 0) {
                    int status;
                    pid_t result = waitpid(engineProcessId, &status, WNOHANG);
                    
                    if (result == engineProcessId) {
                        // Process has terminated
                        Debug::Logger::Log("Engine process has terminated");
                        shouldExit = true;
                    } else if (result == -1) {
                        // Error occurred
                        Debug::Logger::Log("Error checking engine process status: " + std::string(strerror(errno)), Debug::LogLevel::WARNING);
                    }
                }
                
            } catch (const std::exception& e) {
                Debug::Logger::Log("Exception in main loop: " + std::string(e.what()), Debug::LogLevel::CRASH);
                shouldExit = true;
            }
        }
        
        Debug::Logger::Log("Main application loop ended");
    }
    
    void ApplicationManager::Shutdown() {
        if (!isRunning) return;
        
        networkManager->sendMessage("Stop");
        Debug::Logger::Log("Starting application shutdown...");
        isRunning = false;
        shouldExit = true;
        
        // Execute all cleanup tasks
        for (auto& task : cleanupTasks) {
            try {
                task();
            } catch (const std::exception& e) {
                Debug::Logger::Log("Exception during cleanup: " + std::string(e.what()), Debug::LogLevel::WARNING);
            }
        }

        // Execute cleanup tasks in specific order
        CleanupNetwork();
        // CleanupEngine();
        CleanupWindow();
        CleanupSDL();
        Debug::Logger::Log("Application shutdown complete");
    }

    // Update CleanupNetwork method
    void ApplicationManager::CleanupNetwork() {
        Debug::Logger::Log("Cleaning up network...");
        
        // Stop network thread first
        networkThreadRunning = false;
        if (networkThread.joinable()) {
            try {
                networkThread.join();
                Debug::Logger::Log("Network thread joined successfully", Debug::LogLevel::SUCCESS);
            } catch (const std::exception& e) {
                Debug::Logger::Log("Failed to join network thread: " + std::string(e.what()), Debug::LogLevel::WARNING);
            }
        }
        
        // Then cleanup network manager
        if (networkManager) {
            try {
                networkManager->stop();
                networkManager.reset();
                Debug::Logger::Log("Network manager cleaned up successfully");
            } catch (const std::exception& e) {
                Debug::Logger::Log("Failed to cleanup network manager: " + std::string(e.what()), Debug::LogLevel::WARNING);
            }
        }
    }

    // Update CleanupEngine method
void ApplicationManager::CleanupEngine() {
    Debug::Logger::Log("Cleaning up engine process...");
    
    if (engineProcessId > 0) {
        // First try graceful shutdown
        kill(engineProcessId, SIGTERM);
        
        // Wait for process to end
        int status;
        pid_t result = waitpid(engineProcessId, &status, WNOHANG);
        
        if (result == 0) {
            // Process still running, wait a bit then force kill
            sleep(2);
            kill(engineProcessId, SIGKILL);
            waitpid(engineProcessId, nullptr, 0);
        }
        
        engineProcessId = -1;
        Debug::Logger::Log("Engine process cleaned up");
    }
}

    // Update CleanupWindow method
    void ApplicationManager::CleanupWindow() {
        Debug::Logger::Log("Cleaning up window...");
        if (window) {
            window->clean(); // Add a cleanup method to MainWindow if not exists
            window = nullptr;
            Debug::Logger::Log("Window cleaned up");
        }
    }

    // Update CleanupSDL method
    void ApplicationManager::CleanupSDL() {
        Debug::Logger::Log("Cleaning up SDL...");
        TTF_Quit();
        SDL_Quit();
        // IMG_Quit(); // Add if using SDL_image
        Debug::Logger::Log("SDL cleaned up");
    }