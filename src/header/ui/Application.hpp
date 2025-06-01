#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <string>
#include <MainWindow.hpp>
#include <NetworkManager.hpp>
#include <Check_Environment.cpp>
#include <Debugger.hpp>
#include <future>

class ApplicationManager {
private:
    std::unique_ptr<NetworkManager> networkManager;
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<Environment> environment;
    PROCESS_INFORMATION engineProcess = {0};
    std::atomic<bool> isRunning{false};
    std::atomic<bool> shouldExit{false};
    std::vector<std::function<void()>> cleanupTasks;
    queue<string> messagesFrom27015;
    
    // Thread management
    std::thread networkThread;
    std::thread messageProcessorThread;
    std::atomic<bool> networkThreadRunning{false};

    std::mutex messagesMutex;
    static const size_t MAX_MESSAGES = 1000; // Limit buffer size
    bool WaitForServerConnection(int timeoutSeconds = 30)
    {
        Debug::Logger::Log("Waiting for server connection...");
        
        auto startTime = std::chrono::steady_clock::now();
        bool connected = false;
            
        while (!connected) {
            try {
                // Try to connect
                if (networkManager->connectToServer()) {
                    Debug::Logger::Log("Successfully connected to server", Debug::LogLevel::SUCCESS);
                    return true;
                }
                    
                // Check timeout
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>
                    (currentTime - startTime).count();
                    
                if (elapsedSeconds >= timeoutSeconds) {
                    Debug::Logger::Log("Connection timeout after " + 
                        std::to_string(timeoutSeconds) + " seconds", Debug::LogLevel::CRASH);
                    return false;
                }
                    
                // Update status every second
                if (elapsedSeconds % 5 == 0) {
                    Debug::Logger::Log("Waiting for server... " + 
                        std::to_string(timeoutSeconds - elapsedSeconds) + " seconds remaining");
                }
                    
                // Small delay before next attempt
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    
            } catch (const std::exception& e) {
                Debug::Logger::Log("Connection attempt failed: " + 
                    std::string(e.what()), Debug::LogLevel::WARNING);
            }
        }
            
        return false;
    };
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
    void StartNetworkThread();
    void ProcessNetworkMessage(const std::string& message);
    void Shutdown();
};