#include "../../../include/ui/utils.hpp"
#include <string>
#include <vector>

using namespace UI;
using namespace std;

string Utils::OpenFile(nfdfilteritem_t* listItem, int count) {
    NFD::Guard guard;
    NFD::UniquePath outPath;
    const nfdfiltersize_t filterCount =
        (listItem && count > 0) ? static_cast<nfdfiltersize_t>(count) : 0;
    const nfdresult_t res =
        NFD::OpenDialog(outPath, filterCount ? listItem : nullptr, filterCount, nullptr);
    if (res == NFD_OKAY && outPath.get() != nullptr) {
        return string(outPath.get());
    }
    if (res == NFD_CANCEL) {
        return "User pressed cancel.";
    }
    const char* err = NFD::GetError();
    return string("Error: ") + (err ? err : "File dialog failed");
}

string Utils::SaveFile(nfdfilteritem_t* listItem, int count) {
    NFD::Guard guard;
    NFD::UniquePath outPath;
    const nfdfiltersize_t filterCount =
        (listItem && count > 0) ? static_cast<nfdfiltersize_t>(count) : 0;
    const nfdresult_t res =
        NFD::SaveDialog(outPath, filterCount ? listItem : nullptr, filterCount, nullptr, nullptr);
    if (res == NFD_OKAY && outPath.get() != nullptr) {
        return string(outPath.get());
    }
    if (res == NFD_CANCEL) {
        return "User pressed cancel.";
    }
    const char* err = NFD::GetError();
    return string("Error: ") + (err ? err : "Save dialog failed");
}

string Utils::OpenFolder() {
    NFD::Guard guard;
    NFD::UniquePath outPath;
    const nfdresult_t res = NFD::PickFolder(outPath, nullptr);
    if (res == NFD_OKAY && outPath.get() != nullptr) {
        return string(outPath.get());
    }
    if (res == NFD_CANCEL) {
        return "User pressed cancel.";
    }
    const char* err = NFD::GetError();
    return string("Error: ") + (err ? err : "Folder dialog failed");
}

vector<string> Utils::OpenFiles(nfdfilteritem_t* listItem, int count) {
    vector<string> results;
    NFD::Guard guard;
    NFD::UniquePathSet outPaths;
    const nfdfiltersize_t filterCount =
        (listItem && count > 0) ? static_cast<nfdfiltersize_t>(count) : 0;
    const nfdresult_t res = NFD::OpenDialogMultiple(
        outPaths, filterCount ? listItem : nullptr, filterCount, nullptr);
    if (res == NFD_OKAY) {
        nfdpathsetsize_t numPaths = 0;
        if (NFD::PathSet::Count(outPaths, numPaths) == NFD_OKAY) {
            for (nfdpathsetsize_t i = 0; i < numPaths; ++i) {
                NFD::UniquePathSetPath path;
                if (NFD::PathSet::GetPath(outPaths, i, path) == NFD_OKAY &&
                    path.get() != nullptr) {
                    results.emplace_back(path.get());
                }
            }
        }
    } else if (res == NFD_CANCEL) {
        results.emplace_back("User pressed cancel.");
    } else {
        const char* err = NFD::GetError();
        results.push_back(string("Error: ") + (err ? err : "Open files dialog failed"));
    }
    return results;
}

vector<string> Utils::OpenFolders() {
    vector<string> results;
    NFD::Guard guard;
    NFD::UniquePathSet outPaths;
    const nfdresult_t res = NFD::PickFolderMultiple(outPaths, nullptr);
    if (res == NFD_OKAY) {
        nfdpathsetsize_t numPaths = 0;
        if (NFD::PathSet::Count(outPaths, numPaths) == NFD_OKAY) {
            for (nfdpathsetsize_t i = 0; i < numPaths; ++i) {
                NFD::UniquePathSetPath path;
                if (NFD::PathSet::GetPath(outPaths, i, path) == NFD_OKAY &&
                    path.get() != nullptr) {
                    results.emplace_back(path.get());
                }
            }
        }
    } else if (res == NFD_CANCEL) {
        results.emplace_back("User pressed cancel.");
    } else {
        const char* err = NFD::GetError();
        results.push_back(string("Error: ") + (err ? err : "Select folders dialog failed"));
    }
    return results;
}
