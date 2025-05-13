#include "MainWindow.hpp"

void MainWindow::RenderExplorerWindow(HandlerProject::AssetFile assetRoot, bool firstOpenProject) {


    ImGui::Begin("Explorer", nullptr, ImGuiWindowFlags_NoCollapse);
    
    if (ImGui::BeginTabBar("ExplorerTabs")) {
        if (ImGui::BeginTabItem(ICON_FA_ADDRESS_BOOK " Hierarchy")) {
            ImGui::BeginChild("HierarchyTree", ImVec2(0, 0), true);
            
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            
            if (ImGui::TreeNodeEx(ICON_FA_CUBES " Scene", nodeFlags)) {
                if (ImGui::TreeNodeEx(ICON_FA_CAMERA " Main Camera", nodeFlags)) {
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Properties");
                    ImGui::TreePop();
                }
                
                if (ImGui::TreeNodeEx(ICON_FA_USER " Player", nodeFlags)) {
                    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.8f, 1.0f), ICON_FA_IMAGE " Sprite");
                    ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.5f, 1.0f), ICON_FA_VECTOR_SQUARE " Collider");
                    ImGui::TreePop();
                }
                
                if (ImGui::TreeNodeEx(ICON_FA_SKULL " Enemy", nodeFlags)) {
                    ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.8f, 1.0f), ICON_FA_BRAIN " AI Controller");
                    ImGui::TreePop();
                }
                
                ImGui::TreePop();
            }
            
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem(ICON_FA_FOLDER_OPEN " Assets")) {
            if (firstOpenProject) {
                ImGui::BeginGroup();
                
                // Add toolbar above assets
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
                                
                if (ImGui::Button(ICON_FA_SD_CARD " Refresh")) {
                    projectHandler.isOpenedProject = true;
                }
                
                // Tooltip for refresh button
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Manually refresh asset tree");
                }
                
                 ImGui::SameLine();
            
                // Add file watcher toggle button
                bool watcherRunning = projectHandler.IsFileWatcherRunning();
                if (ImGui::Button(watcherRunning ? ICON_FA_EYE " Watching" : ICON_FA_EYE_SLASH " Watch Off")) {
                    if (watcherRunning) {
                        projectHandler.StopFileWatcher();
                    } else {
                        projectHandler.StartFileWatcher();
                    }
                }
                
                // Tooltip for watcher button
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(watcherRunning ? 
                                    "Auto-refresh is active" : 
                                    "Turn on auto-refresh");
                }
                
                ImGui::SameLine();
                
                // Add filter/search box
                HandleSearch();

                ImGui::PopItemWidth();
                
                ImGui::PopStyleColor();
                
                ImGui::BeginChild("AssetsExplorer", ImVec2(0, 0), true);
                projectHandler.DrawAssetTree(assetRoot);
                ImGui::EndChild();
                
                ImGui::EndGroup();
            }
            
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

void MainWindow::RenderInspectorWindow() {
    static char currentScriptName[256] = "";
    strncpy(currentScriptName, currentScriptName, sizeof(currentScriptName) - 1);
    currentScriptName[sizeof(currentScriptName) - 1] = '\0'; // Ensure null-termination
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
    
    ImGui::Text("Selected Object");
    ImGui::Separator();
    
    ImGui::InputText("Name", objectName, IM_ARRAYSIZE(objectName));
    
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", position, 0.1f);
        ImGui::DragFloat3("Rotation", rotation, 0.1f);
        ImGui::DragFloat3("Scale", scale, 0.1f);
    }
    
    if (ImGui::CollapsingHeader("Material")) {
        static float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        ImGui::ColorEdit4("Color", color);
        
        const char* items[] = { "Standard", "Transparent", "Emission" };
        static int item_current = 0;
        ImGui::Combo("Shader", &item_current, items, IM_ARRAYSIZE(items));
        
        static float metallic = 0.0f;
        static float smoothness = 0.5f;
        ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Smoothness", &smoothness, 0.0f, 1.0f);
    }
    
    if (ImGui::CollapsingHeader("Physics")) {
        static bool useGravity = true;
        static bool isKinematic = false;
        static float mass = 1.0f;
        static float drag = 0.0f;
        
        ImGui::Checkbox("Use Gravity", &useGravity);
        ImGui::Checkbox("Is Kinematic", &isKinematic);
        ImGui::InputFloat("Mass", &mass, 0.1f);
        ImGui::InputFloat("Drag", &drag, 0.01f);
    }
    
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    
    if (ImGui::BeginPopup("AddComponentPopup")) {
        ImGui::Text("Components");
        if (ImGui::Selectable("Mesh Renderer")) {}
        if (ImGui::Selectable("Audio Source")) {}
        if (ImGui::Selectable("Collider")) {}
        if (ImGui::Selectable("Particle System")) {}
        if (ImGui::Selectable("Light")) {}
        
        // Script creation section
        static bool showScriptInput = false;
        if (ImGui::Selectable("Script")) {
            showScriptInput = true;
        }
        
        // Show script input UI when Script is selected
        if (showScriptInput) {
            ImGui::Separator();
            ImGui::Text("Create New Script");
            
            // Script input field with better styling
            ImGui::PushItemWidth(-1); // Make input field fill available width
            bool entered = ImGui::InputText("##ScriptName", currentScriptName, IM_ARRAYSIZE(currentScriptName), 
                ImGuiInputTextFlags_EnterReturnsTrue);
            
            // Show placeholder if empty
            if (strlen(currentScriptName) == 0) {
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetItemRectMin().x + 5);
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Enter script name...");
            }
            
            // Create and Cancel buttons
            if (ImGui::Button("Create", ImVec2(120, 0)) || entered) {
                if (strlen(currentScriptName) > 0) {
                    cout << "Create" << endl;
                    projectHandler.NewScripts(currentScriptName);
                    projectHandler.ShowNotification("Script Created", "Script created successfully", projectHandler.blueColor);
                    // Reload file
                    projectHandler.isOpenedProject = true;
                    showScriptInput = false;
                    memset(currentScriptName, 0, sizeof(currentScriptName)); // Clear input
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                showScriptInput = false;
                projectHandler.ShowNotification("Script Creation Cancelled", "Script creation cancelled", projectHandler.redColor);
                memset(currentScriptName, 0, sizeof(currentScriptName)); // Clear input
            }
            
            ImGui::PopItemWidth();
        }
        
        ImGui::EndPopup();
    }
    
    ImGui::End();
}

