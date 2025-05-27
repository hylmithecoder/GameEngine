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
    Editor::Editor() {
        LogInfo("Editor instance created");
    }

    Editor::~Editor() {
        LogInfo("Editor instance destroyed");
    }

    void Editor::Initialize() {
        LogInfo("Initializing IlmeeeEditor...");
        
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
            for (auto& window : windows) {
                if (window && window->IsOpen()) {
                    window->Update();
                    window->Render();
                }
            }

            // Remove closed windows
            windows.erase(
                std::remove_if(windows.begin(), windows.end(),
                    [](const std::unique_ptr<EditorWindow>& window) {
                        return !window || !window->IsOpen();
                    }),
                windows.end()
            );

            // Break condition would be implemented based on your needs
            break; // Placeholder break
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
    // ========== C API Implementation ==========
    // extern "C" {
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
            if (Editor::instance) {
                Editor::instance->Run();
            } else {
                LogError("Editor not initialized. Call EditorInit first.");
            }
        }

        ILMEEEDITOR_API void EditorShutdown() {
            if (Editor::instance) {
                Editor::instance->DestroyInstance();
                LogInfo("Editor shutdown complete");
            }
        }
    // }

} // namespace IlmeeeEditor