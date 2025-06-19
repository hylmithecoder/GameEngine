// #include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <gtk-3.0/gtk/gtk.h>
#include <dlfcn.h>
// #include <CommCtrl.h>
#include <atomic>
#include <Debugger.hpp>
#include <mutex>
// #include <windowsx.h>
#include <queue>
#include <condition_variable>
#include <gtk-3.0/gtk/gtktypes.h>
#include <gtk-3.0/gtk/gtkwindow.h>
#include <gtk-3.0/gtk/gtkdialog.h>
#include <gtk-3.0/gtk/gtkwidget.h>
using namespace Debug;
using namespace std;
#pragma comment(lib, "comctl32.lib")

// Function typedefs
typedef bool (*EngineInitFunc)(const char*, int, int);
typedef void (*EngineRunFunc)();
typedef void (*EngineShutdownFunc)();
typedef bool (*EditorInitFunc)(const char*, int, int);
typedef void (*EditorRunFunc)();
typedef bool (*StartServerFunc)();
typedef bool (*ConnectToEngineFunc)();
typedef bool (*SendCommandToEngineFunc)(const char*);
typedef string (*GetCommandFunc)();
typedef string (*SetProjectPathFunc)();
typedef void (*ExecuteCommandFunc)();

// Loading window class
class LoadingWindow {
private:
    GtkWidget* window = nullptr;
    GtkWidget* progressBar = nullptr;
    GtkWidget* statusLabel = nullptr;
    GtkWidget* titleLabel = nullptr;
    GtkWidget* versionLabel = nullptr;
    std::thread loadingThread;
    bool isVisible = false;
    
    // GTK CSS for styling
    const char* css_style = R"(
        .loading-window {
            background: linear-gradient(to bottom, #252729, #181a1c);
            border: 1px solid #464646;
            border-radius: 15px;
        }
        
        .title-label {
            font-family: 'Ubuntu', sans-serif;
            font-size: 28px;
            font-weight: bold;
            color: white;
        }
        
        .status-label {
            font-family: 'Ubuntu', sans-serif;
            font-size: 16px;
            color: #cccccc;
        }
        
        .version-label {
            font-family: 'Ubuntu', sans-serif;
            font-size: 16px;
            color: #888888;
        }
        
        .loading-progress {
            color: #4890e8;
        }
    )";
    
    static void on_window_destroy(GtkWidget* widget, gpointer data) {
        LoadingWindow* window = static_cast<LoadingWindow*>(data);
        window->isVisible = false;
    }
    
    void ApplyCSS() {
        GtkCssProvider* provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider, css_style, -1, nullptr);
        
        GtkStyleContext* context = gtk_widget_get_style_context(window);
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
        GError* error = nullptr;
        GdkPixbuf* icon = gdk_pixbuf_new_from_file("assets/icons/app_icon.png", &error);
        if (icon) {
            gtk_window_set_icon(GTK_WINDOW(window), icon);
            g_object_unref(icon);
        } else if (error) {
            cout << "Warning: Failed to load window icon: " << error->message << endl;
            g_error_free(error);
        }
        
        // Create main container
        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_container_add(GTK_CONTAINER(window), vbox);
        gtk_container_set_border_width(GTK_CONTAINER(vbox), 20);
        
        // Create title label
        titleLabel = gtk_label_new("ILMEE ENGINE");
        gtk_widget_set_halign(titleLabel, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "title-label");
        gtk_box_pack_start(GTK_BOX(vbox), titleLabel, FALSE, FALSE, 0);
        
        // Add some spacing
        GtkWidget* spacer1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_size_request(spacer1, -1, 20);
        gtk_box_pack_start(GTK_BOX(vbox), spacer1, FALSE, FALSE, 0);
        
        // Create status label
        statusLabel = gtk_label_new("Initializing...");
        gtk_widget_set_halign(statusLabel, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(statusLabel), "status-label");
        gtk_box_pack_start(GTK_BOX(vbox), statusLabel, FALSE, FALSE, 0);
        
