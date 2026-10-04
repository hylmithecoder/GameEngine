// #include <Windows.h>
#include <chrono>
#include <dlfcn.h>
#include <functional>
#include <gtk-3.0/gtk/gtk.h>
#include <iostream>
#include <string>
#include <thread>
// #include <CommCtrl.h>
#include "../../../../include/core_engine/Debugger.hpp"
#include "../../../../include/core_engine/UserDataDir.hpp"
#include "../../../../include/core_engine/net/Protocol.hpp"
#include <atomic>
#include <mutex>
// #include <windowsx.h>
#include <gtk-3.0/gtk/gtkdialog.h>
#include <gtk-3.0/gtk/gtktypes.h>
#include <gtk-3.0/gtk/gtkwidget.h>
#include <gtk-3.0/gtk/gtkwindow.h>
using namespace Debug;
using namespace std;
#pragma comment(lib, "comctl32.lib")

// Function typedefs
typedef bool (*EngineInitFunc)(const char *, int, int);
typedef void (*EngineRunFunc)();
typedef void (*EngineShutdownFunc)();
typedef void (*EngineStopFunc)();
typedef bool (*EditorInitFunc)(const char *, int, int);
typedef void (*EditorRunFunc)();
// Engine IPC, exported by libIlmeeeEditor (see IlmeeeEditor.h).
typedef void (*IpcCallback)(const char *payloadJson, void *user);
typedef bool (*IpcConnectFunc)(const char *url, const char *token);
typedef void (*IpcPollFunc)();
typedef void (*IpcOnFunc)(const char *type, IpcCallback callback, void *user);
typedef void (*IpcDisconnectFunc)();

// Loading window class
class LoadingWindow {
private:
  GtkWidget *window = nullptr;
  GtkWidget *progressBar = nullptr;
  GtkWidget *statusLabel = nullptr;
  GtkWidget *titleLabel = nullptr;
  GtkWidget *versionLabel = nullptr;
  std::thread loadingThread;
  bool isVisible = false;

  // GTK CSS for styling
  const char *css_style = R"(
        .loading-window {
            background: linear-gradient(to bottom, #252729, #181a1c);
            border: 1px solid #464646;
            border-radius: 15px;
        }
        
        .title-label {
            font-family: 'MiSans', sans-serif;
            font-size: 28px;
            font-weight: bold;
            color: white;
        }
        
        .status-label {
            font-family: 'MiSans', sans-serif;
            font-size: 16px;
            color: #cccccc;
        }
        
        .version-label {
            font-family: 'MiSans', sans-serif;
            font-size: 16px;
            color: #888888;
        }
        
        .loading-progress {
            color: #4890e8;
        }
    )";

  static void on_window_destroy(GtkWidget *widget, gpointer data) {
    LoadingWindow *window = static_cast<LoadingWindow *>(data);
    window->isVisible = false;
  }

  void ApplyCSS() {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css_style, -1, nullptr);

    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    g_object_unref(provider);
  }

