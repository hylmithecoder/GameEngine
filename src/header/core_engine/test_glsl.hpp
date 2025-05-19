#ifndef TEST_GLSL_HPP
#define TEST_GLSL_HPP

#include <glad/glad.h>
#include <imgui.h>
#include <SDL.h>

class TestGLSL
{
private:
    GLuint vertexShader;
    GLuint fragmentShader;
    GLuint shaderProgram;

    GLuint vao, vbo;
    GLuint fbo, renderTex;

public:
    TestGLSL();
    ~TestGLSL();
    void draw();
};

#endif
