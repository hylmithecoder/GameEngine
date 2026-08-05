#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/ui/MainWindow.hpp"
#include "../../../include/ui/SvgIconManager.hpp"
using namespace Debug;
namespace fs = std::filesystem;

namespace {

fs::path AbsoluteNormalizedPath(const fs::path &path) {
  std::error_code ec;
  const fs::path absolutePath = fs::absolute(path, ec);
  return ec ? path.lexically_normal() : absolutePath.lexically_normal();
}

bool IsPathInside(const fs::path &parent, const fs::path &candidate) {
  std::error_code ec;
  const fs::path relativePath = fs::relative(candidate, parent, ec);
  if (ec || relativePath.empty() || relativePath == ".") {
    return false;
  }

  const auto firstComponent = relativePath.begin();
  return firstComponent != relativePath.end() &&
         *firstComponent != fs::path("..");
}

fs::path MakeUniqueCopyPath(const fs::path &sourcePath,
                            const fs::path &targetDirectory) {
  const fs::path originalTarget = targetDirectory / sourcePath.filename();
  if (!fs::exists(originalTarget)) {
    return originalTarget;
  }

  const bool isRegularFile = fs::is_regular_file(sourcePath);
  const std::string originalName = sourcePath.filename().string();
  const std::string extension =
      isRegularFile ? sourcePath.extension().string() : std::string();
  const std::string baseName =
      (isRegularFile && !sourcePath.stem().string().empty())
          ? sourcePath.stem().string()
          : originalName;

  for (std::size_t copyIndex = 1;; ++copyIndex) {
    std::string copyName = baseName + "_copy";
    if (copyIndex > 1) {
      copyName += "_" + std::to_string(copyIndex);
    }
    copyName += extension;

    const fs::path candidate = targetDirectory / copyName;
    if (!fs::exists(candidate)) {
      return candidate;
    }
  }
}

bool CopyPath(const fs::path &sourcePath, const fs::path &targetPath,
              std::error_code &ec) {
  ec.clear();
  const fs::file_status sourceStatus = fs::status(sourcePath, ec);
  if (ec) {
    return false;
  }

  if (fs::is_directory(sourceStatus)) {
    fs::copy(sourcePath, targetPath, fs::copy_options::recursive, ec);
  } else if (fs::is_regular_file(sourceStatus)) {
    fs::copy_file(sourcePath, targetPath, fs::copy_options::none, ec);
  } else {
    ec = std::make_error_code(std::errc::operation_not_supported);
  }

  return !ec;
}

} // namespace

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

  nfdchar_t *savePath;

  // prepare filters for the dialog
  nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeeescene"}};

  // show the dialog
  nfdresult_t result =
      NFD_SaveDialog(&savePath, filterItem, 1, NULL, "Untitled.ilmeeescene");
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

void HandlerProject::SaveAsScene() {
  NFD_Init();

  nfdchar_t *savePath;

  // prepare filters for the dialog
  nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeeescene"}};

  // show the dialog
  nfdsavedialogu8args_t args = {0};
  args.filterList = filterItem;
  args.filterCount = 1;
  args.defaultName = (currentSceneName + ".ilmeeescene").c_str();
  nfdresult_t result = NFD_SaveDialogU8_With(&savePath, &args);
  if (result == NFD_OKAY) {
    puts("Success!");
    puts(savePath);
    // remember to free the memory (since NFD_OKAY is returned)
    NFD_FreePath(savePath);
  } else if (result == NFD_CANCEL) {
    puts("User pressed cancel.");
  } else {
    printf("Error: %s\n", NFD_GetError());
  }

  // Quit NFD
  NFD_Quit();
}

