#include "../../../include/core_engine/Debugger.hpp"
#include "../../../include/core_engine/UserDataDir.hpp"
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

std::string ToLowerAsciiStr(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

std::vector<std::string> SplitFilterTokens(const std::string &filter) {
  std::vector<std::string> tokens;
  std::string cur;
  for (char ch : filter) {
    if (ch == ' ' || ch == ',' || ch == ';' || ch == '\t') {
      if (!cur.empty()) {
        tokens.push_back(ToLowerAsciiStr(cur));
        cur.clear();
      }
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) {
    tokens.push_back(ToLowerAsciiStr(cur));
  }
  return tokens;
}

bool MatchesAssetFilter(const std::string &name, bool isDirectory,
                        const std::string &filter,
                        bool keepFoldersOnExtFilter = true) {
  if (filter.empty())
    return true;
  auto tokens = SplitFilterTokens(filter);
  if (tokens.empty())
    return true;

  bool allExtensions = true;
  for (const auto &tok : tokens) {
    if (tok.empty() || tok[0] != '.') {
      allExtensions = false;
      break;
    }
  }

  std::string nameLower = ToLowerAsciiStr(name);
  if (allExtensions) {
    if (isDirectory && keepFoldersOnExtFilter)
      return true;
    for (const auto &extTok : tokens) {
      if (nameLower.size() >= extTok.size() &&
          nameLower.compare(nameLower.size() - extTok.size(), extTok.size(),
                            extTok) == 0) {
        return true;
      }
    }
    return false;
  }

  for (const auto &tok : tokens) {
    if (nameLower.find(tok) != std::string::npos)
      return true;
  }
  return false;
}

size_t PrevUtf8Boundary(const std::string &s, size_t pos) {
  if (pos == 0)
    return 0;
  --pos;
  while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80) {
    --pos;
  }
  return pos;
}

size_t NextUtf8Boundary(const std::string &s, size_t pos) {
  if (pos >= s.size())
    return s.size();
  ++pos;
  while (pos < s.size() && ((unsigned char)s[pos] & 0xC0) == 0x80) {
    ++pos;
  }
  return pos;
}

std::string EllipsizeUtf8ToWidth(const std::string &text, float maxWidth) {
  if (text.empty() || maxWidth <= 8.0f)
    return text;
  if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
    return text;

  const std::string dots = "..";
  size_t cut = text.size();
  while (cut > 0) {
    cut = PrevUtf8Boundary(text, cut);
    std::string candidate = text.substr(0, cut) + dots;
    if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
      return candidate;
  }
  return dots;
}

std::pair<std::string, std::string>
SplitTwoLinesToWidth(const std::string &text, float maxWidth) {
  if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
    return {text, {}};
  }

  size_t pos = 0;
  size_t lastFit = 0;
  size_t preferredBreak = 0;
  while (pos < text.size()) {
    size_t next = NextUtf8Boundary(text, pos);
    std::string slice = text.substr(0, next);
    if (ImGui::CalcTextSize(slice.c_str()).x > maxWidth)
      break;
    lastFit = next;
    char ch = text[pos];
    if (ch == ' ' || ch == '_' || ch == '-' || ch == '.')
      preferredBreak = next;
    pos = next;
  }

  size_t splitAt = (preferredBreak >= 4 && preferredBreak < text.size())
                       ? preferredBreak
                       : (lastFit > 0 ? lastFit : NextUtf8Boundary(text, 0));
  std::string line1 = text.substr(0, splitAt);
  while (!line1.empty() && line1.back() == ' ')
    line1.pop_back();
  std::string rest = text.substr(splitAt);
  while (!rest.empty() && rest.front() == ' ')
    rest.erase(rest.begin());
  std::string line2 = EllipsizeUtf8ToWidth(rest, maxWidth);
  return {line1, line2};
}

