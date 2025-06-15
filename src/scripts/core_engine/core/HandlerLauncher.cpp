#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <CommCtrl.h>
#include <atomic>
#include <Debugger.hpp>
#include <mutex>
#include <windowsx.h>
#include <queue>
#include <condition_variable>
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
    HWND hwnd;
    HWND progressBar;
    HWND statusLabel;
    std::thread loadingThread;
    bool isVisible = false;
    bool isDragging = false;
    POINT dragOffset = {0, 0};
    HBITMAP logoBitmap = nullptr;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        LoadingWindow* window = reinterpret_cast<LoadingWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        
        switch (uMsg) {
        case WM_NCHITTEST: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT rc;
            GetWindowRect(hwnd, &rc);
            if (pt.y <= rc.top + 30) { // Top drag area
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Draw background with Unity-style gradient
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            TRIVERTEX vertex[2];
            vertex[0].x = 0;
            vertex[0].y = 0;
            vertex[0].Red = 0x2500;    // Dark gray-blue
            vertex[0].Green = 0x2700;
            vertex[0].Blue = 0x2900;
            vertex[0].Alpha = 0x0000;
            
            vertex[1].x = rect.right;
            vertex[1].y = rect.bottom;
            vertex[1].Red = 0x1800;    // Darker gray-blue
            vertex[1].Green = 0x1A00;
            vertex[1].Blue = 0x1C00;
            vertex[1].Alpha = 0x0000;
            
            GRADIENT_RECT gRect = {0, 1};
            GradientFill(hdc, vertex, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
            
            // Draw subtle border
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
            SelectObject(hdc, hPen);
            Rectangle(hdc, 0, 0, rect.right, rect.bottom);
            DeleteObject(hPen);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
    
public:
    LoadingWindow() : hwnd(nullptr), progressBar(nullptr), statusLabel(nullptr) {}
    
    
    HICON LoadIconFromFile(const wchar_t* iconPath) {
        return (HICON)LoadImageW(
            NULL,
            iconPath,
            IMAGE_ICON,
            0, 0,  // Use actual icon size
            LR_LOADFROMFILE | LR_DEFAULTSIZE
        );
    }

    bool Create() {
        // Initialize common controls
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_PROGRESS_CLASS;
        InitCommonControlsEx(&icex);
        
        // Register window class
        const wchar_t* className = L"IlmeeLoadingWindow";
        WNDCLASSW wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; // Custom painting
        wc.style = CS_HREDRAW | CS_VREDRAW;
        
        if (!RegisterClassW(&wc)) {
            return false;
        }
        
        // Create window
        int windowWidth = 480;
        int windowHeight = 200;
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - windowWidth) / 2;
        int y = (screenHeight - windowHeight) / 2;
        
        hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_APPWINDOW,
            className,
            L"Ilmee Game Engine",
            WS_POPUP | WS_THICKFRAME, // Add WS_THICKFRAME for resizable borders
            x, y, windowWidth, windowHeight,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );
        
        if (!hwnd) return false;
        
        // Load and set both large and small icons
        HICON hIconLarge = LoadIconFromFile(L"assets/icons/app_icon.ico");
        HICON hIconSmall = LoadIconFromFile(L"assets/icons/app_icon.ico");
        
        if (hIconLarge && hIconSmall) {
            SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconLarge);
            SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        } else {
            Debug::Logger::Log("Failed to load window icons", Debug::LogLevel::WARNING);
        }
        // Set window transparency
        SetLayeredWindowAttributes(hwnd, 0, 245, LWA_ALPHA);
        
        // Create rounded window region
        HRGN region = CreateRoundRectRgn(0, 0, windowWidth, windowHeight, 15, 15);
        SetWindowRgn(hwnd, region, TRUE);
        
        // Create title with custom font
        HFONT titleFont = CreateFontW(28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
            
        HWND titleLabel = CreateWindowW(L"STATIC", L"ILMEE ENGINE",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            0, 20, windowWidth, 40,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessage(titleLabel, WM_SETFONT, (WPARAM)titleFont, TRUE);
        
        // Create modern progress bar
        progressBar = CreateWindowW(PROGRESS_CLASSW, nullptr,
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            40, 120, windowWidth - 80, 4, // Make it thinner
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
            
        // Set progress bar colors
        SendMessage(progressBar, PBM_SETBARCOLOR, 0, RGB(72, 144, 232)); // Blue
        SendMessage(progressBar, PBM_SETBKCOLOR, 0, RGB(45, 45, 48));    // Dark gray
        
        // Create status label with custom font
        HFONT statusFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
            
        statusLabel = CreateWindowW(L"STATIC", L"Initializing...",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, 90, windowWidth - 40, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessage(statusLabel, WM_SETFONT, (WPARAM)statusFont, TRUE);
        
        // Create version label
        HWND versionLabel = CreateWindowW(L"STATIC", L"v1.0.0",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, windowHeight - 30, windowWidth - 40, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        SendMessage(versionLabel, WM_SETFONT, (WPARAM)statusFont, TRUE);
        
        return true;
    }
    
    void Show() {
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
            isVisible = true;
        }
    }
    
    void Hide() {
        if (hwnd) {
            ShowWindow(hwnd, SW_HIDE);
            isVisible = false;
        }
    }
    
    void SetProgress(int percentage) {
        if (progressBar) {
            SendMessage(progressBar, PBM_SETPOS, percentage, 0);
        }
    }
    
    void SetStatus(const std::wstring& status) {
        if (statusLabel) {
            SetWindowTextW(statusLabel, status.c_str());
        }
    }
    
    void ProcessMessages() {
        MSG msg;
        while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    void StartLoadingAnimation() {
        loadingThread = std::thread([this]() {
            int dots = 0;
            while (isVisible) {
                std::wstring baseText = L"Loading";
                for (int i = 0; i < dots; ++i) {
                    baseText += L".";
                }
                SetStatus(baseText);
                dots = (dots + 1) % 4;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });
    }
    
    ~LoadingWindow() {
        if (loadingThread.joinable()) {
            isVisible = false;
            loadingThread.join();
        }
        if (hwnd) {
            DestroyWindow(hwnd);
        }
    }
};

// Utility class for DLL management
class DLLManager {
private:
    HMODULE engineDLL = nullptr;
    HMODULE editorDLL = nullptr;
    
public:
    const vector<string> IlmeeEngine = {"libIlmeeeEngine.dll", "libIlmeeeEditor.dll"};
    bool LoadDLLs() {
        engineDLL = LoadLibraryA(("bin/" + IlmeeEngine[0]).c_str());
        if (!engineDLL) {
            ShowError("Failed to load libIlmeeeEngine.dll");
            return false;
        }
        
        editorDLL = LoadLibraryA(("bin/" + IlmeeEngine[1]).c_str());
        if (!editorDLL) {
            ShowError("Failed to load libIlmeeeEditor.dll");
            Cleanup();
            return false;
        }
        
        return true;
    }
    
    template<typename T>
    T GetFunction(HMODULE dll, const char* functionName) {
        return reinterpret_cast<T>(GetProcAddress(dll, functionName));
    }
    
    void Cleanup() {
        if (editorDLL) {
            FreeLibrary(editorDLL);
            editorDLL = nullptr;
        }
        if (engineDLL) {
            FreeLibrary(engineDLL);
            engineDLL = nullptr;
        }
    }
    
    HMODULE GetEngineDLL() const { return engineDLL; }
    HMODULE GetEditorDLL() const { return editorDLL; }
    
    static void ShowError(const char* message) {
        MessageBoxA(nullptr, message, "Ilmee Launcher - Error", MB_ICONERROR);
    }
    
    ~DLLManager() {
        Cleanup();
    }
};

// Main loading sequence
class LaunchSequence {
private:
    LoadingWindow& loadingWindow;
    DLLManager& dllManager;
    
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

        auto SendCommand = dllManager.GetFunction<SendCommandToEngineFunc>(
            dllManager.GetEditorDLL(), 
            "SendCommandToEngine"
        );

        auto GetReceiveCommand = dllManager.GetFunction<GetCommandFunc>(
            dllManager.GetEditorDLL(), 
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
                                    auto SetProjectPath = dllManager.GetFunction<SetProjectPathFunc>(
                                        dllManager.GetEditorDLL(),
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
                                    auto HandlerMethodExecute = dllManager.GetFunction<ExecuteCommandFunc>(
                                        dllManager.GetEditorDLL(),
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
                MSG msg;
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                    if (msg.message == WM_QUIT) {
                        messageThreadRunning = false;
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
    LaunchSequence(LoadingWindow& window, DLLManager& dll) 
        : loadingWindow(window), dllManager(dll) {
        
        // Define loading steps
        steps = {
            {"Loading Engine DLL...", [&]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                return dllManager.LoadDLLs(); 
            }, 20},            
            {"Initializing Engine...", [&]() {
                Debug::Logger::Log("Initializing Engine...");
                auto Init = dllManager.GetFunction<EngineInitFunc>(dllManager.GetEngineDLL(), "EngineInit");
                if (!Init) {
                    DLLManager::ShowError("Failed to find EngineInit function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                return Init("My First Project", 1280, 720);
                Debug::Logger::Log("Engine initialized", Debug::LogLevel::SUCCESS);
            }, 30},
            {"Initializing Editor...", [&]() {
                Debug::Logger::Log("Initializing Editor...");
                auto InitEditor = dllManager.GetFunction<EditorInitFunc>(dllManager.GetEditorDLL(), "EditorInit");
                if (!InitEditor) {
                    DLLManager::ShowError("Failed to find EditorInit function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                return InitEditor("Ilmee Editor", 1280, 720);
                Debug::Logger::Log("Editor initialized", Debug::LogLevel::SUCCESS);
            }, 25},           
            {"Starting Editor Server...", [&]() {
                Debug::Logger::Log("Starting Editor Server...");
                StartServerFunc StartServer = dllManager.GetFunction<StartServerFunc>(dllManager.GetEditorDLL(), "StartServer");
                if (!StartServer) {
                    DLLManager::ShowError("Failed to find StartServer function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                return StartServer();
                // Debug::Logger::Log("Editor Server started", Debug::LogLevel::SUCCESS);
            }, 15},            
            {"Connecting Editor to Engine...", [&]() {
                Debug::Logger::Log("Connecting Editor to Engine...");
                auto ConnectToEngine = dllManager.GetFunction<ConnectToEngineFunc>(
                    dllManager.GetEditorDLL(), 
                    "ConnectToEngine"
                );
                
                if (!ConnectToEngine) {
                    DLLManager::ShowError("Failed to find ConnectToEngine function");
                    return false;
                }

                if (!ConnectToEngine()) {
                    DLLManager::ShowError("Failed to connect editor to engine");
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
        auto GetReceiveCommand = dllManager.GetFunction<GetCommandFunc>(
            dllManager.GetEditorDLL(), 
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
            std::wstring wideDesc(step.description.begin(), step.description.end());
            loadingWindow.SetStatus(wideDesc);
            loadingWindow.ProcessMessages();
            
            // Execute step
            if (!step.action()) {
                return false;
            }
            
            // Update progress
            currentProgress += step.progressWeight;
            int percentage = (currentProgress * 100) / totalProgress;
            loadingWindow.SetProgress(percentage);
            loadingWindow.ProcessMessages();
        }
        
        loadingWindow.SetStatus(L"Launch Complete!");
        loadingWindow.SetProgress(100);
        loadingWindow.ProcessMessages();
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