        // Create progress bar
        progressBar = gtk_progress_bar_new();
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progressBar), FALSE);
        gtk_style_context_add_class(gtk_widget_get_style_context(progressBar), "loading-progress");
        gtk_box_pack_start(GTK_BOX(vbox), progressBar, FALSE, FALSE, 10);
        
        // Add bottom spacer
        GtkWidget* spacer2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_size_request(spacer2, -1, 10);
        gtk_box_pack_start(GTK_BOX(vbox), spacer2, TRUE, TRUE, 0);
        
        // Create version label
        versionLabel = gtk_label_new("v1.0.0");
        gtk_widget_set_halign(versionLabel, GTK_ALIGN_CENTER);
        gtk_style_context_add_class(gtk_widget_get_style_context(versionLabel), "version-label");
        gtk_box_pack_start(GTK_BOX(vbox), versionLabel, FALSE, FALSE, 0);
        
        // Apply CSS styling
        ApplyCSS();
        gtk_style_context_add_class(gtk_widget_get_style_context(window), "loading-window");
        
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
            g_idle_add([](gpointer data) -> gboolean {
                auto* args = static_cast<pair<GtkWidget*, double>*>(data);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(args->first), args->second);
                delete args;
                return G_SOURCE_REMOVE;
            }, new pair<GtkWidget*, double>(progressBar, progress));
        }
    }
    
    void SetStatus(const string& status) {
        if (statusLabel) {
            // Ensure we're on the main thread for GTK operations
            g_idle_add([](gpointer data) -> gboolean {
                auto* args = static_cast<pair<GtkWidget*, string*>*>(data);
                gtk_label_set_text(GTK_LABEL(args->first), args->second->c_str());
                delete args->second;
                delete args;
                return G_SOURCE_REMOVE;
            }, new pair<GtkWidget*, string*>(statusLabel, new string(status)));
        }
    }
    
    void Destroy() {
        if (window) {
            gtk_widget_destroy(window);
            window = nullptr;
            isVisible = false;
        }
    }
    
    bool IsVisible() const {
        return isVisible;
    }
    
    ~LoadingWindow() {
        Destroy();
    }
};


class LibraryManager {
private:
    void* engineLib = nullptr;
    void* editorLib = nullptr;
    
public:
    const vector<string> IlmeeEngine = {"libIlmeeeEngine.so", "libIlmeeeEditor.so"};
    
    bool LoadLibraries() {
        // Load engine library
        string enginePath = "lib/" + IlmeeEngine[0];
        engineLib = dlopen(enginePath.c_str(), RTLD_LAZY);
        if (!engineLib) {
            ShowError(("Failed to load " + IlmeeEngine[0] + ": " + dlerror()).c_str());
            return false;
        }
        
        // Load editor library
        string editorPath = "lib/" + IlmeeEngine[1];
        editorLib = dlopen(editorPath.c_str(), RTLD_LAZY);
        if (!editorLib) {
            ShowError(("Failed to load " + IlmeeEngine[1] + ": " + dlerror()).c_str());
            Cleanup();
            return false;
        }
        
        return true;
    }
    
