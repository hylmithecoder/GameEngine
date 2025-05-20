#include <SceneRenderer2D.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <Debugger.hpp>

using namespace std;

SceneRenderer2D::SceneRenderer2D(int width, int height)
    : width(width), height(height), cameraZoom(1.0f), gridVisible(false), gridSize(10.0f), snapToGrid(false), cameraPosition(0.0f, 0.0f) {
    cout << "Creating SceneRenderer2D: " << width << "x" << height << endl;
    Init();
}

SceneRenderer2D::~SceneRenderer2D() {
    DestroyFramebuffer();
    
    // Clean up shader and VAO/VBO resources
    if (shaderProgram) glDeleteProgram(shaderProgram);
    if (gizmoShaderProgram) glDeleteProgram(gizmoShaderProgram);
    if (gridShaderProgram) glDeleteProgram(gridShaderProgram);
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO) glDeleteBuffers(1, &quadVBO);
    if (quadEBO) glDeleteBuffers(1, &quadEBO);
    viewPort.Clean();
}

void SceneRenderer2D::DestroyFramebuffer() {
    cout << "DestroyFramebuffer" << endl;
    if (rbo) {
        glDeleteRenderbuffers(1, &rbo);
        rbo = 0;
    }
    
    if (textureID) {
        glDeleteTextures(1, &textureID);
        textureID = 0;
    }
    
    if (framebuffer) {
        glDeleteFramebuffers(1, &framebuffer);
        framebuffer = 0;
    }

    if (m_GridVAO) {
        glDeleteVertexArrays(1, &m_GridVAO);
        m_GridVAO = 0;
    }
    
    if (m_GridVBO) {
        glDeleteBuffers(1, &m_GridVBO);
        m_GridVBO = 0;
    }
}

void SceneRenderer2D::InitShaders() {
    // Create shader program first
    shaderProgram = CreateShaderProgram("assets/shaders/sprite.vert", "assets/shaders/sprite.frag");
    if (shaderProgram == 0) {
        cerr << "Failed to create shader program!" << endl;
        return;
    }
    
    gizmoShaderProgram = CreateShaderProgram("assets/shaders/gizmo.vert", "assets/shaders/gizmo.frag");
    if (gizmoShaderProgram == 0) {
        cerr << "Failed to create gizmo shader program" << endl;
        return;
    }
    
    gridShaderProgram = CreateShaderProgram("assets/shaders/grid.vert", "assets/shaders/grid.frag");
    if (gridShaderProgram == 0) {
        cerr << "Failed to create grid shader program" << endl;
        return;
    }
    
    checkProgramLinking(shaderProgram);
    checkProgramLinking(gizmoShaderProgram);
    checkProgramLinking(gridShaderProgram);

    Debug::Logger::Log("[InitShaders] Shader Program: " + std::to_string(shaderProgram) + " Gizmo Shader Program: " + std::to_string(gizmoShaderProgram) + " Grid Shader Program: " + std::to_string(gridShaderProgram), Debug::LogLevel::SUCCESS);
}