std::string FormatHumanFileSize(const std::string &fullPath) {
  std::error_code ec;
  auto bytes = fs::file_size(fullPath, ec);
  if (ec)
    return "";
  char buf[64];
  if (bytes < 1024)
    snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
  else if (bytes < 1024ULL * 1024ULL)
    snprintf(buf, sizeof(buf), "%.1f KB", (double)bytes / 1024.0);
  else if (bytes < 1024ULL * 1024ULL * 1024ULL)
    snprintf(buf, sizeof(buf), "%.2f MB", (double)bytes / (1024.0 * 1024.0));
  else
    snprintf(buf, sizeof(buf), "%.2f GB",
             (double)bytes / (1024.0 * 1024.0 * 1024.0));
  return buf;
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
  NFD::Guard nfdGuard;
  NFD::UniquePath savePath;

  nfdfilteritem_t filterItem[1] = {{"Scene", "ilmeeescene"}};
  const std::string baseName =
      currentSceneName.empty() ? "Untitled" : currentSceneName;
  const std::string defaultFile = baseName + ".ilmeeescene";
  std::string defaultDir;
  if (!currentScenePath.empty()) {
    defaultDir = fs::path(currentScenePath).parent_path().string();
  } else if (!projectPath.empty()) {
    defaultDir = (fs::path(projectPath) / "scenes").string();
  }

  const nfdresult_t result = NFD::SaveDialog(
      savePath, filterItem, 1,
      defaultDir.empty() ? nullptr : defaultDir.c_str(), defaultFile.c_str());
  if (result == NFD_OKAY && savePath.get() != nullptr) {
    fs::path chosen(savePath.get());
    if (chosen.extension() != ".ilmeeescene") {
      chosen.replace_extension(".ilmeeescene");
    }
    currentScenePath = chosen.string();
    currentSceneName = chosen.stem().string();
    serializer.SaveScene(currentScene, currentScenePath);
    ShowNotification("Scene Saved",
                     "Successfully saved " + chosen.filename().string(),
                     ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
  } else if (result == NFD_CANCEL) {
    puts("User pressed cancel.");
  } else {
    const char *err = NFD::GetError();
    printf("Error: %s\n", err ? err : "Save dialog failed");
    ShowNotification("Save Error",
                     err ? std::string(err) : "Failed to open Save As dialog",
                     ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
  }
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
  const bool resumeWatcher = fileWatcherRunning.load();
  if (resumeWatcher) StopFileWatcher();
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
  currentScenePath = (fs::path(projectPath) / "scenes" / "main.ilmeeescene").string();
  currentSceneName = "main";
  ::Log("Project loaded successfully: " + projectPath,
        Debug::LogLevel::SUCCESS);
  if (resumeWatcher) StartFileWatcher();
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
  const float iconSz = 16.0f;
  ImDrawList *drawList = ImGui::GetWindowDrawList();

  if (node.isDirectory) {
    if (fileExplorerRenameTarget == node.fullPath) {
      HandleRenameFolder(node);
      return;
    }

    const bool isCurrentDir =
        !currentDirectory.empty() &&
        fs::path(currentDirectory).lexically_normal() ==
            fs::path(node.fullPath).lexically_normal();

    ImGuiTreeNodeFlags nodeFlags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding |
        ImGuiTreeNodeFlags_DrawLinesToNodes;
    if (isCurrentDir) {
      nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string treeLabel = "     " + node.name + "##dir_" + node.fullPath;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
    bool nodeOpen = ImGui::TreeNodeEx(treeLabel.c_str(), nodeFlags);
    ImGui::PopStyleVar();

    ImVec2 itemMin = ImGui::GetItemRectMin();
    float itemH = ImGui::GetItemRectSize().y;

    if (svgIcons_) {
      AssetIconInfo iconMeta =
          SvgIconManager::ResolveAssetIcon(node.fullPath, true, nodeOpen);
      ImTextureID tex = svgIcons_->GetIcon(iconMeta.svgPath, 20);
      if (tex) {
        float ix = itemMin.x + ImGui::GetTreeNodeToLabelSpacing() - 1.0f;
        float iy = itemMin.y + (itemH - iconSz) * 0.5f;
        drawList->AddImage(tex, ImVec2(ix, iy), ImVec2(ix + iconSz, iy + iconSz));
      }
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsItemToggledOpen()) {
      currentDirectory = node.fullPath;
      fileExplorerSelectedPaths.clear();
      selectedAsset = nullptr;
    }

    std::string popupId = "context_menu_dir_" + node.fullPath;
    if (ImGui::BeginPopupContextItem(popupId.c_str())) {
      if (ImGui::MenuItem("Copy")) {
        HandleCopy(node);
      }
      if (ImGui::MenuItem("Paste", nullptr, false,
                          !fileExplorerCopyTargets.empty())) {
        HandlePaste(node.fullPath);
      }
      if (ImGui::MenuItem("Import...")) {
        OpenFile();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("New Folder")) {
        fileExplorerRenameBufferSet = true;
        HandleCreateNewFolder(node.fullPath);
      }
      HandleCreateNewFile(node.fullPath);
      ImGui::Separator();
      if (ImGui::MenuItem("Rename")) {
        fileExplorerRenameTarget = node.fullPath;
        strncpy(renameBuffer, node.name.c_str(), sizeof(renameBuffer) - 1);
        renameBuffer[sizeof(renameBuffer) - 1] = '\0';
      }
      if (ImGui::MenuItem("Delete")) {
        DeleteFolder(node.fullPath);
      }
      ImGui::EndPopup();
    }

    if (nodeOpen) {
      for (const auto &child : node.children) {
        DrawAssetTree(child);
      }
      ImGui::TreePop();
    }
  } else {
    if (renamingPath == node.fullPath) {
      HandleRename(node);
      return;
    }

    const bool isSelected = fileExplorerSelectedPaths.contains(node.fullPath);
    ImGuiTreeNodeFlags leafFlags =
        ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
        ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_FramePadding |
        ImGuiTreeNodeFlags_DrawLinesToNodes;
    if (isSelected) {
      leafFlags |= ImGuiTreeNodeFlags_Selected;
    }

    std::string leafLabel = "     " + node.name + "##file_" + node.fullPath;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.85f, 0.90f, 1.0f));
    ImGui::TreeNodeEx(leafLabel.c_str(), leafFlags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImVec2 itemMin = ImGui::GetItemRectMin();
    float itemH = ImGui::GetItemRectSize().y;

    AssetIconInfo iconMeta =
        SvgIconManager::ResolveAssetIcon(node.fullPath, false, false);
    if (svgIcons_) {
      std::string treeSvg =
          (iconMeta.svgPath == node.fullPath)
              ? std::string("assets/icons/svg/ext_image.svg")
              : iconMeta.svgPath;
      ImTextureID tex = svgIcons_->GetIcon(treeSvg, 20);
      if (tex) {
        float ix = itemMin.x + ImGui::GetTreeNodeToLabelSpacing() - 1.0f;
        float iy = itemMin.y + (itemH - iconSz) * 0.5f;
        drawList->AddImage(tex, ImVec2(ix, iy), ImVec2(ix + iconSz, iy + iconSz));
      }
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
      const std::string &fp = node.fullPath;
      ImGui::SetDragDropPayload("ASSET_PATH", fp.c_str(), fp.size() + 1);
      ImGui::Text("%s", node.name.c_str());
      ImGui::EndDragDropSource();
    }

    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(node.name.c_str()).x >
                                      ImGui::GetContentRegionAvail().x * 0.7f) {
      ImGui::SetTooltip("%s", node.name.c_str());
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
      fileExplorerSelectedPaths.clear();
      fileExplorerSelectedPaths.insert(node.fullPath);
    }

    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      if (ilmeee::IsAnimationPath(node.fullPath) ||
          ilmeee::IsAnimationControllerPath(node.fullPath)) {
        if (onFileClicked)
          onFileClicked(node);
      } else {
        HandlerOpenFileWithExtensionName(const_cast<AssetFile &>(node));
        if (onFileClicked)
          onFileClicked(node);
      }
    }

    std::string popupId = "context_menu_" + node.fullPath;
    if (ImGui::BeginPopupContextItem(popupId.c_str())) {
      if (ImGui::MenuItem("Open")) {
        HandlerOpenFileWithExtensionName(const_cast<AssetFile &>(node));
        if (onFileClicked)
          onFileClicked(node);
      }
      if (ImGui::MenuItem("Edit in Code Editor")) {
        OpenFile(node.fullPath);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Copy")) {
        HandleCopy(node);
      }
      if (ImGui::MenuItem("Paste", nullptr, false,
                          !fileExplorerCopyTargets.empty())) {
        HandlePaste(fs::path(node.fullPath).parent_path().string());
      }
      if (ImGui::MenuItem("Rename")) {
        renamingPath = node.fullPath;
        strncpy(renameBuffer, node.name.c_str(), sizeof(renameBuffer) - 1);
        renameBuffer[sizeof(renameBuffer) - 1] = '\0';
      }
      if (ImGui::MenuItem("Delete")) {
        DeleteFileOrFolder(node.fullPath);
      }
      ImGui::EndPopup();
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

void HandlerProject::DrawBreadcrumbs(const std::string &pathStr) {
  if (pathStr.empty()) {
    ImGui::TextDisabled("No folder selected");
    return;
  }

  fs::path cur = fs::path(pathStr).lexically_normal();
  fs::path projRoot =
      projectPath.empty() ? fs::path() : fs::path(projectPath).lexically_normal();

  struct Crumb {
    std::string label;
    std::string targetPath;
  };
  std::vector<Crumb> crumbs;

  std::error_code ec;
  if (!projRoot.empty() &&
      (cur == projRoot || IsPathInside(projRoot, cur))) {
    std::string rootName = projRoot.filename().string();
    if (rootName.empty())
      rootName = "Project";
    crumbs.push_back({std::string(ICON_UI_EXPLORER) + " " + rootName,
                      projRoot.string()});
    if (cur != projRoot) {
      fs::path rel = fs::relative(cur, projRoot, ec);
      fs::path accum = projRoot;
      for (const auto &part : rel) {
        if (part.empty() || part == ".")
          continue;
        accum /= part;
        crumbs.push_back({part.string(), accum.string()});
      }
    }
  } else {
    // External directory (e.g. Engine Motions): show last up to 4 segments
    std::vector<Crumb> allParts;
    fs::path walk = cur;
    while (!walk.empty() && walk != walk.root_path()) {
      std::string seg = walk.filename().string();
      if (!seg.empty())
        allParts.push_back({seg, walk.string()});
      walk = walk.parent_path();
    }
    std::reverse(allParts.begin(), allParts.end());
    size_t startIdx = allParts.size() > 4 ? allParts.size() - 4 : 0;
    for (size_t i = startIdx; i < allParts.size(); ++i) {
      crumbs.push_back(allParts[i]);
    }
  }

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 2.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));

  for (size_t i = 0; i < crumbs.size(); ++i) {
    bool isLast = (i + 1 == crumbs.size());
    if (isLast) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1.0f));
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.64f, 0.69f, 0.77f, 1.0f));
    }

    std::string btnId = crumbs[i].label + "##crumb_" + std::to_string(i);
    if (ImGui::Button(btnId.c_str())) {
      currentDirectory = crumbs[i].targetPath;
      fileExplorerSelectedPaths.clear();
      selectedAsset = nullptr;
    }
    ImGui::PopStyleColor();

    if (!isLast) {
      ImGui::SameLine();
      ImGui::TextDisabled(">");
      ImGui::SameLine();
    }
  }

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

