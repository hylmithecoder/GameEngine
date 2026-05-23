#include <iostream>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <string>
#include "MainWindow.hpp"
#include "../core_engine/NetworkManager.hpp"
#include "../core_engine/Check_Environment.hpp"
#include "../core_engine/Debugger.hpp"
#include "../core_engine/Discordrich.hpp"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <future>

using namespace Debug;
using namespace core_engine;

class ApplicationManager {
private:
    std::unique_ptr<NetworkManager> networkManager;
    MainWindow* window;
    string lastMessageFrom27015;
    std::unique_ptr<Environment> environment;
    std::unique_ptr<DiscordRichPresence> discordRich;
    pid_t engineProcessId = -1;
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
        Log("Waiting for server connection...");
        
        auto startTime = std::chrono::steady_clock::now();
        bool connected = false;
            
        while (!connected) {
            try {
                // Try to connect
                if (networkManager->connectToServer()) {
                    Log("Successfully connected to server", Debug::LogLevel::SUCCESS);
                    return true;
                }
                    
                // Check timeout
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsedSeconds = std::chrono::duration_cast<std::chrono::seconds>
                    (currentTime - startTime).count();
                    
                if (elapsedSeconds >= timeoutSeconds) {
                    Log("Connection timeout after " + 
                        std::to_string(timeoutSeconds) + " seconds", Debug::LogLevel::CRASH);
                    return false;
                }
                    
                // Update status every second
                if (elapsedSeconds % 5 == 0) {
                    Log("Waiting for server... " + 
                        std::to_string(timeoutSeconds - elapsedSeconds) + " seconds remaining");
                }
                    
                // Small delay before next attempt
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    
            } catch (const std::exception& e) {
                Log("Connection attempt failed: " + 
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

    // Set the project path before Initialize() so the editor opens
    // straight into the given project. When empty (no --project arg),
    // the editor falls back to its hardcoded debug scene.
    void SetProjectPath(const std::string& path) { projectPath = path; }
    const std::string& GetProjectPath() const { return projectPath; }

    // When true and no --project was supplied, the standalone debug
    // fallback boots a 2D sprite scene instead of the 3D OBJ. Has no
    // effect when a project is loaded.
    void SetDebug2D(bool v) { debug2D = v; }
    bool GetDebug2D() const { return debug2D; }

private:
    std::string projectPath;
    bool debug2D = false;
};