void SceneRenderer2D::checkProgramLinking(GLuint program) {
    GLint success;
    GLchar infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
        
    if (!success) {
        glGetProgramInfoLog(program, 1024, NULL, infoLog);
        std::cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
    Debug::Logger::Log("Program " + std::to_string(program) + " linked successfully", Debug::LogLevel::SUCCESS);
}

void SceneRenderer2D::Init() {
    // viewPort.getGridShaderProgram();
    std::cout << "[DEBUG] TextureManager instance created, textureCache size: " << textureManager.textureCache.size() << std::endl;

    cout << "Initializing Scene Renderer" << endl;
    
    InitShaders();
    cout << "Shader initialization complete ✅" << endl;

    // Initialize vertex data for rendering quads
    InitQuad();
    cout << "Quad initialization complete ✅" << endl;
    
    // Initialize grid buffers
    InitGridBuffers();
    cout << "Grid buffers initialization complete ✅" << endl;   
    
    // Create framebuffer last
    CreateFramebuffer();
    cout << framebuffer << ", " << textureID << ", " << rbo << endl;
    cout << "Framebuffer creation complete ✅" << endl;

    if (!checkShaderUniforms(gridShaderProgram))
    {
        Debug::Logger::Log("[Check Again] Grid Shader Program: " + std::to_string(gridShaderProgram), Debug::LogLevel::WARNING);
    }
    Debug::Logger::Log("[Check Again] Grid Shader Program: " + std::to_string(gridShaderProgram), Debug::LogLevel::WARNING);
    // viewPort.getGridShaderProgram(gridShaderProgram);
}

void SceneRenderer2D::SetViewportSize(int newWidth, int newHeight) {
    if (width == newWidth && height == newHeight) return;
    
    width = newWidth;
    height = newHeight;
    
    // Recreate framebuffer with new dimensions
    DestroyFramebuffer();
    CreateFramebuffer();
}

void SceneRenderer2D::CreateFramebuffer() {
    // Generate and bind a framebuffer object
    

    // Create a texture attachment for the framebuffer
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);

    // Create a renderbuffer object for depth and stencil attachments
    glGenFramebuffers(1, &rbo);
    glBindFramebuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    // Check if framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        cerr << "Framebuffer is not complete!" << endl;
    } else {
        cout << "Framebuffer successfully created with dimensions: " << width << "x" << height << endl;
    }

    // Unbind the framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// This Method Is Loop Update For Render Scene To Texture And Use in HandleChilWindow.cpp