void MainWindow::RenderMainViewWindow() {
    ImGui::Begin("Main View", nullptr, ImGuiWindowFlags_NoCollapse);
    
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Viewport")) {
            ImVec2 contentSize = ImGui::GetContentRegionAvail();
            
            // Add toolbar for viewport
            ImGui::BeginGroup();
            if (ImGui::Button("Play")) {
                
            }
            ImGui::SameLine();
            if (ImGui::Button("Pause")) {}
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {}
            ImGui::SameLine();
            ImGui::Separator();
            ImGui::SameLine();
            const char* viewModes[] = { "3D", "2D", "UI" };
            static int viewMode = 0;
            ImGui::SetNextItemWidth(100);
            ImGui::Combo("View", &viewMode, viewModes, IM_ARRAYSIZE(viewModes));
            ImGui::EndGroup();
            
            ImGui::Separator();
            
            // Render game viewport
            // projectHandler.sceneRenderer.RenderSceneToTexture(projectHandler.currentScene);
            static ImVec4 viewportBgColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImGui::GetCursorScreenPos(),
                ImVec2(ImGui::GetCursorScreenPos().x + contentSize.x, 
                       ImGui::GetCursorScreenPos().y + contentSize.y - 30),
                ImGui::ColorConvertFloat4ToU32(viewportBgColor)
            );
            
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + contentSize.y - 30);
            ImGui::Text("Game Viewport (Scene would render here)");
            
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Video Player")) {
            renderVideoPlayer();
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Animation")) {
            ImGui::Text("Animation editor will be displayed here");
            ImGui::EndTabItem();
        }
        
        if (ImGui::BeginTabItem("Particle Editor")) {
            ImGui::Text("Particle system editor will be displayed here");
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }
    
    ImGui::End();
}

void MainWindow::RenderConsoleWindow() {
    ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoCollapse);
    
    static int selectedTab = 0;
    ImGui::BeginTabBar("ConsoleTabs");
    
    if (ImGui::BeginTabItem("Output")) {
        selectedTab = 0;
        static char consoleBuffer[4096] = "Game initialized successfully.\nAll systems operational.\nReady to start...\n";
        ImGui::InputTextMultiline("##console", consoleBuffer, IM_ARRAYSIZE(consoleBuffer), 
                                 ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndTabItem();
    }
    
    if (ImGui::BeginTabItem("Errors")) {
        selectedTab = 1;
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "No errors found.");
        ImGui::EndTabItem();
    }
    
    if (ImGui::BeginTabItem("Build")) {
        selectedTab = 2;
        ImGui::Text("Build output will be displayed here");
        ImGui::EndTabItem();
    }
    
    ImGui::EndTabBar();
    
    ImGui::End();
}

