#include "MainWindow.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Debugger.hpp>

void MainWindow::RenderHierarchyWindow() {
    ImGui::Begin("Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

    if (isLoadScene) {
        if (ImGui::TreeNodeEx(projectHandler.currentScene.sceneName.c_str(), nodeFlags)) {
            for (const auto& obj : projectHandler.currentScene.objects) {
                projectHandler.DrawIconFromImage("assets/images/fileicons/box.png", 20, 20);
                if (ImGui::TreeNodeEx(obj.name.c_str(), nodeFlags)) {
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    } else {
        struct SceneElement {
            std::string name;
            std::vector<std::string> properties;
        };

        std::vector<SceneElement> elements = {
            {"Main Camera", {"Properties"}},
            {"Player", {"Sprite", "Collider"}},
            {"Enemy", {"AI Controller"}}
        };

        if (ImGui::TreeNodeEx("Scene", nodeFlags)) {
            projectHandler.DrawIconFromImage("assets/images/fileicons/box.png", 20, 20);
            for (const auto& element : elements) {
                if (ImGui::TreeNodeEx(element.name.c_str(), nodeFlags)) {
                    for (const auto& prop : element.properties) {
                        ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.5f, 1.0f), prop.c_str());
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void MainWindow::RenderExplorerWindow(HandlerProject::AssetFile projectRoot, HandlerProject::AssetFile assetFolder, const std::string& assetPath , bool firstOpenProject) {
    ImGui::Begin("Explorer", nullptr, ImGuiWindowFlags_NoCollapse);    
        // Debug::Logger::Log("Asset Folder: " + assetFolder.fullPath + "\nChildren: "+to_string(assetFolder.children.size()), Debug::LogLevel::INFO);
            if (firstOpenProject) {
                ImGui::BeginGroup();
                
                // Add toolbar above assets
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));

                // Refresh button
                if (ImGui::Button("Refresh")) {
                    projectHandler.isOpenedProject = true;
                }

                // Tooltip for refresh button
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Manually refresh asset tree");
                }

                ImGui::SameLine();

                // Add file watcher toggle button
                bool watcherRunning = projectHandler.IsFileWatcherRunning();
                if (ImGui::Button(watcherRunning ? "Watching" : "Watch Off")) {
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

                // Back button - disabled if in root directory
                std::string rootPath = projectHandler.projectPath + "\\assets";
                bool isInRootDirectory = (projectHandler.currentDirectory == rootPath);

                // Disable button if in root directory
                if (isInRootDirectory) {
                    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
                    ImGui::Button("Back");
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar();
                    
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Already in root directory");
                    }
                } else {
                    if (ImGui::Button("Back")) {
                        size_t lastSlash = projectHandler.currentDirectory.find_last_of("/\\");
                        if (lastSlash != std::string::npos) {
                            projectHandler.currentDirectory = projectHandler.currentDirectory.substr(0, lastSlash);
                            projectHandler.selectedAsset = nullptr; // Reset selection when navigating
                        }
                    }
                    
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Go back to parent folder");
                    }
                }

                ImGui::SameLine();

                // Add filter/search box
                HandleSearch();

                ImGui::PopStyleColor(3);
                
                // Get available content region
                ImVec2 contentSize = ImGui::GetContentRegionAvail();

                // Left panel (Project tree)
                ImGui::BeginChild("ProjectRoot", ImVec2(explorerSplitPosition, 0), true);
                projectHandler.DrawAssetTree(projectRoot);
                ImGui::EndChild();

                // Splitter
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.5f, 0.5f, 0.3f));
                ImGui::Button("##Splitter", ImVec2(4.0f, contentSize.y));
                ImGui::PopStyleColor();

                // Handle splitter dragging
                if (ImGui::IsItemActive()) {
                    float delta = ImGui::GetIO().MouseDelta.x;
                    if (explorerSplitPosition + delta >= MIN_PANEL_WIDTH && 
                        explorerSplitPosition + delta <= contentSize.x - MIN_PANEL_WIDTH) {
                        explorerSplitPosition += delta;
                    }
                }
                
                // Show resize cursor when hovering over splitter
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

                // Right panel (File explorer)
                ImGui::SameLine();
                ImGui::BeginChild("AssetRoot", ImVec2(0, 0), true);
                // Fix: Get files directly from projectHandler instead of through selectedAsset
                // Debug::Logger::Log("If You See it this is work in method RenderExplorerWindow", Debug::LogLevel::SUCCESS);
                projectHandler.DrawFolderGridView();
                ImGui::EndChild();
                
                ImGui::EndGroup();
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
    
    // if (isLoadScene)
    // {
    //     // Still Dummy Inspector Yeah
    //     for (auto& obj : projectHandler.currentScene.objects) {
    //             // projectHandler.DrawIconFromImage("assets/images/fileicons/box.png");
    //             // if (ImGui::TreeNodeEx(obj.name.c_str())) {
    //             //     ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "Properties");
    //             //     ImGui::TreePop();
    //             // }
    //             // Use a temporary buffer for editing the name
    //             char nameBuffer[256];
    //             char spritePath[256];
    //             strncpy(nameBuffer, obj.name.c_str(), sizeof(nameBuffer) - 1);
    //             nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                
    //             for (int i = 0; i < obj.name.length(); i++) {
    //                 ImGui::PushID(i);
    //                 if (ImGui::InputText("##Name", nameBuffer, IM_ARRAYSIZE(nameBuffer))) {
    //                     obj.name = nameBuffer;
    //                 };
    //                 ImGui::PopID();
    //                 if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    //                     // ImGui::DragFloat3("Position", obj.x,obj.y,obj.width, 0.1f);
    //                     // ImGui::DragFloat3("Rotation", obj.rotation, 0.1f);
    //                     // ImGui::DragFloat3("Scale", obj.scale, 0.1f);
    //                 }
    //                 ImGui::PushID(i);
    //                 if (ImGui::CollapsingHeader("Sprite"), ImGuiTreeNodeFlags_DefaultOpen) {
    //                     if (ImGui::InputText("Sprite Path", spritePath, IM_ARRAYSIZE(spritePath))) {
    //                         obj.spritePath = spritePath;
    //                     }
    //                 }
    //                 ImGui::PopID();
    //             }            
    //         }
    // }
    // else {
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
        // }
    }   
    ImGui::End();
}

void MainWindow::RenderSceneToolbarView(ImVec2 parentPos, ImVec2 parentSize) {
    // Calculate toolbar position (top-left corner of scene window with some padding)
    ImVec2 toolbarPos = ImVec2(parentPos.x + 10, parentPos.y + 30);
    
    // Set toolbar window properties
    ImGui::SetNextWindowPos(toolbarPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.8f); // Semi-transparent background
    
    // Toolbar window flags
    ImGuiWindowFlags toolbar_flags = 
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        // ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
    // Begin floating toolbar
    if (ImGui::Begin("Scene Toolbar", nullptr, toolbar_flags)) {
        // Reset View Button
        if (ImGui::Button("Reset View")) {
                Debug::Logger::Log("Resetting camera view", Debug::LogLevel::INFO);
                sceneRenderer2D->ResetCamera();
        }
        
        ImGui::SameLine();
        
        // Zoom controls
        // static float zoom = sceneRenderer2D->zoom;
        ImGui::SetNextItemWidth(100);
        if (ImGui::SliderFloat("##Zoom", &sceneRenderer2D->zoom, 1.0f, 10.0f, "%.2fx")) {
                sceneRenderer2D->SetCameraZoom(sceneRenderer2D->zoom);
        }
        
        ImGui::SameLine();
        ImGui::Text("Zoom");
        
        // New line for more controls
        ImGui::NewLine();

        // Grid size control
        static float gridSize = 50.0f;
        ImGui::SetNextItemWidth(80);
        if (ImGui::DragFloat("##GridSize", &gridSize, 1.0f, 10.0f, 200.0f, "%.0f")) {
                sceneRenderer2D->SetGridSize(gridSize);
        }
        
        ImGui::SameLine();
        ImGui::Text("Grid Size");
        
        // Third line for color controls
        ImGui::NewLine();
        
        // Grid color picker (compact)
        static float gridColor[3] = {0.5f, 0.5f, 0.5f};
        ImGui::SetNextItemWidth(60);
        if (ImGui::ColorEdit3("##GridColor", gridColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
                sceneRenderer2D->SetGridColor(gridColor[0], gridColor[1], gridColor[2], 1.0f);
        }
        
        ImGui::SameLine();
        ImGui::Text("Grid");
        
        ImGui::SameLine();
        
        // Background color picker (compact)
        static float bgColor[3] = {0.2f, 0.2f, 0.2f};
        ImGui::SetNextItemWidth(60);
        if (ImGui::ColorEdit3("##BgColor", bgColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            // if (sceneRenderer2D) {
                sceneRenderer2D->SetBackgroundColor(bgColor[0], bgColor[1], bgColor[2], 1.0f); // You'll need to implement this
            // }
        }
        
        ImGui::SameLine();
        ImGui::Text("Background");
        
        // Snap to grid toggle
        ImGui::NewLine();
        static bool snapToGrid = false;
        ImGui::Checkbox("Snap to Grid", &snapToGrid);
        
        ImGui::SameLine();
        
        // View mode selector
        static int viewMode = 0;
        const char* viewModes[] = {"2D", "3D", "Wireframe"};
        ImGui::SetNextItemWidth(80);
        ImGui::Combo("##ViewMode", &viewMode, viewModes, IM_ARRAYSIZE(viewModes));
    }
    ImGui::End();

    ImGui::PopStyleVar(1);
}

void MainWindow::RenderSceneWindow() {
    if (!showScene) return;

    // Window flags untuk menghilangkan padding dan scrollbar
    ImGuiWindowFlags window_flags = 
        ImGuiWindowFlags_NoCollapse | 
        ImGuiWindowFlags_NoScrollbar | 
        ImGuiWindowFlags_NoScrollWithMouse;  // Tambahkan flag ini

    // Set window properties
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.0f);
    
    // Push style untuk menghilangkan padding window
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    // ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    // ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
    // ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    if (ImGui::Begin("Scene", &showScene, window_flags)) {
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImVec2 contentSize = ImGui::GetContentRegionAvail();

        // Render scene dengan ukuran penuh
        sceneRenderer2D->RenderSceneToTexture(projectHandler.currentScene);
        
        // Dapatkan texture ID dari scene renderer
        // ImTextureID sceneTexture = (ImTextureID)(intptr_t)sceneRenderer2D->GetSceneTextureID();
        
        // Render texture dengan ukuran penuh
        // ImGui::Image(sceneTexture, contentSize, ImVec2(0, 1), ImVec2(1, 0));

        // Render toolbar di atas viewport
        RenderSceneToolbarView(windowPos, windowSize);

        // Status bar dengan background semi-transparan
        ImGui::SetCursorPos(ImVec2(0, windowSize.y - 25));
        // ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
        ImGui::BeginChild("StatusBar", ImVec2(windowSize.x, 25), false);
        ImGui::Text(" Scene View | FPS: %.1f | Zoom: %.2fx", 
                    ImGui::GetIO().Framerate, sceneRenderer2D->GetZoom());
        ImGui::EndChild();
        // ImGui::PopStyleColor();
    }
    ImGui::End();

    // Pop semua style yang di-push
    ImGui::PopStyleVar(1);
}

void MainWindow::RenderMainViewWindow() {
    ImGui::Begin("Main View", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Viewport")) {
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

void MainWindow::RenderViewportToolbar() {
    // Style untuk toolbar
    float toolbarHeight = 28.0f;
    
    // Mode dropdown
    ImGui::Text("Mode:");
    ImGui::SameLine();
    const char* modes[] = { "Select", "Move", "Rotate", "Scale" };
    static int currentMode = 0;
    ImGui::SetNextItemWidth(100);
    if (ImGui::Combo("##viewportMode", &currentMode, modes, IM_ARRAYSIZE(modes))) {
        // Handle mode change
        sceneRenderer2D->SetEditMode((SceneRenderer2D::EditMode)currentMode);
    }
    
    // Tombol grid
    ImGui::SameLine(0, 15);
    static bool showGrid = true;
    if (ImGui::Checkbox("Show Grid", &showGrid)) {
        sceneRenderer2D->SetGridVisible(showGrid);
    }
    
    // Grid size slider
    ImGui::SameLine(0, 15);
    ImGui::Text("Grid Size:");
    ImGui::SameLine();
    static float gridSize = 32.0f;
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderFloat("##gridSize", &gridSize, 8.0f, 64.0f, "%.0f")) {
        sceneRenderer2D->SetGridSize(gridSize);
    }
    
    // Snap to grid
    ImGui::SameLine(0, 15);
    static bool snapToGrid = true;
    if (ImGui::Checkbox("Snap to Grid", &snapToGrid)) {
        sceneRenderer2D->SetSnapToGrid(snapToGrid);
    }
    
    // Camera controls
    ImGui::SameLine(0, 20);
    if (ImGui::Button("Reset Camera")) {
        sceneRenderer2D->ResetCamera();
    }
    
    ImGui::SameLine();
    ImGui::Text("Zoom:");
    ImGui::SameLine();
    static float zoom = 1.0f;
    ImGui::SetNextItemWidth(80);
    if (ImGui::SliderFloat("##zoom", &zoom, 0.1f, 5.0f, "%.1fx")) {
        sceneRenderer2D->SetCameraZoom(zoom);
    }
    
    ImGui::Separator();
}

void MainWindow::HandleViewportInteraction(ImVec2 viewportPos, ImVec2 viewportSize) {
    // Cek apakah mouse berada di dalam viewport
    ImVec2 mousePos = ImGui::GetMousePos();
    bool isHovered = ImGui::IsItemHovered();
    
    // Jika viewport dihover, tampilkan overlay informasi di pojok kanan bawah
    if (isHovered) {
        // ImGui::Text("Now Hovered View Port");
        // ImGui::SameLine();
        // Hitung posisi mouse relatif terhadap viewport (dalam piksel viewport)
        float viewportX = mousePos.x - viewportPos.x;
        float viewportY = mousePos.y - viewportPos.y;
        
        // Konversi koordinat viewport ke koordinat world (dengan memperhitungkan zoom/pan)
        glm::vec2 worldPos = sceneRenderer2D->ViewportToWorldPosition(viewportX, viewportY);
        
        // Tampilkan informasi koordinat di pojok kanan bawah viewport
        char coordText[64];
        snprintf(coordText, sizeof(coordText), "X: %.1f, Y: %.1f", worldPos.x, worldPos.y);
        
        ImVec2 textSize = ImGui::CalcTextSize(coordText);
        ImVec2 textPos = ImVec2(
            viewportPos.x + viewportSize.x - textSize.x - 10,
            viewportPos.y + viewportSize.y - textSize.y - 5
        );
        
        ImGui::GetWindowDrawList()->AddText(textPos, IM_COL32(0, 0, 0, 220), coordText);
        
        // Handling click untuk seleksi objek
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // ImGui::Text("Select Object");
            // ImGui::SameLine();
            cout << "Click" << endl;
            // projectHandler.sceneRenderer->HandleClick(worldPos.x, worldPos.y);
        }
        
        // Handling drag untuk move objek atau pan kamera
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            // ImGui::Text("Drag Object");
            // ImGui::SameLine();
            cout << "Dragging" << endl;
            ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            sceneRenderer2D->HandleDrag(delta.x, delta.y);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }
        
        // Handling zoom dengan mouse wheel
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0) {
            cout << "Handle Zoom" << endl;
            sceneRenderer2D->HandleZoom(wheel);
        }
        
        // Handling key input untuk precision movement
        ImGuiIO& io = ImGui::GetIO();
        if (sceneRenderer2D->HasSelectedObject()) {
            // cout << "Receive Input" << endl;
            float moveAmount = io.KeyShift ? 10.0f : 1.0f;
            
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                cout << "Left Arrow" << endl;
                sceneRenderer2D->MoveSelected(-moveAmount, 0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                cout << "Right Arrow" << endl;
                sceneRenderer2D->MoveSelected(moveAmount, 0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                cout << "Up Arrow" << endl;
                sceneRenderer2D->MoveSelected(0, -moveAmount);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                cout << "Down Arrow" << endl;
                sceneRenderer2D->MoveSelected(0, moveAmount);
            }
            
            // Delete key untuk menghapus objek
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                sceneRenderer2D->DeleteSelected();
            }
        }
    }
}

