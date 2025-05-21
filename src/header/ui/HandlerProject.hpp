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
        ScanAssetsFolder(projectPath);
    }

public:
    HandlerProject()
    {}
    // Class
    Scene currentScene;    
    SceneSerializer serializer;
    std::string currentScenePath;
    bool isSceneLoaded = false;
    // Color
    ImVec4 redColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 greenColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
    ImVec4 blueColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
    // This is a reload and open project bool is very core
    bool isOpenedProject = false;
    ImVec2 thumbnailSize = ImVec2(32, 32);
    // Waktu double click
    float doubleClickTime = 0.3f; // dalam detik
    // Menyimpan state expand/collapse untuk setiap folder
    std::unordered_map<std::string, bool> folderStates;
    float itemSpacing = 8.0f;
    std::string projectPath;
    std::map<std::string, std::vector<std::string>> assetFiles;
    struct AssetFile {
        std::string name;
        bool isDirectory;
        bool isSelected = false;
        std::vector<AssetFile> children;
        std::string fullPath;

        // Timestamp untuk operasi drag & drop
        float lastClickTime = 0.0f;
        
        // Konstruktor
        AssetFile(const std::string& n, const std::string& p, bool isDir = false)
            : name(n), fullPath(p), isDirectory(isDir) {}
    };
    AssetFile BuildAssetTree(const std::string& path)
    {
        // Create AssetFile directly instead of using pointer
        AssetFile node(
            fs::path(path).filename().string(),
            path,
            fs::is_directory(path)
        );

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

    struct IconInfo {
        ImTextureID textureId;
        int width;
        int height;
    };
    std::unordered_map<std::string, IconInfo> iconCacheInfo;
    IconInfo LoadCachedTexture(const string& pathIcon);
    
    IconInfo GetIconForFile(const AssetFile& node) {
        std::string path = "assets/images/fileicons/";
        if (node.isDirectory) {
            path += "folder.png";
        } else {
            std::string ext = fs::path(node.name).extension().string();
            if (ext == ".cpp" || ext == ".hpp") path += "c-.png";
            else if (ext == ".png" || ext == ".jpg") path += "image.png";
            else if (ext == ".fbx" || ext == ".obj") path += "model.png";
            else if (ext == ".prefab") path += "prefab.png";
            else if (ext == ".ilmeescene" || ext == ".unity") path += "scene.png";
            else path += "file.png";
        }

        return LoadCachedTexture(path); // caching disarankan
    }

    // Asset yang dipilih saat ini
    AssetFile* selectedAsset;
    // Callback untuk menangani klik file
    std::function<void(const AssetFile&)> onFileClicked;
    // Favorit folder
    std::vector<std::string> favoriteFolders;
    std::string currentFilter = "";
    void DrawFolderGridView(const std::vector<AssetFile>& files, const std::string& currentPath);
    void DrawBreadcrumbs(const std::string& path);
    void DrawSearchBar(const std::string& path);
    void DrawNavigationBar();
    void DrawQuickAccessPanel();

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
    void DrawIconFromImage(const char* iconPath, int width, int height);
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
    // Hint tanda "&" itu ngambil dari referensi 
    void DrawFileExplorer(const AssetFile& assetFolder);
};