void MainWindow::RenderMenuBar() {    
    // static float volume = 1.0f;
    // static bool isBackgroundChanged = false;
    // static bool isBackgroundActived = false;
    // CurrentBackground currentBg = static_cast<CurrentBackground>(currentBgInt);
    // const char* backgroundOptions[] = {
    //     "Shiroko",
    //     "Shun (Small)"
    // };

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                projectHandler.SaveNewScene();
            }
            if (ImGui::MenuItem("Open Project", "Ctrl+O")) {    
                projectHandler.isOpenedProject = true;            
                projectHandler.OpenFolder();
            }
            if (ImGui::MenuItem("Load Scene", "Ctrl+O")) {
                projectHandler.OpenScene();
            }
            if (ImGui::MenuItem("Save", "Ctrl+S")) {}
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) isRunning = false;
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", "Ctrl+X")) {}
            if (ImGui::MenuItem("Copy", "Ctrl+C")) {}
            if (ImGui::MenuItem("Paste", "Ctrl+V")) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Toggle Dark/Light Theme", "Ctrl+T")) {
                darkTheme = !darkTheme;
                setTheme(darkTheme);
            }
            if (ImGui::MenuItem("Toggle Fullscreen", "F")) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Secondary Window", nullptr, &showSecondary)) {}
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Documentation")) {}
            if (ImGui::MenuItem("About")) {}
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Background")){
            ImGui::Checkbox("Use Background", &isBackgroundActived);
            if (ImGui::Combo(" ", &currentBgInt, backgroundOptions, Background_Count)) {
                currentBg = static_cast<CurrentBackground>(currentBgInt);
                cout << currentBg << endl;
                isBackgroundChanged = true;
                isBackgroundActived = true;
            }
            
            // Tambahan pengaturan background opacity
            ImGui::SliderFloat("Opacity", &volume, 0.1f, 1.0f);
            ImGui::EndMenu();
        }

        // Status bar di menu kanan
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 200);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        
        ImGui::EndMenuBar();
    }
}

void MainWindow::HandleBackground() {
    if (isBackgroundActived)
    {
        if (isBackgroundChanged)
        {
            HandleUpdateBackground(currentBg);
            isBackgroundChanged = false;
        }

        ImGuiIO& io = ImGui::GetIO();
        // Menggunakan ImGui::GetBackgroundDrawList untuk menggambar di lapisan paling belakang
        if (backgroundTexture.TextureID != 0) {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::SetNextWindowBgAlpha(volume); // <--- Bikin window background jadi transparan
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("Background", nullptr,
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoInputs |
                // ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus);
            
            ImGui::Image((ImTextureID)(intptr_t)backgroundTexture.TextureID, io.DisplaySize);
            ImGui::End();
            ImGui::PopStyleVar(2);
        }
    } 
    else if (isBackgroundChanged)
    {
        HandleUpdateBackground(currentBg);
        isBackgroundChanged = false;
    }    
}

void MainWindow::HandleSearch() {
    // Buffer pencarian dan container hasil pencarian.
    static char searchBuffer[128] = "";
    static std::vector<HandlerProject::AssetFile> searchResults;

    // Atur lebar input sesuai jendela.
    ImGui::PushItemWidth(-1);
    // Menampilkan input text dengan hint. Menunggu Enter untuk trigger pencarian.
    if (ImGui::InputTextWithHint("##search", ICON_FA_ANGLE_UP " Search assets...", searchBuffer,
                                 IM_ARRAYSIZE(searchBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Jika buffer tidak kosong, lakukan pencarian.
        if (strlen(searchBuffer) > 0) {
            searchResults.clear();
            // Asumsikan BuildAssetTree menghasilkan struktur asset
            auto assetTree = projectHandler.BuildAssetTree(projectHandler.projectPath);
            projectHandler.SearchFileOrFolder(assetTree, searchBuffer, searchResults);
        }
        // Jika input dikosongkan, bersihkan hasil pencarian.
        else {
            searchResults.clear();
        }
    }
    // ImGui::PopItemWidth();

    // Jika ada hasil pencarian, tampilkan daftar hasil.
    if (!searchResults.empty()) {
        ImGui::Separator();
        ImGui::Text("Search Results:");
        for (const auto &result : searchResults) {
            // Buat label dengan menampilkan nama dan menandai direktori.
            std::string label;
            if (result.isDirectory)
                label = "[DIR] " + result.name;
            else
                label = result.name;

            // Tampilkan hasil sebagai selectable item.
            if (ImGui::Selectable(label.c_str())) {
                // Jika yang dipilih adalah file, periksa ekstensi file.
                if (!result.isDirectory) {
                    std::string extension;
                    size_t pos = result.name.find_last_of('.');
                    if (pos != std::string::npos)
                        extension = result.name.substr(pos);

                    // Jika file ber-ekstensi '.cpp', buka dengan VS Code.
                    if (extension == ".cpp") {
                        // Gunakan perintah sistem; pastikan "code" sudah ada di PATH.
                        std::string command = "code \"" + result.fullPath + "\"";
                        std::system(command.c_str());
                    }
                    // Jika ingin menangani ekstensi lain, bisa tambahkan else if.
                }
            }
            // Opsional: tampilkan full path sebagai tooltip ketika item di-hover.
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("%s", result.fullPath.c_str());
                ImGui::EndTooltip();
            }
        }
        // Jika input search dikosongkan, pastikan daftar hasil juga dikosongkan.
        if (strlen(searchBuffer) == 0)
            searchResults.clear();
    }
}