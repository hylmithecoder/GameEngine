#ifndef TEST_GLSL_HPP
#define TEST_GLSL_HPP

#include <glad/glad.h>
#include <imgui.h>
#include <SDL.h>
#include <iostream>
using namespace std;

// Shader untuk grid dengan zoom dan pan
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
    
    uniform vec2 uViewport;    // Viewport size
    uniform vec2 uPan;         // Pan offset
    uniform float uZoom;       // Zoom level
    uniform vec3 uGridColor;   // Grid line color
    uniform vec3 uBgColor;     // Background color
    uniform float uGridSize;   // Grid cell size
    
    void main() {
        // Ukuran grid dalam pixel (dipengaruhi zoom)
        float gridSize = uGridSize * uZoom;
        
        // Hitung koordinat yang sudah dipan dan di-zoom
        vec2 coord = (gl_FragCoord.xy - uViewport * 0.5) / uZoom + uPan;
        
        // Hitung garis horizontal & vertikal
        float line = step(0.98, abs(fract(coord.x / uGridSize) - 0.5) * 2.0) +
                    step(0.98, abs(fract(coord.y / uGridSize) - 0.5) * 2.0);
                    
        // Garis axis X dan Y lebih tebal
        float axis = 0.0;
        if (abs(coord.x) < 1.0 || abs(coord.y) < 1.0) {
            axis = 0.6;
        }
                    
        // Warna final - campuran background dan grid
        vec3 finalColor = mix(uBgColor, uGridColor, max(line, axis));
        
        FragColor = vec4(finalColor, 1.0);
    }
)";

class ViewportPrototype {
private:
    GLuint vertexShader, fragmentShader, shaderProgram;
    GLuint vao, vbo;
    GLuint fbo, renderTex;
    
    // Viewport attributes
    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);
    ImVec2 lastMousePos = ImVec2(0.0f, 0.0f);
    bool isDragging = false;
    
    // Grid settings
    float gridSize = 32.0f;
    ImVec4 gridColor = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
    ImVec4 bgColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    
    // Viewport dimensions
    int viewportWidth = 512;
    int viewportHeight = 512;
    
