#include "test_glsl.hpp"

#include <iostream>
#include <vector>
#include <windows.h>
#include <GLFW/glfw3.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_glfw.h>
using namespace std;
// Contoh shader
const char* vertexSrc = R"(
    #version 330 core
    layout (location = 0) in vec2 aPos;
    void main() {
        gl_Position = vec4(aPos, 0.0, 1.0);
    }
)";

const char* fragmentSrc = R"(
    #version 330 core
    out vec4 FragColor;
    void main() {
        vec3 Color = vec3(0.2, 0.9, 0.9);
        FragColor = vec4(Color, 0.1);
    }
)";

TestGLSL::TestGLSL()
{
    // Compile Vertex Shader
    vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSrc, NULL);
    glCompileShader(vertexShader);

    // Compile Fragment Shader
    fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSrc, NULL);
    glCompileShader(fragmentShader);

    // Link Shader Program
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);
    cout << shaderProgram << endl;
    // Cleanup shader objects
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Setup rectangle (fullscreen quad)
    float vertices[] = {
        -1.0f, -1.0f, 
         1.0f, -1.0f, 
        -1.0f,  1.0f,
         1.0f,  1.0f
    };
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    // Setup framebuffer
    glGenTextures(1, &renderTex);
    glBindTexture(GL_TEXTURE_2D, renderTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTex, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "Framebuffer not complete!" << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

TestGLSL::~TestGLSL()
{
    glDeleteProgram(shaderProgram);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &renderTex);
    glDeleteFramebuffers(1, &fbo);
}

void TestGLSL::draw()
{
    // Render to framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, 512, 512);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(shaderProgram);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Show in ImGui
    ImGui::Begin("GLSL Test Output");
    ImGui::Text("Rendered Shader:");
    ImGui::Image((ImTextureID)(intptr_t)renderTex, ImVec2(256, 256));
    ImGui::End();
}

int main(int argc, char *argv[]) {
    // Init GLFW
    if (!glfwInit()) return -1;
    GLFWwindow* window = glfwCreateWindow(800, 600, "TestGLSL App", NULL, NULL);
    if (!window) return -1;
    glfwMakeContextCurrent(window);
    gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

    // ImGui Init

    // SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    // SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 4);
    // SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetSwapInterval(1);  // VSync
    cout << "GL Version: " << glGetString(GL_VERSION) << endl;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    TestGLSL shaderTest;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        shaderTest.draw(); // ← tampilkan

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0, 0, 0, 1);
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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return main(__argc, __argv);
}