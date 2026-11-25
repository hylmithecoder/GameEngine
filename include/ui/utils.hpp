#pragma once
#include <string>
#include <nfd.hpp>
#include <vector>
using namespace std;
using namespace NFD;

namespace UI {
    class Utils {
        public:
            string OpenFile(nfdfilteritem_t* listItem, int count);
            string SaveFile(nfdfilteritem_t* listItem, int count);
            string OpenFolder();
            vector<string> OpenFiles(nfdfilteritem_t* listItem, int count);
            vector<string> OpenFolders();
    };
}