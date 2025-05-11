#include "HandlerProject.hpp"
#include "MainWindow.hpp"

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
        "/builds",
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
    ifstream configFile(configPath);
    if (configFile.good()) {
        // TODO: Load project configuration
        configFile.close();
    } else {
        // Create default configuration
        ofstream newConfigFile(configPath);
        if (newConfigFile.is_open()) {
            newConfigFile << "{\n";
            newConfigFile << "    \"projectName\": \"New Project\",\n";
            newConfigFile << "    \"version\": \"1.0.0\",\n";
            newConfigFile << "    \"createdAt\": \"" << getCurrentDateTime() << "\"\n";
            newConfigFile << "}\n";
            newConfigFile.close();
        }
    }

    // Load project assets
    LoadProjectAssets();
    cout << "Project opened successfully: " << folderPath << endl;
    // MainWindow mainwindow(folderPath, 1280, 720);
}

void HandlerProject::ScanAssetsFolder(const std::string& rootFolder) {
    assetFiles.clear();

    for (const auto& entry : fs::recursive_directory_iterator(rootFolder))
    {
        if (entry.is_regular_file())
        {
            std::string filePath = entry.path().string();
            std::string parentFolder = entry.path().parent_path().filename().string();
            std::string filename = entry.path().filename().string();

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
        // Save current text color
        ImVec4 originalColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        
        // Set folder color and icon
        ImGui::PushStyleColor(ImGuiCol_Text, folderColor);
        
        DrawIconFromImage("assets/images/fileicons/folder.png");
        ImGui::SameLine();
        std::string label = node.name;
        bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags);
        
        // Pop color style
        ImGui::PopStyleColor();
        
        // Generate unique popup ID using file path to avoid conflicts
        std::string popupId = "context_menu_dir_" + node.fullPath;
        
        // Handle right click for directories
        if (ImGui::BeginPopupContextItem(popupId.c_str())) {
            
            if (DrawIconFromImage("assets/images/fileicons/trash.png"), ImGui::MenuItem(" Delete")) {
                if (MessageBoxA(NULL,
                    ("Are you sure you want to delete " + node.name + "?").c_str(),
                    "Confirm Delete",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {
                    DeleteFileOrFolder(node.fullPath);
                }
            }
            cout << node.fullPath << endl;
            // cout << node.children[0].fullPath << endl;
            HandleCreateNewFileUI(node.fullPath);
            ImGui::EndPopup();
        }
        
        if (nodeOpen) {
            for (const auto& child : node.children) {
                DrawAssetTree(child);
            }
            ImGui::TreePop();
        }
    }
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
            DrawIconFromImage("assets/images/fileicons/c-.png");
            icon = ICON_FA_CODE;
            fileColor = cppColor;
        }
        else if (isHpp) {
            DrawIconFromImage("assets/images/fileicons/c-.png");
            icon = ICON_FA_CODE;
            fileColor = hppColor;
        }
        else if (isVideo) {
            DrawIconFromImage("assets/images/fileicons/video.png");
            icon = ICON_FA_FILM;
            fileColor = videoColor;
        }
        else if (isImage) {
            DrawIconFromImage("assets/images/fileicons/image.png");
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
        // File item as selectable
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
            DrawIconFromImage("assets/images/fileicons/trash.png");
            if (ImGui::MenuItem(ICON_FA_TRASH " Delete")) {
                if (MessageBoxA(NULL,
                    ("Are you sure you want to delete " + node.name + "?").c_str(),
                    "Confirm Delete",
                    MB_YESNO | MB_ICONWARNING) == IDYES) {
                    DeleteFileOrFolder(node.fullPath);
                }
            }
            
            if (ImGui::MenuItem(ICON_FA_PLUS " Edit")) {
                // Open in appropriate editor based on file type
                std::string cmd;
#ifdef _WIN32
                cmd = "code \"" + node.fullPath + "\"";
#else
                cmd = "code \"" + node.fullPath + "\"";
#endif
                system(cmd.c_str());
            }
            
            if (ImGui::MenuItem(ICON_FA_COPY " Duplicate")) {
                // Add functionality for duplicating file
            }
            
            ImGui::EndPopup();
        }
        
        // Pop color style
        ImGui::PopStyleColor();
        ImGui::EndGroup();
    }
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
            headerFile << "#pragma once\n\n";
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

void HandlerProject::DrawIconFromImage(const char* iconPath) {
    // ImTextureID texID = assets.LoadTextureFromFile(iconPath, &icon_texture_data); // bikin sendiri atau cache
    ImTextureID texID = GetCachedIcon(iconPath);
    if (texID) {
        ImGui::Image(texID, ImVec2(20, 20));
        ImGui::SameLine();
    }
}

ImTextureID HandlerProject::GetCachedIcon(const std::string& path) {
    auto it = iconCache.find(path);
    if (it != iconCache.end())
        return it->second;

    int w, h, channels;
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
        ShowNotification(ICON_FA_INFO " Info", "File watcher is already running", ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
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
    
    ShowNotification(ICON_FA_EYE " File Watcher", "Started monitoring project files", ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
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
    
    ShowNotification(ICON_FA_EYE_SLASH " File Watcher", "Stopped monitoring project files", ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
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
                
                ShowNotification(ICON_FA_FILE " Files Changed", message, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            }
        }
        
    } catch (const fs::filesystem_error& e) {
        cerr << "Error in CheckForFileChanges: " << e.what() << endl;
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
    std::string sceneFolder = projectPath + "/scenes";
    fs::create_directories(sceneFolder);  // pastikan foldernya ada

    std::string fullPath = sceneFolder + "/" + name + ".scene";

    std::ofstream outFile(fullPath);
    if (outFile.is_open()) {
        outFile << "{\n\t\"name\": \"" << name << "\",\n\t\"objects\": []\n}";
        outFile.close();

        ShowNotification("Scene Created", "Scene " + name + " berhasil dibuat.", ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    } else {
        ShowNotification("Failed", "Gagal membuat scene!", ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
}

void HandlerProject::NewFileForExplorer(const std::string& scriptName) {
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
            headerFile << "#pragma once\n\n";
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

void HandlerProject::HandleCreateNewFileUI(const std::string &targetFolder) {
    // --- Variabel static untuk menyimpan state pembuatan file baru ---
    static bool showRenamePopup = false;
    static bool isScriptFile = false;  // false: file kosong, true: script (C++ => .cpp + .hpp)
    static std::string createdFilePath; // Untuk file kosong
    static std::string createdCPPPath;  // Untuk file script *.cpp*
    static std::string createdHPPPath;  // Untuk file script *.hpp*
    static char newFileNameBuffer[256] = "";

    // Tampilkan context menu pada folder target
    if (ImGui::BeginPopupContextItem("FolderContextMenu")) {
        if (ImGui::BeginMenu("Create New File")) {
            // Opsi: File Kosong
            if (ImGui::MenuItem("Empty File")) {
                createdFilePath = targetFolder + "/NewFile.txt";
                std::ofstream ofs(createdFilePath);
                ofs.close();

                // Set default nama file di buffer (pastikan memuat ekstensi)
                std::string defaultName = "NewFile.txt";
                strncpy(newFileNameBuffer, defaultName.c_str(), sizeof(newFileNameBuffer));
                isScriptFile = false;
                showRenamePopup = true;
            }
            // Opsi: Submenu Scripts
            if (ImGui::BeginMenu("Scripts")) {
                // Opsi: C++ Script
                if (ImGui::MenuItem("C++ Script")) {
                    // Default base name untuk script
                    std::string scriptBaseName = "NewScript";
                    createdCPPPath = targetFolder + "/" + scriptBaseName + ".cpp";
                    createdHPPPath = targetFolder + "/" + scriptBaseName + ".hpp";

                    // Buat file .cpp dengan template
                    std::ofstream scriptFile(createdCPPPath);
                    if (scriptFile.is_open()) {
                        scriptFile << "#include \"" << scriptBaseName << ".hpp\"\n\n";
                        scriptFile << "class " << scriptBaseName << " {\n";
                        scriptFile << "private:\n";
                        scriptFile << "    // Add private members here\n\n";
                        scriptFile << "public:\n";
                        scriptFile << "    " << scriptBaseName << "() {\n";
                        scriptFile << "        // Constructor\n";
                        scriptFile << "    }\n\n";
                        scriptFile << "    void Start() {\n";
                        scriptFile << "        // Called when script instance is being loaded\n";
                        scriptFile << "    }\n\n";
                        scriptFile << "    void Update() {\n";
                        scriptFile << "        // Called every frame\n";
                        scriptFile << "    }\n\n";
                        scriptFile << "    ~" << scriptBaseName << "() {\n";
                        scriptFile << "        // Destructor\n";
                        scriptFile << "    }\n";
                        scriptFile << "};\n";
                        scriptFile.close();
                    }

                    // Buat file header (.hpp) yang sesuai
                    std::ofstream headerFile(createdHPPPath);
                    if (headerFile.is_open()) {
                        headerFile << "#pragma once\n\n";
                        headerFile << "class " << scriptBaseName << " {\n";
                        headerFile << "public:\n";
                        headerFile << "    " << scriptBaseName << "();\n";
                        headerFile << "    void Start();\n";
                        headerFile << "    void Update();\n";
                        headerFile << "    ~" << scriptBaseName << "();\n";
                        headerFile << "};\n";
                        headerFile.close();
                    }
                    
                    // Set default base name ke buffer (tanpa ekstensi)
                    strncpy(newFileNameBuffer, scriptBaseName.c_str(), sizeof(newFileNameBuffer));
                    isScriptFile = true;
                    showRenamePopup = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // Tampilkan popup untuk mengganti nama file baru
    if (showRenamePopup) {
        if (isScriptFile) {
            ImGui::OpenPopup("Rename New Script");
            if (ImGui::BeginPopupModal("Rename New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Masukkan nama script (tanpa ekstensi):");
                ImGui::InputText("Script Base Name", newFileNameBuffer, IM_ARRAYSIZE(newFileNameBuffer));
                if (ImGui::Button("Rename", ImVec2(120, 0))) {
                    std::string baseName = newFileNameBuffer;
                    std::string newCPPPath = targetFolder + "/" + baseName + ".cpp";
                    std::string newHPPPath = targetFolder + "/" + baseName + ".hpp";
                    try {
                        std::filesystem::rename(createdCPPPath, newCPPPath);
                        std::filesystem::rename(createdHPPPath, newHPPPath);
                        createdCPPPath = newCPPPath;
                        createdHPPPath = newHPPPath;
                    } catch (const std::exception &e) {
                        std::cerr << "Error renaming script files: " << e.what() << std::endl;
                    }
                    showRenamePopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    try {
                        if (std::filesystem::exists(createdCPPPath))
                            std::filesystem::remove(createdCPPPath);
                        if (std::filesystem::exists(createdHPPPath))
                            std::filesystem::remove(createdHPPPath);
                    } catch (const std::exception &e) {
                        std::cerr << "Error removing script files: " << e.what() << std::endl;
                    }
                    showRenamePopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        } else {
            ImGui::OpenPopup("Rename New File");
            if (ImGui::BeginPopupModal("Rename New File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Masukkan nama file baru:");
                ImGui::InputText("File Name", newFileNameBuffer, IM_ARRAYSIZE(newFileNameBuffer));
                if (ImGui::Button("Rename", ImVec2(120, 0))) {
                    std::string newPath = targetFolder + "/" + std::string(newFileNameBuffer);
                    try {
                        std::filesystem::rename(createdFilePath, newPath);
                        createdFilePath = newPath;
                    } catch (const std::exception &e) {
                        std::cerr << "Error renaming file: " << e.what() << std::endl;
                    }
                    showRenamePopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    try {
                        if (std::filesystem::exists(createdFilePath))
                            std::filesystem::remove(createdFilePath);
                    } catch (const std::exception &e) {
                        std::cerr << "Error removing file: " << e.what() << std::endl;
                    }
                    showRenamePopup = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

}