public:
    ViewportPrototype() {
        // Compile Vertex Shader
        vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexSrc, NULL);
        glCompileShader(vertexShader);
        checkShaderCompilation(vertexShader, "Vertex Shader");

        // Compile Fragment Shader
        fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentSrc, NULL);
        glCompileShader(fragmentShader);
        checkShaderCompilation(fragmentShader, "Fragment Shader");

        // Link Shader Program
        shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        checkProgramLinking(shaderProgram);
        
        // Cleanup shader objects
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Setup rectangle (fullscreen quad)
        float vertices[] = {
            -1.0f, -1.0f, // bottom left
             1.0f, -1.0f, // bottom right
            -1.0f,  1.0f, // top left
             1.0f,  1.0f  // top right
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportWidth, viewportHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderTex, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            cerr << "Framebuffer not complete!" << endl;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    ~ViewportPrototype() {
        glDeleteProgram(shaderProgram);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
        glDeleteTextures(1, &renderTex);
        glDeleteFramebuffers(1, &fbo);
    }

    void checkShaderCompilation(GLuint shader, const char* name) {
        GLint success;
        GLchar infoLog[1024];
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            cerr << "ERROR::SHADER::" << name << "::COMPILATION_FAILED\n" << infoLog << endl;
        }
    }

    void checkProgramLinking(GLuint program) {
        GLint success;
        GLchar infoLog[1024];
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        
        if (!success) {
            glGetProgramInfoLog(program, 1024, NULL, infoLog);
            cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << endl;
        }
    }

    void draw() {
        // Render grid to framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, viewportWidth, viewportHeight);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(shaderProgram);
        
        // Set uniform values
        glUniform2f(glGetUniformLocation(shaderProgram, "uViewport"), viewportWidth, viewportHeight);
        glUniform2f(glGetUniformLocation(shaderProgram, "uPan"), pan.x, pan.y);
        glUniform1f(glGetUniformLocation(shaderProgram, "uZoom"), zoom);
        glUniform3f(glGetUniformLocation(shaderProgram, "uGridColor"), gridColor.x, gridColor.y, gridColor.z);
        glUniform3f(glGetUniformLocation(shaderProgram, "uBgColor"), bgColor.x, bgColor.y, bgColor.z);
        glUniform1f(glGetUniformLocation(shaderProgram, "uGridSize"), gridSize);
        
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Show in ImGui
        ImGui::Begin("GLSL Viewport Prototype");
        
        // Controls
        if (ImGui::CollapsingHeader("Viewport Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Zoom", &zoom, 0.1f, 10.0f);
            ImGui::DragFloat2("Pan", &pan.x, 1.0f);
            ImGui::SliderFloat("Grid Size", &gridSize, 4.0f, 64.0f);
            ImGui::ColorEdit3("Grid Color", &gridColor.x);
            ImGui::ColorEdit3("Background Color", &bgColor.x);
            string currentShaderProgram = to_string(shaderProgram);
            ImGui::Text("Current Shader: %s", currentShaderProgram.c_str());
            if (ImGui::Button("Reset View")) {
                zoom = 1.0f;
                pan = ImVec2(0.0f, 0.0f);
            }
        }
        
        // Viewport area
        ImVec2 viewportPos = ImGui::GetCursorScreenPos();
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        
        // Ensure square aspect ratio if needed
        // viewportSize.x = viewportSize.y = min(viewportSize.x, viewportSize.y);
        
        // Update viewport dimensions if window resized
        if (viewportWidth != static_cast<int>(viewportSize.x) || 
            viewportHeight != static_cast<int>(viewportSize.y)) {
            
            viewportWidth = static_cast<int>(viewportSize.x);
            viewportHeight = static_cast<int>(viewportSize.y);
            
            // Resize framebuffer texture
            glBindTexture(GL_TEXTURE_2D, renderTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewportWidth, viewportHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        
        // Draw the viewport texture
        ImGui::Image((ImTextureID)(intptr_t)renderTex, viewportSize);
        
        // Handle mouse interactions within viewport
        if (ImGui::IsItemHovered()) {
            // Mouse wheel for zoom
            if (ImGui::GetIO().MouseWheel != 0.0f) {
                // Get mouse position relative to viewport
                ImVec2 mousePos = ImGui::GetIO().MousePos;
                ImVec2 mousePosInViewport = ImVec2(mousePos.x - viewportPos.x, mousePos.y - viewportPos.y);
                
                // Calculate world position before zoom
                ImVec2 worldPosBeforeZoom = ImVec2(
                    (mousePosInViewport.x - viewportSize.x * 0.5f) / zoom + pan.x,
                    (mousePosInViewport.y - viewportSize.y * 0.5f) / zoom + pan.y
                );
                
                // Apply zoom (mouse wheel)
                float zoomFactor = 1.1f;
                if (ImGui::GetIO().MouseWheel > 0.0f)
                    zoom *= zoomFactor;
                else
                    zoom /= zoomFactor;
                    
                // Clamp zoom
                zoom = max(0.1f, min(zoom, 10.0f));
                
                // Calculate world position after zoom
                ImVec2 worldPosAfterZoom = ImVec2(
                    (mousePosInViewport.x - viewportSize.x * 0.5f) / zoom + pan.x,
                    (mousePosInViewport.y - viewportSize.y * 0.5f) / zoom + pan.y
                );
                
                // Adjust pan to keep mouse over same world position
                pan.x += (worldPosBeforeZoom.x - worldPosAfterZoom.x);
                pan.y += (worldPosBeforeZoom.y - worldPosAfterZoom.y);
            }
            
            // Middle mouse button drag for panning
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                
                if (!isDragging) {
                    isDragging = true;
                    lastMousePos = mousePos;
                } else {
                    // Calculate delta and apply to pan (adjusted for zoom)
                    pan.x -= (mousePos.x - lastMousePos.x) / zoom;
                    pan.y -= (mousePos.y - lastMousePos.y) / zoom;
                    lastMousePos = mousePos;
                }
            } else {
                isDragging = false;
            }
            
            // Display coordinates under cursor
            ImVec2 mousePos = ImGui::GetMousePos();
            ImVec2 mousePosInViewport = ImVec2(mousePos.x - viewportPos.x, mousePos.y - viewportPos.y);
            ImVec2 worldPos = ImVec2(
                (mousePosInViewport.x - viewportSize.x * 0.5f) / zoom + pan.x,
                (mousePosInViewport.y - viewportSize.y * 0.5f) / zoom + pan.y
            );
            
            ImGui::SetTooltip("Pos: (%.1f, %.1f)", worldPos.x, worldPos.y);
        }
        
        // Status bar
        ImGui::Text("Zoom: %.2fx | Pan: (%.1f, %.1f) | Grid Size: %.1f", 
                    zoom, pan.x, pan.y, gridSize);
        
        ImGui::End();
    }
};

#endif
