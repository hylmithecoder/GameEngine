#pragma once
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>
// #include <direct.h>
#include "../audio/FFmpegWrapper.hpp"
#include "../core_engine/SceneSerializer.hpp"
#include "assets.hpp"
#include "core_engine/Debugger.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <map>
#include <mutex>
#include <nfd.hpp>
#include <ranges>
#include <stb/stb_image.h>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#include <shellapi.h>
#include <windows.h>
#endif
using namespace std;
namespace fs = filesystem;
using namespace Debug;

class VulkanHandler;

class HandlerProject {
private:
  string getCurrentDateTime() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    localtime_r(&now, &tstruct);
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return string(buf);
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
  HandlerProject() {}
  // Class
  Scene currentScene;
  SceneSerializer serializer;
  string currentScenePath;
  string currentSceneName;
  bool isSceneLoaded = false;
  // Color
  ImVec4 redColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
  ImVec4 greenColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
  ImVec4 blueColor = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
  // This is a reload and open project bool is very core
  bool isOpenedProject = false;
  ImVec2 thumbnailSize = ImVec2(96, 96);
  // Waktu double click
  float doubleClickTime = 0.3f; // dalam detik
  // Menyimpan state expand/collapse untuk setiap folder
  unordered_map<string, bool> folderStates;
  float itemSpacing = 8.0f;
  string projectPath;
  map<string, vector<string>> assetFiles;
  struct AssetFile {
    string name;
    bool isDirectory;
    bool isSelected = false;
    vector<AssetFile> children;
    string fullPath;

    // Timestamp untuk operasi drag & drop
    float time = 0.0f;
    float lastClickTime = -0.2f;

    // Konstruktor
    AssetFile() : name(""), fullPath(""), isDirectory(false) {}
    AssetFile(const string &n, const string &p, bool isDir = false)
        : name(n), fullPath(p), isDirectory(isDir) {}
  };
  AssetFile rootAsset;
  AssetFile BuildAssetTree(const string &path) {
    // Create AssetFile directly instead of using pointer
    AssetFile node(fs::path(path).filename().string(), path,
                   fs::is_directory(path));

    if (node.isDirectory) {
      for (const auto &entry : fs::directory_iterator(path)) {
        node.children.push_back(BuildAssetTree(entry.path().string()));
      }
    }

    return node;
  }
  vector<AssetFile> GetFilesInDirectory(const string &path) {
    vector<AssetFile> result;

    if (!fs::is_directory(path)) {
      // DEBUG_LOGF("Bukan direktori: %s", LogLevel::ERROR, path.c_str());
      return result;
    }

    for (const auto &entry : fs::directory_iterator(path)) {
      string name = entry.path().filename().string();
      string full = entry.path().string();
      bool isDir = entry.is_directory();
      AssetFile file(name, full, isDir);
      result.push_back(file);
    }

    return result;
  }

  struct Notification {
    string title;
    string message;
    ImVec4 color;
    float startTime;
    float duration;

    Notification(const string &t, const string &m, ImVec4 c, float st, float d)
        : title(t), message(m), color(c), startTime(st), duration(d) {};
  };

  struct IconInfo {
    ImTextureID textureId;
    int width;
    int height;
  };
  IconInfo GenerateVideoThumbnail(const string &videoPath);
  unordered_map<string, IconInfo> iconCacheInfo;
  IconInfo LoadCachedTexture(const string &pathIcon);

  IconInfo GetIconForFile(const AssetFile &node) {
    string path = "assets/images/fileicons/";
    if (node.isDirectory) {
      path += "folder.png";
    } else {
      string ext = fs::path(node.name).extension().string();
      if (ext == ".cpp" || ext == ".hpp")
        path += "c-.png";
      else if (ext == ".png" || ext == ".jpg" || ext == ".webp" ||
               ext == ".jpeg")
        path = node.fullPath;
      else if (ext == ".mp4" || ext == ".avi" || ext == ".mov" ||
               ext == ".mkv") {
        // Generate thumbnail for video files
        auto cachedThumbnail = iconCacheInfo.find(node.fullPath);
        if (cachedThumbnail != iconCacheInfo.end()) {
          return cachedThumbnail->second;
        }
        IconInfo thumbnail = GenerateVideoThumbnail(node.fullPath);
        iconCacheInfo[node.fullPath] = thumbnail;
        return thumbnail;
      } else if (ext == ".fbx" || ext == ".obj")
        path += "file.png";
      else if (ext == ".prefab")
        path += "file.png";
      else if (ext == ".ilmeeescene" || ext == ".unity")
        path += "file.png";
      else
        path += "file.png";
    }

    return LoadCachedTexture(path);
  }
  bool IsFrameValid(const AVFrame *frame, int width, int height);
  float CalculateColorVariance(const uint8_t *data, int width, int height);
  void FlipImageVertically(unsigned char *data, int width, int height,
                           int channels);
  // Asset yang dipilih saat ini
  AssetFile *selectedAsset;
  // Callback untuk menangani klik file
  function<void(const AssetFile &)> onFileClicked;
  // Favorit folder
  vector<string> favoriteFolders;
  string currentFilter = "";
  string currentDirectory = "";
  void DrawFolderGridView();
  void DrawBreadcrumbs(const string &path);
  void DrawSearchBar(const string &path);
  void DrawNavigationBar();
  void DrawQuickAccessPanel();
  void HandlerOpenFileWithExtensionName(AssetFile &currentNode);

