#include "EngineCore.hpp"
#include <iostream>

namespace EngineCore {
    void Initialize() {
        std::cout << "Engine Initialized" << std::endl;
    }

    void Shutdown() {
        std::cout << "Engine Shutdown" << std::endl;
    }
}