void HandlerProject::DrawFileExplorer(AssetFile &node) {
  ImGui::PushID(node.fullPath.c_str());

  const float padX = 8.0f;
  const float lineH = ImGui::GetTextLineHeight();
  const float labelAreaH = lineH * 2.0f + 8.0f;
  const float cardWidth = thumbnailSize.x + padX * 2.0f;
  const float cardHeight = thumbnailSize.y + labelAreaH + 10.0f;
  ImVec2 totalSize(cardWidth, cardHeight);

  ImVec2 cursorPos = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##ItemButton", totalSize);
  const bool itemHovered = ImGui::IsItemHovered();
  const bool itemSelected = fileExplorerSelectedPaths.contains(node.fullPath);

  AssetIconInfo iconMeta =
      SvgIconManager::ResolveAssetIcon(node.fullPath, node.isDirectory, false);

  // Rich informative Unity-style tooltip on hover
  if (itemHovered) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(node.name.c_str());
    if (node.isDirectory) {
      ImGui::TextDisabled("Folder");
    } else {
      std::string sizeText = FormatHumanFileSize(node.fullPath);
      if (!iconMeta.badgeLabel.empty() && !sizeText.empty()) {
        ImGui::TextDisabled("%s Asset  •  %s", iconMeta.badgeLabel.c_str(),
                            sizeText.c_str());
      } else if (!iconMeta.badgeLabel.empty()) {
        ImGui::TextDisabled("%s Asset", iconMeta.badgeLabel.c_str());
      }
      if (ilmeee::IsAnimationPath(node.fullPath) ||
          ilmeee::IsAnimationControllerPath(node.fullPath)) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.65f, 1.0f),
                           "Double-click to preview in Animation panel");
      }
    }
    ImGui::EndTooltip();
  }

  // Drag source for dropping assets onto Scene viewport / Inspector slots
  if (!node.isDirectory &&
      ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
    const std::string &fp = node.fullPath;
    ImGui::SetDragDropPayload("ASSET_PATH", fp.c_str(), fp.size() + 1);
    ImGui::Text("%s %s", iconMeta.badgeLabel.empty() ? "" :
                         ("[" + iconMeta.badgeLabel + "]").c_str(),
                node.name.c_str());
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
    } else {
      HandlerOpenFileWithExtensionName(node);
      selectedAsset = &node;
      if (onFileClicked)
        onFileClicked(node);
    }
  } else if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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

  if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
      !fileExplorerSelectedPaths.contains(node.fullPath)) {
    fileExplorerSelectedPaths.clear();
    fileExplorerSelectedPaths.insert(node.fullPath);
  }

  // Draw Unity-style Asset Card surface
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 cardMax(cursorPos.x + cardWidth, cursorPos.y + cardHeight);
  if (itemSelected) {
    drawList->AddRectFilled(
        cursorPos, cardMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.35f, 0.60f, 0.42f)),
        5.0f);
    drawList->AddRect(
        cursorPos, cardMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.26f, 0.54f, 0.98f, 0.95f)),
        5.0f, 0, 1.5f);
  } else if (itemHovered) {
    drawList->AddRectFilled(
        cursorPos, cardMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.23f, 0.28f, 0.65f)),
        5.0f);
    drawList->AddRect(
        cursorPos, cardMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.32f, 0.36f, 0.43f, 0.70f)),
        5.0f, 0, 1.0f);
  } else {
    drawList->AddRectFilled(
        cursorPos, cardMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(0.14f, 0.15f, 0.18f, 0.45f)),
        5.0f);
  }

  // Draw padded crisp icon / preview thumbnail
  const float iconInset = 6.0f;
  const float drawSide = std::max(24.0f, thumbnailSize.x - iconInset * 2.0f);
  const float iconPosX = cursorPos.x + (cardWidth - drawSide) * 0.5f;
  const float iconPosY = cursorPos.y + 6.0f + iconInset * 0.5f;
  IconInfo texInfo = GetIconForFile(node);
  if (texInfo.textureId) {
    // Preserve aspect ratio for non-square image thumbnails
    float w = drawSide;
    float h = drawSide;
    if (texInfo.width > 0 && texInfo.height > 0 &&
        texInfo.width != texInfo.height) {
      float aspect = (float)texInfo.width / (float)texInfo.height;
      if (aspect > 1.0f) {
        h = drawSide / aspect;
      } else {
        w = drawSide * aspect;
      }
    }
    float ox = iconPosX + (drawSide - w) * 0.5f;
    float oy = iconPosY + (drawSide - h) * 0.5f;
    drawList->AddImage(texInfo.textureId, ImVec2(ox, oy),
                       ImVec2(ox + w, oy + h));
  }

  // Extension pill badge at bottom-right of icon area (for files)
  if (!node.isDirectory && !iconMeta.badgeLabel.empty() &&
      thumbnailSize.x >= 52.0f) {
    ImGui::SetWindowFontScale(0.76f);
    ImVec2 badgeTxtSz = ImGui::CalcTextSize(iconMeta.badgeLabel.c_str());
    const float bPadX = 4.0f;
    const float bPadY = 1.5f;
    const float bW = badgeTxtSz.x + bPadX * 2.0f;
    const float bH = badgeTxtSz.y + bPadY * 2.0f;
    ImVec2 bMax(cursorPos.x + cardWidth - 5.0f,
                cursorPos.y + thumbnailSize.y + 1.0f);
    ImVec2 bMin(bMax.x - bW, bMax.y - bH);

    ImVec4 pillBg(iconMeta.accentColor.x * 0.78f,
                  iconMeta.accentColor.y * 0.78f,
                  iconMeta.accentColor.z * 0.78f, 0.94f);
    drawList->AddRectFilled(bMin, bMax, ImGui::ColorConvertFloat4ToU32(pillBg),
                            3.0f);
    drawList->AddText(ImVec2(bMin.x + bPadX, bMin.y + bPadY),
                      IM_COL32(250, 252, 255, 255),
                      iconMeta.badgeLabel.c_str());
    ImGui::SetWindowFontScale(1.0f);
  }

  // Two-line centered UTF-8 safe filename
  auto [line1, line2] = SplitTwoLinesToWidth(node.name, cardWidth - 8.0f);
  const float textBaseY = cursorPos.y + thumbnailSize.y + 5.0f;
  const ImU32 textCol =
      itemSelected
          ? IM_COL32(255, 255, 255, 255)
          : (node.isDirectory ? IM_COL32(235, 239, 245, 255)
                              : IM_COL32(208, 214, 224, 255));

  ImVec2 l1Sz = ImGui::CalcTextSize(line1.c_str());
  drawList->AddText(
      ImVec2(cursorPos.x + (cardWidth - l1Sz.x) * 0.5f, textBaseY), textCol,
      line1.c_str());
  if (!line2.empty()) {
    ImVec2 l2Sz = ImGui::CalcTextSize(line2.c_str());
    drawList->AddText(ImVec2(cursorPos.x + (cardWidth - l2Sz.x) * 0.5f,
                             textBaseY + lineH),
                      IM_COL32(165, 174, 188, 255), line2.c_str());
  }

  // Context menu & inline rename
  if (fileExplorerRenameTarget == node.fullPath) {
    HandleRenameOperation(node, cursorPos, cardWidth, cardHeight);
  } else {
    if (ImGui::BeginPopupContextItem()) {
      if (!node.isDirectory) {
        if ((ilmeee::IsAnimationPath(node.fullPath) ||
             ilmeee::IsAnimationControllerPath(node.fullPath)) &&
            ImGui::MenuItem("Preview Motion")) {
          if (onFileClicked)
            onFileClicked(node);
        }
        if (ImGui::MenuItem("Open")) {
          HandlerOpenFileWithExtensionName(const_cast<AssetFile &>(node));
          if ((ilmeee::IsAnimationPath(node.fullPath) ||
               ilmeee::IsAnimationControllerPath(node.fullPath)) &&
              onFileClicked)
            onFileClicked(node);
        }
        ImGui::Separator();
      } else {
        if (ImGui::MenuItem("Open Folder")) {
          currentDirectory = node.fullPath;
          fileExplorerSelectedPaths.clear();
        }
        ImGui::Separator();
      }

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
      if (ImGui::MenuItem("Rename")) {
        fileExplorerRenameTarget = node.fullPath;
        strncpy(renameBuffer, node.name.c_str(), sizeof(renameBuffer) - 1);
        renameBuffer[sizeof(renameBuffer) - 1] = '\0';
      }
      if (ImGui::MenuItem("Delete")) {
        if (node.isDirectory) {
          DeleteFolder(node.fullPath);
        } else {
          DeleteFileOrFolder(node.fullPath);
        }
      }

      ImGui::EndPopup();
    }
  }

  ImGui::SameLine(0, itemSpacing);
  if (ImGui::GetCursorPosX() + cardWidth >
      ImGui::GetWindowContentRegionMax().x) {
    ImGui::NewLine();
  }

  ImGui::PopID();
}

