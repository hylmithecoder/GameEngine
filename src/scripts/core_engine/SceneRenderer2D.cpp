#include "SceneRenderer2D.hpp"
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

    GLint success;
    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    try {
        if (!success) 
        {
            char infoLog[512];
            glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
            throw runtime_error(infoLog);
        }
        else 
        {
            cout << "Shader program link success" << endl;
        }
    }
    catch (runtime_error& e) {
        cerr << "Shader program link error: " << e.what() << endl;
        return;
    }
    
    Debug::Logger::Log("Shader Program: " + std::to_string(shaderProgram) + " Gizmo Shader Program: " + std::to_string(gizmoShaderProgram) + " Grid Shader Program: " + std::to_string(gridShaderProgram), Debug::LogLevel::SUCCESS);
}

void SceneRenderer2D::Init() {
    std::cout << "[DEBUG] TextureManager instance created, textureCache size: " << textureManager.textureCache.size() << std::endl;

    cout << "Initializing Scene Renderer" << endl;
    
    // Initialize vertex data for rendering quads
    InitQuad();
    cout << "Quad initialization complete ✅" << endl;
    
    // Initialize grid buffers
    InitGridBuffers();
    cout << "Grid buffers initialization complete ✅" << endl;
    
    InitShaders();
    cout << "Shader initialization complete ✅" << endl;
    
    // Create framebuffer last
    CreateFramebuffer();
    cout << framebuffer << ", " << textureID << ", " << rbo << endl;
    cout << "Framebuffer creation complete ✅" << endl;
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
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    // Create a texture attachment for the framebuffer
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);

    // Create a renderbuffer object for depth and stencil attachments
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
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

// This Method Is Loop Update For Render Scene To Texture And Use in HandleChilWindow.cpp
void SceneRenderer2D::RenderSceneToTexture(const Scene& scene) {
    // Bind our framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    
    // Set the viewport to match our framebuffer size
    glViewport(0, 0, width, height);
    
    // Clear the framebuffer
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
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
    if (gridVisible) {
        DrawGrid(projection, view);
    }
    
    // Draw all objects in the scene
    for (const auto& obj : scene.objects) {
        // GLuint tex = textureManager.LoadTexture(obj.spritePath);
        // if (tex != 0) {
        //     DrawSprite(tex, obj.x, obj.y, obj.width, obj.height, 
        //               obj.rotation, obj.scaleX, obj.scaleY);
        // }
    }
    
    // Draw selection gizmo for selected object
    if (selectedObject != nullptr) {
        // DrawSelectionGizmo(*selectedObject);
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
    cout << "Grid Shader Program: " << gridShaderProgram << " Shader Program: " << shaderProgram << " Gizmo Shader Program: " << gizmoShaderProgram << endl;
    if (gridShaderProgram == 0) {
        Debug::Logger::Log("[Debug] Grid Shader Program: " + std::to_string(gridShaderProgram), Debug::LogLevel::CRASH);
        return;
    }

    glUseProgram(gridShaderProgram);
    cout << gridShaderProgram << endl;
    // Set uniforms
    GLint projLoc = glGetUniformLocation(gridShaderProgram, "u_Projection");
    GLint viewLoc = glGetUniformLocation(gridShaderProgram, "u_View");
    GLint colorLoc = glGetUniformLocation(gridShaderProgram, "u_Color");
    Debug::Logger::Log("[Debug] projLoc: " + std::to_string(projLoc) + " viewLoc: " + std::to_string(viewLoc) + " colorLoc: " + std::to_string(colorLoc), Debug::LogLevel::SUCCESS);
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    glUniform4f(colorLoc, 0.7f, 0.7f, 0.7f, 0.5f);

    // Disable depth
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glLineWidth(1.0f);

    float halfWidth = width * 0.5f / cameraZoom;
    float halfHeight = height * 0.5f / cameraZoom;

    float left = cameraPosition.x - halfWidth;
    float right = cameraPosition.x + halfWidth;
    float bottom = cameraPosition.y - halfHeight;
    float top = cameraPosition.y + halfHeight;

    std::cout << "[Grid Area] L:" << left << " R:" << right
            << " B:" << bottom << " T:" << top << std::endl;

    left = floor(left / gridSize) * gridSize;
    right = ceil(right / gridSize) * gridSize;
    bottom = floor(bottom / gridSize) * gridSize;
    top = ceil(top / gridSize) * gridSize;

    std::vector<float> gridVertices;

    // Test only: 2 lines, X and Y axis
    gridVertices.push_back(-10.0f); gridVertices.push_back(0.0f);
    gridVertices.push_back(10.0f);  gridVertices.push_back(0.0f);

    gridVertices.push_back(0.0f);  gridVertices.push_back(-10.0f);
    gridVertices.push_back(0.0f);  gridVertices.push_back(10.0f);


    // float left = cameraPosition.x - width * 0.5f / cameraZoom;
    // float right = cameraPosition.x + width * 0.5f / cameraZoom;
    // float top = cameraPosition.y + height * 0.5f / cameraZoom;
    // float bottom = cameraPosition.y - height * 0.5f / cameraZoom;

    // float visibleWidth = right - left;
    // float visibleHeight = top - bottom;
    // int maxLinesX = std::min(static_cast<int>(visibleWidth / gridSize) + 2, 200);
    // int maxLinesY = std::min(static_cast<int>(visibleHeight / gridSize) + 2, 200);

    // left = floor(left / gridSize) * gridSize;
    // right = ceil(right / gridSize) * gridSize;
    // bottom = floor(bottom / gridSize) * gridSize;
    // top = ceil(top / gridSize) * gridSize;

    // std::vector<float> gridVertices;
    // gridVertices.reserve((maxLinesX + maxLinesY) * 4);

    // int lineCount = 0;
    // for (float x = left; x <= right && lineCount < maxLinesX; x += gridSize, lineCount++) {
    //     // gridVertices.push_back(x); gridVertices.push_back(bottom);
    //     // gridVertices.push_back(x); gridVertices.push_back(top);
    //     gridVertices.push_back(-10.0f); gridVertices.push_back(0.0f);
    //     gridVertices.push_back(10.0f);  gridVertices.push_back(0.0f);
    // }

    // lineCount = 0;
    // for (float y = bottom; y <= top && lineCount < maxLinesY; y += gridSize, lineCount++) {
    //     // gridVertices.push_back(left); gridVertices.push_back(y);
    //     // gridVertices.push_back(right); gridVertices.push_back(y);
    //     gridVertices.push_back(0.0f);  gridVertices.push_back(-10.0f);
    //     gridVertices.push_back(0.0f);  gridVertices.push_back(10.0f);
    // }

    if (gridVertices.size() > m_MaxGridLines * 4) {
        Debug::Logger::Log("Grid vertices size is too large, resizing to " + std::to_string(m_MaxGridLines * 4), Debug::LogLevel::WARNING);
        gridVertices.resize(m_MaxGridLines * 4);
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gridVertices.size() * sizeof(float), gridVertices.data());

    glBindVertexArray(m_GridVAO);
    glEnableVertexAttribArray(0); // <- make sure it's enabled
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gridVertices.size() / 2));
    glBindVertexArray(0);

    Debug::Logger::Log("Grid Line: " + std::to_string(gridVertices.size() / 2), Debug::LogLevel::SUCCESS);

    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
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