  struct SceneObject {
    string name;
    float x, y, width, height, rotation, scaleX, scaleY;
    string spritePath;
    int parentId = -1;    // -1 kalau root
    vector<int> children; // index ke child objects
  };

  void WriteBinaryScene(const string &fullPath, const string &sceneName,
                        const vector<SceneObject> &objects) {
    ofstream out(fullPath, ios::binary);
    if (!out)
      return;

    // Write magic header
    out.write("ILMEEESC", 8);

    // Write scene name
    uint8_t sceneLen = static_cast<uint8_t>(sceneName.size());
    out.write(reinterpret_cast<const char *>(&sceneLen), 1);
    out.write(sceneName.data(), sceneLen);

    // Write object count
    uint8_t count = static_cast<uint8_t>(objects.size());
    out.write(reinterpret_cast<const char *>(&count), 1);

    for (const auto &obj : objects) {
      uint8_t nameLen = static_cast<uint8_t>(obj.name.size());
      out.write(reinterpret_cast<const char *>(&nameLen), 1);
      out.write(obj.name.data(), nameLen);

      out.write(reinterpret_cast<const char *>(&obj.x), sizeof(float));
      out.write(reinterpret_cast<const char *>(&obj.y), sizeof(float));
      out.write(reinterpret_cast<const char *>(&obj.width), sizeof(float));
      out.write(reinterpret_cast<const char *>(&obj.height), sizeof(float));
      out.write(reinterpret_cast<const char *>(&obj.rotation), sizeof(float));
      out.write(reinterpret_cast<const char *>(&obj.scaleX), sizeof(float));
      out.write(reinterpret_cast<const char *>(&obj.scaleY), sizeof(float));

      uint8_t spriteLen = static_cast<uint8_t>(obj.spritePath.size());
      out.write(reinterpret_cast<const char *>(&spriteLen), 1);
      out.write(obj.spritePath.data(), spriteLen);
    }

    out.close();
  }

  vector<Notification> notifications;
  Assets assets;
  TextureData icon_texture_data;
  unordered_map<string, ImTextureID> iconCache;
  ImTextureID GetCachedIcon(const string &path);
  // File monitoring system
  thread fileWatcherThread;
  atomic<bool> fileWatcherRunning;
  chrono::milliseconds fileWatcherInterval;
  mutex fileWatcherMutex;
  condition_variable fileWatcherCV;
  unordered_map<string, time_t> fileTimestamps;
  bool fileChangesDetected;
  string renamingPath = "";
  char renameBuffer[256] = {};
  string fileExplorerRenameTarget = ""; // Path folder yang sedang direname
  bool fileExplorerIsRenaming = false;
  bool fileExplorerRenameBufferSet =
      false; // Add this variable to fix the issue
  string fileExplorerCopyTarget = "";
  string fileTargetImport = "";

  void OpenFile();
  void OpenFolder();
  void OpenProject(const char *folderPath);
  void DrawAssetTree(const AssetFile &node);
  void ScanAssetsFolder(const string &rootFolder);
  void NewScripts(const string &name);
  void DeleteFileOrFolder(const string &filePathOrFolderPath);
  void OpenFile(const string &fileName);
  void ShowNotification(const string &title, const string &message,
                        ImVec4 color);
  void RenderNotifications();
  void DrawIconFromImage(const char *iconPath, int width, int height);
  // File monitoring methods
  void StartFileWatcher();
  void StopFileWatcher();
  void FileWatcherLoop();
  bool CheckForFileChanges();
  void HandleFileChanges();
  void SnapshotProjectFiles();
  void CheckAndRefreshAssets();
  bool IsFileWatcherRunning() const { return fileWatcherRunning; }
  void SearchFileOrFolder(const AssetFile &node, const string &query,
                          vector<AssetFile> &results);
  void NewScene(const string &name);
  void HandleCreateNewFile(const string &name);
  void HandleRename(const AssetFile &node);
  void DeleteFolder(const string &folderPath);
  void HandleCreateNewFolder(const string &targetFolder);
  void HandleRenameFolder(const AssetFile &node);
  void HandleCopy(const AssetFile &node);
  void HandlePaste(const string &targetFolder);
  void HandleImport(const string &targetFile);
  void SaveNewScene();
  void OpenScene();
  // Hint tanda "&" itu ngambil dari referensi
  void DrawFileExplorer(AssetFile &assetFolder);
  void HandleRenameFileOrFolder(const AssetFile &node);
  void HandleRenameOperation(AssetFile &node, const ImVec2 &cursorPos,
                             float itemWidth, float itemHeight);
  void SaveAsScene();

  void SetVulkanHandler(VulkanHandler *handler) { vulkanHandler = handler; }

private:
  VulkanHandler *vulkanHandler = nullptr;
};