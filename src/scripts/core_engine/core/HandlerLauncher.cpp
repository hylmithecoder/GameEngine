#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <functional>
#include <CommCtrl.h>
using namespace std;
#pragma comment(lib, "comctl32.lib")

// Function typedefs
typedef bool (*EngineInitFunc)(const char*, int, int);
typedef void (*EngineRunFunc)();
typedef void (*EngineShutdownFunc)();
typedef bool (*EditorInitFunc)(const char*, int, int);
typedef bool (*StartServerFunc)();
typedef bool (*ConnectToEngineFunc)();
typedef bool (*SendCommandToEngineFunc)(const char*);
typedef string (*GetReceiveCommandFunc)();

// Loading window class
class LoadingWindow {
private:
    HWND hwnd;
    HWND progressBar;
    HWND statusLabel;
    std::thread loadingThread;
    bool isVisible = false;
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        LoadingWindow* window = reinterpret_cast<LoadingWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        
        switch (uMsg) {
        case WM_CREATE:
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Draw background gradient
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            TRIVERTEX vertex[2];
            vertex[0].x = 0;
            vertex[0].y = 0;
            vertex[0].Red = 0x2000;
            vertex[0].Green = 0x2000;
            vertex[0].Blue = 0x2800;
            vertex[0].Alpha = 0x0000;
            
            vertex[1].x = rect.right;
            vertex[1].y = rect.bottom;
            vertex[1].Red = 0x1000;
            vertex[1].Green = 0x1000;
            vertex[1].Blue = 0x1800;
            vertex[1].Alpha = 0x0000;
            
            GRADIENT_RECT gRect = {0, 1};
            GradientFill(hdc, vertex, 2, &gRect, 1, GRADIENT_FILL_RECT_V);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE:
            return 0; // Prevent closing during loading
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
public:
    LoadingWindow() : hwnd(nullptr), progressBar(nullptr), statusLabel(nullptr) {}
    
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
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className,
            L"Ilmee Game Engine - Loading...",
            WS_POPUP | WS_BORDER,
            x, y, windowWidth, windowHeight,
            nullptr, nullptr, GetModuleHandle(nullptr), nullptr
        );
        
        if (!hwnd) {
            return false;
        }
        
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        
        // Create logo/title text
        CreateWindowW(L"STATIC", L"ILMEE",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, 30, windowWidth - 40, 40,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
        // Set large font for title
        HWND titleLabel = GetWindow(hwnd, GW_CHILD);
        HFONT titleFont = CreateFontW(32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        SendMessage(titleLabel, WM_SETFONT, (WPARAM)titleFont, TRUE);
        
        // Create status label
        statusLabel = CreateWindowW(L"STATIC", L"Initializing...",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, 80, windowWidth - 40, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
        // Create progress bar
        progressBar = CreateWindowW(PROGRESS_CLASSW, nullptr,
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            40, 110, windowWidth - 80, 25,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
        SendMessage(progressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessage(progressBar, PBM_SETPOS, 0, 0);
        
        // Create version/copyright info
        CreateWindowW(L"STATIC", L"Game Engine v1.0 - Loading Assets...",
            WS_VISIBLE | WS_CHILD | SS_CENTER,
            20, 150, windowWidth - 40, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
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
    bool LoadDLLs() {
        engineDLL = LoadLibraryA("bin/libIlmeeeEngine.dll");
        if (!engineDLL) {
            ShowError("Failed to load libIlmeeeEngine.dll");
            return false;
        }
        
        editorDLL = LoadLibraryA("bin/libIlmeeeEditor.dll");
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
    
public:
    LaunchSequence(LoadingWindow& window, DLLManager& dll) 
        : loadingWindow(window), dllManager(dll) {
        
        // Define loading steps
        steps = {
            {"Loading Engine DLL...", [&]() { 
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
                return dllManager.LoadDLLs(); 
            }, 20},
            
            {"Initializing Engine...", [&]() {
                auto Init = dllManager.GetFunction<EngineInitFunc>(dllManager.GetEngineDLL(), "EngineInit");
                if (!Init) {
                    DLLManager::ShowError("Failed to find EngineInit function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1200));
                return Init("Ilmee Game Engine", 1280, 720);
            }, 30},
            
            {"Starting Engine Server...", [&]() {
                auto StartServer = dllManager.GetFunction<StartServerFunc>(dllManager.GetEditorDLL(), "StartServer");
                if (!StartServer) {
                    DLLManager::ShowError("Failed to find StartServer function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                return StartServer();
            }, 15},
            
            {"Initializing Editor...", [&]() {
                auto InitEditor = dllManager.GetFunction<EditorInitFunc>(dllManager.GetEditorDLL(), "EditorInit");
                if (!InitEditor) {
                    DLLManager::ShowError("Failed to find EditorInit function");
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                return InitEditor("Ilmee Editor", 1280, 720);
            }, 25},
            
            {"Connecting Editor to Engine...", [&]() {
                auto ConnectToEngine = dllManager.GetFunction<ConnectToEngineFunc>(
                    dllManager.GetEditorDLL(), 
                    "ConnectToEngine"
                );
                
                if (!ConnectToEngine) {
                    DLLManager::ShowError("Failed to find ConnectToEngine function");
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(400));
                if (!ConnectToEngine()) {
                    DLLManager::ShowError("Failed to connect editor to engine");
                    return false;
                }
                
                // Send and receive test message
                auto SendCommand = dllManager.GetFunction<SendCommandToEngineFunc>(
                    dllManager.GetEditorDLL(), 
                    "SendCommandToEngine"
                );
                
                if (SendCommand) {
                    SendCommand("Open Folder Now !!!\n");
                    SendCommand("GetEngineMessage");
                    string response = GetEngineMessage();
                    std::cout << "Engine Response: " + response << std::endl;
                }
                
                return true;
            }, 10}
        };
    }

    string GetEngineMessage() {
        auto GetReceiveCommand = dllManager.GetFunction<GetReceiveCommandFunc>(
            dllManager.GetEditorDLL(), 
            "GetReceiveCommand"
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
        
        return true;
    }
};