public:
  LoadingWindow() = default;

  bool Create() {
    // Initialize GTK if not already done
    if (!gtk_init_check(nullptr, nullptr)) {
      cerr << "Failed to initialize GTK" << endl;
      return false;
    }

    // Create main window
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Ilmee Game Engine");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 200);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_decorated(GTK_WINDOW(window), TRUE);

    // Set window icon
    GError *error = nullptr;
    GdkPixbuf *icon =
        gdk_pixbuf_new_from_file("assets/icons/app_icon.png", &error);
    if (icon) {
      gtk_window_set_icon(GTK_WINDOW(window), icon);
      g_object_unref(icon);
    } else if (error) {
      cout << "Warning: Failed to load window icon: " << error->message << endl;
      g_error_free(error);
    }

    // Create main container
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);

    // Create title label
    titleLabel = gtk_label_new("ILMEEE ENGINE");
    gtk_widget_set_halign(titleLabel, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel),
                                "title-label");
    gtk_box_pack_start(GTK_BOX(vbox), titleLabel, FALSE, FALSE, 0);

    // Add some spacing
    GtkWidget *spacer1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(spacer1, -1, 20);
    gtk_box_pack_start(GTK_BOX(vbox), spacer1, FALSE, FALSE, 0);

    // Create status label
    statusLabel = gtk_label_new("Initializing...");
    gtk_widget_set_halign(statusLabel, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(statusLabel),
                                "status-label");
    gtk_box_pack_start(GTK_BOX(vbox), statusLabel, FALSE, FALSE, 0);

    // Create progress bar
    progressBar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progressBar), FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(progressBar),
                                "loading-progress");
    gtk_box_pack_start(GTK_BOX(vbox), progressBar, FALSE, FALSE, 10);

    // Add bottom spacer
    GtkWidget *spacer2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(spacer2, -1, 10);
    gtk_box_pack_start(GTK_BOX(vbox), spacer2, TRUE, TRUE, 0);

    // Create version label
    versionLabel = gtk_label_new("v1.0.0");
    gtk_widget_set_halign(versionLabel, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(versionLabel),
                                "version-label");
    gtk_box_pack_start(GTK_BOX(vbox), versionLabel, FALSE, FALSE, 0);

    // Apply CSS styling
    ApplyCSS();
    gtk_style_context_add_class(gtk_widget_get_style_context(window),
                                "loading-window");

    // Connect destroy signal
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), this);

    return true;
  }

  void Show() {
    if (window) {
      gtk_widget_show_all(window);
      isVisible = true;
    }
  }

  void Hide() {
    if (window) {
      gtk_widget_hide(window);
      isVisible = false;
    }
  }

  void SetProgress(double progress) {
    if (progressBar) {
      // Ensure we're on the main thread for GTK operations
      g_idle_add(
          [](gpointer data) -> gboolean {
            auto *args = static_cast<pair<GtkWidget *, double> *>(data);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(args->first),
                                          args->second);
            delete args;
            return G_SOURCE_REMOVE;
          },
          new pair<GtkWidget *, double>(progressBar, progress));
    }
  }

  void SetStatus(const string &status) {
    if (statusLabel) {
      // Ensure we're on the main thread for GTK operations
      g_idle_add(
          [](gpointer data) -> gboolean {
            auto *args = static_cast<pair<GtkWidget *, string *> *>(data);
            gtk_label_set_text(GTK_LABEL(args->first), args->second->c_str());
            delete args->second;
            delete args;
            return G_SOURCE_REMOVE;
          },
          new pair<GtkWidget *, string *>(statusLabel, new string(status)));
    }
  }

  void Destroy() {
    if (window) {
      gtk_widget_destroy(window);
      window = nullptr;
      isVisible = false;
    }
  }

  bool IsVisible() const { return isVisible; }

  ~LoadingWindow() { Destroy(); }
};

class LibraryManager {
private:
  void *engineLib = nullptr;
  void *editorLib = nullptr;

public:
  const vector<string> IlmeeEngine = {"libIlmeeeEngine.so",
                                      "libIlmeeeEditor.so"};

  // Locate one of our shared libraries. The launcher is started from Hub,
  // from a shell in the repo root, or from an installed bin/, so resolving
  // "lib/<name>" against the working directory only worked by accident; both
  // build/ and the installed bundle put the libraries in ../lib next to bin/.
  static string ResolveLibrary(const string &name) {
    namespace fs = std::filesystem;
    const fs::path exeDir = ilmeee::ExecutableDir();
    std::error_code ec;
    for (const fs::path &candidate :
         {exeDir / ".." / "lib" / name, exeDir / "lib" / name,
          exeDir / name}) {
      if (fs::exists(candidate, ec))
        return fs::weakly_canonical(candidate, ec).string();
    }
    // Nothing found on disk: hand the bare soname to dlopen so RPATH and the
    // usual loader search still get their chance.
    return name;
  }

  bool LoadLibraries() {
    // Load engine library
    string enginePath = ResolveLibrary(IlmeeEngine[0]);
    engineLib = dlopen(enginePath.c_str(), RTLD_LAZY);
    if (!engineLib) {
      ShowError(
          ("Failed to load " + enginePath + ": " + dlerror()).c_str());
      return false;
    }

    // Load editor library
    string editorPath = ResolveLibrary(IlmeeEngine[1]);
    editorLib = dlopen(editorPath.c_str(), RTLD_LAZY);
    if (!editorLib) {
      ShowError(
          ("Failed to load " + editorPath + ": " + dlerror()).c_str());
      Cleanup();
      return false;
    }

    return true;
  }

  template <typename T> T GetFunction(void *lib, const char *functionName) {
    // Clear any existing error
    dlerror();

    void *symbol = dlsym(lib, functionName);
    char *error = dlerror();
    if (error != nullptr) {
      ShowError(
          ("Failed to get function " + string(functionName) + ": " + error)
              .c_str());
      return nullptr;
    }

    return reinterpret_cast<T>(symbol);
  }

