#include "../../include/vulkan/VulkanBase.hpp"
#include <iostream>

class GameTest : public vkhandler::VulkanBase {
public:
  void OnInit() override {
    std::cout << "Game Test Initialized!" << std::endl;
    // Setup game resources here (buffers, textures, etc.)
  }

  void OnUpdate(float deltaTime) override {
    // Game logic here
    ImGui::Begin("Game Test Controls");
    ImGui::Text("Delta Time: %.4f", deltaTime);
    ImGui::Text("FPS: %.1f", 1.0f / deltaTime);
    if (ImGui::Button("Exit Game")) {
      isRunning = false;
    }
    ImGui::End();
  }

  void OnRender(VkCommandBuffer cmd) override {
    // Draw game world here
  }

  void OnCleanup() override {
    std::cout << "Game Test Cleaning Up..." << std::endl;
  }
};

int main(int argc, char *argv[]) {
  GameTest game;
  if (game.Init("Vulkan Game Test", 1280, 720)) {
    game.Run();
  }
  return 0;
}
