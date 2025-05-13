#include "SceneRenderer2D.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace std;

SceneRenderer2D::SceneRenderer2D(int width, int height)
    : width(width), height(height) {
    cout << "Creating SceneRenderer2D: " << width << "x" << height << endl;
    Init();
}

SceneRenderer2D::~SceneRenderer2D() {
    DestroyFramebuffer();
    
    // Clean up shader and VAO/VBO resources
    if (shaderProgram) glDeleteProgram(shaderProgram);
    if (quadVAO) glDeleteVertexArrays(1, &quadVAO);
    if (quadVBO) glDeleteBuffers(1, &quadVBO);
    if (quadEBO) glDeleteBuffers(1, &quadEBO);
}

void SceneRenderer2D::Init() {
    cout << "Initializing Scene Renderer" << endl;
    
    // Create shader program first
    shaderProgram = CreateShaderProgram("assets/shaders/sprite.vert", "assets/shaders/sprite.frag");
    if (shaderProgram == 0) {
        cerr << "Failed to create shader program!" << endl;
        return;
    }
    
    // Initialize vertex data for rendering quads
    InitQuad();
    cout << "Quad initialization complete ✅" << endl;
    
    // Create framebuffer last
    CreateFramebuffer();
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
}

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
    
    // Disable blending when done
    glDisable(GL_BLEND);
    
    // Unbind the framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SceneRenderer2D::DrawGrid(const glm::mat4& projection, const glm::mat4& view) {
    // Use a simple shader for grid lines
    // This is simplified - you'd need a shader program for grid lines
    cout << "Draw Grid" << endl;
    glUseProgram(gridShaderProgram);
    
    // Set uniforms for grid shader
    glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "u_Projection"), 
                      1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "u_View"), 
                      1, GL_FALSE, glm::value_ptr(view));
    
    // Calculate grid boundaries based on viewport and camera
    float left = cameraPosition.x - width * 0.5f / cameraZoom;
    float right = cameraPosition.x + width * 0.5f / cameraZoom;
    float top = cameraPosition.y - height * 0.5f / cameraZoom;
    float bottom = cameraPosition.y + height * 0.5f / cameraZoom;
    
    // Snap grid boundaries to grid size
    left = floor(left / gridSize) * gridSize;
    right = ceil(right / gridSize) * gridSize;
    top = floor(top / gridSize) * gridSize;
    bottom = ceil(bottom / gridSize) * gridSize;
    
    // Set grid line color (light gray)
    glUniform4f(glGetUniformLocation(gridShaderProgram, "u_Color"), 0.5f, 0.5f, 0.5f, 0.5f);
    
    // Draw vertical grid lines
    for (float x = left; x <= right; x += gridSize) {
        float vertices[] = {
            x, top,
            x, bottom
        };
        
        // Setup VAO/VBO for line
        // Draw line using separate DrawLine function or direct OpenGL calls
    }
    
    // Draw horizontal grid lines
    for (float y = top; y <= bottom; y += gridSize) {
        float vertices[] = {
            left, y,
            right, y
        };
        
        // Setup VAO/VBO for line
        // Draw line using separate DrawLine function or direct OpenGL calls
    }
}

void SceneRenderer2D::DrawSelectionGizmo(const GameObject& obj) {
    // Use gizmo shader program
    glUseProgram(gizmoShaderProgram);
    
    // Set color for selection outline (e.g., yellow)
    glUniform4f(glGetUniformLocation(gizmoShaderProgram, "u_Color"), 1.0f, 1.0f, 0.0f, 1.0f);
    
    // Calculate corners of the object
    float x1 = obj.x;
    float y1 = obj.y;
    float x2 = obj.x + obj.width * obj.scaleX;
    float y2 = obj.y + obj.height * obj.scaleY;
    
    // Add a small margin for the selection rectangle
    float margin = 2.0f / cameraZoom; // 2 pixels in world space
    x1 -= margin;
    y1 -= margin;
    x2 += margin;
    y2 += margin;
    
    // Create rectangle vertices
    float vertices[] = {
        x1, y1,
        x2, y1,
        x2, y2,
        x1, y2,
        x1, y1 // Close the loop
    };
    
    // Draw selection rectangle
    // Setup VAO/VBO for line
    // Draw line strip using direct OpenGL calls or helper function
    
    // If in appropriate edit mode, draw handles for rotation/scaling
    if (currentMode == EditMode::ROTATE || currentMode == EditMode::SCALE) {
        // Draw handles at corners and midpoints of edges
        // ...
    }
}

void SceneRenderer2D::RenderScene() {
    // Test function to render a simple scene
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
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
    
    return program;
}

std::string SceneRenderer2D::LoadFileAsString(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << path << std::endl;
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
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
    cameraPosition = glm::vec3(0.0f, 0.0f, 0.0f);
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
    
    // Clamp zoom to reasonable range
    cameraZoom = std::max(0.1f, std::min(newZoom, 5.0f));
}

void SceneRenderer2D::MoveSelected(float deltaX, float deltaY) {
    if (selectedObject) {
        selectedObject->x += deltaX;
        selectedObject->y += deltaY;
        
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