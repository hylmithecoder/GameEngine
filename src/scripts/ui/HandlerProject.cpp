#include <Debugger.hpp>
#include "MainWindow.hpp"
using namespace Debug;

void HandlerProject::OpenFolder() {
    NFD::Guard nfdGuard;

    // auto-freeing memory
    NFD::UniquePath outPath;

    // show the dialog
    nfdresult_t result = NFD::PickFolder(outPath);
        if (result == NFD_OKAY) {
            cout << outPath.get() << endl;
            OpenProject(outPath.get());
        } else if (result == NFD_CANCEL) {
            cout << "User pressed cancel." << endl;
        } else {
            cout << "Error: " << NFD::GetError() << endl;
        }
}

void HandlerProject::SaveNewScene() {
    NFD_Init();

    nfdchar_t* savePath;

    // prepare filters for the dialog
    nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeescene"}};

    // show the dialog
    nfdresult_t result = NFD_SaveDialog(&savePath, filterItem, 1, NULL, "Untitled.ilmeescene");
    if (result == NFD_OKAY) {
        // Extract just the filename from the full path
        std::string fullPath = savePath;
        std::string filename = fs::path(fullPath).stem().string();
        
        // Create the scene with just the filename
        NewScene(filename);
        
        // remember to free the memory
        NFD_FreePath(savePath);
        
        ShowNotification("Scene Created", "Created new scene: " + filename, 
            ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    } else if (result == NFD_CANCEL) {
        puts("User pressed cancel.");
    } else {
        printf("Error: %s\n", NFD_GetError());
        ShowNotification("Error", "Failed to create scene", 
            ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }

    NFD_Quit();
}

void HandlerProject::OpenScene() {
    NFD::Guard nfdGuard;
    NFD::UniquePath outPath;

    // Prepare filters for scene files
    nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeescene"}};

    try {
        // Show open file dialog
        nfdresult_t result = NFD::OpenDialog(outPath, filterItem, 1, projectPath.c_str());
        
        if (result == NFD_OKAY && outPath.get() != nullptr) {
            std::string scenePath = outPath.get();
            
            // Validate file extension
            if (fs::path(scenePath).extension() != ".ilmeescene") {
                ShowNotification("Error", "Invalid scene file format", 
                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                return;
            }

            // Try to load the scene
            try {
                currentScene = serializer.LoadScene(scenePath);
                std::string sceneName = fs::path(scenePath).stem().string();
                ShowNotification("Scene Loaded", 
                    "Successfully loaded scene: " + sceneName,
                    ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                
                // Update scene state
                isSceneLoaded = true;
                currentScenePath = scenePath;
                
            } catch (const std::exception& e) {
                ShowNotification("Load Error", 
                    "Failed to load scene: " + std::string(e.what()),
                    ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            }
        } else if (result == NFD_CANCEL) {
            // User cancelled - no need for notification
        } else {
            ShowNotification("Error", 
                "Failed to open file dialog: " + std::string(NFD::GetError()),
                ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
    } catch (const std::exception& e) {
        ShowNotification("Error", 
            "Unexpected error: " + std::string(e.what()),
            ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}

void HandlerProject::OpenFile() {
    NFD::Guard nfdGuard;
    NFD::UniquePathSet outPaths;

    // Define supported file types
    nfdfilteritem_t filterItem[4] = {
        {"Source Files", "c,cpp,h,hpp"},
        {"Images", "png,jpg,jpeg,gif,bmp"},
        {"Audio", "mp3,wav,ogg"},
        {"Video", "mp4,mkv,avi,mov"}
    };

    // Show the dialog with filters for multiple selection
    nfdresult_t result = NFD::OpenDialogMultiple(outPaths, filterItem, 4);
    
    if (result == NFD_OKAY) {
        nfdpathsetsize_t numPaths;
        NFD::PathSet::Count(outPaths, numPaths);

        for (nfdpathsetsize_t i = 0; i < numPaths; ++i) {
            NFD::UniquePathSetPath path;
            NFD::PathSet::GetPath(outPaths, i, path);
            
            std::string currentFile = path.get();
            std::cout << "Selected file " << i + 1 << ": " << currentFile << std::endl;
            
            // Store the current file path
            fileTargetImport = currentFile;
            
            // Determine appropriate subfolder based on file extension
            std::string ext = fs::path(currentFile).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            std::string targetFolder;
            if (ext == ".cpp" || ext == ".h" || ext == ".hpp") {
                targetFolder = projectPath + "/assets/scripts";
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp") {
                targetFolder = projectPath + "/assets/textures";
            } else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg") {
                targetFolder = projectPath + "/assets/audio";
            } else if (ext == ".mp4" || ext == ".mkv" || ext == ".avi" || ext == ".mov") {
                targetFolder = projectPath + "/assets/video";
            } else {
                targetFolder = projectPath + "/assets"; // Default to main assets folder
            }

            // Create target folder if it doesn't exist
            fs::create_directories(targetFolder);
            
            // Import the current file
            HandleImport(targetFolder);
        }

        // Show summary notification
        if (numPaths > 0) {
            ShowNotification("Import Complete", 
                "Imported " + std::to_string(numPaths) + " file(s)", 
                ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        }
        
    } else if (result == NFD_CANCEL) {
        std::cout << "User cancelled file selection." << std::endl;
    } else {
        std::cout << "Error: " << NFD::GetError() << std::endl;
        ShowNotification("Error", "Failed to open file dialog", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}

void HandlerProject::OpenProject(const char* folderPath) {
    // Check if folder path is valid
    isOpenedProject = true;
    struct stat info;
    if (stat(folderPath, &info) != 0 || !(info.st_mode & S_IFDIR)) {
        cerr << "Invalid project folder path: " << folderPath << endl;
        return;
    }

    // Store the project path
    projectPath = folderPath;
    
    // Create project structure if it doesn't exist
    vector<string> directories = {
        "/assets",
        "/assets/textures",
        "/assets/audio",
        "/assets/models",
        "/assets/scripts",
        "/scenes",
        "/build",
        "/config"
    };

    for (const auto& dir : directories) {
        string fullPath = string(folderPath) + dir;
        if (_mkdir(fullPath.c_str()) != 0 && errno != EEXIST) {
            cerr << "Error creating directory: " << fullPath << endl;
        }
    }

    // Load project configuration if exists
    string configPath = string(folderPath) + "/config/project.json";
    const char* readConfigPath = configPath.c_str();
    ifstream configFile(configPath);
    if (configFile.good()) {
        Debug::Logger::Log("Loading project configuration: " + string(readConfigPath), Debug::LogLevel::SUCCESS);
        configFile.close();
    } else {
        // Create default configuration
        string nameProject = fs::path(folderPath).stem().string();
        ofstream newConfigFile(configPath);
        if (newConfigFile.is_open()) {
            newConfigFile << "{\n";
            newConfigFile << "    \"projectName\": \"" + nameProject + "\",\n";
            newConfigFile << "    \"version\": \"1.0.0\",\n";
            newConfigFile << "    \"createdAt\": \"" << getCurrentDateTime() << "\"\n";
            newConfigFile << "}\n";
            newConfigFile.close();
        }
    }

    // Load project assets
    LoadProjectAssets();
    Debug::Logger::Log("Project loaded successfully: " + projectPath, Debug::LogLevel::SUCCESS);
    // MainWindow mainwindow(folderPath, 1280, 720);
}

void HandlerProject::ScanAssetsFolder(const std::string& rootFolder) {
    Debug::Logger::Log("Scanning Folder Root Project: "+rootFolder);
    assetFiles.clear();

    for (const auto& entry : fs::recursive_directory_iterator(rootFolder))
    {
        if (entry.is_regular_file())
        {
            std::string filePath = entry.path().string();
            std::string parentFolder = entry.path().parent_path().filename().string();
            std::string filename = entry.path().filename().string();

            Debug::Logger::Log("File Path: "+filePath+" Parent Folder: "+parentFolder+" File Name: "+filename, Debug::LogLevel::INFO);
            assetFiles[parentFolder].push_back(filename);
        }
    }
}

void HandlerProject::DrawAssetTree(const AssetFile& node) {
    // Define colors for different file types
    ImVec4 folderColor = ImVec4(1.0f, 0.87f, 0.36f, 1.0f);      // Yellow
    ImVec4 cppColor = ImVec4(0.46f, 0.78f, 1.0f, 1.0f);        // Light Blue
    ImVec4 hppColor = ImVec4(0.71f, 0.46f, 1.0f, 1.0f);        // Purple
    ImVec4 videoColor = ImVec4(1.0f, 0.44f, 0.37f, 1.0f);       // Red
    ImVec4 defaultFileColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);   // Light Gray
    
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    
    if (node.isDirectory) {
    // Simpan warna teks awal
    ImVec4 originalColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Atur warna folder dan tampilkan ikon folder
    ImGui::PushStyleColor(ImGuiCol_Text, folderColor);
    DrawIconFromImage("assets/images/fileicons/folder.png", 20, 20);
    ImGui::SameLine();
    std::string label = node.name;

    // Folder
    if (fileExplorerRenameTarget == node.fullPath) {
        // ImGui::PushStyleColor(ImGuiCol_Text, originalColor);
        HandleRenameFolder(node);        
    }
    else {
            bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags);
                ImGui::PopStyleColor(); // Kembalikan warna teks

                // Generate ID popup context berdasarkan fullPath untuk menghindari konflik
                std::string popupId = "context_menu_dir_" + node.fullPath;

                // Tampilkan popup context pada folder ketika klik kanan
                if (ImGui::BeginPopupContextItem(popupId.c_str())) {
                    if (ImGui::MenuItem(" Copy")) {
                        HandleCopy(node);
                    }

                    if (ImGui::MenuItem(" Paste")) {
                        HandlePaste(node.fullPath);
                    }
                    
                    if (ImGui::MenuItem(" Import")) {
                        OpenFile();
                    }

                    if (ImGui::MenuItem(" Create New Folder")) {
                        fileExplorerRenameBufferSet = true;
                        HandleCreateNewFolder(node.fullPath);
                    }
                    // Opsi Delete
                    if (ImGui::MenuItem(" Delete")) {
                        if (MessageBoxA(NULL,
                            ("Are you sure you want to delete " + node.name + "?").c_str(),
                            "Confirm Delete",
                            MB_YESNO | MB_ICONWARNING) == IDYES) {
                            DeleteFolder(node.fullPath);
                        }
                    }

                    // Panggil method untuk membuat file baru di folder yang diklik
                    HandleCreateNewFile(node.fullPath);

                    if (ImGui::MenuItem(" Rename")) {
                        fileExplorerRenameTarget = node.fullPath;
                        strcpy(renameBuffer, node.name.c_str());                
                        // HandleRename(node.fullPath);
                    }

                    ImGui::EndPopup();
                }
                // ImGui::EndGroup();

                // Jika node terbuka, render child-nya
                if (nodeOpen) {
                        for (const auto& child : node.children) {
                            DrawAssetTree(child);
                        }
                        ImGui::TreePop();
                    }
            }
    }
    // File
    else {
        // Check file extension
        std::string ext = fs::path(node.name).extension().string();
        bool isCpp = (ext == ".cpp");
        bool isHpp = (ext == ".hpp");
        bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".m4a" || ext == ".avi" || ext == ".mov");
        bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp");
        bool isAudio = (ext == ".mp3" || ext == ".wav" || ext == ".ogg");
        bool isShader = (ext == ".glsl" || ext == ".shader" || ext == ".frag" || ext == ".vert");
        
        // Generate unique popup ID using file path to avoid conflicts
        std::string popupId = "context_menu_" + node.fullPath;
        
        // Start group for the selectable item with icon
        ImGui::BeginGroup();        
        // DrawIconFromImage("assets/images/fileicons/folder.png");
        // Choose appropriate icon and color based on file type
        const char* icon = ICON_FA_FILE;
        ImVec4 fileColor = defaultFileColor;
        
        if (isCpp) {
            DrawIconFromImage("assets/images/fileicons/c-.png", 20, 20);
            icon = ICON_FA_CODE;
            fileColor = cppColor;
        }
        else if (isHpp) {
            DrawIconFromImage("assets/images/fileicons/c-.png", 20, 20);
            icon = ICON_FA_CODE;
            fileColor = hppColor;
        }
        else if (isVideo) {
            DrawIconFromImage("assets/images/fileicons/video.png", 20, 20);
            icon = ICON_FA_FILM;
            fileColor = videoColor;
        }
        else if (isImage) {
            DrawIconFromImage("assets/images/fileicons/image.png", 20, 20);
            icon = ICON_FA_IMAGE;
            fileColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);  // Green
        }
        else if (isAudio) {
            icon = ICON_FA_MUSIC;
            fileColor = ImVec4(0.8f, 0.4f, 0.8f, 1.0f);  // Purple
        }
        else if (isShader) {
            icon = ICON_FA_MICROCHIP;
            fileColor = ImVec4(0.4f, 0.8f, 0.8f, 1.0f);  // Cyan
        }
        
        // Push color for the icon and text
        ImGui::PushStyleColor(ImGuiCol_Text, fileColor);
        
        // Create label with icon
        std::string label = node.name;

        if (renamingPath == node.fullPath) {
            HandleRename(node);
        } 
        else {
        // Ini adalah UI File nya
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (isCpp) {
#ifdef _WIN32
                    std::string cmd = "code \"" + projectPath + "\"";
                    system(cmd.c_str());
#else
                    std::string cmd = "code \"assets\"";
                    system(cmd.c_str());
#endif
                }
                else if (isVideo) {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
                    std::string cmd = "xdg-open \"" + node.fullPath + "\"";
                    system(cmd.c_str());
#endif
                }
                else if (isImage || isAudio) {
#ifdef _WIN32
                    ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
                    std::string cmd = "xdg-open \"" + node.fullPath + "\"";
                    system(cmd.c_str());
#endif
                }
            }
        }
        
        // Context menu for files
        if (ImGui::BeginPopupContextItem(popupId.c_str())) {
            // ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.0f, 1.0f));
            // DrawIconFromImage("assets/images/fileicons/trash.png");
            if (ImGui::MenuItem(" Copy")) {
                HandleCopy(node);
            }

            if (ImGui::MenuItem(" Paste")) {
                HandlePaste(fileExplorerCopyTarget);
            }
            
            if (ImGui::MenuItem(" Delete")) {
                if (MessageBoxA(NULL,
                    ("Are you sure you want to delete " + node.name + "?").c_str(),
                    "Confirm Delete",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {
                    DeleteFileOrFolder(node.fullPath);
                }
            }
            
            if (ImGui::MenuItem(" Edit")) {
                // Open in appropriate editor based on file type
                std::string cmd;
#ifdef _WIN32
                cmd = "code \"" + node.fullPath + "\"";
#else
                cmd = "code \"" + node.fullPath + "\"";
#endif
                system(cmd.c_str());
            }
            
            if (ImGui::MenuItem(" Rename")) {
                renamingPath = node.fullPath;
                strcpy(renameBuffer, node.name.c_str());                
                // HandleRename(node.fullPath);
            }
            
            ImGui::EndPopup();
        }
        
        // Pop color style
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        }
    }
}

void HandlerProject::HandleRenameFileOrFolder(const AssetFile& node) {
    ImGui::PushID(node.fullPath.c_str());
    ImGui::SetNextItemWidth(200);

    if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        
        std::string newName = renameBuffer;
        if (newName.empty()) {
            ShowNotification("Rename Failed", "Name cannot be empty", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        } else {
            std::string newPath = fs::path(node.fullPath).parent_path().string() + "/" + newName;
            
            // If it's a file, preserve the extension
            if (!node.isDirectory) {
                std::string ext = fs::path(node.fullPath).extension().string();
                if (!ext.empty() && fs::path(newName).extension().string().empty()) {
                    newPath += ext;
                }
            }

            try {
                if (!fs::exists(newPath)) {
                    fs::rename(node.fullPath, newPath);
                    ShowNotification("Renamed", 
                        node.name + " renamed to " + fs::path(newPath).filename().string(), 
                        ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    
                    // Trigger refresh
                    isOpenedProject = true;
                } else {
                    ShowNotification("Rename Failed", 
                        "A file or folder with this name already exists", 
                        ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
            } catch (const std::exception& e) {
                ShowNotification("Rename Failed", e.what(), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            }
        }
        fileExplorerRenameTarget.clear();
    }

    // Cancel rename if clicked outside
    if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
        fileExplorerRenameTarget.clear();
    }

    ImGui::PopID();
}

void HandlerProject::DrawFileExplorer(AssetFile& node)
{
    static string localPath = "";
    // Debug::Logger::Log("Drawing File Explorer: " + node.name, Debug::LogLevel::INFO);
    ImGui::PushID(node.fullPath.c_str());

    float itemWidth = thumbnailSize.x + itemSpacing * 2;
    float itemHeight = thumbnailSize.y + ImGui::GetFontSize() + 8.0f;
    ImVec2 totalSize(itemWidth, itemHeight);

    // Simpan posisi awal
    ImVec2 cursorPos = ImGui::GetCursorScreenPos();

    // InvisibleButton sebagai bounding box klik
    float currentTime = ImGui::GetTime();
    
    // Periksa apakah ini double-click (hanya jika node yang sama yang diklik sebelumnya)
    bool isDoubleClick = false;
    if (localPath == node.fullPath && (currentTime - node.lastClickTime) < doubleClickTime) {
        isDoubleClick = true;
    }

    if (ImGui::InvisibleButton("##ItemButton", totalSize)) {
        if (isDoubleClick) {
            Logger::Log("Double-clicked: " + node.name);
            
            if (node.isDirectory) {
                Logger::Log("Opening folder: " + node.name);
                currentDirectory = node.fullPath;
                Debug::Logger::Log("Current Directory changed to: " + currentDirectory, Debug::LogLevel::SUCCESS);
                // Reset selection setelah pindah folder
                selectedAsset = nullptr;
                ShowNotification("Opening folder: " + node.name, "Explorer", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            } else {
                HandlerOpenFileWithExtensionName(node);
                Logger::Log("Opening file: " + node.name);                
                selectedAsset = const_cast<AssetFile*>(&node);
                ShowNotification("Opening file: " + node.name, "Explorer", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                if (onFileClicked) onFileClicked(node);
            }

            // Reset lastClickTime untuk mencegah triple-click
            node.lastClickTime = 0.0f;

        } else {
            // Single click - hanya select item, jangan ubah directory
            Logger::Log("Time From Struct AssetFile: "+to_string(node.lastClickTime) +" Time Now: "+to_string(currentTime), LogLevel::SUCCESS);
            selectedAsset = nullptr;
            localPath = node.fullPath;
            Debug::Logger::Log("Current Directory changed to: " + localPath, Debug::LogLevel::SUCCESS);
            node.lastClickTime = currentTime;
            
            ShowNotification("Selected: " + node.name, "Explorer", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
        }
    }

    // Highlight jika terpilih
    if (node.fullPath == localPath) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(cursorPos, ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.6f, 1.0f, 0.2f)), 4.0f);
        drawList->AddRect(cursorPos, ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.6f, 1.0f, 1.0f)), 4.0f, 0, 2.0f);
    }

    // Gambar ikon di tengah
    float iconPosX = cursorPos.x + (itemWidth - thumbnailSize.x) * 0.5f;
    float iconPosY = cursorPos.y + 4.0f;
    ImGui::GetWindowDrawList()->AddImage(
        GetIconForFile(node).textureId,
        ImVec2(iconPosX, iconPosY),
        ImVec2(iconPosX + thumbnailSize.x, iconPosY + thumbnailSize.y)
        // ImVec2(0, 1),
        // ImVec2(1, 0)
    );

    // Nama file
    std::string displayName = node.name;
    if (displayName.length() > 15) {
        displayName = displayName.substr(0, 12) + "...";
    }

    ImVec2 textSize = ImGui::CalcTextSize(displayName.c_str());
    float textPosX = cursorPos.x + (itemWidth - textSize.x) * 0.5f;
    float textPosY = iconPosY + thumbnailSize.y + 4.0f;

    ImGui::GetWindowDrawList()->AddText(ImVec2(textPosX, textPosY), 
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.9f, 0.9f, 0.9f, 1.0f)), 
        displayName.c_str());

    // Context menu
    if (fileExplorerRenameTarget == node.fullPath) {
        HandleRenameOperation(node, cursorPos, itemWidth, itemHeight);
    } else {
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Rename")) {
                fileExplorerRenameTarget = node.fullPath;
                strcpy(renameBuffer, node.name.c_str());
            }
            
            if (ImGui::MenuItem("Delete")) {
                if (MessageBoxA(NULL,
                    ("Are you sure you want to delete " + node.name + "?").c_str(),
                    "Confirm Delete",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {
                    if (node.isDirectory) {
                        DeleteFolder(node.fullPath);
                    } else {
                        DeleteFileOrFolder(node.fullPath);
                    }
                }
            }


            if (!node.isDirectory) {                
                ImGui::Separator();      
                if (ImGui::MenuItem("Open")) {
                    HandlerOpenFileWithExtensionName(const_cast<AssetFile&>(node));
                }
                if (ImGui::MenuItem("Copy")) {
                    HandleCopy(node);
                }
            }

            ImGui::EndPopup();
        }
    }
    
    // Next item posisi horizontal
    ImGui::SameLine(0, itemSpacing);
    if (ImGui::GetCursorPosX() + itemWidth > ImGui::GetWindowContentRegionMax().x) {
        ImGui::NewLine();
    }

    ImGui::PopID();
}

void HandlerProject::DrawFolderGridView() {
    // Gunakan currentDirectory yang sudah di-update dari double-click
    std::vector<AssetFile> localFiles = GetFilesInDirectory(currentDirectory);

    // Define rootDirectory as the initial projectPath or another appropriate root
    std::string rootDirectory = projectPath+"\\assets";

    ImGui::SetNextItemWidth(100);
    // ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::SliderFloat("Size", &thumbnailSize.x, 32.0f, 96.0f);
    thumbnailSize.y = thumbnailSize.x;

    // Tampilkan current directory path
    ImGui::Text("Current: %s", currentDirectory.c_str());
    
    ImGui::Separator();

    if (ImGui::BeginChild("FileGrid", ImVec2(0, 0), false)) {
        float contentWidth = ImGui::GetContentRegionAvail().x;
        int itemsPerRow = static_cast<int>(contentWidth / (thumbnailSize.x + itemSpacing * 2));
        if (itemsPerRow < 1) itemsPerRow = 1;

        // Filter files berdasarkan currentFilter
        std::vector<AssetFile> filteredFiles;
        for (const auto& file : localFiles) {
            if (currentFilter.empty() || file.name.find(currentFilter) != std::string::npos) {
                filteredFiles.push_back(file);
            }
        }

        // Sort: directories first, then alphabetically
        std::sort(filteredFiles.begin(), filteredFiles.end(),
            [](const AssetFile& a, const AssetFile& b) {
                if (a.isDirectory && !b.isDirectory) return true;
                if (!a.isDirectory && b.isDirectory) return false;
                return a.name < b.name;
            });

        // Draw each file/folder
        for (auto& file : filteredFiles) {
            // Debug::Logger::Log("Drawing file in grid: " + file.name, Debug::LogLevel::SUCCESS);
            DrawFileExplorer(file);
        }
        // Draw the "New Folder" button
        if (ImGui::BeginPopupContextWindow("FileGridContentMenu", 
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            
            bool isInRootDirectory = (currentDirectory == projectPath + "\\assets");
            
            if (ImGui::BeginMenu("Create New")) {
                if (ImGui::MenuItem("Folder")) {
                    HandleCreateNewFolder(currentDirectory);
                }
                
                if (ImGui::BeginMenu("Script")) {
                    if (ImGui::BeginMenu("C++ Script")) {
                        HandleCreateNewFile(currentDirectory);
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Shader")) {
                        // HandleCreateShader(currentDirectory);
                        ShowNotification("New Shader", "Creating new shader...", 
                            ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                    }
                    ImGui::EndMenu();
                }

                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Paste", nullptr, false, !fileExplorerCopyTarget.empty())) {
                HandlePaste(currentDirectory);
            }

            if (ImGui::MenuItem("Import Files...")) {
                OpenFile();
            }

            if (!isInRootDirectory) {
                ImGui::Separator();
                if (ImGui::MenuItem("Show in Explorer")) {
                    #ifdef _WIN32
                        ShellExecuteA(NULL, "explore", currentDirectory.c_str(), 
                            NULL, NULL, SW_SHOW);
                    #else
                        std::string cmd = "xdg-open \"" + currentDirectory + "\"";
                        system(cmd.c_str());
                    #endif
                }
            }

            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    // ImGui::PopStyleColor();
}

void HandlerProject::NewScripts(const std::string& scriptName) {
    string scriptPath = projectPath + "/assets/scripts/";
    string headerPath = projectPath + "/assets/scripts/header/";
    string fullPath = scriptPath + scriptName + ".cpp";
    
    // Create scripts directory if it doesn't exist
    if (_mkdir(scriptPath.c_str()) != 0 && errno != EEXIST || _mkdir(headerPath.c_str()) != 0 && errno != EEXIST) {
        cerr << "Error creating scripts directory" << endl;
        return;
    }

    // Check if file already exists
    ifstream checkFile(fullPath.c_str());
    if (checkFile.good()) {
        cerr << "Script file already exists: " << fullPath << endl;
        checkFile.close();
        return;
    }

    // Create and write the new script file
    ofstream scriptFile(fullPath);
    if (scriptFile.is_open()) {
        // Write template code
        scriptFile << "#include \"" << scriptName << ".hpp\"\n\n";
        scriptFile << "class " << scriptName << " {\n";
        scriptFile << "private:\n";
        scriptFile << "    // Add private members here\n\n";
        scriptFile << "public:\n";
        scriptFile << "    " << scriptName << "() {\n";
        scriptFile << "        // Constructor\n";
        scriptFile << "    }\n\n";
        scriptFile << "    void Start() {\n";
        scriptFile << "        // Called when script instance is being loaded\n";
        scriptFile << "    }\n\n";
        scriptFile << "    void Update() {\n";
        scriptFile << "        // Called every frame\n";
        scriptFile << "    }\n\n";
        scriptFile << "    ~" << scriptName << "() {\n";
        scriptFile << "        // Destructor\n";
        scriptFile << "    }\n";
        scriptFile << "};\n";
        
        scriptFile.close();

        // Create corresponding header file
        string headerFullPath = headerPath + scriptName + ".hpp";
        ofstream headerFile(headerFullPath);
        if (headerFile.is_open()) {
            headerFile << "#pragma once\n";
            headerFile << "#include <GameEngine.h>\n\n";
            headerFile << "class " << scriptName << " {\n";
            headerFile << "public:\n";
            headerFile << "    " << scriptName << "();\n";
            headerFile << "    void Start();\n";
            headerFile << "    void Update();\n";
            headerFile << "    ~" << scriptName << "();\n";
            headerFile << "};\n";
            headerFile.close();
        }

        cout << "Created script: " << fullPath << endl;
        cout << "Created Header: " << headerFullPath << endl;
        if (MessageBoxA(NULL, 
            ("New script created successfully. Do you want to open the file?"),
            "GameEngine SDL",
            MB_YESNO | MB_ICONINFORMATION) == IDYES)
        {
            OpenFile(projectPath);
        }
    } else {
        cerr << "Error creating script file: " << fullPath << endl;
    }
}

void HandlerProject::OpenFile(const std::string& fileName) {
    #ifdef _WIN32
        std::string cmd = "code \"" + fileName + "\"";
        system(cmd.c_str());
    #else
        std::string cmd = "code \"assets\"";
        system(cmd.c_str());
    #endif
}

void HandlerProject::DeleteFileOrFolder(const std::string& filePath) {
    if (remove(filePath.c_str()) != 0) {
        cerr << "Error deleting file/folder: " << filePath << endl;
    } else {
        cout << "Deleted file/folder: " << filePath << endl;
        // Refresh 
        isOpenedProject = true;
    }
}

void HandlerProject::ShowNotification(const std::string& title, const std::string& message, ImVec4 color) {
    
    notifications.push_back(Notification{
        title,
        message,
        color,
        static_cast<float>(ImGui::GetTime()),
        3.0f  // Duration in seconds
    });
}

void HandlerProject::RenderNotifications() {
    float padding = 10.0f;
    float notificationWidth = 300.0f;
    float startY = padding;
    float currentTime = ImGui::GetTime();
    
    // Process notifications from oldest to newest
    for (int i = 0; i < notifications.size(); i++) {
        auto& notification = notifications[i];
        
        // Check if notification has expired
        if (currentTime > notification.startTime + notification.duration) {
            // Remove expired notification
            notifications.erase(notifications.begin() + i);
            i--; // Adjust index after removal
            continue;
        }
        
        // Calculate fade based on time remaining
        float timeLeft = (notification.startTime + notification.duration) - currentTime;
        float alpha = (timeLeft < 1.0f) ? timeLeft : 1.0f;
        
        // Position notification in top-right corner
        ImVec2 windowSize = ImGui::GetMainViewport()->Size;
        ImGui::SetNextWindowPos(ImVec2(windowSize.x - notificationWidth - padding, startY));
        ImGui::SetNextWindowSize(ImVec2(notificationWidth, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.9f * alpha);
        
        // Create unique ID for each notification
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));
        
        std::string windowID = "Notification_" + std::to_string(i);
        if (ImGui::Begin(windowID.c_str(), nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | 
                        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | 
                        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings)) {
            
            // Title with color
            ImGui::PushStyleColor(ImGuiCol_Text, notification.color);
            // DrawIconFromImage("assets/images/fileicons/info.png");
            ImGui::Text("%s", notification.title.c_str());
            ImGui::PopStyleColor();
            
            // Message
            ImGui::Spacing();
            ImGui::TextWrapped("%s", notification.message.c_str());
            
            // Progress bar
            ImGui::Spacing();
            float progress = 1.0f - (timeLeft / notification.duration);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, notification.color);
            ImGui::ProgressBar(progress, ImVec2(-1, 5.0f), "");
            ImGui::PopStyleColor();
            
            ImGui::End();
        }
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        
        // Update starting Y position for next notification
        startY += ImGui::GetItemRectSize().y + padding;
    }
}

void HandlerProject::DrawIconFromImage(const char* iconPath, int width, int height) {
    // ImTextureID texID = assets.LoadTextureFromFile(iconPath, &icon_texture_data); // bikin sendiri atau cache
    ImTextureID texID = GetCachedIcon(iconPath);
    if (texID) {
        ImGui::Image(texID, ImVec2(width, height));
        ImGui::SameLine();
    }
}

HandlerProject::IconInfo HandlerProject::LoadCachedTexture(const std::string& path) {
    // Sudah ada di cache? Return langsung
    auto it = iconCacheInfo.find(path);
    if (it != iconCacheInfo.end()) return it->second;

    // Load dari file
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true); // Wajib sebelum stbi_load
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load icon: " << path << std::endl;
        return { (ImTextureID)0, 0, 0 };
    }

    // Buat OpenGL texture (atau sesuai renderer kamu)
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // filter bisa disesuaikan
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    FlipImageVertically(data, width, height, channels);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    stbi_image_free(data);

    IconInfo info;
    info.textureId = (ImTextureID)(intptr_t)textureId;
    info.width = width;
    info.height = height;

    iconCacheInfo[path] = info;
    return info;
}

ImTextureID HandlerProject::GetCachedIcon(const std::string& path) {
    auto it = iconCache.find(path);
    if (it != iconCache.end())
        return it->second;

    int w, h, channels;
    // stbi_set_flip_vertically_on_load(true); // Wajib sebelum stbi_load
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data) return (ImTextureID)0;

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(data);

    iconCache[path] = (ImTextureID)(intptr_t)tex;
    return iconCache[path];
}

void HandlerProject::StartFileWatcher() {
    // Check if watcher is already running
    if (fileWatcherRunning) {
        ShowNotification(" Info", "File watcher is already running", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
        return;
    }
    
    // Initialize file watcher
    fileWatcherRunning = true;
    fileWatcherInterval = std::chrono::milliseconds(1000); // Check every second
    fileChangesDetected = false;
    
    // Initialize file timestamps with current state
    fileTimestamps.clear();
    SnapshotProjectFiles();
    
    // Start watcher thread
    fileWatcherThread = std::thread(&HandlerProject::FileWatcherLoop, this);
    
    ShowNotification(" File Watcher", "Started monitoring project files", ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
    cout << "File watcher started" << endl;
}

void HandlerProject::StopFileWatcher() {
    if (!fileWatcherRunning) {
        return;
    }
    
    // Signal the watcher thread to stop
    {
        std::lock_guard<std::mutex> lock(fileWatcherMutex);
        fileWatcherRunning = false;
    }
    
    // Notify watcher to wake up and terminate
    fileWatcherCV.notify_one();
    
    // Wait for thread to finish
    if (fileWatcherThread.joinable()) {
        fileWatcherThread.join();
    }
    
    ShowNotification(" File Watcher", "Stopped monitoring project files", ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
    cout << "File watcher stopped" << endl;
}

void HandlerProject::FileWatcherLoop() {
    while (true) {
        // Wait for interval or stop signal
        {
            std::unique_lock<std::mutex> lock(fileWatcherMutex);
            fileWatcherCV.wait_for(lock, fileWatcherInterval, [this]() { return !fileWatcherRunning; });
            
            // Check if we're supposed to stop
            if (!fileWatcherRunning) {
                break;
            }
        }
        
        // Check for file changes
        if (CheckForFileChanges()) {
            // Signal main thread that changes were detected
            fileChangesDetected = true;
        }
    }
}

void HandlerProject::SnapshotProjectFiles() {
    try {
        // Clear existing timestamps
        fileTimestamps.clear();
        
        // Recursively scan the project directory
        for (const auto& entry : fs::recursive_directory_iterator(projectPath)) {
            const auto path = entry.path().string();
            const auto lastWriteTime = fs::last_write_time(entry);
            const auto timePoint = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                lastWriteTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            const auto timestamp = std::chrono::system_clock::to_time_t(timePoint);
            
            fileTimestamps[path] = timestamp;
        }
    } catch (const fs::filesystem_error& e) {
        cerr << "Error in SnapshotProjectFiles: " << e.what() << endl;
    }
}

bool HandlerProject::CheckForFileChanges() {
    bool changesDetected = false;
    
    try {
        // Track new or modified files
        std::vector<std::string> newFiles;
        std::vector<std::string> modifiedFiles;
        std::vector<std::string> deletedFiles;
        
        // Create a copy of current timestamps to identify deleted files
        auto currentTimestamps = fileTimestamps;
        
        // Recursively scan the project directory for new/modified files
        for (const auto& entry : fs::recursive_directory_iterator(projectPath)) {
            const auto path = entry.path().string();
            const auto lastWriteTime = fs::last_write_time(entry);
            const auto timePoint = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                lastWriteTime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            const auto timestamp = std::chrono::system_clock::to_time_t(timePoint);
            
            // Remove from the current timestamps map (remaining entries will be deleted files)
            currentTimestamps.erase(path);
            
            // Check if file is new
            auto it = fileTimestamps.find(path);
            if (it == fileTimestamps.end()) {
                // New file found
                newFiles.push_back(path);
                fileTimestamps[path] = timestamp;
                changesDetected = true;
            } 
            // Check if file was modified
            else if (timestamp > it->second) {
                // Modified file found
                modifiedFiles.push_back(path);
                it->second = timestamp;
                changesDetected = true;
            }
        }
        
        // Any remaining entries in the copy must be deleted files
        for (const auto& [path, timestamp] : currentTimestamps) {
            deletedFiles.push_back(path);
            fileTimestamps.erase(path);
            changesDetected = true;
        }
        
        // Handle detected changes
        if (changesDetected) {
            // Log changes
            for (const auto& path : newFiles) {
                cout << "New file detected: " << path << endl;
            }
            
            for (const auto& path : modifiedFiles) {
                cout << "Modified file detected: " << path << endl;
            }
            
            for (const auto& path : deletedFiles) {
                cout << "Deleted file detected: " << path << endl;
            }
            
            // Only show notification for significant changes
            if (!newFiles.empty() || !deletedFiles.empty() || modifiedFiles.size() > 2) {
                std::string message = "";
                if (!newFiles.empty()) {
                    message += std::to_string(newFiles.size()) + " new, ";
                }
                if (!modifiedFiles.empty()) {
                    message += std::to_string(modifiedFiles.size()) + " modified, ";
                }
                if (!deletedFiles.empty()) {
                    message += std::to_string(deletedFiles.size()) + " deleted, ";
                }
                
                // Remove trailing comma and space
                if (!message.empty()) {
                    message = message.substr(0, message.length() - 2);
                    message += " files detected";
                }
                
                ShowNotification(" Files Changed", message, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            }
        }
        
    } catch (const fs::filesystem_error& e) {
        Debug::Logger::Log("Error in CheckForFileChanges: " + std::string(e.what()), Debug::LogLevel::CRASH);
    }
    
    return changesDetected;
}

// Call this method from your main loop
void HandlerProject::HandleFileChanges() {
    // Check if file changes were detected
    if (fileChangesDetected) {
        // Reset flag
        fileChangesDetected = false;
        
        // Rebuild asset tree to reflect changes
        BuildAssetTree(projectPath);
        
        // Log that assets were refreshed
        cout << "Asset tree refreshed due to file changes" << endl;
    }
}

// Method to actively check for changes when needed
void HandlerProject::CheckAndRefreshAssets() {
    if (CheckForFileChanges()) {
        isOpenedProject = true;
        // ShowNotification("Assets Refreshed", "Project files have been updated", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
    } else {
        // ShowNotification("Assets", "Already up to date", ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
    }
}

void HandlerProject::SearchFileOrFolder(const AssetFile& node, const std::string& query, std::vector<AssetFile>& results) {
    // Case-insensitive search
    std::string nameLower = node.name;
    std::string queryLower = query;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
    std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

    if (nameLower.find(queryLower) != std::string::npos) {
        results.push_back(node);
    }

    if (node.isDirectory) {
        for (const auto& child : node.children) {
            SearchFileOrFolder(child, query, results);
        }
    }
}

void HandlerProject::NewScene(const std::string& name) {
    std::string sceneFolder = projectPath + "/assets/scenes";
    fs::create_directories(sceneFolder);  // pastikan foldernya ada

    std::string fullPath = sceneFolder + "/" + name + ".ilmeescene";

    // 1. Buat file scene kosong
    std::ofstream outFile(fullPath);
    if (outFile.is_open()) {
        outFile << "{\n\t\"sceneName\": \"" << name << "\",\n\t\"objects\": []\n}";
        outFile.close();

        // 2. Load langsung file ke currentScene (gunakan SceneSerializer)
        try {
            // currentScene = serializer.LoadScene(fullPath);

            ShowNotification("Scene Created & Loaded",
                             "Scene " + name + " berhasil dibuat dan dimuat.",
                             ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        } catch (const std::exception& e) {
            ShowNotification("Load Failed",
                             std::string("Scene dibuat tapi gagal dimuat: ") + e.what(),
                             ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
        }
    } else {
        ShowNotification("Failed", "Gagal membuat scene!",
                         ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}


void HandlerProject::HandleCreateNewFile(const std::string &targetFolder) {
    static bool isCreating = false;
    static bool isScriptFile = false;
    static std::string createdFilePath;
    static std::string createdCPPPath;
    static std::string createdHPPPath;
    static char renameBuffer[256] = "";

    if (ImGui::BeginMenu(" Create New File")) {
        if (ImGui::MenuItem("Empty File")) {
            std::string defaultName = "NewFile.txt";
            createdFilePath = targetFolder + "/" + defaultName;
            std::ofstream ofs(createdFilePath); ofs.close();
            strncpy(renameBuffer, "NewFile", sizeof(renameBuffer));
            isCreating = true;
            isScriptFile = false;
        }

        if (ImGui::BeginMenu("Scripts")) {
            if (ImGui::MenuItem("C++ Script")) {
                std::string baseName = "NewScript";
                createdCPPPath = targetFolder + "/" + baseName + ".cpp";
                createdHPPPath = targetFolder + "/" + baseName + ".hpp";

                // .cpp template
                std::ofstream cpp(createdCPPPath);
                if (cpp.is_open()) {
                // Write template code
                cpp << "#include \"" << baseName << ".hpp\"\n\n";
                cpp << "class " << baseName << " {\n";
                cpp << "private:\n";
                cpp << "    // Add private members here\n\n";
                cpp << "public:\n";
                cpp << "    " << baseName << "() {\n";
                cpp << "        // Constructor\n";
                cpp << "    }\n\n";
                cpp << "    void Start() {\n";
                cpp << "        // Called when script instance is being loaded\n";
                cpp << "    }\n\n";
                cpp << "    void Update() {\n";
                cpp << "        // Called every frame\n";
                cpp << "    }\n\n";
                cpp << "    ~" << baseName << "() {\n";
                cpp << "        // Destructor\n";
                cpp << "    }\n";
                cpp << "};\n";
                cpp.close();
                }

                // .hpp template
                std::ofstream hpp(createdHPPPath);
                if (hpp.is_open()) {
                    hpp << "#pragma once\n\n";
                    hpp << "class " << baseName << " {\n";
                    hpp << "public:\n";
                    hpp << "    " << baseName << "();\n";
                    hpp << "    void Start();\n";
                    hpp << "    void Update();\n";
                    hpp << "    ~" << baseName << "();\n";
                    hpp << "};\n";
                    hpp.close();
                }

                strncpy(renameBuffer, "NewScript", sizeof(renameBuffer));
                isCreating = true;
                isScriptFile = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    // Menampilkan InputText jika file sedang dibuat
    if (isCreating) {
        ImGui::PushID("CreateNewFileRename");
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("##newfilename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {

            std::string newBaseName = renameBuffer[0] ? renameBuffer : (isScriptFile ? "NewScript" : "NewFile");

            if (isScriptFile) {
                std::string newCPPPath = targetFolder + "/" + newBaseName + ".cpp";
                std::string newHPPPath = targetFolder + "/" + newBaseName + ".hpp";
                if (!std::filesystem::exists(newCPPPath) && !std::filesystem::exists(newHPPPath)) {
                    std::filesystem::rename(createdCPPPath, newCPPPath);
                    std::filesystem::rename(createdHPPPath, newHPPPath);
                    ShowNotification("Created", "Script created: " + newBaseName, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                } else {
                    ShowNotification("Rename Failed", "File already exists.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
            } else {
                std::string newPath = targetFolder + "/" + newBaseName + ".txt";
                if (!std::filesystem::exists(newPath)) {
                    std::filesystem::rename(createdFilePath, newPath);
                    ShowNotification("Created", "File created: " + newBaseName, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                } else {
                    ShowNotification("Rename Failed", "File already exists.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
            }

            isCreating = false;
        }

        // Batal rename jika klik di luar input
        if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
            isCreating = false;
        }
        ImGui::PopID();
    }
}

void HandlerProject::HandleRename(const AssetFile& node) {
    ImGui::PushID(node.fullPath.c_str());
    ImGui::SetNextItemWidth(200);

        if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                std::string newPath = fs::path(node.fullPath).parent_path().string() + "/" + renameBuffer;
                if (!fs::exists(newPath)) {
                    fs::rename(node.fullPath, newPath);
                    ShowNotification("Renamed", node.name + " renamed to " + renameBuffer, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                } else {
                    ShowNotification("Rename Failed", "File already exists.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
                renamingPath.clear();
            }

            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
                renamingPath.clear();
            }

            ImGui::PopID();
            ImGui::PopStyleColor();
            ImGui::EndGroup();
}

void HandlerProject::HandleCreateNewFolder(const std::string &targetFolder) {
    static char newFolderNameBuffer[256] = "New Folder";

    std::string newFolderPath = targetFolder + "/" + std::string(newFolderNameBuffer);
            try {
            // Membuat folder baru secara rekursif, jika belum ada
            bool created = std::filesystem::create_directory(newFolderPath);
            if (created) {
                std::cout << "Folder created: " << newFolderPath << std::endl;
            } else {
                        // Jika folder sudah ada atau gagal dibuat tanpa exception
                std::cerr << "Failed to create folder (may already exist): " << newFolderPath << std::endl;
            }
            } catch (const std::filesystem::filesystem_error &e) {
                std::cerr << "Error creating folder: " << e.what() << std::endl;
            }
}

void HandlerProject::DeleteFolder(const std::string& folderPath) {
    // Menggunakan std::error_code untuk menangani error tanpa melempar exception
    std::error_code ec;
    // Hapus folder beserta isinya secara rekursif
    std::uintmax_t numRemoved = std::filesystem::remove_all(folderPath, ec);

    if (ec) {
        std::cerr << "Error deleting folder: " << folderPath << " - " 
                  << ec.message() << std::endl;
    } else {
        std::cout << "Deleted folder: " << folderPath 
                  << " (" << numRemoved << " items removed)" << std::endl;
        // Refresh atau update status proyek
        isOpenedProject = true;
    }
}

void HandlerProject::HandleRenameFolder(const AssetFile& node) {
    ImGui::PushID(node.fullPath.c_str());
    ImGui::SetNextItemWidth(200);

        if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                std::string newPath = fs::path(node.fullPath).parent_path().string() + "/" + renameBuffer;
                if (!fs::exists(newPath)) {
                    fs::rename(node.fullPath, newPath);
                    ShowNotification("Renamed", node.name + " renamed to " + renameBuffer, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                } else {
                    ShowNotification("Rename Failed", "Folder already exists.", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
                fileExplorerRenameTarget.clear();
            }

            if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
                fileExplorerRenameTarget.clear();
            }

            ImGui::PopID();
            ImGui::PopStyleColor();
            // ImGui::EndGroup();
}

void HandlerProject::HandleCopy(const AssetFile& node) {
    fileExplorerCopyTarget = node.fullPath;
    ShowNotification("Copy", "Copied: " + node.name, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
}

void HandlerProject::HandlePaste(const std::string& targetFolder) {
    if (fileExplorerCopyTarget.empty()) {
        ShowNotification("Paste Failed", "No item in clipboard!", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        return;
    }

    try {
        fs::path sourcePath = fileExplorerCopyTarget;
        fs::path targetPath = fs::path(targetFolder) / sourcePath.filename();

        // Check if target already exists
        if (fs::exists(targetPath)) {
            string newName = sourcePath.stem().string() + "_copy" + sourcePath.extension().string();
            targetPath = fs::path(targetFolder) / newName;
        }

        if (fs::is_directory(sourcePath)) {
            // Copy directory recursively
            fs::copy(sourcePath, targetPath, fs::copy_options::recursive);
        } else {
            // Copy file
            fs::copy(sourcePath, targetPath, fs::copy_options::overwrite_existing);
        }

        ShowNotification("Paste Complete", "Pasted: " + targetPath.filename().string(), 
            ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        
        // Clear copy target after successful paste
        fileExplorerCopyTarget.clear();
        
        // Refresh project
        isOpenedProject = true;

    } catch (const fs::filesystem_error& e) {
        ShowNotification("Paste Failed", e.what(), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}

void HandlerProject::HandleImport(const std::string& targetFolder) {
    if (fileTargetImport.empty()) {
        ShowNotification("Import Failed", "No file selected!", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        return;
    }

    try {
        fs::path sourcePath = fileTargetImport;
        fs::path targetPath = fs::path(targetFolder) / sourcePath.filename();

        // Check if target already exists
        if (fs::exists(targetPath)) {
            string newName = sourcePath.stem().string() + "_copy" + sourcePath.extension().string();
            targetPath = fs::path(targetFolder) / newName;
        }

        // Copy file
        fs::copy(sourcePath, targetPath, fs::copy_options::overwrite_existing);

        ShowNotification("Import Successful", 
            "Imported: " + sourcePath.filename().string() + "\nTo: " + targetFolder, 
            ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        
        // Clear import target
        fileTargetImport.clear();
        
        // Refresh project
        isOpenedProject = true;

    } catch (const fs::filesystem_error& e) {
        ShowNotification("Import Failed", e.what(), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}

void HandlerProject::HandlerOpenFileWithExtensionName(AssetFile& node)
{
    std::string ext = fs::path(node.name).extension().string();
    bool isCpp = (ext == ".cpp");
    bool isHpp = (ext == ".hpp");
    bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".m4a" || ext == ".avi" || ext == ".mov");
    bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp");
    bool isAudio = (ext == ".mp3" || ext == ".wav" || ext == ".ogg");
    bool isShader = (ext == ".glsl" || ext == ".shader" || ext == ".frag" || ext == ".vert");

    if (isCpp || isHpp) {
#ifdef _WIN32
        std::string cmd = "code \"" + projectPath + "\"";            
#else
        std::string cmd = "code \"" + node.fullPath + "\"";
#endif
        system(cmd.c_str());
    } 
    else if (isVideo) {
#ifdef _WIN32
        ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
        std::string cmd = "xdg-open \"" + node.fullPath + "\"";
        system(cmd.c_str());
#endif
    }
    else if (isImage || isAudio) {
#ifdef _WIN32
        ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
        std::string cmd = "xdg-open \"" + node.fullPath + "\"";
        system(cmd.c_str());
#endif
    }
}

bool HandlerProject::IsFrameValid(const AVFrame* frame, int width, int height) {
    int totalPixels = width * height * 3;
    const uint8_t* data = frame->data[0];

    uint8_t minVal = 255;
    uint8_t maxVal = 0;

    for (int i = 0; i < totalPixels; i++) {
        if (data[i] < minVal) minVal = data[i];
        if (data[i] > maxVal) maxVal = data[i];
    }

    // Bedanya harus cukup besar agar tidak dianggap hitam/putih saja
    return (maxVal - minVal) > 30;
}

float HandlerProject::CalculateColorVariance(const uint8_t* data, int width, int height) {
    int totalPixels = width * height;
    int totalRGB = totalPixels * 3;

    long long sum = 0;
    long long sumSq = 0;

    for (int i = 0; i < totalRGB; ++i) {
        int val = data[i];
        sum += val;
        sumSq += val * val;
    }

    float mean = (float)sum / totalRGB;
    float variance = ((float)sumSq / totalRGB) - (mean * mean);
    return variance;
}

void HandlerProject::FlipImageVertically(unsigned char* data, int width, int height, int channels) {
    int stride = width * channels;
    std::vector<unsigned char> row(stride);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* rowTop = data + y * stride;
        unsigned char* rowBottom = data + (height - y - 1) * stride;
        std::memcpy(row.data(), rowTop, stride);
        std::memcpy(rowTop, rowBottom, stride);
        std::memcpy(rowBottom, row.data(), stride);
    }
}

HandlerProject::IconInfo HandlerProject::GenerateVideoThumbnail(const std::string& videoPath) {
    IconInfo thumbnailInfo;
    thumbnailInfo.width = thumbnailSize.x;
    thumbnailInfo.height = thumbnailSize.y;

    AVFormatContext* formatContext = nullptr;
    if (avformat_open_input(&formatContext, videoPath.c_str(), nullptr, nullptr) != 0) {
        Debug::Logger::Log("Failed to open video file: " + videoPath, Debug::LogLevel::CRASH);
        return LoadCachedTexture("assets/images/fileicons/video.png");
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        avformat_close_input(&formatContext);
        return LoadCachedTexture("assets/images/fileicons/video.png");
    }

    // Find the video stream
    int videoStream = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStream = i;
            break;
        }
    }

    if (videoStream == -1) {
        avformat_close_input(&formatContext);
        return LoadCachedTexture("assets/images/fileicons/video.png");
    }

    // Get codec parameters
    AVCodecParameters* codecParams = formatContext->streams[videoStream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecContext, codecParams);
    avcodec_open2(codecContext, codec, nullptr);

    // Seek to 1/3 of the video
    int64_t duration = formatContext->duration / AV_TIME_BASE;
    int64_t third = duration / 3;
    int64_t half = duration / 2;
    int64_t quarter = duration / 4;

    // Coba seek ke tengah dulu
    av_seek_frame(formatContext, -1, half * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);


    // Read frames
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
        
    // Allocate buffer for RGB frame
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, 
                                            thumbnailSize.x, 
                                            thumbnailSize.y, 
                                            1);
    uint8_t* buffer = (uint8_t*)av_malloc(numBytes);
    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer,
                        AV_PIX_FMT_RGB24, thumbnailSize.x, thumbnailSize.y, 1);

    // Initialize software scaler
    SwsContext* swsContext = sws_getContext(
        codecContext->width, codecContext->height, codecContext->pix_fmt,
        thumbnailSize.x, thumbnailSize.y, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    int framesTried = 0;
    float maxVariance = 0.0f;
    std::vector<uint8_t> bestFrameData(numBytes);

    // int framesTried = 0;
    const int maxFramesToTry = 20;

    while (av_read_frame(formatContext, packet) >= 0 && framesTried < maxFramesToTry) {
        if (packet->stream_index == videoStream) {
            if (avcodec_send_packet(codecContext, packet) >= 0) {
                while (avcodec_receive_frame(codecContext, frame) >= 0) {
                    sws_scale(swsContext, frame->data, frame->linesize, 0,
                            codecContext->height, rgbFrame->data, rgbFrame->linesize);

                    float variance = CalculateColorVariance(rgbFrame->data[0], thumbnailSize.x, thumbnailSize.y);

                    if (variance > maxVariance) {
                        maxVariance = variance;
                        memcpy(bestFrameData.data(), rgbFrame->data[0], numBytes);
                    }

                    framesTried++;
                }
            }
        }
        av_packet_unref(packet);
    }


    // Create texture from RGB data
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    // FlipImageVertically(bestFrameData.data(), thumbnailSize.x, thumbnailSize.y, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, thumbnailSize.x, thumbnailSize.y, 
                0, GL_RGB, GL_UNSIGNED_BYTE, rgbFrame->data[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    thumbnailInfo.textureId = (ImTextureID)(intptr_t)textureID;

    // Cleanup
    av_free(buffer);
    av_frame_free(&rgbFrame);
    av_frame_free(&frame);
    av_packet_free(&packet);
    sws_freeContext(swsContext);
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);

    return thumbnailInfo;
}

void HandlerProject::HandleRenameOperation(AssetFile& node, const ImVec2& cursorPos, float itemWidth, float itemHeight) {
    ImGui::PushID((node.fullPath + "_rename").c_str());
    
    // Position the input box over the item
    ImGui::SetCursorScreenPos(cursorPos);
    
    // Style for rename input
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    
    ImGui::SetNextItemWidth(itemWidth);
    if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        
        std::string newName = renameBuffer;
        if (newName.empty()) {
            ShowNotification("Rename Failed", "Name cannot be empty", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        } else {
            std::string newPath = fs::path(node.fullPath).parent_path().string() + "/" + newName;
            
            // Preserve extension for files
            if (!node.isDirectory) {
                std::string ext = fs::path(node.fullPath).extension().string();
                if (!ext.empty() && fs::path(newName).extension().string().empty()) {
                    newPath += ext;
                }
            }

            try {
                if (!fs::exists(newPath)) {
                    fs::rename(node.fullPath, newPath);
                    ShowNotification("Renamed", 
                        fs::path(node.fullPath).filename().string() + " → " + fs::path(newPath).filename().string(), 
                        ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    
                    isOpenedProject = true; // Trigger refresh
                } else {
                    ShowNotification("Rename Failed", 
                        "A file or folder with this name already exists", 
                        ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                }
            } catch (const std::exception& e) {
                ShowNotification("Rename Failed", e.what(), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            }
        }
        fileExplorerRenameTarget.clear();
    }

    // Cancel rename if clicked outside
    if (!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) {
        fileExplorerRenameTarget.clear();
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}