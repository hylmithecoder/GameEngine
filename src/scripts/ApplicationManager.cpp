#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <string>
#include <MainWindow.hpp>
#include <NetworkManager.hpp>
#include "ui/Check_Environment.cpp"
#include <Debugger.hpp>

class ApplicationManager {
private:
    std::unique_ptr<NetworkManager> networkManager;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<Environment> environment;
    PROCESS_INFORMATION engineProcess = {0};
    std::atomic<bool> isRunning{false};
    std::atomic<bool> shouldExit{false};
    std::vector<std::function<void()>> cleanupTasks;
    
    // Thread management
    std::thread networkThread;
    std::thread messageProcessorThread;
    std::atomic<bool> networkThreadRunning{false};

    std::mutex messagesMutex;
    static const size_t MAX_MESSAGES = 1000; // Limit buffer size
    
public:
    ApplicationManager() {
        networkManager = std::make_unique<NetworkManager>();
        environment = std::make_unique<Environment>();
        
        // Register cleanup tasks in reverse order of initialization
        RegisterCleanupTask([this]() { CleanupWindow(); });
        RegisterCleanupTask([this]() { CleanupNetwork(); });
        RegisterCleanupTask([this]() { CleanupEngine(); });
        RegisterCleanupTask([this]() { CleanupSDL(); });
    }
    
    ~ApplicationManager() {
        Shutdown();
    }
    
    void RegisterCleanupTask(std::function<void()> task) {
        cleanupTasks.insert(cleanupTasks.begin(), task);
    }
    
    bool Initialize() {
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
            window = std::make_unique<MainWindow>("Ilmeee Editor", 1280, 720);
            if (!window) {
                Debug::Logger::Log("Failed to create main window", Debug::LogLevel::CRASH);
                return false;
            }
            
            // Print working directory
            char cwd[512];
            if (_getcwd(cwd, sizeof(cwd))) {
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
    
    bool LaunchEngine() {
        try {
            Debug::Logger::Log("[IlmeeeEditor] Starting network server...");
            isRunning = true;
            if (!networkManager->startServer()) {
                Debug::Logger::Log("Failed to start network server", Debug::LogLevel::CRASH);
                MessageBoxA(0, "Failed to start network server", "Error", MB_OK | MB_ICONERROR);
                return false;
            }
            // In your engine initialization
            if (!networkManager->connectToServer()) {
                Debug::Logger::Log("Failed to connect to editor", Debug::LogLevel::CRASH);
                return false;
            }
            
            Debug::Logger::Log("[IlmeeeEditor] Launching engine process...");
            STARTUPINFOA si = { sizeof(si) };
            std::string command = "HandlerIlmeeeEngine.exe -project MyGameProject";
            
            BOOL result = CreateProcessA(
                nullptr, 
                const_cast<char*>(command.c_str()), 
                nullptr, nullptr, FALSE, 
                CREATE_NEW_PROCESS_GROUP, // Allow proper termination
                nullptr, nullptr, &si, &engineProcess
            );
            
            if (!result) {
                DWORD error = GetLastError();
                Debug::Logger::Log("Failed to launch engine. Error code: " + std::to_string(error), Debug::LogLevel::CRASH);
                MessageBoxA(0, "Failed to launch HandlerIlmeeeEngine.exe", "Error", MB_OK | MB_ICONERROR);
                return false;
            }
            
            // Wait for connection establishment
            Debug::Logger::Log("[IlmeeeEditor] Waiting for engine connection...");
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Send initial configuration
            networkManager->sendMessage("init:MyGameProject");
            Debug::Logger::Log("[IlmeeeEditor] Engine launched successfully");
            
            // Start network processing thread
            StartNetworkThread();
            
            return true;
            
        } catch (const std::exception& e) {
            Debug::Logger::Log("Exception during engine launch: " + std::string(e.what()), Debug::LogLevel::CRASH);
            return false;
        }
    }
    
    void StartNetworkThread() {
        networkThreadRunning = true;
        networkThread = std::thread([this]() {
            Debug::Logger::Log("[IlmeeeEditor] Network thread started");
            
            while (networkThreadRunning && isRunning) {
            try {
                // Debug::Logger::Log("Wait for network message...");
                std::string message = networkManager->receiveMessage();
                if (!message.empty()) {
                    Debug::Logger::Log("Received: " + message);
                    ProcessNetworkMessage(message);
                    
                    // Push message to UI with thread safety
                    {
                        // std::lock_guard<std::mutex> lock(messagesMutex);
                        // window->PushMessage(message);
                    }
                }
                
                // Check engine process
                if (engineProcess.hProcess) {
                    DWORD exitCode;
                    if (GetExitCodeProcess(engineProcess.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                        Debug::Logger::Log("Engine process has terminated unexpectedly", Debug::LogLevel::CRASH);
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
    
    void ProcessNetworkMessage(const std::string& message) {
        if (message == "shutdown" || message == "exit") {
            Debug::Logger::Log("Received shutdown command from engine");
            shouldExit = true;
        }
        // Add more message processing as needed
    }
    
    void Run() {
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
                
                // Update and render
                window->update();
                window->render();
                
                // Check engine process status
                if (engineProcess.hProcess) {
                    DWORD exitCode;
                    if (GetExitCodeProcess(engineProcess.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                        Debug::Logger::Log("Engine process has terminated");
                        shouldExit = true;
                    }
                }
                
            } catch (const std::exception& e) {
                Debug::Logger::Log("Exception in main loop: " + std::string(e.what()), Debug::LogLevel::CRASH);
                shouldExit = true;
            }
        }
        
        Debug::Logger::Log("Main application loop ended");
    }
    
    void Shutdown() {
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
    
private:
    // Update CleanupNetwork method
    void CleanupNetwork() {
        Debug::Logger::Log("Cleaning up network...");
        
        // Stop network thread first
        networkThreadRunning = false;
        if (networkThread.joinable()) {
            try {
                networkThread.join();
                Debug::Logger::Log("Network thread joined successfully");
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
    void CleanupEngine() {
        Debug::Logger::Log("Cleaning up engine process...");
        
        if (engineProcess.hProcess) {
            // First try graceful shutdown
            DWORD exitCode;
            if (GetExitCodeProcess(engineProcess.hProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                // Send termination signal
                GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetProcessId(engineProcess.hProcess));
                
                // Wait for process to end
                DWORD waitResult = WaitForSingleObject(engineProcess.hProcess, 2000);
                if (waitResult == WAIT_TIMEOUT) {
                    Debug::Logger::Log("Engine not responding to graceful shutdown, force terminating...", Debug::LogLevel::WARNING);
                    TerminateProcess(engineProcess.hProcess, 1);
                }
            }
            
            // Ensure handles are closed
            CloseHandle(engineProcess.hProcess);
            CloseHandle(engineProcess.hThread);
            memset(&engineProcess, 0, sizeof(engineProcess));
            Debug::Logger::Log("Engine process cleaned up");
        }
    }

    // Update CleanupWindow method
    void CleanupWindow() {
        Debug::Logger::Log("Cleaning up window...");
        if (window) {
            window->clean(); // Add a cleanup method to MainWindow if not exists
            window.reset();
            window = nullptr;
            Debug::Logger::Log("Window cleaned up");
        }
    }

    // Update CleanupSDL method
    void CleanupSDL() {
        Debug::Logger::Log("Cleaning up SDL...");
        TTF_Quit();
        SDL_Quit();
        // IMG_Quit(); // Add if using SDL_image
        Debug::Logger::Log("SDL cleaned up");
    }
};