void HandlerProject::DrawFolderGridView() {
  const double now = ImGui::GetTime();
  if (gridFilesDirectory != currentDirectory ||
      gridFilesVersion != assetTreeVersion ||
      now - gridFilesTime >= kGridRefreshSec) {
    gridFiles = GetFilesInDirectory(currentDirectory);
    std::sort(gridFiles.begin(), gridFiles.end(),
              [](const AssetFile &a, const AssetFile &b) {
                if (a.isDirectory != b.isDirectory)
                  return a.isDirectory;
                return ToLowerAsciiStr(a.name) < ToLowerAsciiStr(b.name);
              });
    gridFilesDirectory = currentDirectory;
    gridFilesVersion = assetTreeVersion;
    gridFilesTime = now;
  }

  const std::string rootDirectory =
      (fs::path(projectPath) / "assets").lexically_normal().string();

  // Top Breadcrumb + Filter Chip + Compact Zoom Bar (Unity Project Header)
  DrawBreadcrumbs(currentDirectory);

  const float rightControlsWidth =
      currentFilter.empty() ? 124.0f : 230.0f;
  float availRight = ImGui::GetWindowContentRegionMax().x - rightControlsWidth;
  if (availRight > ImGui::GetCursorPosX() + 12.0f) {
    ImGui::SameLine(availRight);
  } else {
    ImGui::SameLine();
  }

  if (!currentFilter.empty()) {
    std::string chipLabel = currentFilter;
    if (chipLabel.size() > 12)
      chipLabel = chipLabel.substr(0, 10) + "..";
    chipLabel = std::string(ICON_UI_FILTER " ") + chipLabel + " ×";
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.36f, 0.62f, 0.85f));
    if (ImGui::SmallButton(chipLabel.c_str())) {
      currentFilter.clear();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Clear active filter (%s)", currentFilter.c_str());
    ImGui::SameLine();
  }

  ImGui::TextDisabled(ICON_UI_GRID);
  ImGui::SameLine(0, 4.0f);
  ImGui::SetNextItemWidth(92.0f);
  ImGui::SliderFloat("##GridIconSize", &thumbnailSize.x, 48.0f, 120.0f,
                     "%.0fpx");
  thumbnailSize.y = thumbnailSize.x;

  ImGui::Separator();

  if (ImGui::BeginChild("FileGrid", ImVec2(0, 0), false)) {
    size_t visibleCount = 0;
    for (auto &file : gridFiles) {
      if (!MatchesAssetFilter(file.name, file.isDirectory, currentFilter, true))
        continue;
      DrawFileExplorer(file);
      ++visibleCount;
    }

    if (visibleCount == 0) {
      ImGui::Spacing();
      ImGui::TextDisabled(currentFilter.empty()
                              ? "  This folder is empty."
                              : "  No assets match the current filter.");
    }

    if (ImGui::BeginPopupContextWindow("FileGridContentMenu",
                                       ImGuiPopupFlags_MouseButtonRight |
                                           ImGuiPopupFlags_NoOpenOverItems)) {

      const bool isInRootDirectory =
          fs::path(currentDirectory).lexically_normal() ==
          fs::path(rootDirectory).lexically_normal();

      if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("Folder")) {
          HandleCreateNewFolder(currentDirectory);
        }

        if (ImGui::BeginMenu("C++ Script")) {
          HandleCreateNewFile(currentDirectory);
          ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Shader")) {
          ShowNotification("New Shader", "Creating new shader...",
                           ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
        }

        ImGui::EndMenu();
      }

      if (ImGui::MenuItem("Paste", nullptr, false,
                          !fileExplorerCopyTargets.empty())) {
        HandlePaste(currentDirectory);
      }

      ImGui::Separator();
      if (ImGui::MenuItem("Import Files...")) {
        OpenFile();
      }

      if (ImGui::MenuItem("Import Folder...")) {
        ImportFolder();
      }

      if (!isInRootDirectory) {
        ImGui::Separator();
        if (ImGui::MenuItem("Reveal in File Manager")) {
#ifdef _WIN32
          ShellExecuteA(NULL, "explore", currentDirectory.c_str(), NULL, NULL,
                        SW_SHOW);
#else
          std::string cmd = "xdg-open \"" + currentDirectory + "\"";
          if (system(cmd.c_str()) != 0) {}
#endif
        }
      }

      ImGui::EndPopup();
    }
  }
  ImGui::EndChild();
}