// void MainWindow::RenderGameViewport() {
//     ImVec2 contentSize = ImGui::GetContentRegionAvail();

//     // Render game scene texture (pastikan sceneRenderer telah di-init)
//     sceneRenderer2D->RenderSceneToUI();
//     // GLuint texID = sceneRenderer2D->CreateShaderProgram("assets/shaders/shader.frag", "assets/shaders/shader.vert");
//     // // GLuint texID = sceneRenderer2D->CreateWhiteTexture();
//     // if (texID == 0) {
//     //     // cout << "Viewport texture not initialized!" << endl;
//     //     ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "⚠ Viewport texture not initialized!");
//     // } else {
//     //     cout << "Viewport texture initialized!" << texID << endl;
//     //     ImGui::Image((ImTextureID)(intptr_t)texID, contentSize, ImVec2(0, 1), ImVec2(1, 0));
//     // }

//     // Optional debug footer
//     ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
//     ImGui::Text("Game Viewport (Scene will render here)");
// }


void MainWindow::RenderConsoleWindow() {
    if (!showConsole) return;
    // Set window properties
    ImGui::Begin("Console", &showConsole, ImGuiWindowFlags_NoCollapse);
    
    static int selectedTab = 0;
    ImGui::BeginTabBar("ConsoleTabs");
    
    if (ImGui::BeginTabItem("Output")) {
        selectedTab = 0;
        static char consoleBuffer[4096] = "Game initialized successfully.\nAll systems operational.\nReady to start...\n";
        // Concatenate logFromIlmeeeEditor vector into a single string
        std::string combinedLog;
        for (const auto& line : logFromIlmeeeEditor) {
            combinedLog += line + "\n";
        }
        // Copy to buffer and ensure null-termination
        strncpy(consoleBuffer, combinedLog.c_str(), sizeof(consoleBuffer) - 1);
        consoleBuffer[sizeof(consoleBuffer) - 1] = '\0';
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

        if (ImGui::BeginMenu("Windows")) {
            if (ImGui::MenuItem("Explorer", nullptr, &showExplorer)) {}
            if (ImGui::MenuItem("Inspector", nullptr, &showInspector)) {}
            if (ImGui::MenuItem("Scene", "Ctrl+1", &showScene)) {}
            if (ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy)) { RenderHierarchyWindow(); }
            if (ImGui::MenuItem("Console", "Ctrl+2", &showConsole)) {}
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
    // ImGui::PushItemWidth(-1);
    // Menampilkan input text dengan hint. Menunggu Enter untuk trigger pencarian.
    if (ImGui::InputTextWithHint("##search", "Search assets...", searchBuffer,
                                 IM_ARRAYSIZE(searchBuffer))) {
        projectHandler.currentFilter = searchBuffer;
        // Jika buffer tidak kosong, lakukan pencarian.
        if (strlen(searchBuffer) > 0 || projectHandler.currentFilter != "") {
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

    // Tombol tambahan untuk filter cepat
        ImGui::SameLine();
        if (ImGui::Button("Filter")) {
            ImGui::OpenPopup("FilterOptions");
            // showingFilterPopup = true;
        }
        
        // Popup filter
        if (ImGui::BeginPopup("FilterOptions")) {
            if (ImGui::MenuItem("All Files")) {
                projectHandler.currentFilter = "";
                strcpy(searchBuffer, "");
            }
            if (ImGui::MenuItem("Scripts (.cpp, .c)")) {
                projectHandler.currentFilter = ".cpp .c";
                strcpy(searchBuffer, ".cpp .c");
            }
            if (ImGui::MenuItem("Models (.fbx, .obj)")) {
                projectHandler.currentFilter = ".fbx .obj";
                strcpy(searchBuffer, ".fbx .obj");
            }
            if (ImGui::MenuItem("Images (.png, .jpg)")) {
                projectHandler.currentFilter = ".png .jpg .jpeg";
                strcpy(searchBuffer, ".png .jpg .jpeg");
            }
            ImGui::EndPopup();
            // showingFilterPopup = false;
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
                }
            }
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

void MainWindow::RenderPlayMenu() {
    ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    float buttonHeight = 20.0f;
    float toolbarHeight = buttonHeight;  // Tinggi window sama dengan tombol

    float menuBarHeight = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewportSize.x, toolbarHeight));

    ImGuiWindowFlags toolbar_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.0f)); // Semi-transparent background

    if (ImGui::Begin("PlayControlsToolbar", nullptr, toolbar_flags)) {
        ImVec2 buttonSize(30, buttonHeight);
        float spacing = 5.0f;
        float totalWidth = (buttonSize.x * 3) + (spacing * 2);
        float startX = (viewportSize.x - totalWidth) * 0.5f;

        ImGui::SetCursorPosX(startX);

        // Play
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
        if (ImGui::Button("##Play", buttonSize)) {
            Debug::Logger::Log("Starting game...", Debug::LogLevel::INFO);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Ctrl+P)");
        ImGui::PopStyleColor(3);

        // Pause
        ImGui::SameLine(0, spacing);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.8f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.6f, 0.1f, 1.0f));
        if (ImGui::Button("##Pause", buttonSize)) {
            Debug::Logger::Log("Pausing game...", Debug::LogLevel::INFO);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause (Ctrl+Shift+P)");
        ImGui::PopStyleColor(3);

        // Stop
        ImGui::SameLine(0, spacing);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("##Stop", buttonSize)) {
            Debug::Logger::Log("Stopping game...", Debug::LogLevel::INFO);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (Ctrl+S)");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);
    }
    ImGui::End();
    ImGui::PopStyleColor(1); // Pop background color
    ImGui::PopStyleVar(4);
}