void SceneRenderer2D::RenderScene() {
    // Test function to render a simple scene
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glClearColor(1.0f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Enable blending for transparent textures
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw a test white sprite at position 100,100
    GLuint testTextureID = CreateWhiteTexture();
    DrawSprite(testTextureID, 100.0f, 100.0f, 64.0f, 64.0f);
    
    // Clean up the test texture
    glDeleteTextures(1, &testTextureID);
    
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneRenderer2D::InitQuad() {
    cout << "Initializing quad geometry" << endl;
    
    // Vertex data for a quad with positions and texture coordinates
    float quadVertices[] = {
        // positions      // texcoords
        0.0f, 0.0f,       0.0f, 0.0f,  // bottom left
        1.0f, 0.0f,       1.0f, 0.0f,  // bottom right
        1.0f, 1.0f,       1.0f, 1.0f,  // top right
        0.0f, 1.0f,       0.0f, 1.0f   // top left
    };
    
    // Indices for the quad (using 2 triangles)
    unsigned int indices[] = {
        0, 1, 2,  // first triangle
        0, 2, 3   // second triangle
    };
    
    // Generate and bind VAO
    glGenVertexArrays(1, &quadVAO);
    glBindVertexArray(quadVAO);
    
    // Generate and bind VBO
    glGenBuffers(1, &quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    
    // Generate and bind EBO
    glGenBuffers(1, &quadEBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quadEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    // Position attribute (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    
    // Texture coordinate attribute (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    // Unbind VAO and VBO
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, 512, nullptr, log);
        std::cerr << "Shader linking error: " << log << std::endl;
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        glDeleteProgram(program);
        return 0;
    }
    
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