#pragma once

#if defined(_WIN32) || defined(_WIN64)
#ifdef ILMEEEDITOR_EXPORTS
#define ILMEEEDITOR_API __declspec(dllexport)
#else
#define ILMEEEDITOR_API __declspec(dllimport)
#endif
#else
#ifdef ILMEEEDITOR_EXPORTS
#define ILMEEEDITOR_API __attribute__((visibility("default")))
#else
#define ILMEEEDITOR_API
#endif
#endif

#include <SDL3/SDL.h>
#include <functional>
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
using namespace std;

namespace ilmeee::net {
class MessageBus;
class WsClientTransport;
} // namespace ilmeee::net

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

  // Engine IPC client: connects to the core (GameEngineSDL), the only server.
  std::unique_ptr<ilmeee::net::WsClientTransport> ipcTransport;
  std::unique_ptr<ilmeee::net::MessageBus> ipcBus;
  // Host-registered listeners (IpcOn), keyed by message type. Payloads are
  // handed over as JSON text so the C API stays C.
  std::map<std::string, std::vector<std::function<void(const std::string &)>>>
      ipcListeners;
  void ForwardIpcType(const std::string &type);
  void NotifyIpcListeners(const std::string &type, const std::string &payload);

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
  void CreateProject(const std::string &name, const std::string &path);
  void LoadScene();
  void OpenProject(const std::string &path);
  void CloseProject();

  // Window management
  void AddWindow(std::unique_ptr<EditorWindow> window);
  void RemoveWindow(EditorWindow *window);

  // Callbacks
  void SetOnProjectOpenedCallback(const std::function<void()> &callback);

  // Engine IPC. ConnectIpc() returns immediately; the transport keeps
  // dialling until the core answers. Everything is dispatched in PollIpc(),
  // on the thread that calls it.
  bool ConnectIpc(const std::string &url, const std::string &token);
  void PollIpc();
  bool IsIpcReady() const;
  bool EmitIpc(const std::string &type, const std::string &payloadJson);
  void OnIpc(const std::string &type,
             std::function<void(const std::string &payloadJson)> callback);
  void DisconnectIpc();

  string projectPath;
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
  Project(const std::string &name, const std::string &path,
          const std::string &version, const std::string &lastModified,
          const std::string &description, bool isFavorite, int projectType,
          const ImVec4 &accentColor);

  // Getters
  const std::string &GetName() const { return name; }
  const std::string &GetPath() const { return path; }
  const std::string &GetVersion() const { return version; }
  const std::string &GetLastModified() const { return lastModified; }
  const std::string &GetDescription() const { return description; }
  bool IsFavorite() const { return isFavorite; }
  int GetProjectType() const { return projectType; }
  ImVec4 GetAccentColor() const { return accentColor; }
};

class ILMEEEDITOR_API Debugger {
public:
  static void LogInfo(const std::string &message);
  static void LogWarning(const std::string &message);
  static void LogError(const std::string &message);
  static void LogSuccess(const std::string &message);
};

// ========== EditorWindow Base Class ==========
class ILMEEEDITOR_API EditorWindow {
protected:
  std::string title;
  bool isOpen;
  ImVec2 size;
  ImVec2 position;

public:
  EditorWindow(const std::string &title)
      : title(title), isOpen(true), size(800, 600), position(0, 0) {}
  virtual ~EditorWindow() = default;

  virtual void Render() = 0;
  virtual void Update() {}

  bool IsOpen() const { return isOpen; }
  void Close() { isOpen = false; }
  const std::string &GetTitle() const { return title; }
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

  void RecordProjectOpen(const std::string &path, int projectType);

  void AddFavorite(const std::string &path);

  void RemoveFavorite(const std::string &path);

  std::vector<Recommendation>
  GetRecommendations(const std::vector<std::unique_ptr<Project>> &allProjects);

  float CalculateScore(const Project &project);
  std::string GenerateReason(const Project &project);
};

class RecommendationWindow : public EditorWindow {
private:
  RecommendationSystem *recommendationSystem;
  std::vector<RecommendationSystem::Recommendation> recommendations;
  std::vector<std::unique_ptr<Project>> *projectsPtr;

public:
  RecommendationWindow(RecommendationSystem *recSys,
                       std::vector<std::unique_ptr<Project>> *projects)
      : EditorWindow("Project Recommendations"), recommendationSystem(recSys),
        projectsPtr(projects) {
    RefreshRecommendations();
  }

  void RefreshRecommendations();

  void Render() override;
};

ILMEEEDITOR_API void LogInfo(const std::string &message);
ILMEEEDITOR_API void LogWarning(const std::string &message);
ILMEEEDITOR_API void LogError(const std::string &message);
ILMEEEDITOR_API void LogSuccess(const std::string &message);

extern "C" {
ILMEEEDITOR_API bool EditorInit(const char *title, int width, int height);
ILMEEEDITOR_API void EditorRun();
ILMEEEDITOR_API void EditorShutdown();
ILMEEEDITOR_API void LoadScene();
ILMEEEDITOR_API void SetProjectPath(string &path);

// Engine IPC (see core_engine/net/Protocol.hpp for the message catalog).
// `payloadJson` is a JSON object as text; null means {}.
typedef void (*IpcCallback)(const char *payloadJson, void *user);
ILMEEEDITOR_API bool IpcConnect(const char *url, const char *token);
ILMEEEDITOR_API void IpcPoll();
ILMEEEDITOR_API bool IpcIsReady();
ILMEEEDITOR_API bool IpcEmit(const char *type, const char *payloadJson);
ILMEEEDITOR_API void IpcOn(const char *type, IpcCallback callback, void *user);
ILMEEEDITOR_API void IpcDisconnect();
}
} // namespace IlmeeeEditor