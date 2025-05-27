#pragma once

#ifdef ILMEEEDITOR_EXPORTS
#define ILMEEEDITOR_API __declspec(dllexport)
#else
#define ILMEEEDITOR_API __declspec(dllimport)
#endif

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <imgui.h>
#include <glad/glad.h>
#include <SDL.h>
#include <imgui_impl_sdl2.h>
#include <TCPConnection.hpp>

namespace IlmeeeEditor {

    // Forward declarations
    class Project;
    class EditorWindow;

    // Editor Class
    class ILMEEEDITOR_API Editor {
    private:
        std::vector<std::unique_ptr<Project>> projects;
        std::vector<std::unique_ptr<EditorWindow>> windows;
        std::function<void()> onProjectOpenedCallback;
        TCPConnection tcpClient;

    public:
        static std::unique_ptr<Editor> instance;
        Editor();
        ~Editor();

        // Lifecycle methods
        void Initialize();
        void Run();
        void Shutdown();
        void DestroyInstance();

        // Project management
        void CreateProject(const std::string& name, const std::string& path);
        void OpenProject(const std::string& path);
        void CloseProject();
        
        // Window management
        void AddWindow(std::unique_ptr<EditorWindow> window);
        void RemoveWindow(EditorWindow* window);
        
        // Callbacks
        void SetOnProjectOpenedCallback(const std::function<void()>& callback);
    
        bool connectToEngine() {
            return tcpClient.connectToServer();
        }
        
        bool sendCommandToEngine(const std::string& command) {
            return tcpClient.sendMessage(command);
        }
    };
    
    class ILMEEEDITOR_API Project {
    private:
        std::string name;
        std::string path;
        std::string version;
        std::string lastModified;
        std::string description;
        bool isFavorite;
        int projectType;
        ImVec4 accentColor;
    public:
        Project(const std::string& name, const std::string& path, const std::string& version,
                const std::string& lastModified, const std::string& description, bool isFavorite,
                int projectType, const ImVec4& accentColor);
        
        // Getters
        const std::string& GetName() const { return name; }
        const std::string& GetPath() const { return path; }
        const std::string& GetVersion() const { return version; }
        const std::string& GetLastModified() const { return lastModified; }
        const std::string& GetDescription() const { return description; }
        bool IsFavorite() const { return isFavorite; }
        int GetProjectType() const { return projectType; }
        ImVec4 GetAccentColor() const { return accentColor; }
    };

    class ILMEEEDITOR_API Debugger {
    public:
        static void LogInfo(const std::string& message);
        static void LogWarning(const std::string& message);
        static void LogError(const std::string& message);
        static void LogSuccess(const std::string& message);
    };

    // ========== EditorWindow Base Class ==========
    class ILMEEEDITOR_API EditorWindow {
    protected:
        std::string title;
        bool isOpen;
        ImVec2 size;
        ImVec2 position;

    public:
        EditorWindow(const std::string& title) : title(title), isOpen(true), size(800, 600), position(0, 0) {}
        virtual ~EditorWindow() = default;

        virtual void Render() = 0;
        virtual void Update() {}

        bool IsOpen() const { return isOpen; }
        void Close() { isOpen = false; }
        const std::string& GetTitle() const { return title; }
    };

    // ========== Recommendation System ==========
    class ILMEEEDITOR_API RecommendationSystem {
    private:

    public:
        std::vector<std::string> favoriteProjects;
        struct Recommendation {
            std::string projectPath;
            std::string projectName;
            std::string reason;
            float score;
            int projectType;
            ImVec4 accentColor;
        };

        void RecordProjectOpen(const std::string& path, int projectType);

        void AddFavorite(const std::string& path);

        void RemoveFavorite(const std::string& path);


        std::vector<Recommendation> GetRecommendations(const std::vector<std::unique_ptr<Project>>& allProjects);

        float CalculateScore(const Project& project);
        std::string GenerateReason(const Project& project);
    };

    class RecommendationWindow : public EditorWindow {
    private:
        RecommendationSystem* recommendationSystem;
        std::vector<RecommendationSystem::Recommendation> recommendations;
        std::vector<std::unique_ptr<Project>>* projectsPtr;

    public:
        RecommendationWindow(RecommendationSystem* recSys, std::vector<std::unique_ptr<Project>>* projects)
            : EditorWindow("Project Recommendations"), recommendationSystem(recSys), projectsPtr(projects) {
            RefreshRecommendations();
        }

        void RefreshRecommendations();

        void Render() override;
    };

    ILMEEEDITOR_API void LogInfo(const std::string& message);
    ILMEEEDITOR_API void LogWarning(const std::string& message);
    ILMEEEDITOR_API void LogError(const std::string& message);
    ILMEEEDITOR_API void LogSuccess(const std::string& message);

    extern "C" {
        ILMEEEDITOR_API bool EditorInit(const char* title, int width, int height);
        ILMEEEDITOR_API void EditorRun();
        ILMEEEDITOR_API void EditorShutdown();
        ILMEEEDITOR_API bool StartServer();
        ILMEEEDITOR_API bool SendCommandToEngine(const char* command);
        ILMEEEDITOR_API bool ConnectToEngine();
    }
} // namespace GameEditor