  void Cleanup() {
    if (editorLib) {
      dlclose(editorLib);
      editorLib = nullptr;
    }
    if (engineLib) {
      dlclose(engineLib);
      engineLib = nullptr;
    }
  }

  void *GetEngineLib() const { return engineLib; }
  void *GetEditorLib() const { return editorLib; }

  static void ShowError(const char *message) {
    // Always report on stderr as well: the dialog is invisible when the
    // launcher is started by the editor (no one is watching a modal), and a
    // failed load would otherwise leave nothing at all in the log.
    std::cerr << "[HandlerIlmeeeEngine] " << message << std::endl;

    // Use GTK message dialog instead of MessageBox
    GtkWidget *dialog =
        gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                               GTK_BUTTONS_OK, "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), "Ilmee Launcher - Error");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
  }
};

// Utility class for DLL management
// class DLLManager {
// private:
//     HMODULE engineDLL = nullptr;
//     HMODULE editorDLL = nullptr;

// public:
//     const vector<string> IlmeeEngine = {"libIlmeeeEngine.dll",
//     "libIlmeeeEditor.dll"}; bool LoadDLLs() {
//         engineDLL = LoadLibraryA(("bin/" + IlmeeEngine[0]).c_str());
//         if (!engineDLL) {
//             ShowError("Failed to load libIlmeeeEngine.dll");
//             return false;
//         }

//         editorDLL = LoadLibraryA(("bin/" + IlmeeEngine[1]).c_str());
//         if (!editorDLL) {
//             ShowError("Failed to load libIlmeeeEditor.dll");
//             Cleanup();
//             return false;
//         }

//         return true;
//     }

//     template<typename T>
//     T GetFunction(HMODULE dll, const char* functionName) {
//         return reinterpret_cast<T>(GetProcAddress(dll, functionName));
//     }

//     void Cleanup() {
//         if (editorDLL) {
//             FreeLibrary(editorDLL);
//             editorDLL = nullptr;
//         }
//         if (engineDLL) {
//             FreeLibrary(engineDLL);
//             engineDLL = nullptr;
//         }
//     }

//     HMODULE GetEngineDLL() const { return engineDLL; }
//     HMODULE GetEditorDLL() const { return editorDLL; }

//     static void ShowError(const char* message) {
//         MessageBoxA(nullptr, message, "Ilmee Launcher - Error",
//         MB_ICONERROR);
//     }

//     ~DLLManager() {
//         Cleanup();
//     }
// };

// Main loading sequence
class LaunchSequence {
private:
  LoadingWindow &loadingWindow;
  LibraryManager &libManager;

  struct LoadingStep {
    std::string description;
    std::function<bool()> action;
    int progressWeight;
  };

  std::vector<LoadingStep> steps;
  std::atomic<bool> messageThreadRunning{false};
  std::thread messagePollingThread;

  // Where the core is listening; empty when started by hand (no IPC).
  std::string ipcUrl;
  std::string ipcToken;
  EngineStopFunc engineStop = nullptr;
  std::atomic<bool> stopRequested{false};

  // engine.stop from the core, or the core going away: either way the engine
  // loop has to end so this process exits instead of lingering as an orphan.
  static void OnStopRequested(const char *payloadJson, void *user) {
    auto *self = static_cast<LaunchSequence *>(user);
    Log("[HandlerIlmeeeEngine] Stop requested by core: " +
            std::string(payloadJson ? payloadJson : "{}"),
        Debug::LogLevel::INFO);
    self->stopRequested = true;
    if (self->engineStop)
      self->engineStop();
  }

  bool ConnectToCore() {
    if (ipcUrl.empty()) {
      Log("No " + std::string(ilmeee::net::proto::kIpcUrlArg) +
              " given; running without a connection to the editor",
          Debug::LogLevel::WARNING);
      return true;
    }

    auto IpcOn = libManager.GetFunction<IpcOnFunc>(libManager.GetEditorLib(),
                                                   "IpcOn");
    auto IpcConnect = libManager.GetFunction<IpcConnectFunc>(
        libManager.GetEditorLib(), "IpcConnect");
    engineStop = libManager.GetFunction<EngineStopFunc>(
        libManager.GetEngineLib(), "EngineStop");
    if (!IpcOn || !IpcConnect || !engineStop)
      return false; // GetFunction already reported which one.

    IpcOn(ilmeee::net::proto::kEngineStop, &LaunchSequence::OnStopRequested,
          this);
    IpcOn(ilmeee::net::proto::kCoreLost, &LaunchSequence::OnStopRequested,
          this);

    // Returns at once: the client keeps dialling in the background, so there
    // is nothing to wait for here.
    if (!IpcConnect(ipcUrl.c_str(), ipcToken.c_str())) {
      LibraryManager::ShowError("Failed to start the editor connection");
      return false;
    }
    return true;
  }