void HandlerProject::OpenScene() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;

  // Prepare filters for scene files
  nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeeescene"}};

  try {
    // Show open file dialog
    nfdresult_t result =
        NFD::OpenDialog(outPath, filterItem, 1, projectPath.c_str());

    if (result == NFD_OKAY && outPath.get() != nullptr) {
      std::string scenePath = outPath.get();

      // Validate file extension
      if (fs::path(scenePath).extension() != ".ilmeeescene") {
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

      } catch (const std::exception &e) {
        ShowNotification("Load Error",
                         "Failed to load scene: " + std::string(e.what()),
                         ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
      }
    } else if (result == NFD_CANCEL) {
      // User cancelled - no need for notification
    } else {
      ShowNotification("Error",
                       "Failed to open file dialog: " +
                           std::string(NFD::GetError()),
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }
  } catch (const std::exception &e) {
    ShowNotification("Error", "Unexpected error: " + std::string(e.what()),
                     ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
}

void HandlerProject::OpenFile() {
  NFD::Guard nfdGuard;
  NFD::UniquePathSet outPaths;

  // Define supported file types
  nfdfilteritem_t filterItem[4] = {{"Source Files", "c,cpp,h,hpp"},
                                   {"Images", "png,jpg,jpeg,gif,bmp"},
                                   {"Audio", "mp3,wav,ogg"},
                                   {"Video", "mp4,mkv,avi,mov"}};

  // Show the dialog with filters for multiple selection.  Start in the folder
  // currently open in the engine so imported files follow the user's target.
  const char *defaultPath =
      currentDirectory.empty() ? nullptr : currentDirectory.c_str();
  nfdresult_t result =
      NFD::OpenDialogMultiple(outPaths, filterItem, 4, defaultPath);

  if (result == NFD_OKAY) {
    nfdpathsetsize_t numPaths;
    NFD::PathSet::Count(outPaths, numPaths);
    std::size_t importedCount = 0;

    for (nfdpathsetsize_t i = 0; i < numPaths; ++i) {
      NFD::UniquePathSetPath path;
      if (NFD::PathSet::GetPath(outPaths, i, path) != NFD_OKAY ||
          !path) {
        continue;
      }

      std::string currentFile = path.get();
      std::cout << "Selected file " << i + 1 << ": " << currentFile
                << std::endl;

      // Every selected file is copied into the folder currently open in the
      // Explorer.  The previous implementation routed files by extension,
      // which made Import Files ignore the folder the user was looking at.
      fileTargetImport = currentFile;

      std::string targetFolder = currentDirectory;
      if (targetFolder.empty() || !fs::is_directory(targetFolder)) {
        targetFolder = (fs::path(projectPath) / "assets").string();
      }

      // Create target folder if it doesn't exist
      fs::create_directories(targetFolder);

      // Import the current file
      if (HandleImport(targetFolder)) {
        ++importedCount;
      }
    }

    // Show summary notification
    if (importedCount > 0) {
      ShowNotification("Import Complete",
                       "Imported " + std::to_string(importedCount) +
                           " file(s)",
                       ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    } else if (numPaths > 0) {
      ShowNotification("Import Failed", "No selected files could be imported.",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    }

  } else if (result == NFD_CANCEL) {
    std::cout << "User cancelled file selection." << std::endl;
  } else {
    std::cout << "Error: " << NFD::GetError() << std::endl;
    ShowNotification("Error", "Failed to open file dialog",
                     ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
}

void HandlerProject::ImportFolder() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;

  const char *defaultPath = currentDirectory.empty()
                                ? nullptr
                                : currentDirectory.c_str();
  const nfdresult_t result = NFD::PickFolder(outPath, defaultPath);

  if (result == NFD_OKAY && outPath.get() != nullptr) {
    const std::string selectedFolder = outPath.get();
    std::error_code ec;
    if (selectedFolder.empty() ||
        !fs::is_directory(fs::path(selectedFolder), ec)) {
      ShowNotification("Import Folder Failed",
                       "The selected path is not a folder.",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
      return;
    }

    std::string targetFolder = currentDirectory;
    ec.clear();
    if (targetFolder.empty() ||
        !fs::is_directory(fs::path(targetFolder), ec)) {
      targetFolder = (fs::path(projectPath) / "assets").string();
    }

    // HandleImport uses the same recursive copy and collision-safe naming as
    // the Explorer clipboard, so an imported folder is copied as one folder
    // with all of its contents preserved.
    fileTargetImport = selectedFolder;
    HandleImport(targetFolder);
  } else if (result == NFD_CANCEL) {
    // Cancellation is expected and needs no notification.
  } else {
    ShowNotification("Import Folder Failed",
                     "Could not choose a folder: " +
                         std::string(NFD::GetError()),
                     ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
}

void HandlerProject::OpenProject(const char *folderPath) {
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
      "/assets",         "/assets/textures", "/assets/audio", "/assets/models",
      "/assets/scripts", "/scenes",          "/build",        "/config"};

  for (const auto &dir : directories) {
    string fullPath = string(folderPath) + dir;
    if (mkdir(fullPath.c_str(), 0755) != 0 && errno != EEXIST) {
      cerr << "Error creating directory: " << fullPath << endl;
    }
  }

  // Load project configuration if exists
  string configPath = string(folderPath) + "/config/project.json";
  const char *readConfigPath = configPath.c_str();
  ifstream configFile(configPath);
  if (configFile.good()) {
    ::Log("Loading project configuration: " + string(readConfigPath),
          Debug::LogLevel::SUCCESS);
    configFile.close();
  } else {
    // Create default configuration
    string nameProject = fs::path(folderPath).stem().string();
    ofstream newConfigFile(configPath);
    if (newConfigFile.is_open()) {
      newConfigFile << "{\n";
      newConfigFile << "    \"projectName\": \"" + nameProject + "\",\n";
      newConfigFile << "    \"version\": \"1.0.0\",\n";
      newConfigFile << "    \"createdAt\": \"" << getCurrentDateTime()
                    << "\"\n";
      newConfigFile << "}\n";
      newConfigFile.close();
    }
  }

  // Load project assets
  LoadProjectAssets();
  rootAsset = BuildAssetTree(projectPath);
  currentDirectory = projectPath + "/assets";
  ::Log("Project loaded successfully: " + projectPath,
        Debug::LogLevel::SUCCESS);
  // MainWindow mainwindow(folderPath, 1280, 720);
}

void HandlerProject::ScanAssetsFolder(const std::string &rootFolder) {
  ::Log("Scanning Folder Root Project: " + rootFolder);
  assetFiles.clear();

  for (const auto &entry : fs::recursive_directory_iterator(rootFolder)) {
    if (entry.is_regular_file()) {
      std::string filePath = entry.path().string();
      std::string parentFolder = entry.path().parent_path().filename().string();
      std::string filename = entry.path().filename().string();

      ::Log("File Path: " + filePath + " Parent Folder: " + parentFolder +
                " File Name: " + filename,
            Debug::LogLevel::INFO);
      assetFiles[parentFolder].push_back(filename);
    }
  }
}

void HandlerProject::DrawAssetTree(const AssetFile &node) {
  // Define colors for different file types
  ImVec4 folderColor = ImVec4(1.0f, 0.87f, 0.36f, 1.0f);    // Yellow
  ImVec4 cppColor = ImVec4(0.46f, 0.78f, 1.0f, 1.0f);       // Light Blue
  ImVec4 hppColor = ImVec4(0.71f, 0.46f, 1.0f, 1.0f);       // Purple
  ImVec4 videoColor = ImVec4(1.0f, 0.44f, 0.37f, 1.0f);     // Red
  ImVec4 defaultFileColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f); // Light Gray

  ImGuiTreeNodeFlags nodeFlags =
      ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

  if (node.isDirectory) {
    // Simpan warna teks awal
    ImVec4 originalColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Atur warna folder dan tampilkan ikon folder
    ImGui::PushStyleColor(ImGuiCol_Text, folderColor);
    if (svgIcons_) {
      svgIcons_->DrawIcon("assets/icons/svg/folder.svg", 20);
    }
    ImGui::SameLine();
    std::string label = node.name;

    // Folder
    if (fileExplorerRenameTarget == node.fullPath) {
      // ImGui::PushStyleColor(ImGuiCol_Text, originalColor);
      HandleRenameFolder(node);
    } else {
      bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags);
      ImGui::PopStyleColor(); // Kembalikan warna teks

      // Generate ID popup context berdasarkan fullPath untuk menghindari
      // konflik
      std::string popupId = "context_menu_dir_" + node.fullPath;

      // Tampilkan popup context pada folder ketika klik kanan
      if (ImGui::BeginPopupContextItem(popupId.c_str())) {
        if (ImGui::MenuItem(" Copy")) {
          HandleCopy(node);
        }

        if (ImGui::MenuItem(" Paste", nullptr, false,
                            !fileExplorerCopyTargets.empty())) {
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
          // if (MessageBoxA(NULL,
          //     ("Are you sure you want to delete " + node.name + "?").c_str(),
          //     "Confirm Delete",
          //     MB_YESNO | MB_ICONWARNING) == IDYES) {
          DeleteFolder(node.fullPath);
          // }
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
        for (const auto &child : node.children) {
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
    bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".m4a" ||
                    ext == ".avi" || ext == ".mov");
    bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".gif" || ext == ".bmp");
    bool isAudio = (ext == ".mp3" || ext == ".wav" || ext == ".ogg");
    bool isShader = (ext == ".glsl" || ext == ".shader" || ext == ".frag" ||
                     ext == ".vert");

    // Generate unique popup ID using file path to avoid conflicts
    std::string popupId = "context_menu_" + node.fullPath;

    // Start group for the selectable item with icon
    ImGui::BeginGroup();
    // Choose appropriate SVG icon based on file type
    std::string svgIcon = "assets/icons/svg/file.svg";
    ImVec4 fileColor = defaultFileColor;

    if (isCpp) {
      svgIcon = "assets/icons/svg/cpp.svg";
      fileColor = cppColor;
    } else if (isHpp) {
      svgIcon = "assets/icons/svg/cpp.svg";
      fileColor = hppColor;
    } else if (isVideo) {
      svgIcon = "assets/icons/svg/video.svg";
      fileColor = videoColor;
    } else if (isImage) {
      svgIcon = "assets/icons/svg/image.svg";
      fileColor = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
    } else if (isAudio) {
      svgIcon = "assets/icons/svg/music.svg";
      fileColor = ImVec4(0.8f, 0.4f, 0.8f, 1.0f);
    } else if (isShader) {
      svgIcon = "assets/icons/svg/shader.svg";
      fileColor = ImVec4(0.4f, 0.8f, 0.8f, 1.0f);
    }

    // Draw SVG icon inline
    if (svgIcons_) {
      svgIcons_->DrawIcon(svgIcon, 20);
    }

    // Push color for the icon and text
    ImGui::PushStyleColor(ImGuiCol_Text, fileColor);

    // Create label with icon
    std::string label = node.name;

    if (renamingPath == node.fullPath) {
      HandleRename(node);
    } else {
      // Ini adalah UI File nya
      if (ImGui::Selectable(label.c_str(), false,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        if (ImGui::IsMouseDoubleClicked(0)) {
          if (isCpp) {
#ifdef _WIN32
            std::string cmd = "antigravity \"" + projectPath + "\"";
            system(cmd.c_str());
#else
            std::string cmd = "antigravity \"assets\"";
            system(cmd.c_str());
#endif
          } else if (isVideo) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL,
                          SW_SHOW);
#else
            std::string cmd = "xdg-open \"" + node.fullPath + "\"";
            system(cmd.c_str());
#endif
          } else if (isImage || isAudio) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL,
                          SW_SHOW);
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

        if (ImGui::MenuItem(" Paste", nullptr, false,
                            !fileExplorerCopyTargets.empty())) {
          // A file is not a paste destination; use its containing folder.
          HandlePaste(fs::path(node.fullPath).parent_path().string());
        }

        if (ImGui::MenuItem(" Delete")) {
          // if (MessageBoxA(NULL,
          //     ("Are you sure you want to delete " + node.name + "?").c_str(),
          //     "Confirm Delete",
          //     MB_YESNO | MB_ICONWARNING) == IDYES) {
          DeleteFileOrFolder(node.fullPath);
          // }
        }

        if (ImGui::MenuItem(" Edit")) {
          // Open in appropriate editor based on file type
          std::string cmd;
#ifdef _WIN32
          cmd = "antigravity \"" + node.fullPath + "\"";
#else
          cmd = "antigravity \"" + node.fullPath + "\"";
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

void HandlerProject::HandleRenameFileOrFolder(const AssetFile &node) {
  ImGui::PushID(node.fullPath.c_str());
  ImGui::SetNextItemWidth(200);

  if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_AutoSelectAll)) {

    std::string newName = renameBuffer;
    if (newName.empty()) {
      ShowNotification("Rename Failed", "Name cannot be empty",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    } else {
      std::string newPath =
          fs::path(node.fullPath).parent_path().string() + "/" + newName;

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
                           node.name + " renamed to " +
                               fs::path(newPath).filename().string(),
                           ImVec4(0.3f, 1.0f, 0.3f, 1.0f));

          // Trigger refresh
          isOpenedProject = true;
        } else {
          ShowNotification("Rename Failed",
                           "A file or folder with this name already exists",
                           ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
      } catch (const std::exception &e) {
        ShowNotification("Rename Failed", e.what(),
                         ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
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

void HandlerProject::DrawFileExplorer(AssetFile &node) {
  // The grid entries are rebuilt from disk every frame, so selection must live
  // in the handler rather than in the temporary AssetFile value.
  ImGui::PushID(node.fullPath.c_str());

  float itemWidth = thumbnailSize.x + itemSpacing * 2;
  float itemHeight = thumbnailSize.y + ImGui::GetFontSize() + 8.0f;
  ImVec2 totalSize(itemWidth, itemHeight);

  // Simpan posisi awal
  ImVec2 cursorPos = ImGui::GetCursorScreenPos();

  ImGui::InvisibleButton("##ItemButton", totalSize);
  const bool itemHovered = ImGui::IsItemHovered();

  // Drag source: files can be dragged out (e.g. a model dropped onto the
  // Scene viewport to instantiate it). Payload is the file's full path.
  if (!node.isDirectory &&
      ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
    const std::string &fp = node.fullPath;
    ImGui::SetDragDropPayload("ASSET_PATH", fp.c_str(), fp.size() + 1);
    ImGui::Text("%s", node.name.c_str());
    ImGui::EndDragDropSource();
  }

  if (itemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    if (!fileExplorerSelectedPaths.contains(node.fullPath)) {
      if (!ImGui::GetIO().KeyCtrl) {
        fileExplorerSelectedPaths.clear();
      }
      fileExplorerSelectedPaths.insert(node.fullPath);
    }

    if (node.isDirectory) {
      currentDirectory = node.fullPath;
      selectedAsset = nullptr;
      fileExplorerSelectedPaths.clear();
      ::Log("Opened folder: " + currentDirectory, Debug::LogLevel::SUCCESS);
      ShowNotification("Opening folder: " + node.name, "Explorer",
                       ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
    } else {
      HandlerOpenFileWithExtensionName(node);
      selectedAsset = &node;
      ShowNotification("Opening file: " + node.name, "Explorer",
                       ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
      if (onFileClicked)
        onFileClicked(node);
    }
  } else if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    // Ctrl-click keeps the existing selection and toggles this item. A normal
    // click starts a new selection, matching file-manager behavior.
    if (ImGui::GetIO().KeyCtrl) {
      if (fileExplorerSelectedPaths.contains(node.fullPath)) {
        fileExplorerSelectedPaths.erase(node.fullPath);
      } else {
        fileExplorerSelectedPaths.insert(node.fullPath);
      }
    } else {
      fileExplorerSelectedPaths.clear();
      fileExplorerSelectedPaths.insert(node.fullPath);
    }
    selectedAsset = node.isDirectory ? nullptr : &node;
  }

  // Right-clicking an unselected item makes it the only selected item. If it
  // is already selected, preserve the other selected items for multi-copy.
  if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
      !fileExplorerSelectedPaths.contains(node.fullPath)) {
    fileExplorerSelectedPaths.clear();
    fileExplorerSelectedPaths.insert(node.fullPath);
  }

  // Highlight selected items.
  if (fileExplorerSelectedPaths.contains(node.fullPath)) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        cursorPos, ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.6f, 1.0f, 0.2f)), 4.0f);
    drawList->AddRect(
        cursorPos, ImVec2(cursorPos.x + itemWidth, cursorPos.y + itemHeight),
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.6f, 1.0f, 1.0f)), 4.0f, 0,
        2.0f);
  }

  // Gambar ikon di tengah
  float iconPosX = cursorPos.x + (itemWidth - thumbnailSize.x) * 0.5f;
  float iconPosY = cursorPos.y + 4.0f;
  ImGui::GetWindowDrawList()->AddImage(
      GetIconForFile(node).textureId, ImVec2(iconPosX, iconPosY),
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

  ImGui::GetWindowDrawList()->AddText(
      ImVec2(textPosX, textPosY),
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
        // if (MessageBoxA(NULL,
        //     ("Are you sure you want to delete " + node.name + "?").c_str(),
        //     "Confirm Delete",
        //     MB_YESNO | MB_ICONWARNING) == IDYES) {
        if (node.isDirectory) {
          DeleteFolder(node.fullPath);
        } else {
          DeleteFileOrFolder(node.fullPath);
        }
        // }
      }

      ImGui::Separator();
      if (!node.isDirectory) {
        if (ImGui::MenuItem("Open")) {
          HandlerOpenFileWithExtensionName(const_cast<AssetFile &>(node));
        }
      }

      // Folders are first-class clipboard items too.  A paste on a file uses
      // its containing directory; a paste on a folder uses that folder.
      if (ImGui::MenuItem("Copy")) {
        HandleCopy(node);
      }
      if (ImGui::MenuItem("Paste", nullptr, false,
                          !fileExplorerCopyTargets.empty())) {
        const std::string targetFolder =
            node.isDirectory ? node.fullPath
                             : fs::path(node.fullPath).parent_path().string();
        HandlePaste(targetFolder);
      }

      ImGui::EndPopup();
    }
  }

  // Next item posisi horizontal
  ImGui::SameLine(0, itemSpacing);
  if (ImGui::GetCursorPosX() + itemWidth >
      ImGui::GetWindowContentRegionMax().x) {
    ImGui::NewLine();
  }

  ImGui::PopID();
}

void HandlerProject::DrawFolderGridView() {
  // Gunakan currentDirectory yang sudah di-update dari double-click
  std::vector<AssetFile> localFiles = GetFilesInDirectory(currentDirectory);

  // Use filesystem paths for comparisons so the Explorer behaves the same on
  // Windows and POSIX systems.
  const std::string rootDirectory =
      (fs::path(projectPath) / "assets").lexically_normal().string();

  ImGui::SetNextItemWidth(100);
  // ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  ImGui::SliderFloat("Size", &thumbnailSize.x, 32.0f, 96.0f);
  thumbnailSize.y = thumbnailSize.x;

  // Tampilkan current directory path
  ImGui::Text("Current: %s", currentDirectory.c_str());

  ImGui::Separator();

  if (ImGui::BeginChild("FileGrid", ImVec2(0, 0), false)) {
    float contentWidth = ImGui::GetContentRegionAvail().x;
    int itemsPerRow =
        static_cast<int>(contentWidth / (thumbnailSize.x + itemSpacing * 2));
    if (itemsPerRow < 1)
      itemsPerRow = 1;

    // Filter files berdasarkan currentFilter
    std::vector<AssetFile> filteredFiles;
    for (const auto &file : localFiles) {
      if (currentFilter.empty() ||
          file.name.find(currentFilter) != std::string::npos) {
        filteredFiles.push_back(file);
      }
    }

    // Sort: directories first, then alphabetically
    std::sort(filteredFiles.begin(), filteredFiles.end(),
              [](const AssetFile &a, const AssetFile &b) {
                if (a.isDirectory && !b.isDirectory)
                  return true;
                if (!a.isDirectory && b.isDirectory)
                  return false;
                return a.name < b.name;
              });

    // Draw each file/folder
    for (auto &file : filteredFiles) {
      // ::Log("Drawing file in grid: " + file.name, Debug::LogLevel::SUCCESS);
      DrawFileExplorer(file);
    }
    // Draw the "New Folder" button
    if (ImGui::BeginPopupContextWindow("FileGridContentMenu",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {

      const bool isInRootDirectory =
          fs::path(currentDirectory).lexically_normal() ==
          fs::path(rootDirectory).lexically_normal();

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

      if (ImGui::MenuItem("Paste", nullptr, false,
                          !fileExplorerCopyTargets.empty())) {
        HandlePaste(currentDirectory);
      }

      if (ImGui::MenuItem("Import Files...")) {
        OpenFile();
      }

      if (ImGui::MenuItem("Import Folder...")) {
        ImportFolder();
      }

      if (!isInRootDirectory) {
        ImGui::Separator();
        if (ImGui::MenuItem("Show in Explorer")) {
#ifdef _WIN32
          ShellExecuteA(NULL, "explore", currentDirectory.c_str(), NULL, NULL,
                        SW_SHOW);
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

void HandlerProject::NewScripts(const std::string &scriptName) {
  string scriptPath = projectPath + "/assets/scripts/";
  string headerPath = projectPath + "/assets/scripts/header/";
  string fullPath = scriptPath + scriptName + ".cpp";

  // Create scripts directory if it doesn't exist
  try {
    std::filesystem::create_directories(scriptPath);
    std::filesystem::create_directories(headerPath);
  } catch (const std::filesystem::filesystem_error &e) {
    cerr << "Error creating directories: " << e.what() << endl;
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
    // if (MessageBoxA(NULL,
    //     ("New script created successfully. Do you want to open the file?"),
    //     "GameEngine SDL",
    //     MB_YESNO | MB_ICONINFORMATION) == IDYES)
    // {
    OpenFile(projectPath);
    // }
  } else {
    cerr << "Error creating script file: " << fullPath << endl;
  }
}

void HandlerProject::OpenFile(const std::string &fileName) {
#ifdef _WIN32
  std::string cmd = "antigravity \"" + fileName + "\"";
  system(cmd.c_str());
#else
  std::string cmd = "antigravity \"assets\"";
  system(cmd.c_str());
#endif
}

void HandlerProject::DeleteFileOrFolder(const std::string &filePath) {
  if (remove(filePath.c_str()) != 0) {
    cerr << "Error deleting file/folder: " << filePath << endl;
  } else {
    cout << "Deleted file/folder: " << filePath << endl;
    // Refresh
    isOpenedProject = true;
  }
}

void HandlerProject::ShowNotification(const std::string &title,
                                      const std::string &message,
                                      ImVec4 color) {

  notifications.push_back(Notification{
      title, message, color, static_cast<float>(ImGui::GetTime()),
      3.0f // Duration in seconds
  });
}

void HandlerProject::RenderNotifications() {
  float padding = 10.0f;
  float notificationWidth = 300.0f;
  float startY = padding;
  float currentTime = ImGui::GetTime();

  // Process notifications from oldest to newest
  for (int i = 0; i < notifications.size(); i++) {
    auto &notification = notifications[i];

    // Check if notification has expired
    if (currentTime > notification.startTime + notification.duration) {
      // Remove expired notification
      notifications.erase(notifications.begin() + i);
      i--; // Adjust index after removal
      continue;
    }

    // Calculate fade based on time remaining
    float timeLeft =
        (notification.startTime + notification.duration) - currentTime;
    float alpha = (timeLeft < 1.0f) ? timeLeft : 1.0f;

    // Position notification in top-right corner
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(
        ImVec2(windowSize.x - notificationWidth - padding, startY));
    ImGui::SetNextWindowSize(ImVec2(notificationWidth, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.9f * alpha);

    // Create unique ID for each notification
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));

    std::string windowID = "Notification_" + std::to_string(i);
    if (ImGui::Begin(windowID.c_str(), nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoSavedSettings)) {

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

void HandlerProject::DrawIconFromImage(const char *iconPath, int width,
                                       int height) {
  // ImTextureID texID = assets.LoadTextureFromFile(iconPath,
  // &icon_texture_data); // bikin sendiri atau cache
  ImTextureID texID = GetCachedIcon(iconPath);
  if (texID) {
    ImGui::Image(texID, ImVec2(width, height));
    ImGui::SameLine();
  }
}

HandlerProject::IconInfo
HandlerProject::LoadCachedTexture(const std::string &path) {
  // Already cached? Return directly
  auto it = iconCacheInfo.find(path);
  if (it != iconCacheInfo.end())
    return it->second;

  // SVG files: route through SvgIconManager
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") {
    if (svgIcons_) {
      ImTextureID tex = svgIcons_->GetIcon(path, 64);
      if (tex) {
        IconInfo info;
        info.textureId = tex;
        info.width = 64;
        info.height = 64;
        iconCacheInfo[path] = info;
        return info;
      }
    }
    return {(ImTextureID)0, 0, 0};
  }

  // PNG/JPG: use stbi + VulkanHandler
  if (vulkanHandler) {
    int width, height, channels;
    if (!stbi_info(path.c_str(), &width, &height, &channels)) {
      return {(ImTextureID)0, 0, 0};
    }

    ImTextureID tex = (ImTextureID)vulkanHandler->LoadImage(path.c_str());

    IconInfo info;
    info.textureId = tex;
    info.width = width;
    info.height = height;

    iconCacheInfo[path] = info;
    return info;
  }

  return {(ImTextureID)0, 0, 0};
}

ImTextureID HandlerProject::GetCachedIcon(const std::string &path) {
  auto it = iconCache.find(path);
  if (it != iconCache.end())
    return it->second;

  if (vulkanHandler) {
    ImTextureID tex = (ImTextureID)vulkanHandler->LoadImage(path.c_str());
    iconCache[path] = tex;
    return tex;
  }

  return (ImTextureID)0;
}

void HandlerProject::StartFileWatcher() {
  // Check if watcher is already running
  if (fileWatcherRunning) {
    ShowNotification(" Info", "File watcher is already running",
                     ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
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

  ShowNotification(" File Watcher", "Started monitoring project files",
                   ImVec4(0.4f, 0.8f, 0.4f, 1.0f));
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

  ShowNotification(" File Watcher", "Stopped monitoring project files",
                   ImVec4(0.8f, 0.4f, 0.4f, 1.0f));
  cout << "File watcher stopped" << endl;
}

void HandlerProject::FileWatcherLoop() {
  while (true) {
    // Wait for interval or stop signal
    {
      std::unique_lock<std::mutex> lock(fileWatcherMutex);
      fileWatcherCV.wait_for(lock, fileWatcherInterval,
                             [this]() { return !fileWatcherRunning; });

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
    for (const auto &entry : fs::recursive_directory_iterator(projectPath)) {
      const auto path = entry.path().string();
      const auto lastWriteTime = fs::last_write_time(entry);
      const auto timePoint =
          std::chrono::time_point_cast<std::chrono::system_clock::duration>(
              lastWriteTime - fs::file_time_type::clock::now() +
              std::chrono::system_clock::now());
      const auto timestamp = std::chrono::system_clock::to_time_t(timePoint);

      fileTimestamps[path] = timestamp;
    }
  } catch (const fs::filesystem_error &e) {
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
    for (const auto &entry : fs::recursive_directory_iterator(projectPath)) {
      const auto path = entry.path().string();
      const auto lastWriteTime = fs::last_write_time(entry);
      const auto timePoint =
          std::chrono::time_point_cast<std::chrono::system_clock::duration>(
              lastWriteTime - fs::file_time_type::clock::now() +
              std::chrono::system_clock::now());
      const auto timestamp = std::chrono::system_clock::to_time_t(timePoint);

      // Remove from the current timestamps map (remaining entries will be
      // deleted files)
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
    for (const auto &[path, timestamp] : currentTimestamps) {
      deletedFiles.push_back(path);
      fileTimestamps.erase(path);
      changesDetected = true;
    }

    // Handle detected changes
    if (changesDetected) {
      // Log changes
      for (const auto &path : newFiles) {
        cout << "New file detected: " << path << endl;
      }

      for (const auto &path : modifiedFiles) {
        cout << "Modified file detected: " << path << endl;
      }

      for (const auto &path : deletedFiles) {
        cout << "Deleted file detected: " << path << endl;
      }

      // Only show notification for significant changes
      if (!newFiles.empty() || !deletedFiles.empty() ||
          modifiedFiles.size() > 2) {
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

        ShowNotification(" Files Changed", message,
                         ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
      }
    }

  } catch (const fs::filesystem_error &e) {
    ::Log("Error in CheckForFileChanges: " + std::string(e.what()),
          Debug::LogLevel::CRASH);
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
    // ShowNotification("Assets Refreshed", "Project files have been updated",
    // ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
  } else {
    // ShowNotification("Assets", "Already up to date", ImVec4(0.4f, 0.8f,
    // 0.4f, 1.0f));
  }
}

void HandlerProject::SearchFileOrFolder(const AssetFile &node,
                                        const std::string &query,
                                        std::vector<AssetFile> &results) {
  // Case-insensitive search
  std::string nameLower = node.name;
  std::string queryLower = query;
  std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                 ::tolower);
  std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
                 ::tolower);

  if (nameLower.find(queryLower) != std::string::npos) {
    results.push_back(node);
  }

  if (node.isDirectory) {
    for (const auto &child : node.children) {
      SearchFileOrFolder(child, query, results);
    }
  }
}

void HandlerProject::NewScene(const std::string &name) {
  std::string sceneFolder = projectPath + "/assets/scenes";
  currentSceneName = name;
  fs::create_directories(sceneFolder);

  std::string fullPath = sceneFolder + "/" + name + ".ilmeeescene";

  std::vector<SceneObject> defaultObjects = {
      {"Camera", 0, 0, 100, 100, 0, 1, 1, "assets/camera.png"},
      {"Light", 0, 0, 100, 100, 0, 1, 1, "assets/light.png"}};

  WriteBinaryScene(fullPath, name, defaultObjects);

  ShowNotification("Scene Created",
                   "Binary scene " + name + " berhasil dibuat.",
                   ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
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
      std::ofstream ofs(createdFilePath);
      ofs.close();
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
    if (ImGui::InputText("##newfilename", renameBuffer,
                         IM_ARRAYSIZE(renameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue |
                             ImGuiInputTextFlags_AutoSelectAll)) {

      std::string newBaseName = renameBuffer[0]
                                    ? renameBuffer
                                    : (isScriptFile ? "NewScript" : "NewFile");

      if (isScriptFile) {
        std::string newCPPPath = targetFolder + "/" + newBaseName + ".cpp";
        std::string newHPPPath = targetFolder + "/" + newBaseName + ".hpp";
        if (!std::filesystem::exists(newCPPPath) &&
            !std::filesystem::exists(newHPPPath)) {
          std::filesystem::rename(createdCPPPath, newCPPPath);
          std::filesystem::rename(createdHPPPath, newHPPPath);
          ShowNotification("Created", "Script created: " + newBaseName,
                           ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        } else {
          ShowNotification("Rename Failed", "File already exists.",
                           ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
      } else {
        std::string newPath = targetFolder + "/" + newBaseName + ".txt";
        if (!std::filesystem::exists(newPath)) {
          std::filesystem::rename(createdFilePath, newPath);
          ShowNotification("Created", "File created: " + newBaseName,
                           ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        } else {
          ShowNotification("Rename Failed", "File already exists.",
                           ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
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

void HandlerProject::HandleRename(const AssetFile &node) {
  ImGui::PushID(node.fullPath.c_str());
  ImGui::SetNextItemWidth(200);

  if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_AutoSelectAll)) {
    std::string newPath =
        fs::path(node.fullPath).parent_path().string() + "/" + renameBuffer;
    if (!fs::exists(newPath)) {
      fs::rename(node.fullPath, newPath);
      ShowNotification("Renamed", node.name + " renamed to " + renameBuffer,
                       ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    } else {
      ShowNotification("Rename Failed", "File already exists.",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
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

  std::string newFolderPath =
      targetFolder + "/" + std::string(newFolderNameBuffer);
  try {
    // Membuat folder baru secara rekursif, jika belum ada
    bool created = std::filesystem::create_directory(newFolderPath);
    if (created) {
      std::cout << "Folder created: " << newFolderPath << std::endl;
    } else {
      // Jika folder sudah ada atau gagal dibuat tanpa exception
      std::cerr << "Failed to create folder (may already exist): "
                << newFolderPath << std::endl;
    }
  } catch (const std::filesystem::filesystem_error &e) {
    std::cerr << "Error creating folder: " << e.what() << std::endl;
  }
}

void HandlerProject::DeleteFolder(const std::string &folderPath) {
  // Menggunakan std::error_code untuk menangani error tanpa melempar exception
  std::error_code ec;
  // Hapus folder beserta isinya secara rekursif
  std::uintmax_t numRemoved = std::filesystem::remove_all(folderPath, ec);

  if (ec) {
    std::cerr << "Error deleting folder: " << folderPath << " - "
              << ec.message() << std::endl;
  } else {
    std::cout << "Deleted folder: " << folderPath << " (" << numRemoved
              << " items removed)" << std::endl;
    // Refresh atau update status proyek
    isOpenedProject = true;
  }
}

void HandlerProject::HandleRenameFolder(const AssetFile &node) {
  ImGui::PushID(node.fullPath.c_str());
  ImGui::SetNextItemWidth(200);

  if (ImGui::InputText("##rename", renameBuffer, IM_ARRAYSIZE(renameBuffer),
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_AutoSelectAll)) {
    std::string newPath =
        fs::path(node.fullPath).parent_path().string() + "/" + renameBuffer;
    if (!fs::exists(newPath)) {
      fs::rename(node.fullPath, newPath);
      ShowNotification("Renamed", node.name + " renamed to " + renameBuffer,
                       ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    } else {
      ShowNotification("Rename Failed", "Folder already exists.",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
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

void HandlerProject::HandleCopy(const AssetFile &node) {
  std::vector<std::string> selectedPaths;

  // A context menu opened on one of several selected items copies the whole
  // selection.  A tree-menu copy (or a normal single selection) copies only
  // the item that opened the menu.
  if (fileExplorerSelectedPaths.contains(node.fullPath)) {
    selectedPaths.assign(fileExplorerSelectedPaths.begin(),
                         fileExplorerSelectedPaths.end());
  } else {
    selectedPaths.push_back(node.fullPath);
  }

  std::sort(selectedPaths.begin(), selectedPaths.end());
  fileExplorerCopyTargets = std::move(selectedPaths);

  if (fileExplorerCopyTargets.size() == 1) {
    ShowNotification("Copy", "Copied: " + node.name,
                     ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
  } else {
    ShowNotification(
        "Copy",
        "Copied " + std::to_string(fileExplorerCopyTargets.size()) + " items",
        ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
  }
}

void HandlerProject::HandlePaste(const std::string &targetFolder) {
  if (fileExplorerCopyTargets.empty()) {
    ShowNotification("Paste Failed", "No item in clipboard!",
                     ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    return;
  }

  try {
    std::error_code ec;
    const fs::path destinationDirectory =
        AbsoluteNormalizedPath(fs::path(targetFolder));
    if (!fs::exists(destinationDirectory, ec) ||
        !fs::is_directory(destinationDirectory, ec)) {
      ShowNotification("Paste Failed",
                       "The current Explorer location is not a folder.",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
      return;
    }

    // Sort by path depth so a selected folder covers any selected child file.
    // This avoids copying the same child twice when the user Ctrl-clicks both.
    std::vector<fs::path> sourcePaths;
    for (const std::string &source : fileExplorerCopyTargets) {
      const fs::path sourcePath = AbsoluteNormalizedPath(fs::path(source));
      ec.clear();
      if (!fs::exists(sourcePath, ec)) {
        continue;
      }
      sourcePaths.push_back(sourcePath);
    }
    std::sort(sourcePaths.begin(), sourcePaths.end(),
              [](const fs::path &left, const fs::path &right) {
                return left.string().size() < right.string().size();
              });

    std::vector<fs::path> pathsToCopy;
    for (const fs::path &sourcePath : sourcePaths) {
      if (std::find(pathsToCopy.begin(), pathsToCopy.end(), sourcePath) !=
          pathsToCopy.end()) {
        continue;
      }

      const bool sourceIsDirectory = fs::is_directory(sourcePath, ec);
      ec.clear();
      if (sourcePath == destinationDirectory ||
          (sourceIsDirectory &&
           IsPathInside(sourcePath, destinationDirectory))) {
        continue;
      }

      bool coveredByFolder = false;
      for (const fs::path &parentPath : pathsToCopy) {
        if (fs::is_directory(parentPath, ec) &&
            IsPathInside(parentPath, sourcePath)) {
          coveredByFolder = true;
          break;
        }
        ec.clear();
      }
      if (!coveredByFolder) {
        pathsToCopy.push_back(sourcePath);
      }
    }

    std::size_t copiedCount = 0;
    std::vector<std::string> failures;
    for (const fs::path &sourcePath : pathsToCopy) {
      const fs::path targetPath =
          MakeUniqueCopyPath(sourcePath, destinationDirectory);
      ec.clear();
      if (CopyPath(sourcePath, targetPath, ec)) {
        ++copiedCount;
      } else {
        failures.push_back(sourcePath.filename().string() + ": " +
                           ec.message());
      }
    }

    if (copiedCount > 0) {
      fileExplorerCopyTargets.clear();
      rootAsset = BuildAssetTree(projectPath);
      isOpenedProject = true;
    }

    if (copiedCount == 0) {
      ShowNotification("Paste Failed",
                       failures.empty() ? "No valid items could be pasted here."
                                        : failures.front(),
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    } else if (failures.empty()) {
      ShowNotification("Paste Complete",
                       "Pasted " + std::to_string(copiedCount) + " item(s)",
                       ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
    } else {
      ShowNotification("Paste Partially Complete",
                       "Pasted " + std::to_string(copiedCount) + " item(s); " +
                           std::to_string(failures.size()) + " failed",
                       ImVec4(1.0f, 0.7f, 0.2f, 1.0f));
    }
  } catch (const fs::filesystem_error &e) {
    ShowNotification("Paste Failed", e.what(), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
}

bool HandlerProject::HandleImport(const std::string &targetFolder) {
  if (fileTargetImport.empty()) {
    ShowNotification("Import Failed", "No file selected!",
                     ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    return false;
  }

  try {
    const fs::path sourcePath = AbsoluteNormalizedPath(fileTargetImport);
    const fs::path destinationDirectory =
        AbsoluteNormalizedPath(fs::path(targetFolder));
    if (!fs::is_directory(destinationDirectory)) {
      throw fs::filesystem_error(
          "Import target is not a directory", destinationDirectory,
          std::make_error_code(std::errc::not_a_directory));
    }

    if (sourcePath == destinationDirectory ||
        (fs::is_directory(sourcePath) &&
         IsPathInside(sourcePath, destinationDirectory))) {
      throw fs::filesystem_error(
          "Cannot copy a folder into itself", sourcePath,
          std::make_error_code(std::errc::invalid_argument));
    }

    const fs::path targetPath =
        MakeUniqueCopyPath(sourcePath, destinationDirectory);
    std::error_code ec;
    if (!CopyPath(sourcePath, targetPath, ec)) {
      throw fs::filesystem_error("Unable to copy imported item", sourcePath,
                                 targetPath, ec);
    }

    ShowNotification("Import Successful",
                     "Imported: " + sourcePath.filename().string() +
                         "\nTo: " + targetFolder,
                     ImVec4(0.3f, 1.0f, 0.3f, 1.0f));

    // Clear import target
    fileTargetImport.clear();

    rootAsset = BuildAssetTree(projectPath);
    isOpenedProject = true;
    return true;

  } catch (const fs::filesystem_error &e) {
    ShowNotification("Import Failed", e.what(), ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    return false;
  }
}

void HandlerProject::HandlerOpenFileWithExtensionName(AssetFile &node) {
  std::string ext = fs::path(node.name).extension().string();
  bool isCpp = (ext == ".cpp");
  bool isHpp = (ext == ".hpp");
  bool isVideo = (ext == ".mp4" || ext == ".mkv" || ext == ".m4a" ||
                  ext == ".avi" || ext == ".mov");
  bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                  ext == ".gif" || ext == ".bmp");
  bool isAudio = (ext == ".mp3" || ext == ".wav" || ext == ".ogg");
  bool isShader =
      (ext == ".glsl" || ext == ".shader" || ext == ".frag" || ext == ".vert");

  if (isCpp || isHpp) {
#ifdef _WIN32
    std::string cmd = "antigravity \"" + projectPath + "\"";
#else
    std::string cmd = "antigravity \"" + node.fullPath + "\"";
#endif
    system(cmd.c_str());
  } else if (isVideo) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
    std::string cmd = "xdg-open \"" + node.fullPath + "\"";
    system(cmd.c_str());
#endif
  } else if (isImage || isAudio) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
    std::string cmd = "xdg-open \"" + node.fullPath + "\"";
    system(cmd.c_str());
#endif
  }
}

bool HandlerProject::IsFrameValid(const AVFrame *frame, int width, int height) {
  int totalPixels = width * height * 3;
  const uint8_t *data = frame->data[0];

  uint8_t minVal = 255;
  uint8_t maxVal = 0;

  for (int i = 0; i < totalPixels; i++) {
    if (data[i] < minVal)
      minVal = data[i];
    if (data[i] > maxVal)
      maxVal = data[i];
  }

  // Bedanya harus cukup besar agar tidak dianggap hitam/putih saja
  return (maxVal - minVal) > 30;
}

float HandlerProject::CalculateColorVariance(const uint8_t *data, int width,
                                             int height) {
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

void HandlerProject::FlipImageVertically(unsigned char *data, int width,
                                         int height, int channels) {
  int stride = width * channels;
  std::vector<unsigned char> row(stride);
  for (int y = 0; y < height / 2; ++y) {
    unsigned char *rowTop = data + y * stride;
    unsigned char *rowBottom = data + (height - y - 1) * stride;
    std::memcpy(row.data(), rowTop, stride);
    std::memcpy(rowTop, rowBottom, stride);
    std::memcpy(rowBottom, row.data(), stride);
  }
}

HandlerProject::IconInfo
HandlerProject::GenerateVideoThumbnail(const std::string &videoPath) {
  IconInfo thumbnailInfo;
  thumbnailInfo.width = thumbnailSize.x;
  thumbnailInfo.height = thumbnailSize.y;

  AVFormatContext *formatContext = nullptr;
  if (avformat_open_input(&formatContext, videoPath.c_str(), nullptr,
                          nullptr) != 0) {
    ::Log("Failed to open video file: " + videoPath, Debug::LogLevel::CRASH);
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
  AVCodecParameters *codecParams =
      formatContext->streams[videoStream]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
  AVCodecContext *codecContext = avcodec_alloc_context3(codec);
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
  AVFrame *frame = av_frame_alloc();
  AVFrame *rgbFrame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();

  // Allocate buffer for RGB frame
  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, thumbnailSize.x,
                                          thumbnailSize.y, 1);
  uint8_t *buffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer,
                       AV_PIX_FMT_RGB24, thumbnailSize.x, thumbnailSize.y, 1);

  // Initialize software scaler
  SwsContext *swsContext =
      sws_getContext(codecContext->width, codecContext->height,
                     codecContext->pix_fmt, thumbnailSize.x, thumbnailSize.y,
                     AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

  int framesTried = 0;
  float maxVariance = 0.0f;
  std::vector<uint8_t> bestFrameData(numBytes);

  // int framesTried = 0;
  const int maxFramesToTry = 20;

  while (av_read_frame(formatContext, packet) >= 0 &&
         framesTried < maxFramesToTry) {
    if (packet->stream_index == videoStream) {
      if (avcodec_send_packet(codecContext, packet) >= 0) {
        while (avcodec_receive_frame(codecContext, frame) >= 0) {
          sws_scale(swsContext, frame->data, frame->linesize, 0,
                    codecContext->height, rgbFrame->data, rgbFrame->linesize);

          float variance = CalculateColorVariance(
              rgbFrame->data[0], thumbnailSize.x, thumbnailSize.y);

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
  // GLuint textureID;
  // glGenTextures(1, &textureID);
  // glBindTexture(GL_TEXTURE_2D, textureID);
  // // FlipImageVertically(bestFrameData.data(), thumbnailSize.x,
  // thumbnailSize.y,
  // // 4);
  // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, thumbnailSize.x, thumbnailSize.y, 0,
  //              GL_RGB, GL_UNSIGNED_BYTE, rgbFrame->data[0]);
  // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  thumbnailInfo.textureId = (ImTextureID)0;

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

void HandlerProject::HandleRenameOperation(AssetFile &node,
                                           const ImVec2 &cursorPos,
                                           float itemWidth, float itemHeight) {
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
                       ImGuiInputTextFlags_EnterReturnsTrue |
                           ImGuiInputTextFlags_AutoSelectAll)) {

    std::string newName = renameBuffer;
    if (newName.empty()) {
      ShowNotification("Rename Failed", "Name cannot be empty",
                       ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    } else {
      std::string newPath =
          fs::path(node.fullPath).parent_path().string() + "/" + newName;

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
                           fs::path(node.fullPath).filename().string() + " → " +
                               fs::path(newPath).filename().string(),
                           ImVec4(0.3f, 1.0f, 0.3f, 1.0f));

          isOpenedProject = true; // Trigger refresh
        } else {
          ShowNotification("Rename Failed",
                           "A file or folder with this name already exists",
                           ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
      } catch (const std::exception &e) {
        ShowNotification("Rename Failed", e.what(),
                         ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
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