    template<typename T>
    T GetFunction(void* lib, const char* functionName) {
        // Clear any existing error
        dlerror();
        
        void* symbol = dlsym(lib, functionName);
        char* error = dlerror();
        if (error != nullptr) {
            ShowError(("Failed to get function " + string(functionName) + ": " + error).c_str());
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
    
    void* GetEngineLib() const { return engineLib; }
    void* GetEditorLib() const { return editorLib; }
    
    static void ShowError(const char* message) {
        // Use GTK message dialog instead of MessageBox
        GtkWidget* dialog = gtk_message_dialog_new(
            nullptr,
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_OK,
            "%s", message
        );
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
//     const vector<string> IlmeeEngine = {"libIlmeeeEngine.dll", "libIlmeeeEditor.dll"};
//     bool LoadDLLs() {
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
//         MessageBoxA(nullptr, message, "Ilmee Launcher - Error", MB_ICONERROR);
//     }
    
//     ~DLLManager() {
//         Cleanup();
//     }
// };

// Main loading sequence
class LaunchSequence {
private:
    LoadingWindow& loadingWindow;
    // DLLManager& dllManager;
    LibraryManager& libManager;
    
    struct LoadingStep {
        std::string description;
        std::function<bool()> action;
        int progressWeight;
    };
    
    std::vector<LoadingStep> steps;
    std::atomic<bool> messageThreadRunning{false};
    std::thread messagePollingThread;
    std::queue<std::string> messageQueue;
    std::mutex queueMutex;

    std::atomic<bool> isProcessingMessage{false};
    std::condition_variable messageCV;
    std::mutex processingMutex;

    void RunMessageLoop() {
        Debug::Logger::Log("Starting message loop...", Debug::LogLevel::INFO);
        messageThreadRunning = true;

        auto SendCommand = libManager.GetFunction<SendCommandToEngineFunc>(
            libManager.GetEditorLib(), 
            "SendCommandToEngine"
        );

        auto GetReceiveCommand = libManager.GetFunction<GetCommandFunc>(
            libManager.GetEditorLib(),
            "GetCommandFromEngine"
        );

        // auto RunEditor = dllManager.GetFunction<EditorRunFunc>(
        //     dllManager.GetEditorDLL(), 
        //     "EditorRun"
        // );
        // RunEditor();

        // auto LoadScene = dllManager.GetFunction<ExecuteCommandFunc>(
        //     dllManager.GetEditorDLL(), 
        //     "LoadScene"
        // );
        // LoadScene();

        if (!SendCommand || !GetReceiveCommand) {
            Debug::Logger::Log("Failed to initialize message functions", Debug::LogLevel::CRASH);
            return;
        }

        std::atomic<bool> processingMessage{false};
        
        while (messageThreadRunning) {
            try {
                if (!processingMessage) {
                    std::string message = GetReceiveCommand();
                    if (!message.empty()) {
                        processingMessage = true;
                        
                        std::thread([this, message, &processingMessage]() {
                            try {
                                std::lock_guard<std::mutex> lock(queueMutex);
                                messageQueue.push(message);
                                Debug::Logger::Log("[HandlerIlmeeeEngine] Received: " + message, Debug::LogLevel::INFO);
                                
                                // Check if message contains directory info
                                // if (message.find("C:/") || message.find("E:/") != string::npos || message.find("F:/")) {
                                //     auto SetProjectPath = dllManager.GetFunction<ExecuteCommandFunc>(
                                //         dllManager.GetEditorDLL(),
                                //         "SetProjectPath"
                                //     );
                                //     Debug::Logger::Log("Current path now: "+message);
                                //     if (SetProjectPath) {
                                //         // Extract path from message
                                //         std::string path = message;
                                //         SetProjectPath();
                                //         Debug::Logger::Log("Set project path: " + path, Debug::LogLevel::INFO);
                                //     }
                                // } else 
                                if (message == "E:\\Game Engine Folder\\My First Project") {
                                    auto SetProjectPath = libManager.GetFunction<SetProjectPathFunc>(
                                        libManager.GetEditorLib(),
                                        "SetProjectPath"
                                    );
                                    if (SetProjectPath)
                                    {
                                        Debug::Logger::Log("Set project path: " + message, Debug::LogLevel::SUCCESS);
                                        SetProjectPath();
                                    }
                                }
                                if (message == "LoadScene") {
                                    Debug::Logger::Log("Processing LoadScene command...");
                                    auto HandlerMethodExecute = libManager.GetFunction<ExecuteCommandFunc>(
                                        libManager.GetEditorLib(),
                                        message.c_str()
                                    );
                                    
                                    if (HandlerMethodExecute) {
                                        HandlerMethodExecute();
                                    }
                                }
                                
                                processingMessage = false;
                            } catch (const std::exception& e) {
                                Debug::Logger::Log("Message processing error: " + std::string(e.what()), 
                                    Debug::LogLevel::CRASH);
                                processingMessage = false;
                            }
                        }).detach();
                    }
                }

                // Heartbeat check
                static auto lastHeartbeat = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastHeartbeat).count() >= 5) {
                    if (SendCommand) {
                        // SendCommand("Info Heartbeat");
                    }
                    lastHeartbeat = now;
                }

                // Process Windows messages
                // MSG msg;
                // while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                //     TranslateMessage(&msg);
                //     DispatchMessage(&msg);
                //     if (msg.message == WM_QUIT) {
                //         messageThreadRunning = false;
                //         break;
                //     }
                // }
                while (g_main_context_iteration(NULL, FALSE)) {
                    // Process all pending GTK events
                    if (!messageThreadRunning) {
                        break;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            } catch (const std::exception& e) {
                Debug::Logger::Log("Message loop error: " + std::string(e.what()), 
                    Debug::LogLevel::CRASH);
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
    }
    
    void StopMessagePolling() {
        messageThreadRunning = false;
        if (messagePollingThread.joinable()) {
            messagePollingThread.join();
        }
    }
    
public:
    // Ui loading sequence
    LaunchSequence(LoadingWindow& window, LibraryManager& dllManager) 
        : loadingWindow(window), libManager(dllManager) {
        
        // Define loading steps
        steps = {
            {"Loading Engine DLL...", [&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                return libManager.LoadLibraries(); 
            }, 20},            
            {"Initializing Engine...", [&]() {
                Debug::Logger::Log("Initializing Engine...");
                auto Init = libManager.GetFunction<EngineInitFunc>(libManager.GetEngineLib(), "EngineInit");
                if (!Init) {
                    LibraryManager::ShowError("Failed to find EngineInit function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                return Init("My First Project", 1280, 720);
                Debug::Logger::Log("Engine initialized", Debug::LogLevel::SUCCESS);
            }, 30},
            {"Initializing Editor...", [&]() {
                Debug::Logger::Log("Initializing Editor...");
                auto InitEditor = libManager.GetFunction<EditorInitFunc>(libManager.GetEditorLib(), "EditorInit");
                if (!InitEditor) {
                    LibraryManager::ShowError("Failed to find EditorInit function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                return InitEditor("Ilmee Editor", 1280, 720);
                Debug::Logger::Log("Editor initialized", Debug::LogLevel::SUCCESS);
            }, 25},           
            {"Starting Editor Server...", [&]() {
                Debug::Logger::Log("Starting Editor Server...");
                StartServerFunc StartServer = libManager.GetFunction<StartServerFunc>(libManager.GetEditorLib(), "StartServer");
                if (!StartServer) {
                    LibraryManager::ShowError("Failed to find StartServer function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                return StartServer();
                // Debug::Logger::Log("Editor Server started", Debug::LogLevel::SUCCESS);
            }, 15},            
            {"Connecting Editor to Engine...", [&]() {
                Debug::Logger::Log("Connecting Editor to Engine...");
                auto ConnectToEngine = libManager.GetFunction<ConnectToEngineFunc>(
                    libManager.GetEditorLib(), 
                    "ConnectToEngine"
                );
                
                if (!ConnectToEngine) {
                    LibraryManager::ShowError("Failed to find ConnectToEngine function");
                    return false;
                }

                if (!ConnectToEngine()) {
                    LibraryManager::ShowError("Failed to connect editor to engine");
                    return false;
                }
                
                // Start message polling after successful connection
                // StartMessagePolling();
                
                // Test message
                // auto SendCommand = dllManager.GetFunction<SendCommandToEngineFunc>(
                //     dllManager.GetEditorDLL(), 
                //     "SendCommandToEngine"
                // );
                
                // if (SendCommand) {
                //     SendCommand("[HandlerLauncher] Communication initialized");
                // }
                
                return true;
                // Debug::Logger::Log("Editor connected to engine", Debug::LogLevel::SUCCESS);
            }, 10}
        };
    }

    // Dont Use Like This is not safe TODO: You should create a string method with asyncron to handle receive message from Ui Editor
    string GetEngineMessage() {
        auto GetReceiveCommand = libManager.GetFunction<GetCommandFunc>(
            libManager.GetEditorLib(), 
            "GetCommandFromEngine"
        );
        
        if (!GetReceiveCommand) {
            std::cout << "Failed to find GetReceiveCommand function" << std::endl;
            return "Error: Cannot receive messages from engine";
        }

        try {
            string message = GetReceiveCommand();
            if (!message.empty()) {
                std::cout << "Engine Message: " + message << std::endl;
                return message;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error receiving message: " + string(e.what())<< endl;
            // DLLManager::ShowError(("Error receiving message: " + string(e.what())).c_str());
            return "Error: " + string(e.what());
        }

        return "No message";
    }
    
    bool Execute() {
        int totalProgress = 0;
        int currentProgress = 0;
        
        // Calculate total weight
        for (const auto& step : steps) {
            totalProgress += step.progressWeight;
        }
        
        for (const auto& step : steps) {
            // Update status
            loadingWindow.SetStatus(step.description);
            // loadingWindow.ProcessMessages();
            
            // Execute step
            if (!step.action()) {
                return false;
            }
            
            // Update progress
            currentProgress += step.progressWeight;
            int percentage = (currentProgress * 100) / totalProgress;
            loadingWindow.SetProgress(percentage);
            // loadingWindow.ProcessMessages();
        }
        
        loadingWindow.SetStatus("Launch Complete!");
        loadingWindow.SetProgress(100);
        // loadingWindow.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        Debug::Logger::Log("Launch Complete!", Debug::LogLevel::SUCCESS);
        // Start message loop in a separate thread
        std::thread messageThread(&LaunchSequence::RunMessageLoop, this);
        messageThread.detach(); // Let it run independently

        return true;
    }
    
    std::vector<std::string> GetPendingMessages() {
        std::vector<std::string> messages;
        std::lock_guard<std::mutex> lock(queueMutex);
        
        while (!messageQueue.empty()) {
            messages.push_back(messageQueue.front());
            messageQueue.pop();
        }
        
        return messages;
    }

    // Modify the destructor to ensure clean shutdown
    ~LaunchSequence() {
        messageThreadRunning = false;
        this_thread::sleep_for(std::chrono::milliseconds(100));
        // StopMessagePolling();
        std::cout  << "Destroying LaunchSequence" << std::endl;
    }
};