  // Dispatches IPC messages (handlers run on this thread) and pumps GTK,
  // which the engine-side file dialogs need.
  void RunMessageLoop() {
    Log("Starting message loop...", Debug::LogLevel::INFO);
    auto IpcPoll = libManager.GetFunction<IpcPollFunc>(
        libManager.GetEditorLib(), "IpcPoll");

    while (messageThreadRunning) {
      if (IpcPoll)
        IpcPoll();
      while (messageThreadRunning && g_main_context_iteration(NULL, FALSE)) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

public:
  // Ui loading sequence
  LaunchSequence(LoadingWindow &window, LibraryManager &dllManager,
                 std::string url, std::string token)
      : loadingWindow(window), libManager(dllManager), ipcUrl(std::move(url)),
        ipcToken(std::move(token)) {

    // Define loading steps
    steps = {
        {"Loading Engine DLL...",
         [&]() {
           std::this_thread::sleep_for(std::chrono::milliseconds(800));
           return libManager.LoadLibraries();
         },
         20},
        {"Initializing Engine...",
         [&]() {
           Log("Initializing Engine...");
           auto Init = libManager.GetFunction<EngineInitFunc>(
               libManager.GetEngineLib(), "EngineInit");
           if (!Init) {
             LibraryManager::ShowError("Failed to find EngineInit function");
             return false;
           }
           std::this_thread::sleep_for(std::chrono::milliseconds(1200));
           return Init("My First Project", 1280, 720);
         },
         30},
        {"Initializing Editor...",
         [&]() {
           Log("Initializing Editor...");
           auto InitEditor = libManager.GetFunction<EditorInitFunc>(
               libManager.GetEditorLib(), "EditorInit");
           if (!InitEditor) {
             LibraryManager::ShowError("Failed to find EditorInit function");
             return false;
           }
           std::this_thread::sleep_for(std::chrono::milliseconds(1000));
           return InitEditor("Ilmee Editor", 1280, 720);
         },
         25},
        {"Connecting to Ilmeee Editor...",
         [&]() {
           Log("Connecting to Ilmeee Editor...");
           return ConnectToCore();
         },
         25}};
  }

  bool Execute() {
    int totalProgress = 0;
    int currentProgress = 0;

    // Calculate total weight
    for (const auto &step : steps) {
      totalProgress += step.progressWeight;
    }

    for (const auto &step : steps) {
      // Update status
      loadingWindow.SetStatus(step.description);

      // Execute step
      if (!step.action()) {
        return false;
      }

      // Update progress
      currentProgress += step.progressWeight;
      int percentage = (currentProgress * 100) / totalProgress;
      loadingWindow.SetProgress(percentage);
    }

    loadingWindow.SetStatus("Launch Complete!");
    loadingWindow.SetProgress(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    Log("Launch Complete!", Debug::LogLevel::SUCCESS);

    messageThreadRunning = true;
    messagePollingThread = std::thread(&LaunchSequence::RunMessageLoop, this);
    return true;
  }

  // True once the core asked us to stop (possibly before the engine loop
  // even started, in which case there is no point starting it).
  bool StopRequested() const { return stopRequested; }

  // Joins the message thread, then closes the IPC connection. Call before
  // the libraries go away.
  void Shutdown() {
    messageThreadRunning = false;
    if (messagePollingThread.joinable())
      messagePollingThread.join();
    if (!ipcUrl.empty() && libManager.GetEditorLib()) {
      auto IpcDisconnect = libManager.GetFunction<IpcDisconnectFunc>(
          libManager.GetEditorLib(), "IpcDisconnect");
      if (IpcDisconnect)
        IpcDisconnect();
    }
  }

  ~LaunchSequence() {
    Shutdown();
    std::cout << "Destroying LaunchSequence" << std::endl;
  }
};
