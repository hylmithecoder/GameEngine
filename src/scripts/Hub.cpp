#define _WIN32_WINNT 0x0A00
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <string>
#include <nfd.hpp>
#include <filesystem>
#include <ctime>
using namespace std;

struct Project {
    std::string name;
    std::string path;
    std::string version;
    std::string lastModified;
    std::string description;
    bool isFavorite;
    int projectType; // 0: Game, 1: App, 2: Tool, 3: Library
    ImVec4 accentColor;
};

class IlmeeHub {
private:
    std::vector<Project> projects;
    std::vector<std::string> recentFiles;
    int selectedProject = -1;
    bool showCreateDialog = false;
    bool showAbout = false;
    char newProjectName[256] = "";
    char newProjectPath[256] = "";
    char newProjectDesc[512] = "";
    int newProjectType = 0;
    char searchBuffer[256] = "";
    int currentView = 0; // 0: Grid, 1: List
    
    // Theme colors
    ImVec4 primaryColor = ImVec4(0.2f, 0.4f, 0.8f, 1.0f);
    ImVec4 secondaryColor = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    ImVec4 accentColor = ImVec4(0.3f, 0.7f, 0.9f, 1.0f);
    ImVec4 textColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);

public:
    IlmeeHub() {
        // Initialize sample projects
        projects.push_back({"Awesome Game", "/home/user/projects/awesome_game", "v1.2.3", "2 days ago", "Epic adventure game with stunning graphics", true, 0, ImVec4(0.8f, 0.2f, 0.2f, 1.0f)});
        projects.push_back({"Data Visualizer", "/home/user/projects/data_viz", "v0.8.1", "1 week ago", "Interactive data visualization tool", false, 1, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)});
        projects.push_back({"Audio Engine", "/home/user/projects/audio_engine", "v2.1.0", "3 days ago", "High-performance audio processing library", true, 3, ImVec4(0.8f, 0.6f, 0.2f, 1.0f)});
        projects.push_back({"Image Editor", "/home/user/projects/img_editor", "v1.0.0", "5 days ago", "Lightweight image editing application", false, 2, ImVec4(0.6f, 0.2f, 0.8f, 1.0f)});
        projects.push_back({"Network Library", "/home/user/projects/netlib", "v3.0.2", "1 month ago", "Cross-platform networking library", false, 3, ImVec4(0.2f, 0.6f, 0.8f, 1.0f)});
        
        recentFiles = {"config.ini", "shader.glsl", "texture.png", "audio.wav", "script.lua"};
    }

    void SetupTheme() {
        ImGuiStyle& style = ImGui::GetStyle();
        
        // Colors
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.95f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.15f, 0.15f, 0.95f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        style.Colors[ImGuiCol_TitleBg] = primaryColor;
        style.Colors[ImGuiCol_TitleBgActive] = primaryColor;
        style.Colors[ImGuiCol_Button] = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
        style.Colors[ImGuiCol_ButtonHovered] = accentColor;
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        style.Colors[ImGuiCol_HeaderActive] = accentColor;
        
        // Styling
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 4.0f;
        style.TabRounding = 4.0f;
        style.WindowPadding = ImVec2(12.0f, 12.0f);
        style.FramePadding = ImVec2(8.0f, 6.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
        style.ScrollbarSize = 16.0f;
        style.GrabMinSize = 12.0f;
    }

    void DrawTopBar() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::BeginChild("TopBar", ImVec2(0, 80), true, ImGuiWindowFlags_NoScrollbar);
        
        // Logo and title
        ImGui::PushFont(nullptr); // Would use large font here
        ImGui::TextColored(accentColor, "ILMEE");
        ImGui::SameLine();
        ImGui::Text("HUB");
        ImGui::PopFont();
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 300);
        
        // Search bar
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputTextWithHint("##search", "Search projects...", searchBuffer, sizeof(searchBuffer))) {
            // Filter projects based on search
        }
        
        ImGui::SameLine();
        if (ImGui::Button(currentView == 0 ? "Grid" : "List")) {
            currentView = 1 - currentView;
        }
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void DrawSidebar() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 1.0f));
        ImGui::BeginChild("Sidebar", ImVec2(250, 0), true);
        
        // Navigation buttons
        ImGui::PushStyleColor(ImGuiCol_Button, primaryColor);
        if (ImGui::Button("Projects", ImVec2(-1, 40))) {}
        ImGui::PopStyleColor();
        
        if (ImGui::Button("Templates", ImVec2(-1, 40))) {}
        if (ImGui::Button("Settings", ImVec2(-1, 40))) {}
        if (ImGui::Button("Learn", ImVec2(-1, 40))) {}
        
        ImGui::Separator();
        
        // Recent files
        ImGui::Text("Recent Files");
        ImGui::BeginChild("RecentFiles", ImVec2(0, 150));
        for (const auto& file : recentFiles) {
            if (ImGui::Selectable(file.c_str())) {
                // Open recent file
            }
        }
        ImGui::EndChild();
        
        ImGui::Separator();
        
        // Quick actions
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
        if (ImGui::Button("New Project", ImVec2(-1, 35))) {
            showCreateDialog = true;
        }
        ImGui::PopStyleColor();
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.4f, 0.2f, 1.0f));
        if (ImGui::Button("Open Project", ImVec2(-1, 35))) {
            OpenFolder();
        }
        ImGui::PopStyleColor();
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void DrawProjectCard(const Project& project, int index) {
        ImGui::PushID(index);
        
        // Card background
        ImVec2 cardSize(280, 160);
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.12f, 1.0f));
        ImGui::BeginChild("ProjectCard", cardSize, true);
        
        // Project type indicator
        const char* typeNames[] = {"GAME", "APP", "TOOL", "LIB"};
        ImGui::PushStyleColor(ImGuiCol_Button, project.accentColor);
        ImGui::Button(typeNames[project.projectType], ImVec2(50, 20));
        ImGui::PopStyleColor();
        
        // Favorite star
        ImGui::SameLine(cardSize.x - 40);
        if (project.isFavorite) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "★");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "☆");
        }
        
        // Project name
        ImGui::PushFont(nullptr); // Would use medium font
        ImGui::Text("%s", project.name.c_str());
        ImGui::PopFont();
        
        // Version and last modified
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "v%s", project.version.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "• %s", project.lastModified.c_str());
        
        // Description
        ImGui::TextWrapped("%s", project.description.c_str());
        
        // Action buttons
        ImGui::SetCursorPosY(cardSize.y - 35);
        ImGui::PushStyleColor(ImGuiCol_Button, project.accentColor);
        if (ImGui::Button("Open", ImVec2(60, 25))) {
            selectedProject = index;
            // Launch project
        }
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        if (ImGui::Button("...", ImVec2(25, 25))) {
            ImGui::OpenPopup("ProjectMenu");
        }
        
        // Context menu
        if (ImGui::BeginPopup("ProjectMenu")) {
            if (ImGui::MenuItem("Open in Explorer")) {}
            if (ImGui::MenuItem("Duplicate")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Remove from List")) {}
            if (ImGui::MenuItem("Delete Project", nullptr, false, false)) {} // Disabled for safety
            ImGui::EndPopup();
        }
        
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }

    void DrawProjectGrid() {
        ImGui::BeginChild("ProjectGrid");
        
        // Calculate columns
        float windowWidth = ImGui::GetContentRegionAvail().x;
        int columns = std::max(1, (int)(windowWidth / 300.0f));
        
        for (int i = 0; i < projects.size(); i++) {
            if (strlen(searchBuffer) > 0) {
                // Simple search filter
                std::string searchTerm = searchBuffer;
                std::transform(searchTerm.begin(), searchTerm.end(), searchTerm.begin(), ::tolower);
                std::string projectName = projects[i].name;
                std::transform(projectName.begin(), projectName.end(), projectName.begin(), ::tolower);
                
                if (projectName.find(searchTerm) == std::string::npos) {
                    continue;
                }
            }
            
            DrawProjectCard(projects[i], i);
            
            if ((i + 1) % columns != 0 && i < projects.size() - 1) {
                ImGui::SameLine();
            }
        }
        
        ImGui::EndChild();
    }

    void DrawProjectList() {
        ImGui::BeginChild("ProjectList");
        
        // Table headers
        if (ImGui::BeginTable("ProjectTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();
            
            for (int i = 0; i < projects.size(); i++) {
                if (strlen(searchBuffer) > 0) {
                    std::string searchTerm = searchBuffer;
                    std::transform(searchTerm.begin(), searchTerm.end(), searchTerm.begin(), ::tolower);
                    std::string projectName = projects[i].name;
                    std::transform(projectName.begin(), projectName.end(), projectName.begin(), ::tolower);
                    
                    if (projectName.find(searchTerm) == std::string::npos) {
                        continue;
                    }
                }
                
                ImGui::TableNextRow();
                
                ImGui::TableNextColumn();
                if (projects[i].isFavorite) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "★ ");
                    ImGui::SameLine();
                }
                ImGui::Text("%s", projects[i].name.c_str());
                
                ImGui::TableNextColumn();
                const char* typeNames[] = {"Game", "App", "Tool", "Lib"};
                ImGui::TextColored(projects[i].accentColor, "%s", typeNames[projects[i].projectType]);
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", projects[i].version.c_str());
                
                ImGui::TableNextColumn();
                ImGui::Text("%s", projects[i].lastModified.c_str());
                
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Button, projects[i].accentColor);
                if (ImGui::Button("Open")) {
                    selectedProject = i;
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
            
            ImGui::EndTable();
        }
        
        ImGui::EndChild();
    }

    void DrawCreateProjectDialog() {
        if (!showCreateDialog) return;
        
        ImGui::OpenPopup("Create New Project");
        
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 400));
        
        if (ImGui::BeginPopupModal("Create New Project", &showCreateDialog)) {
            ImGui::Text("Create a new project");
            ImGui::Separator();
            
            ImGui::InputTextWithHint("Project Name", "Enter project name...", newProjectName, sizeof(newProjectName));
            ImGui::InputTextWithHint("Project Path", "Choose project location...", newProjectPath, sizeof(newProjectPath));
            
            ImGui::Text("Project Type:");
            ImGui::RadioButton("Game", &newProjectType, 0); ImGui::SameLine();
            ImGui::RadioButton("Application", &newProjectType, 1); ImGui::SameLine();
            ImGui::RadioButton("Tool", &newProjectType, 2); ImGui::SameLine();
            ImGui::RadioButton("Library", &newProjectType, 3);
            
            ImGui::InputTextMultiline("Description", newProjectDesc, sizeof(newProjectDesc), ImVec2(-1, 100));
            
            ImGui::Separator();
            
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if (ImGui::Button("Create Project", ImVec2(120, 0))) {
                // Create new project
                Project newProject;
                newProject.name = newProjectName;
                newProject.path = newProjectPath;
                newProject.version = "v1.0.0";
                newProject.lastModified = "Just now";
                newProject.description = newProjectDesc;
                newProject.isFavorite = false;
                newProject.projectType = newProjectType;
                newProject.accentColor = ImVec4(0.3f + (rand() % 5) * 0.1f, 0.3f + (rand() % 5) * 0.1f, 0.3f + (rand() % 5) * 0.1f, 1.0f);
                
                projects.insert(projects.begin(), newProject);
                
                // Clear form
                memset(newProjectName, 0, sizeof(newProjectName));
                memset(newProjectPath, 0, sizeof(newProjectPath));
                memset(newProjectDesc, 0, sizeof(newProjectDesc));
                newProjectType = 0;
                
                showCreateDialog = false;
            }
            ImGui::PopStyleColor();
            
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                showCreateDialog = false;
            }
            
            ImGui::EndPopup();
        }
    }

    void Render() {
        SetupTheme();
        
        // Main window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Ilmee Hub", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar);
        
        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Project", "Ctrl+N")) {
                    showCreateDialog = true;
                }
                if (ImGui::MenuItem("Open Project", "Ctrl+O")) 
                {
                    OpenFolder();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Grid View", nullptr, currentView == 0);
                ImGui::MenuItem("List View", nullptr, currentView == 1);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) {
                    showAbout = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        DrawTopBar();
        
        // Main content area
        ImGui::BeginGroup();
        DrawSidebar();
        ImGui::SameLine();
        
        // Project display area
        ImGui::BeginChild("MainContent");
        if (currentView == 0) {
            DrawProjectGrid();
        } else {
            DrawProjectList();
        }
        ImGui::EndChild();
        ImGui::EndGroup();
        
        DrawCreateProjectDialog();
        
        // About dialog
        if (showAbout) {
            ImGui::OpenPopup("About Ilmee Hub");
            if (ImGui::BeginPopupModal("About Ilmee Hub", &showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Ilmee Hub v1.0.0");
                ImGui::Text("Advanced Project Management Tool");
                ImGui::Separator();
                ImGui::Text("Built with SDL2 and Dear ImGui");
                ImGui::Text("© 2025 Ilmee Development");
                if (ImGui::Button("Close")) {
                    showAbout = false;
                }
                ImGui::EndPopup();
            }
        }
        
        ImGui::End();
    }

    void OpenFolder()
    {
        NFD::Guard nfdGuard;

        // auto-freeing memory
        NFD::UniquePath outPath;

        // show the dialog
        nfdresult_t result = NFD::PickFolder(outPath);
        if (result == NFD_OKAY) {
            cout << outPath.get() << endl;
        } else if (result == NFD_CANCEL) {
            cout << "User pressed cancel." << endl;
        } else {
            cout << "Error: " << NFD::GetError() << endl;
        }
    }
};