void HandlerProject::OpenFile(const std::string &fileName) {
  const auto settings = ilmeee::LoadCodeEditorSettings(
      ilmeee::UserDataRoot() / "editor-preferences.json");
  std::string error;
  if (!ilmeee::OpenInCodeEditor(settings, fileName, error, projectPath)) {
    ShowNotification("Code Editor", error, redColor);
    ::Log(error, Debug::LogLevel::ERROR);
  }
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

  std::lock_guard<std::mutex> lock(notificationMutex);
  pendingNotifications.push_back(Notification{
      title, message, color, 0.0f,
      3.0f // Duration in seconds
  });
}

void HandlerProject::RenderNotifications() {
  float padding = 10.0f;
  float notificationWidth = 300.0f;
  float startY = padding;
  float currentTime = ImGui::GetTime();
  // File watching and script builds can notify from worker threads. Only
  // this UI thread touches ImGui time and the visible notification list.
  {
    std::lock_guard<std::mutex> lock(notificationMutex);
    for (auto &notification : pendingNotifications) {
      notification.startTime = currentTime;
      notifications.push_back(std::move(notification));
    }
    pendingNotifications.clear();
  }

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
HandlerProject::GetIconForFile(const AssetFile &node) {
  AssetIconInfo meta =
      SvgIconManager::ResolveAssetIcon(node.fullPath, node.isDirectory, false);

  if (!node.isDirectory) {
    if (meta.isImagePreviewable) {
      IconInfo imgTex = LoadCachedTexture(node.fullPath);
      if (imgTex.textureId)
        return imgTex;
      return LoadCachedTexture("assets/icons/svg/ext_image.svg");
    }

    if (meta.isVideoPreviewable) {
      auto cachedThumbnail = iconCacheInfo.find(node.fullPath);
      if (cachedThumbnail != iconCacheInfo.end()) {
        return cachedThumbnail->second;
      }
      IconInfo thumbnail = GenerateVideoThumbnail(node.fullPath);
      if (!thumbnail.textureId) {
        thumbnail = LoadCachedTexture("assets/icons/svg/ext_video.svg");
      }
      iconCacheInfo[node.fullPath] = thumbnail;
      return thumbnail;
    }

    if (meta.isModelPreviewable && ilmeee::IsPreviewableModel(node.name)) {
      const std::string preview = modelThumbnails.Request(node.fullPath);
      if (!preview.empty()) {
        IconInfo modelTex = LoadCachedTexture(preview);
        if (modelTex.textureId)
          return modelTex;
      }
      return LoadCachedTexture("assets/icons/svg/ext_model.svg");
    }
  }

  return LoadCachedTexture(meta.svgPath);
}

HandlerProject::IconInfo
HandlerProject::LoadCachedTexture(const std::string &path) {
  auto it = iconCacheInfo.find(path);
  if (it != iconCacheInfo.end())
    return it->second;

  std::string extLower = ToLowerAsciiStr(fs::path(path).extension().string());
  if (extLower == ".svg") {
    if (svgIcons_) {
      constexpr int kGridSvgRasterPx = 128;
      ImTextureID tex = svgIcons_->GetIcon(path, kGridSvgRasterPx);
      if (tex) {
        IconInfo info;
        info.textureId = tex;
        info.width = kGridSvgRasterPx;
        info.height = kGridSvgRasterPx;
        iconCacheInfo[path] = info;
        return info;
      }
    }
    return {(ImTextureID)0, 0, 0};
  }

  if (vulkanHandler) {
    int width = 0, height = 0, channels = 0;
    if (!stbi_info(path.c_str(), &width, &height, &channels)) {
      return {(ImTextureID)0, 0, 0};
    }

    try {
      ImTextureID tex = (ImTextureID)vulkanHandler->LoadImage(path.c_str());
      IconInfo info;
      info.textureId = tex;
      info.width = width;
      info.height = height;
      iconCacheInfo[path] = info;
      return info;
    } catch (const std::exception &) {
      return {(ImTextureID)0, 0, 0};
    }
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
  std::lock_guard<std::mutex> lock(fileWatcherMutex);
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
  std::lock_guard<std::mutex> lock(fileWatcherMutex);
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
  if (!query.empty() &&
      MatchesAssetFilter(node.name, node.isDirectory, query, false)) {
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
  static std::string createdFilePath;
  static char renameBuffer[256] = "";

  if (ImGui::BeginMenu(" Create New File")) {
    if (ImGui::MenuItem("Empty File")) {
      std::string defaultName = "NewFile.txt";
      createdFilePath = targetFolder + "/" + defaultName;
      std::ofstream ofs(createdFilePath);
      ofs.close();
      strncpy(renameBuffer, "NewFile", sizeof(renameBuffer));
      isCreating = true;
    }

    if (ImGui::BeginMenu("Scripts")) {
      if (ImGui::MenuItem("C++ Script")) {
        RequestCreateScript(targetFolder);
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

      std::string newBaseName = renameBuffer[0] ? renameBuffer : "NewFile";
      {
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
  std::string ext = ToLowerAsciiStr(fs::path(node.name).extension().string());
  bool isTextOrCode =
      (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c" ||
       ext == ".hpp" || ext == ".h" || ext == ".hh" || ext == ".inl" ||
       ext == ".glsl" || ext == ".shader" || ext == ".frag" || ext == ".vert" ||
       ext == ".comp" || ext == ".hlsl" || ext == ".cl" || ext == ".py" ||
       ext == ".lua" || ext == ".sh" || ext == ".js" || ext == ".ts" ||
       ext == ".nix" || ext == ".txt" || ext == ".json" || ext == ".toml" ||
       ext == ".yaml" || ext == ".yml" || ext == ".ini" || ext == ".xml" ||
       ext == ".cmake" || ext == ".md" || ext == ".csv" || ext == ".log");
  bool isMediaOrExternal =
      (ext == ".mp4" || ext == ".mkv" || ext == ".webm" || ext == ".avi" ||
       ext == ".mov" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
       ext == ".gif" || ext == ".bmp" || ext == ".webp" || ext == ".mp3" ||
       ext == ".wav" || ext == ".ogg" || ext == ".flac" || ext == ".m4a" ||
       ext == ".blend");

  if (isTextOrCode) {
    OpenFile(node.fullPath);
  } else if (isMediaOrExternal) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", node.fullPath.c_str(), NULL, NULL, SW_SHOW);
#else
    std::string cmd = "xdg-open \"" + node.fullPath + "\" &";
    int rc = system(cmd.c_str());
    (void)rc;
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
  const int thumbW = 128;
  const int thumbH = 128;
  IconInfo thumbnailInfo{(ImTextureID)0, thumbW, thumbH};

  AVFormatContext *formatContext = nullptr;
  if (avformat_open_input(&formatContext, videoPath.c_str(), nullptr,
                          nullptr) != 0) {
    return LoadCachedTexture("assets/icons/svg/ext_video.svg");
  }

  if (avformat_find_stream_info(formatContext, nullptr) < 0) {
    avformat_close_input(&formatContext);
    return LoadCachedTexture("assets/icons/svg/ext_video.svg");
  }

  int videoStream = -1;
  for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
    if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      videoStream = i;
      break;
    }
  }

  if (videoStream == -1) {
    avformat_close_input(&formatContext);
    return LoadCachedTexture("assets/icons/svg/ext_video.svg");
  }

  AVCodecParameters *codecParams =
      formatContext->streams[videoStream]->codecpar;
  const AVCodec *codec = avcodec_find_decoder(codecParams->codec_id);
  if (!codec || codecParams->width <= 0 || codecParams->height <= 0) {
    avformat_close_input(&formatContext);
    return LoadCachedTexture("assets/icons/svg/ext_video.svg");
  }
  AVCodecContext *codecContext = avcodec_alloc_context3(codec);
  avcodec_parameters_to_context(codecContext, codecParams);
  if (avcodec_open2(codecContext, codec, nullptr) < 0) {
    avcodec_free_context(&codecContext);
    avformat_close_input(&formatContext);
    return LoadCachedTexture("assets/icons/svg/ext_video.svg");
  }

  int64_t duration = formatContext->duration > 0
                         ? formatContext->duration / AV_TIME_BASE
                         : 0;
  int64_t half = duration / 2;
  if (half > 0) {
    av_seek_frame(formatContext, -1, half * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
  }

  AVFrame *frame = av_frame_alloc();
  AVFrame *rgbFrame = av_frame_alloc();
  AVPacket *packet = av_packet_alloc();

  int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, thumbW, thumbH, 1);
  uint8_t *buffer = (uint8_t *)av_malloc(numBytes);
  av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer,
                       AV_PIX_FMT_RGB24, thumbW, thumbH, 1);

  SwsContext *swsContext =
      sws_getContext(codecContext->width, codecContext->height,
                     codecContext->pix_fmt, thumbW, thumbH,
                     AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);

  int framesTried = 0;
  float maxVariance = -1.0f;
  std::vector<uint8_t> bestFrameData(numBytes, 0);
  bool gotAnyFrame = false;

  const int maxFramesToTry = 20;
  while (swsContext && av_read_frame(formatContext, packet) >= 0 &&
         framesTried < maxFramesToTry) {
    if (packet->stream_index == videoStream) {
      if (avcodec_send_packet(codecContext, packet) >= 0) {
        while (avcodec_receive_frame(codecContext, frame) >= 0) {
          sws_scale(swsContext, frame->data, frame->linesize, 0,
                    codecContext->height, rgbFrame->data, rgbFrame->linesize);

          float variance =
              CalculateColorVariance(rgbFrame->data[0], thumbW, thumbH);
          if (!gotAnyFrame || variance > maxVariance) {
            maxVariance = variance;
            memcpy(bestFrameData.data(), rgbFrame->data[0], numBytes);
            gotAnyFrame = true;
          }
          framesTried++;
        }
      }
    }
    av_packet_unref(packet);
  }

  if (gotAnyFrame && svgIcons_) {
    std::vector<unsigned char> rgba(static_cast<size_t>(thumbW) *
                                    static_cast<size_t>(thumbH) * 4);
    for (int y = 0; y < thumbH; ++y) {
      const uint8_t *srcRow = bestFrameData.data() + y * rgbFrame->linesize[0];
      unsigned char *dstRow = rgba.data() + static_cast<size_t>(y) * thumbW * 4;
      for (int x = 0; x < thumbW; ++x) {
        dstRow[x * 4 + 0] = srcRow[x * 3 + 0];
        dstRow[x * 4 + 1] = srcRow[x * 3 + 1];
        dstRow[x * 4 + 2] = srcRow[x * 3 + 2];
        dstRow[x * 4 + 3] = 255;
      }
    }
    thumbnailInfo.textureId =
        svgIcons_->UploadRgbaPixels(rgba.data(), thumbW, thumbH);
    thumbnailInfo.width = codecContext->width;
    thumbnailInfo.height = codecContext->height;
  }

  av_free(buffer);
  av_frame_free(&rgbFrame);
  av_frame_free(&frame);
  av_packet_free(&packet);
  if (swsContext)
    sws_freeContext(swsContext);
  avcodec_free_context(&codecContext);
  avformat_close_input(&formatContext);

  if (!thumbnailInfo.textureId) {
    return LoadCachedTexture("assets/icons/svg/ext_video.svg");
  }
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
