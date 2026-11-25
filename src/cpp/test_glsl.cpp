#include "../../include/core_engine/test_glsl.hpp"

#include <iostream>
#include <vector>
#include <string>
// #include <windows.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_glfw.h>
using namespace std;

int main(int argc, char *argv[]) {
    // Init GLFW
    if (!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    
    GLFWwindow* window = glfwCreateWindow(1024, 768, "Viewport Prototype", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // ImGui Init
    cout << "GL Version: " << glGetString(GL_VERSION) << endl;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    
    // Set ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.FrameRounding = 3.0f;
    style.FramePadding = ImVec2(5, 5);
    
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ViewportPrototype viewport;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Create a full-window dockspace
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | 
                                    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus;
                                      
        ImGuiViewport* viewport_main = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport_main->WorkPos);
        ImGui::SetNextWindowSize(viewport_main->WorkSize);
        ImGui::SetNextWindowViewport(viewport_main->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        
        ImGui::Begin("DockSpace", nullptr, window_flags);
        ImGui::PopStyleVar(3);
        
        // DockSpace
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        
        ImGui::End();
        
        // Draw our prototype viewport
        viewport.draw();
        
        // Properties panel (example)
        ImGui::Begin("Properties");
        ImGui::Text("Object Properties");
        ImGui::Separator();
        static char name[128] = "Object_1";
        ImGui::InputText("Name", name, IM_ARRAYSIZE(name));
        static float position[2] = {0.0f, 0.0f};
        ImGui::DragFloat2("Position", position, 0.1f);
        static float rotation = 0.0f;
        ImGui::SliderAngle("Rotation", &rotation);
        static float scale[2] = {1.0f, 1.0f};
        ImGui::DragFloat2("Scale", scale, 0.01f, 0.01f, 10.0f);
        ImGui::Separator();
        ImGui::End();
        
        // Layer panel
        ImGui::Begin("Layers");
        ImGui::Text("Layer Control");
        ImGui::Separator();
        
        static bool layerVisible[3] = {true, true, false};
        static bool layerLocked[3] = {false, false, true};
        static const char* layerNames[3] = {"Background", "Main", "Overlay"};
        
        for (int i = 0; i < 3; i++) {
            ImGui::PushID(i);
            
            ImGui::Checkbox("##visible", &layerVisible[i]);
            ImGui::SameLine();
            
            if (layerLocked[i]) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "🔒");
            } else {
                ImGui::Text("  ");
            }
            ImGui::SameLine();
            
            if (ImGui::Selectable(layerNames[i], false, 0, ImVec2(-1, 0))) {
                // Select layer
            }
            
            if (ImGui::BeginPopupContextItem()) {
                ImGui::MenuItem("Rename");
                ImGui::MenuItem("Delete");
                ImGui::Checkbox("Locked", &layerLocked[i]);
                ImGui::EndPopup();
            }
            
            ImGui::PopID();
        }
        
        if (ImGui::Button("+ Add Layer")) {
            // Add new layer
        }
        
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
//     return main(__argc, __argv);
// }