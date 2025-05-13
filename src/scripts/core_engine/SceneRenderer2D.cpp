#include "SceneRenderer2D.hpp"
#define STBI_IMAGE_IMPLEMENTATION
#include <TextureManager.hpp>
#include <iostream>

SceneRenderer2D::SceneRenderer2D(int width, int height)
    : width(width), height(height) {
    Init();
}

SceneRenderer2D::~SceneRenderer2D() {
    DestroyFramebuffer();
}

void SceneRenderer2D::Init() {
    // CreateFramebuffer();
}

// void SceneRenderer2D::CreateFramebuffer() {
//     glGenFramebuffers(1, &framebuffer);
//     glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

//     glGenTextures(1, &viewportTexture);
//     glBindTexture(GL_TEXTURE_2D, viewportTexture);
//     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//     glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, viewportTexture, 0);

//     glGenRenderbuffers(1, &depthBuffer);
//     glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer);
//     glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
//     glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer);

//     if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
//         std::cerr << "Framebuffer not complete!" << std::endl;
//     }

//     glBindFramebuffer(GL_FRAMEBUFFER, 0);
// }

void SceneRenderer2D::DestroyFramebuffer() {
    // if (depthBuffer) glDeleteRenderbuffers(1, &depthBuffer);
    // if (viewportTexture) glDeleteTextures(1, &viewportTexture);
    // if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
}

void SceneRenderer2D::RenderSceneToTexture(const Scene& scene) {
    // glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    // glViewport(0, 0, width, height);
    // glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // for (const auto& obj : scene.objects) {
    //     GLuint tex = textureManager.LoadTexture(obj.spritePath);
    //     if (tex != 0) {
    //         DrawSprite(tex, obj.x, obj.y); // <- Render sprite
    //     }
    // }

    // glBindFramebuffer(GL_FRAMEBUFFER, 0);
}


void SceneRenderer2D::DrawSprite(GLuint textureID, float x, float y) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + 64, y);
    glTexCoord2f(1, 1); glVertex2f(x + 64, y + 64);
    glTexCoord2f(0, 1); glVertex2f(x, y + 64);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
}