// Main application class
class Application {
private:
    SDL_Window* window;
    SDL_Renderer* renderer;
    IlmeeHub hub;
    bool running = true;

public:
    bool Initialize() {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
        
        window = SDL_CreateWindow("Ilmee Hub", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 580, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (!window) return false;
        
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) return false;
        
        // Setup ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        string fontPath = "assets/fonts/zh-cn.ttf";
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 13.0f);
        
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        ImGui_ImplSDLRenderer2_Init(renderer);
        
        SetWindowIcon();
        return true;
    }
    
    void Run() {
        while (running) {
            HandleEvents();
            Render();
        }
    }

    void SetWindowIcon() {
        SDL_Surface* icon = SDL_LoadBMP("assets/icons/app_icon.bmp");
        if (icon) {
            SDL_SetWindowIcon(window, icon);
            SDL_FreeSurface(icon);
        } else {
            SDL_Log("Failed to load icon: %s", SDL_GetError());
        }
    }
    
    void HandleEvents() {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }
    }
    
    void Render() {
        // Start ImGui frame
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        
        // Render hub
        hub.Render();
        
        // Render ImGui
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }
    
    ~Application() {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

int main(int argc, char* argv[]) {
    Application app;
    
    if (!app.Initialize()) {
        SDL_Log("Failed to initialize application!");
        return -1;
    }
    
    app.Run();
    return 0;
}