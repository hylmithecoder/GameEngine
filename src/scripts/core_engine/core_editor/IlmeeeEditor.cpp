#include "IlmeeeEditor.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <map>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <fstream>
#include <nfd.hpp>
namespace fs = std::filesystem;
namespace IlmeeeEditor {

    // Static member initialization
    std::unique_ptr<Editor> Editor::instance = nullptr;
    std::map<std::string, int> recentlyOpened;
    std::map<std::string, int> projectTypeUsage;

    // ========== Project Implementation ==========
    Project::Project(const std::string& name, const std::string& path, const std::string& version,
                    const std::string& lastModified, const std::string& description, bool isFavorite,
                    int projectType, const ImVec4& accentColor)
        : name(name), path(path), version(version), lastModified(lastModified), 
          description(description), isFavorite(isFavorite), projectType(projectType), 
          accentColor(accentColor) {
    }

    // ========== Debugger Implementation ==========
    void Debugger::LogInfo(const std::string& message) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, 9); // Blue
        std::cout << "[INFO] " << message << std::endl;
        SetConsoleTextAttribute(hConsole, 15); // Reset to default
    }

    void Debugger::LogWarning(const std::string& message) {        
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, 14); // Yellow
        std::cout << "[WARNING] " << message << std::endl;
        SetConsoleTextAttribute(hConsole, 15); // Reset to default
    }

    void Debugger::LogError(const std::string& message) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, 12); // Red
        std::cerr << "[ERROR] " << message << std::endl;
        SetConsoleTextAttribute(hConsole, 15); // Reset to default
    }

    void Debugger::LogSuccess(const std::string& message) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(hConsole, 10); // Green
        std::cout << "[SUCCESS] " << message << std::endl;
        SetConsoleTextAttribute(hConsole, 15); // Reset to default
    }

    // Global logging functions
    void LogInfo(const std::string& message) {
        Debugger::LogInfo(message);
    }

    void LogWarning(const std::string& message) {
        Debugger::LogWarning(message);
    }

    void LogError(const std::string& message) {
        Debugger::LogError(message);
    }

    void LogSuccess(const std::string& message) {
        Debugger::LogSuccess(message);
    } 
    
    void RecommendationSystem::RecordProjectOpen(const std::string& path, int projectType) {
        recentlyOpened[path]++;
        projectTypeUsage[std::to_string(projectType)]++;
    }

    void RecommendationSystem::AddFavorite(const std::string& path) {
        auto it = std::find(favoriteProjects.begin(), favoriteProjects.end(), path);
        if (it == favoriteProjects.end()) {
            favoriteProjects.push_back(path);
        }
    }

    void RecommendationSystem::RemoveFavorite(const std::string& path) {
            auto it = std::find(favoriteProjects.begin(), favoriteProjects.end(), path);
            if (it != favoriteProjects.end()) {
                favoriteProjects.erase(it);
            }
        }

        std::vector<RecommendationSystem::Recommendation> RecommendationSystem::GetRecommendations(const std::vector<std::unique_ptr<Project>>& allProjects) {
            std::vector<Recommendation> recommendations;

            for (const auto& project : allProjects) {
                Recommendation rec;
                rec.projectPath = project->GetPath();
                rec.projectName = project->GetName();
                rec.projectType = project->GetProjectType();
                rec.accentColor = project->GetAccentColor();
                rec.score = CalculateScore(*project);
                rec.reason = GenerateReason(*project);

                if (rec.score > 0.1f) { // Minimum threshold
                    recommendations.push_back(rec);
                }
            }

            // Sort by score (highest first)
            std::sort(recommendations.begin(), recommendations.end(),
                     [](const Recommendation& a, const Recommendation& b) {
                         return a.score > b.score;
                     });

            return recommendations;
        }
    
    float RecommendationSystem::CalculateScore(const Project& project) {
        float score = 0.0f;

        // Favorite projects get highest priority
        if (project.IsFavorite()) {
            score += 1.0f;
        }

        // Recently opened projects
        auto recentIt = recentlyOpened.find(project.GetPath());
        if (recentIt != recentlyOpened.end()) {
           score += 0.5f * std::min(1.0f, recentIt->second / 10.0f);
        }

        // Frequently used project types
        auto typeIt = projectTypeUsage.find(std::to_string(project.GetProjectType()));
        if (typeIt != projectTypeUsage.end()) {
            score += 0.3f * std::min(1.0f, typeIt->second / 20.0f);
        }

        // Recent modification (newer = higher score)
        // This is a simplified version - in real implementation you'd parse the date
        if (!project.GetLastModified().empty()) {
            score += 0.2f;
        }

        return score;
    }
    std::string RecommendationSystem::GenerateReason(const Project& project) {
        if (project.IsFavorite()) {
                return "Favorite project";
        }

        auto recentIt = recentlyOpened.find(project.GetPath());
        if (recentIt != recentlyOpened.end() && recentIt->second > 5) {
            return "Frequently opened";
        }

        if (recentIt != recentlyOpened.end() && recentIt->second > 0) {
            return "Recently opened";
        }

        auto typeIt = projectTypeUsage.find(std::to_string(project.GetProjectType()));
        if (typeIt != projectTypeUsage.end() && typeIt->second > 10) {
            return "Similar to your recent projects";
        }

        return "Suggested for you";
    }

    // ========== Editor Implementation ==========
    struct SceneObject {
        std::string name;
        float x, y, width, height, rotation, scaleX, scaleY;
        std::string spritePath;
    };

    struct SceneData {
        std::string sceneName;
        std::vector<SceneObject> objects;
    };

    SceneData DeserializeScene(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Failed to open file.");

        char magic[8];
        in.read(magic, 8);
        if (strncmp(magic, "ILMEEESC", 8) != 0) {
            throw std::runtime_error("Invalid scene file (magic header mismatch).");
        }

        SceneData scene;

        // Read scene name
        uint8_t sceneNameLen;
        in.read(reinterpret_cast<char*>(&sceneNameLen), 1);
        scene.sceneName.resize(sceneNameLen);
        in.read(&scene.sceneName[0], sceneNameLen);

        // Read object count
        uint8_t objCount;
        in.read(reinterpret_cast<char*>(&objCount), 1);

        for (int i = 0; i < objCount; ++i) {
            SceneObject obj;

            // Read name
            uint8_t nameLen;
            in.read(reinterpret_cast<char*>(&nameLen), 1);
            obj.name.resize(nameLen);
            in.read(&obj.name[0], nameLen);

            // Read floats
            in.read(reinterpret_cast<char*>(&obj.x), sizeof(float));
            in.read(reinterpret_cast<char*>(&obj.y), sizeof(float));
            in.read(reinterpret_cast<char*>(&obj.width), sizeof(float));
            in.read(reinterpret_cast<char*>(&obj.height), sizeof(float));
            in.read(reinterpret_cast<char*>(&obj.rotation), sizeof(float));
            in.read(reinterpret_cast<char*>(&obj.scaleX), sizeof(float));
            in.read(reinterpret_cast<char*>(&obj.scaleY), sizeof(float));

            // Read spritePath
            uint8_t spriteLen;
            in.read(reinterpret_cast<char*>(&spriteLen), 1);
            obj.spritePath.resize(spriteLen);
            in.read(&obj.spritePath[0], spriteLen);

            scene.objects.push_back(obj);
        }

        return scene;
    }
    Editor::Editor() {
        LogInfo("Editor instance created");
    }

    Editor::~Editor() {
        LogInfo("Editor instance destroyed");
    }

    void Editor::Initialize() {
        // LogInfo("Initializing IlmeeeEditor...");
        
        // Initialize ImGui context if not already done
        // This would typically be done in the main application
        LogSuccess("Editor initialized successfully");
    }

    void Editor::Run() {
        LogInfo("Starting editor main loop...");
        
        // Main editor loop would go here
        // This is typically handled by the main application loop
        while (true) {
            // Update all windows
            // for (auto& window : windows) {
            //     if (window && window->IsOpen()) {
            //         sendCommandToEngine("UpdateWindow " + window->GetTitle());
            //         window->Update();
            //         window->Render();
            //     }
            // }
            // std::this_thread::sleep_for(chrono::milliseconds(16));

            // // Remove closed windows
            // windows.erase(
            //     std::remove_if(windows.begin(), windows.end(),
            //         [](const std::unique_ptr<EditorWindow>& window) {
            //             return !window || !window->IsOpen();
            //         }),
            //     windows.end()
            // );

            // Break condition would be implemented based on your needs
            // break; // Placeholder break
        }
    }

    void Editor::Shutdown() {
        LogInfo("Shutting down editor...");
        
        // Clear all windows
        windows.clear();
        
        // Clear all projects
        projects.clear();
        
        LogSuccess("Editor shutdown complete");
    }

    void Editor::DestroyInstance() {
        if (instance) {
            instance->Shutdown();
            instance.reset();
            LogInfo("Editor instance destroyed");
        }
    }

    void Editor::CreateProject(const std::string& name, const std::string& path) {
        LogInfo("Creating new project: " + name + " at " + path);
        
        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        
        // Create new project with default values
        auto project = std::make_unique<Project>(
            name, path, "1.0.0", ss.str(), 
            "New project created with IlmeeeEditor", 
            false, 0, ImVec4(0.2f, 0.4f, 0.8f, 1.0f)
        );
        
        projects.push_back(std::move(project));
        
        // Trigger callback if set
        if (onProjectOpenedCallback) {
            onProjectOpenedCallback();
        }
        
        LogSuccess("Project created successfully");
    }

    void Editor::OpenProject(const std::string& path) {
        LogInfo("Opening project from: " + path);
        
        // Check if project already exists
        for (const auto& project : projects) {
            if (project->GetPath() == path) {
                LogWarning("Project already open: " + path);
                return;
            }
        }
        
        // In a real implementation, you would load project data from file
        // For now, create a dummy project
        std::filesystem::path projectPath(path);
        std::string projectName = projectPath.filename().string();
        
        auto project = std::make_unique<Project>(
            projectName, path, "1.0.0", "2024-01-15 10:30:00",
            "Loaded project", false, 1, ImVec4(0.8f, 0.2f, 0.4f, 1.0f)
        );
        
        projects.push_back(std::move(project));
        
        // Trigger callback if set
        if (onProjectOpenedCallback) {
            onProjectOpenedCallback();
        }
        
        LogSuccess("Project opened successfully");
    }

    void Editor::LoadScene() {
        NFD::Guard nfdGuard;
        NFD::UniquePath outPath;

        // Prepare filters for scene files
        nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeescene"}};

        try {
            // Show open file dialog
            nfdresult_t result = NFD::OpenDialog(outPath, filterItem, 1, projectPath.c_str());
            
            if (result == NFD_OKAY && outPath.get() != nullptr) {
                std::string scenePath = outPath.get();
                
                // Validate file extension
                if (fs::path(scenePath).extension() != ".ilmeescene") {
                    LogError("Invalid scene file format");
                    return;
                }

                // Try to load the scene
                try {
                    SceneData loaded = DeserializeScene(scenePath);

                    // Update editor state or pass to renderer or UI system
                    LogInfo("Scene Loaded: " + loaded.sceneName);
                    for (const auto& obj : loaded.objects) {
                        LogInfo("Loaded Object: " + obj.name + " at (" + std::to_string(obj.x) + ", " + std::to_string(obj.y) + ")");
                        // TODO: Tambahkan ke internal scene renderer atau canvas editor
                    }
                    // Update scene state
                    
                } catch (const std::exception& e) {
                    LogError(std::string("Failed to load scene: ") + e.what());
                }
            } else if (result == NFD_CANCEL) {
                // User cancelled - no need for notification
            } else {

            }
        } catch (const std::exception& e) {
        }
        // Extract just the filename from the full path
    }

    void Editor::CloseProject() {
        if (!projects.empty()) {
            std::string projectName = projects.back()->GetName();
            projects.pop_back();
            LogInfo("Closed project: " + projectName);
        } else {
            LogWarning("No project to close");
        }
    }

    void Editor::AddWindow(std::unique_ptr<EditorWindow> window) {
        if (window) {
            LogInfo("Adding window: " + window->GetTitle());
            windows.push_back(std::move(window));
        }
    }

    void Editor::RemoveWindow(EditorWindow* window) {
        if (window) {
            LogInfo("Removing window: " + window->GetTitle());
            windows.erase(
                std::remove_if(windows.begin(), windows.end(),
                    [window](const std::unique_ptr<EditorWindow>& w) {
                        return w.get() == window;
                    }),
                windows.end()
            );
        }
    }

    void Editor::SetOnProjectOpenedCallback(const std::function<void()>& callback) {
        onProjectOpenedCallback = callback;
        LogInfo("Project opened callback set");
    }

    // ========== Sample Recommendation Window ==========
    void RecommendationWindow::RefreshRecommendations() {
        if (!recommendationSystem || !projectsPtr) return;

        recommendations = recommendationSystem->GetRecommendations(*projectsPtr);
        LogInfo("Recommendations refreshed, count: " + std::to_string(recommendations.size()));
    }
    void RecommendationWindow::Render() {
        if (!ImGui::Begin("Recommendations")) {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Refresh")) {
            RefreshRecommendations();
        }

        if (recommendations.empty()) {
            ImGui::Text("No recommendations available.");
        } else {
            for (const auto& rec : recommendations) {
                ImGui::PushID(rec.projectPath.c_str());
                ImGui::TextColored(rec.accentColor, "%s", rec.projectName.c_str());
                ImGui::Text("Reason: %s", rec.reason.c_str());
                ImGui::Text("Score: %.2f", rec.score);
                if (ImGui::Button("Open Project")) {
                    // Open project logic here
                    LogInfo("Opening project: " + rec.projectPath);
                }
                ImGui::PopID();
            }
        }

        ImGui::End();
    }
    
        bool Editor::connectToEngine() {
            std::cout << "Connecting to engine..." << std::endl;
            return tcpClient.connectToEngine();
        }

        bool Editor::startServer() {
            std::cout << "Starting server..." << std::endl;
            return tcpClient.startServerCore();
        }
        
        bool Editor::sendCommandToEngine(const std::string& command) {
            std::cout << "Sending command to engine: " << command << std::endl;
            return tcpClient.sendMessageToEngine(command);
        }

        std::string Editor::receiveMessageFromEngine() {
            // std::cout << "Receiving message from engine..." << std::endl;
            std::string msg = tcpClient.receiveMessageFromEngine();
            // std::cout << "Message: " << msg << std::endl; 
            return msg;
        }

    // ========== C API Implementation ==========
    extern "C" {
        ILMEEEDITOR_API bool EditorInit(const char* title, int width, int height) {
            try {
                LogInfo(std::string("Initializing editor: ") + title + 
                       " (" + std::to_string(width) + "x" + std::to_string(height) + ")");
                
                if (!Editor::instance) {
                    Editor::instance = std::make_unique<Editor>();
                    Editor::instance->Initialize();
                }
                
                return true;
            } catch (const std::exception& e) {
                LogError(std::string("Failed to initialize editor: ") + e.what());
                return false;
            }
        }

        ILMEEEDITOR_API void EditorRun() {
            try {                
                LogSuccess("Hei This is a method Editor Run You Call");
                if (Editor::instance) {
                    Editor::instance->Run();
                } else {
                    LogError("Editor not initialized. Call EditorInit first.");
                }
            }
            catch (const std::exception& e) {
                LogError(std::string("Failed to run editor: ") + e.what());
            }
        }

        ILMEEEDITOR_API void EditorShutdown() {
            if (Editor::instance) {
                Editor::instance->DestroyInstance();
                LogInfo("Editor shutdown complete");
            }
        }

        ILMEEEDITOR_API bool StartServer() {
            if (Editor::instance) {
                Editor::instance->startServer();
            }
            return true;
        }
        
        ILMEEEDITOR_API bool SendCommandToEngine(const char* command) {
            if (Editor::instance) {
                return Editor::instance->sendCommandToEngine(std::string(command));
            }
            LogError("Editor not initialized. Call EditorInit first.");
            return false;
        }

        ILMEEEDITOR_API bool ConnectToEngine() {
            if (Editor::instance) {
                return Editor::instance->connectToEngine();
            }
            LogError("Editor not initialized. Call EditorInit first.");
            return false;
        }

        ILMEEEDITOR_API string GetCommandFromEngine() {
            // if (Editor::instance) {
                std::string command = Editor::instance->receiveMessageFromEngine();  // Make sure this returns string
                // cout << "Received command from engine: " << command << endl;
                return command;
            // }
            // // LogError("Editor not initialized. Call EditorInit first.");
            // return "Still Empty";
        }

        ILMEEEDITOR_API string TestString() {
            // LogWarning("Hello from libIlmeeeEditor.dll");
            return "Hello from libIlmeeeEditor.dll";
        }

        ILMEEEDITOR_API void LoadScene() {
            if (Editor::instance) {
                Editor::instance->LoadScene();
            }
        }
    }
} // namespace IlmeeeEditor
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            IlmeeeEditor::LogInfo("IlmeeeEditor DLL loaded");
            break;
        case DLL_THREAD_ATTACH: {
            // IlmeeeEditor::LogSuccess("This is runtime DLL");
            // IlmeeeEditor::LogInfo("Received command from engine: " + IlmeeeEditor::GetCommandFromEngine());
            break;
        }
        case DLL_THREAD_DETACH:
            break;
        case DLL_PROCESS_DETACH:
            IlmeeeEditor::LogInfo("IlmeeeEditor DLL unloaded");
            break;
    }
    return TRUE;
}