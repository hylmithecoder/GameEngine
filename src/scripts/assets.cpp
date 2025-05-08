#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#include <GL/gl3w.h>    // atau #include <glad/glad.h>
#include <GLFW/glfw3.h>

// Untuk memuat gambar
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Struktur untuk menyimpan data tekstur
struct TextureData {
    GLuint TextureID = 0;  // ID tekstur OpenGL
    int Width = 0;         // Lebar gambar
    int Height = 0;        // Tinggi gambar
};

// Fungsi untuk memuat gambar sebagai tekstur
bool LoadTextureFromFile(const char* filename, TextureData* out_texture)
{
    // Memuat gambar dari file
    int image_width = 0;
    int image_height = 0;
    int channels = 0;
    
    // Memuat gambar dengan stb_image
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, &channels, 4);
    if (image_data == NULL)
        return false;

    // Membuat tekstur OpenGL
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup parameter filtering tekstur
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Upload data gambar ke tekstur
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    
    // Bebaskan memori gambar setelah dikonversi ke tekstur
    stbi_image_free(image_data);

    // Simpan data tekstur
    out_texture->TextureID = image_texture;
    out_texture->Width = image_width;
    out_texture->Height = image_height;

    return true;
}

int main(int, char**)
{
    // Setup window GLFW
    if (!glfwInit())
        return 1;
    
    // Setting versi OpenGL
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    
    // Buat window dengan GLFW
    GLFWwindow* window = glfwCreateWindow(1280, 720, "ImGui dengan Background Image", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    
    // Initialize OpenGL loader
    bool err = gl3wInit() != 0;
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Setup ImGui style
    ImGui::StyleColorsDark();
    
    // Setup Platform/Renderer bindings
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Memuat gambar background
    TextureData background_texture;
    bool ret = LoadTextureFromFile("background.jpg", &background_texture);
    if (!ret) {
        fprintf(stderr, "Gagal memuat gambar background!\n");
        return 1;
    }

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll dan handle events
        glfwPollEvents();

        // Start ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Buat fullscreen window untuk background
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Background", nullptr, 
            ImGuiWindowFlags_NoDecoration | 
            ImGuiWindowFlags_NoInputs | 
            ImGuiWindowFlags_NoNav | 
            ImGuiWindowFlags_NoBringToFrontOnFocus | 
            ImGuiWindowFlags_NoFocusOnAppearing | 
            ImGuiWindowFlags_NoMove);
        
        // Menggambar gambar background sebagai tekstur
        // Mendapatkan ukuran window
        ImVec2 window_size = ImGui::GetContentRegionAvail();
        
        // Menampilkan gambar yang telah dimuat sebagai background
        ImGui::Image((void*)(intptr_t)background_texture.TextureID, window_size);
        
        ImGui::End();
        ImGui::PopStyleVar(2);

        // Buat window ImGui normal di atas background
        ImGui::Begin("Window dengan Background");
        ImGui::Text("Ini adalah contoh window ImGui dengan gambar background");
        ImGui::End();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    // Hapus tekstur
    glDeleteTextures(1, &background_texture.TextureID);

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}