void SceneRenderer2D::LastGridShaderProgram()
{
    Debug::Logger::Log("Last Grid Shader Program: " + std::to_string(gridShaderProgram), Debug::LogLevel::SUCCESS);
    GLuint currentGridProgram = gridShaderProgram;
    cout << to_string(currentGridProgram) << endl;
    // viewPort.getGridShaderProgram(gridShaderProgram);
}
void SceneRenderer2D::RenderSceneToTexture(const Scene& scene) {
    // Bind our framebuffer
    // glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Set the viewport to match our framebuffer size
    // glViewport(0, 0, width, height);
    
    // Clear the framebuffer
    // glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    // glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Enable alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Render each game object in the scene
    glm::mat4 projection = glm::ortho(
        -width * 0.5f / cameraZoom, width * 0.5f / cameraZoom,
        -height * 0.5f / cameraZoom, height * 0.5f / cameraZoom,
        -1.0f, 1.0f
    );
    
    glm::mat4 view = glm::translate(glm::mat4(1.0f), 
                                  glm::vec3(-cameraPosition.x, -cameraPosition.y, 0.0f));
    
    // Draw grid if enabled
    DrawGrid(projection, view);
    
    // Draw all objects in the scene
    for (const auto& obj : scene.objects) {
        GLuint tex = textureManager.LoadTexture(obj.spritePath);
        if (tex != 0) {
            DrawSprite(tex, obj.x, obj.y, obj.width, obj.height, 
                      obj.rotation, obj.scaleX, obj.scaleY);
        }
    }
    
    // Draw selection gizmo for selected object
    if (selectedObject != nullptr) {
        DrawSelectionGizmo(*selectedObject);
    }

    Debug::Logger::Log("[Debug] camZoom: " + std::to_string(cameraZoom)
          + " camPos: (" + std::to_string(cameraPosition.x) + ", " + std::to_string(cameraPosition.y) + ")"
          + " width: " + std::to_string(width) + " height: " + std::to_string(height)
          + " gridSize: " + std::to_string(gridSize)
          + " VAO: " + std::to_string(m_GridVAO) + " VBO: " + std::to_string(m_GridVBO)
          + "\nMax Grid Lines: " + std::to_string(m_MaxGridLines)
          + " Current Shader: " + std::to_string(shaderProgram)
          + " Current Gizmo Shader: " + std::to_string(gizmoShaderProgram)
          + " Current Grid Shader: " + std::to_string(gridShaderProgram), Debug::LogLevel::INFO);

    
    // Disable blending when done
    glDisable(GL_BLEND);
    
    // Unbind the framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneRenderer2D::DrawGrid(const glm::mat4& projection, const glm::mat4& view) {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        glUseProgram(gridShaderProgram);

        string info = to_string(gridShaderProgram);
        string info2 = to_string(textureID);
        ImGui::Text("Grid Shader Program: %s | And Texture ID: %s", info.c_str(), info2.c_str());

        if (!checkShaderUniforms(gridShaderProgram)) {
            Debug::Logger::Log("Missing required uniforms in grid shader!", Debug::LogLevel::CRASH);
            return;
        }
        
        // Set uniform values
        glUniform2f(glGetUniformLocation(gridShaderProgram, "uViewport"), width, height);
        glUniform2f(glGetUniformLocation(gridShaderProgram, "uPan"), pan.x, pan.y);
        glUniform1f(glGetUniformLocation(gridShaderProgram, "uZoom"), zoom);
        glUniform3f(glGetUniformLocation(gridShaderProgram, "uGridColor"), gridColor.x, gridColor.y, gridColor.z);
        glUniform3f(glGetUniformLocation(gridShaderProgram, "uBgColor"), bgColor.x, bgColor.y, bgColor.z);
        glUniform1f(glGetUniformLocation(gridShaderProgram, "uGridSize"), gridSize);
        
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
   
        // Controls
        if (ImGui::CollapsingHeader("Viewport Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Zoom", &zoom, 0.1f, 10.0f);
            ImGui::DragFloat2("Pan", &pan.x, 1.0f);
            ImGui::SliderFloat("Grid Size", &gridSize, 4.0f, 64.0f);
            ImGui::ColorEdit3("Grid Color", &gridColor.x);
            ImGui::ColorEdit3("Background Color", &bgColor.x);
            std::string currentShaderProgram = to_string(shaderProgram);
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
        viewportSize.x = viewportSize.y = min(viewportSize.x, viewportSize.y);
        
        // Update viewport dimensions if window resized
        if (width != static_cast<int>(viewportSize.x) || 
            height != static_cast<int>(viewportSize.y)) {
            
            width = static_cast<int>(viewportSize.x);
            height = static_cast<int>(viewportSize.y);
            
            // Resize framebuffer texture
            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
        
        // Draw the viewport texture
        ImGui::Image((ImTextureID)(intptr_t)textureID, viewportSize);
        
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
                zoom = std::max(0.1f, std::min(zoom, 10.0f));
                
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
}

bool SceneRenderer2D::checkShaderUniforms(GLuint program) {
    Debug::Logger::Log("Checking shader uniforms for program: " + std::to_string(program), Debug::LogLevel::INFO);
    
    bool allValid = true;

    // Store uniform names and their expected types
    const std::vector<std::pair<const char*, const char*>> uniforms = {
        {"uViewport", "vec2"},
        {"uPan", "vec2"},
        {"uZoom", "float"},
        {"uGridColor", "vec3"},
        {"uBgColor", "vec3"},
        {"uGridSize", "float"}
    };

    // Check each uniform
    for (const auto& uniform : uniforms) {
        try {
            validateUniformLocation(program, uniform.first, uniform.second);
        } catch (const std::runtime_error& e) {
            Debug::Logger::Log(e.what(), Debug::LogLevel::CRASH);
            allValid = false;
        }
    }

    return allValid;
}

void SceneRenderer2D::validateUniformLocation(GLuint program, const char* uniformName, const char* uniformType) {
    GLint location = glGetUniformLocation(program, uniformName);
    
    if (location == -1) {
        throw std::runtime_error("Uniform '" + std::string(uniformName) + 
                               "' (" + uniformType + ") not found in shader program");
    }

    // Get uniform info
    GLint size;
    GLenum type;
    GLchar actualName[128];
    glGetActiveUniform(program, location, sizeof(actualName), nullptr, &size, &type, actualName);

    std::string typeStr;
    switch (type) {
        case GL_FLOAT: typeStr = "float"; break;
        case GL_FLOAT_VEC2: typeStr = "vec2"; break;
        case GL_FLOAT_VEC3: typeStr = "vec3"; break;
        case GL_FLOAT_VEC4: typeStr = "vec4"; break;
        default: typeStr = "unknown";
    }

    Debug::Logger::Log("Found uniform '" + std::string(uniformName) + 
                      "' at location " + std::to_string(location) + 
                      " (type: " + typeStr + ")", Debug::LogLevel::SUCCESS);
}

void SceneRenderer2D::DrawSelectionGizmo(const GameObject& obj) {
    cout << "Draw Gizmo Shader" << endl;
    // string info = "Name: " + obj.name + " Position x: " + std::to_string(obj.x) + " Position y: " + std::to_string(obj.y) + " Sprite Path: " + obj.spritePath;
    // cout << info << endl;

    glUseProgram(gizmoShaderProgram);
    
    // Set color for selection outline (yellow)
    glUniform4f(glGetUniformLocation(gizmoShaderProgram, "u_Color"), 1.0f, 1.0f, 0.0f, 1.0f);
    
    // Set projection and view matrices
    glm::mat4 projection = glm::ortho(
        -width * 0.5f / cameraZoom, width * 0.5f / cameraZoom,
        -height * 0.5f / cameraZoom, height * 0.5f / cameraZoom,
        -1.0f, 1.0f
    );
    
    glm::mat4 view = glm::translate(glm::mat4(1.0f), 
        glm::vec3(-cameraPosition.x, -cameraPosition.y, 0.0f));

    glUniformMatrix4fv(glGetUniformLocation(gizmoShaderProgram, "u_Projection"), 
        1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(gizmoShaderProgram, "u_View"), 
        1, GL_FALSE, glm::value_ptr(view));
    
    // Calculate corners with margin
    float margin = 2.0f / cameraZoom;
    float x1 = obj.x - margin;
    float y1 = obj.y - margin;
    float x2 = obj.x + obj.width * obj.scaleX + margin;
    float y2 = obj.y + obj.height * obj.scaleY + margin;
    
    // Create rectangle vertices (5 points to close the loop)
    float vertices[] = {
        x1, y1,  // Bottom left
        x2, y1,  // Bottom right
        x2, y2,  // Top right
        x1, y2,  // Top left
        x1, y1   // Back to start to close the loop
    };
    
    // Create and bind VAO/VBO
    GLuint gizmoVAO, gizmoVBO;
    glGenVertexArrays(1, &gizmoVAO);
    glGenBuffers(1, &gizmoVBO);
    
    glBindVertexArray(gizmoVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gizmoVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Set up vertex attributes
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Enable line smoothing for better appearance
    glEnable(GL_LINE_SMOOTH);
    glLineWidth(2.0f);
    
    // Draw the selection rectangle
    glDrawArrays(GL_LINE_STRIP, 0, 5);
    
    // Draw handles for rotation/scaling if in appropriate mode
    if (currentMode == EditMode::ROTATE || currentMode == EditMode::SCALE) {
        // Calculate handle positions (corners and midpoints)
        float handlePositions[] = {
            x1, y1,      // Bottom left
            (x1+x2)/2, y1, // Bottom middle
            x2, y1,      // Bottom right
            x2, (y1+y2)/2, // Right middle
            x2, y2,      // Top right
            (x1+x2)/2, y2, // Top middle
            x1, y2,      // Top left
            x1, (y1+y2)/2  // Left middle
        };
        
        // Update buffer with handle positions
        glBufferData(GL_ARRAY_BUFFER, sizeof(handlePositions), handlePositions, GL_STATIC_DRAW);
        
        // Draw handles as points
        glPointSize(8.0f);
        glDrawArrays(GL_POINTS, 0, 8);
    }
    
    // Cleanup
    glDisable(GL_LINE_SMOOTH);
    glBindVertexArray(0);
    glDeleteVertexArrays(1, &gizmoVAO);
    glDeleteBuffers(1, &gizmoVBO);
}

void SceneRenderer2D::InitQuad() {
    cout << "Initializing quad geometry" << endl;
    
    // Vertex data for a quad with positions and texture coordinates
    float vertices[] = {
        -1.0f, -1.0f, // bottom left
        1.0f, -1.0f, // bottom right
        -1.0f,  1.0f, // top left
        1.0f,  1.0f  // top right
    };
    
    // Indices for the quad (using 2 triangles)
    unsigned int indices[] = {
        0, 1, 2,  // first triangle
        0, 2, 3   // second triangle
    };
    
    // Generate and bind VAO
    glGenBuffers(1, &quadVBO);
    glGenVertexArrays(1, &quadVAO);
    
    // Generate and bind VBO
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Generate and bind EBO
    // glGenBuffers(1, &quadEBO);
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    // Position attribute (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);    
    
    // Texture coordinate attribute (vec2)
    // glEnableVertexAttribArray(1);
    // glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    // Unbind VAO and VBO
    // glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void SceneRenderer2D::DrawSprite(GLuint textureID, float x, float y, float width, float height, 
                               float rotation, float scaleX, float scaleY) {
    glUseProgram(shaderProgram);
    
    // Calculate orthographic projection matrix (similar to Unity's 2D camera)
    glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width), 
                                      static_cast<float>(height), 0.0f, 
                                      -1.0f, 1.0f);
    
    // Create model matrix for transformations
    glm::mat4 model = glm::mat4(1.0f);
    
    // Position the sprite
    model = glm::translate(model, glm::vec3(x, y, 0.0f));
    
    // Rotate around the sprite's center
    model = glm::translate(model, glm::vec3(width * 0.5f, height * 0.5f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::translate(model, glm::vec3(-width * 0.5f, -height * 0.5f, 0.0f));
    
    // Scale the sprite
    model = glm::scale(model, glm::vec3(width * scaleX, height * scaleY, 1.0f));
    
    // Set uniforms
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_Projection"), 1, GL_FALSE, 
                       glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "u_Model"), 1, GL_FALSE, 
                       glm::value_ptr(model));
    
    // Bind texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(glGetUniformLocation(shaderProgram, "u_Texture"), 0);
    
    // Draw the quad
    glBindVertexArray(quadVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

GLuint SceneRenderer2D::GetViewportTextureID() const {
    return textureID;
}

GLuint SceneRenderer2D::LoadShaderFromFile(const std::string& path, GLenum type) {
    cout << "Loading shader from file: " << path << endl;
    
    // Open and read the shader file
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        std::cerr << "Failed to open shader file: " << path << std::endl;
        return 0;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    const char* src = source.c_str();
    Debug::Logger::Log("Shader Source: \n" + source, Debug::LogLevel::WARNING);
    // Create and compile the shader
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    
    // Check for compilation errors
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, 512, nullptr, log);
        std::cerr << "Shader compile error (" << path << "): " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

void SceneRenderer2D::InitGridBuffers() {
    glGenVertexArrays(1, &m_GridVAO);
    glGenBuffers(1, &m_GridVBO);

    glBindVertexArray(m_GridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
    glBufferData(GL_ARRAY_BUFFER, m_MaxGridLines * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    cout << "Max Grid Lines: " << m_MaxGridLines << endl;
    // Simple 2D position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

    glBindVertexArray(0);
    cout << m_GridVAO << " " << m_GridVBO << " " << m_MaxGridLines << endl;
}

GLuint SceneRenderer2D::CreateShaderProgram(const std::string& vertPath, const std::string& fragPath) {
    cout << "Creating shader program from: " << vertPath << " and " << fragPath << endl;
    
    // Load and compile the vertex and fragment shaders
    GLuint vertShader = LoadShaderFromFile(vertPath, GL_VERTEX_SHADER);
    if (vertShader == 0) return 0;
    
    GLuint fragShader = LoadShaderFromFile(fragPath, GL_FRAGMENT_SHADER);
    if (fragShader == 0) {
        glDeleteShader(vertShader);
        return 0;
    }
    
    // Create and link the shader program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    
    // Check for linking errors
    // GLint linked;
    // glGetProgramiv(program, GL_LINK_STATUS, &linked);
    // if (!linked) {
    //     char log[512];
    //     glGetProgramInfoLog(program, 512, nullptr, log);
    //     std::cerr << "Shader linking error: " << log << std::endl;
    //     glDeleteShader(vertShader);
    //     glDeleteShader(fragShader);
    //     glDeleteProgram(program);
    //     return 0;
    // }
    
    // Once linked, the shader objects can be deleted
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    Debug::Logger::Log("[Debug] Shader Program Successfully Created: " + std::to_string(program), Debug::LogLevel::SUCCESS);
    return program;
}

GLuint SceneRenderer2D::CreateWhiteTexture() {
    unsigned char whitePixel[] = { 255, 255, 255, 255 };
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

// Implementasi method setting edit mode
void SceneRenderer2D::SetEditMode(EditMode mode) {
    currentMode = mode;
    // Reset selected if switching to SELECT mode
    if (mode == EditMode::SELECT && selectedObject != nullptr) {
        // Optionally deselect current object
    }
}

void SceneRenderer2D::SetGridVisible(bool visible) {
    gridVisible = visible;
}

void SceneRenderer2D::SetGridSize(float size) {
    gridSize = size;
}

void SceneRenderer2D::SetSnapToGrid(bool snap) {
    snapToGrid = snap;
}

void SceneRenderer2D::ResetCamera() {
    cameraPosition = glm::vec2(0.0f, 0.0f);
    cameraZoom = 1.0f;
}

void SceneRenderer2D::SetCameraZoom(float zoom) {
    cameraZoom = std::max(0.1f, std::min(zoom, 5.0f)); // Clamp zoom between 0.1x and 5x
}

glm::vec2 SceneRenderer2D::ViewportToWorldPosition(float viewX, float viewY) const {
    // Konversi dari koordinat viewport ke koordinat world
    // Memperhitungkan zoom dan pan kamera
    float worldX = viewX / cameraZoom + cameraPosition.x;
    float worldY = viewY / cameraZoom + cameraPosition.y;
    return glm::vec2(worldX, worldY);
}

glm::vec2 SceneRenderer2D::WorldToViewportPosition(float worldX, float worldY) const {
    // Konversi dari koordinat world ke koordinat viewport
    float viewX = (worldX - cameraPosition.x) * cameraZoom;
    float viewY = (worldY - cameraPosition.y) * cameraZoom;
    return glm::vec2(viewX, viewY);
}

void SceneRenderer2D::HandleClick(float worldX, float worldY) {
    // Reset selection
    selectedObject = nullptr;
    selectedObjectIndex = -1;
    
    // Find clicked object (in reverse order to select topmost object)
    for (int i = currentScene.objects.size() - 1; i >= 0; i--) {
        GameObject& obj = currentScene.objects[i];
        
        // Check if click is within object bounds
        if (worldX >= obj.x && worldX <= obj.x + obj.width * obj.scaleX &&
            worldY >= obj.y && worldY <= obj.y + obj.height * obj.scaleY) {
            selectedObject = &obj;
            selectedObjectIndex = i;
            
            // You could potentially add a callback for selection change
            break;
        }
    }
}

void SceneRenderer2D::HandleDrag(float deltaX, float deltaY) {
    // Scale delta by zoom factor for consistent movement
    float scaledDeltaX = deltaX / cameraZoom;
    float scaledDeltaY = deltaY / cameraZoom;
    
    if (selectedObject != nullptr && currentMode == EditMode::MOVE) {
        // Move selected object
        selectedObject->x += scaledDeltaX;
        selectedObject->y += scaledDeltaY;
        
        // Snap to grid if enabled
        if (snapToGrid) {
            selectedObject->x = round(selectedObject->x / gridSize) * gridSize;
            selectedObject->y = round(selectedObject->y / gridSize) * gridSize;
        }
    }
    else if (selectedObject != nullptr && currentMode == EditMode::ROTATE) {
        // Calculate rotation based on drag distance
        // This is a simple implementation - you might want something more sophisticated
        selectedObject->rotation += scaledDeltaX * 0.5f;
        
        // Normalize rotation to 0-360 degrees
        while (selectedObject->rotation >= 360.0f) selectedObject->rotation -= 360.0f;
        while (selectedObject->rotation < 0.0f) selectedObject->rotation += 360.0f;
    }
    else if (selectedObject != nullptr && currentMode == EditMode::SCALE) {
        // Scale object based on drag
        selectedObject->scaleX = std::max(0.1f, selectedObject->scaleX + scaledDeltaX * 0.01f);
        selectedObject->scaleY = std::max(0.1f, selectedObject->scaleY + scaledDeltaY * 0.01f);
    }
    else {
        // If no object selected or in SELECT mode, pan the camera
        cameraPosition.x -= scaledDeltaX;
        cameraPosition.y -= scaledDeltaY;
    }
}

void SceneRenderer2D::HandleZoom(float delta) {
    // Adjust zoom level
    float zoomFactor = 0.1f;
    float newZoom = cameraZoom * (1.0f + delta * zoomFactor);
    cout << "Zoom Factor: " << newZoom << endl;
    // Clamp zoom to reasonable range
    cameraZoom = std::max(0.1f, std::min(newZoom, 5.0f));
}

void SceneRenderer2D::MoveSelected(float deltaX, float deltaY) {
    if (selectedObject) {
        selectedObject->x += deltaX;
        selectedObject->y += deltaY;
        cout << "X: " << selectedObject->x << ", Y: " << selectedObject->y << endl;
        // Snap to grid if enabled
        if (snapToGrid) {
            selectedObject->x = round(selectedObject->x / gridSize) * gridSize;
            selectedObject->y = round(selectedObject->y / gridSize) * gridSize;
        }
    }
}

void SceneRenderer2D::DeleteSelected() {
    if (selectedObjectIndex >= 0 && selectedObjectIndex < currentScene.objects.size()) {
        currentScene.objects.erase(currentScene.objects.begin() + selectedObjectIndex);
        selectedObject = nullptr;
        selectedObjectIndex = -1;
    }
}

bool SceneRenderer2D::HasSelectedObject() const {
    return selectedObject != nullptr;
}

void ViewPort::Init()
{
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
            std::cerr << "Framebuffer not complete!" << std::endl;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        Debug::Logger::Log("[Init] Grid Shader Program: " + std::to_string(shaderProgram), Debug::LogLevel::SUCCESS);
}

// ViewPort::~ViewPort()
// {
//     glDeleteProgram(shaderProgram);
//     glDeleteBuffers(1, &vbo);
//     glDeleteVertexArrays(1, &vao);
//     glDeleteTextures(1, &renderTex);
//     glDeleteFramebuffers(1, &fbo);
// }

void ViewPort::checkShaderCompilation(GLuint shader, const char* name) {
    GLint success;
    GLchar infoLog[1024];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        
    if (!success) {
        glGetShaderInfoLog(shader, 1024, NULL, infoLog);
        std::cerr << "ERROR::SHADER::" << name << "::COMPILATION_FAILED\n" << infoLog << std::endl;
    }
}

void ViewPort::checkProgramLinking(GLuint program) {
    GLint success;
    GLchar infoLog[1024];
    glGetProgramiv(program, GL_LINK_STATUS, &success);
        
    if (!success) {
        glGetProgramInfoLog(program, 1024, NULL, infoLog);
        std::cerr << "ERROR::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }
}

void ViewPort::Clean()
{
    glDeleteProgram(shaderProgram);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteTextures(1, &renderTex);
    glDeleteFramebuffers(1, &fbo);
}

void ViewPort::drawGrid() {
    Debug::Logger::Log("Grid Shader Program: " + to_string(shaderProgram), Debug::LogLevel::WARNING);
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
        // ImGui::Begin("GLSL Viewport Prototype");
        
        // Controls
        if (ImGui::CollapsingHeader("Viewport Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Zoom", &zoom, 0.1f, 10.0f);
            ImGui::DragFloat2("Pan", &pan.x, 1.0f);
            ImGui::SliderFloat("Grid Size", &gridSize, 4.0f, 64.0f);
            ImGui::ColorEdit3("Grid Color", &gridColor.x);
            ImGui::ColorEdit3("Background Color", &bgColor.x);
            
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
                zoom = std::max(0.1f, std::min(zoom, 10.0f));
                
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
        
        // ImGui::End();
    }

void ViewPort::getGridShaderProgram(GLuint& program)
{
    Debug::Logger::Log("Current Grid Program: "+to_string(program), Debug::LogLevel::SUCCESS);
}