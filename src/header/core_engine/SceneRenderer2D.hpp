#pragma once
#include <string>
#include "Scene.hpp"
#include "TextureManager.hpp"
// #define STB_IMAGE_IMPLEMENTATION
// #include <GL/glew.h>
#include <stb_image.h>
#include <GLES2/gl2.h>
// #include <GL/glcorearb.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_glfw.h>
// #include <imgui_impl_opengl3_loader.h>
#include <GLFW/glfw3.h>

class SceneRenderer2D {
public:
    SceneRenderer2D(int width, int height);
    ~SceneRenderer2D();

    void Init();
    void RenderSceneToTexture(const Scene& scene);
    unsigned int GetViewportTextureID() const { return viewportTexture; }
    void DrawSprite(GLuint textureID, float x, float y);
    GLuint GetRenderTexture() const {
        return textureColorbuffer; // atau nama variabel framebuffer texture kamu
    }

private:
    void CreateFramebuffer();
    void DestroyFramebuffer();

    int width;
    int height;
    unsigned int framebuffer = 0;
    unsigned int viewportTexture = 0;
    unsigned int depthBuffer = 0;
    unsigned int textureColorbuffer = 0;

    TextureManager textureManager;
};