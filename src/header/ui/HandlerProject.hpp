#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <direct.h>
#include <ctime>
#include <chrono>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <iostream>
#include <nfd.hpp>
#include <filesystem>
#include <map>
#include <unordered_map>
#include <imgui.h>
#include <assets.hpp>
#include <stb_image.h>
#include <ranges>
#include <algorithm>
#include <nfd.h>
#include <SceneSerializer.hpp>
#include <SceneRenderer2D.hpp>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
using namespace std;
namespace fs = std::filesystem;

class HandlerProject {
private:
    std::string getCurrentDateTime() {
        time_t now = time(0);
        struct tm tstruct;
        char buf[80];
        localtime_s(&tstruct, &now);
        strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
        return std::string(buf);
    }

    void LoadProjectAssets() {
        // TODO: Implement asset loading
        string texturesPath = projectPath + "/assets/textures";
        string audioPath = projectPath + "/assets/audio";
        string modelsPath = projectPath + "/assets/models";
        string scriptsPath = projectPath + "/assets/scripts";

        // Scan directories and load assets
    }

public:
    HandlerProject()
    {}
    // Class
    Scene currentScene;    
    SceneSerializer serializer;
    std::string currentScenePath;
    SceneRenderer2D* sceneRenderer;
    bool isSceneLoaded = false;
    // Color
    ImVec4 redColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 greenColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImVec4 blueColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
    // This is a reload and open project bool is very core
    bool isOpenedProject = false;
    std::string projectPath;
    std::map<std::string, std::vector<std::string>> assetFiles;
    struct AssetFile {
        std::string name;
        bool isDirectory;
        std::vector<AssetFile> children;
        std::string fullPath;
    };
    AssetFile BuildAssetTree(const std::string& path)
    {
        AssetFile node;
        node.name = fs::path(path).filename().string();
        node.fullPath = path;
        node.isDirectory = fs::is_directory(path);

        if (node.isDirectory)
        {
            for (const auto& entry : fs::directory_iterator(path))
            {
                node.children.push_back(BuildAssetTree(entry.path().string()));
            }
        }

        return node;
    }
    struct Notification {
        std::string title;
        std::string message;
        ImVec4 color;
        float startTime;
        float duration;

        Notification(const std::string& t, const std::string& m, ImVec4 c, float st, float d) 
            : title(t), message(m), color(c), startTime(st), duration(d) {};
    };
    std::vector<Notification> notifications;
    Assets assets;
    TextureData icon_texture_data;
    std::unordered_map<std::string, ImTextureID> iconCache;
    ImTextureID GetCachedIcon(const std::string& path);
    // File monitoring system
    std::thread fileWatcherThread;
    std::atomic<bool> fileWatcherRunning;
    std::chrono::milliseconds fileWatcherInterval;
    std::mutex fileWatcherMutex;
    std::condition_variable fileWatcherCV;
    std::unordered_map<std::string, std::time_t> fileTimestamps;
    bool fileChangesDetected;
    std::string renamingPath = "";
    char renameBuffer[256] = {};
    std::string fileExplorerRenameTarget = ""; // Path folder yang sedang direname
    bool fileExplorerIsRenaming = false; 
    bool fileExplorerRenameBufferSet = false; // Add this variable to fix the issue
    std::string fileExplorerCopyTarget = "";
    std::string fileTargetImport = "";

    void OpenFile();
    void NewProject();
    void OpenFolder();
    void OpenProject(const char* folderPath);
    void DrawAssetTree(const AssetFile& node);
    void ScanAssetsFolder(const std::string& rootFolder);
    void NewScripts(const std::string& name);
    void DeleteFileOrFolder(const std::string& filePathOrFolderPath);
    void OpenFile(const std::string& fileName);
    void ShowNotification(const std::string& title, const std::string& message, ImVec4 color);
    void RenderNotifications();
    void DrawIconFromImage(const char* iconPath);
    // File monitoring methods
    void StartFileWatcher();
    void StopFileWatcher();
    void FileWatcherLoop();
    bool CheckForFileChanges();
    void HandleFileChanges();
    void SnapshotProjectFiles();
    void CheckAndRefreshAssets();
    bool IsFileWatcherRunning() const {
        return fileWatcherRunning;
    }
    void SearchFileOrFolder(const AssetFile& node, const std::string& query, std::vector<AssetFile>& results);
    void NewScene(const std::string& name);
    void HandleCreateNewFile(const std::string &name);
    void HandleRename(const AssetFile& node);
    void DeleteFolder(const std::string& folderPath);
    void HandleCreateNewFolder(const std::string &targetFolder);
    void HandleRenameFolder(const AssetFile& node);
    void HandleCopy(const AssetFile& node);
    void HandlePaste(const std::string& targetFolder);
    void HandleImport(const std::string& targetFile);
    void SaveNewScene();
    void OpenScene();
};