#include <SceneRenderer3D.hpp>

// Fungsi untuk menghitung view matrix
glm::mat4 SceneRenderer3D::CalculateViewMatrix() const {
    if (projType == ProjectionType::ORTHOGRAPHIC) {
        // Untuk tampilan orthographic seperti editor (top-down, side view, dll)
        return glm::translate(glm::mat4(1.0f), 
                             glm::vec3(-cameraPosition.x, -cameraPosition.y, -cameraPosition.z));
    } else {
        // Untuk tampilan perspective (3D view)
        // Kalkulasi arah pandang kamera berdasarkan yaw dan pitch
        glm::vec3 front;
        front.x = cos(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
        front.y = sin(glm::radians(cameraPitch));
        front.z = sin(glm::radians(cameraYaw)) * cos(glm::radians(cameraPitch));
        
        glm::vec3 cameraFront = glm::normalize(front);
        return glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);
    }
}

// Fungsi untuk menghitung projection matrix
glm::mat4 SceneRenderer3D::CalculateProjectionMatrix() const {
    if (projType == ProjectionType::ORTHOGRAPHIC) {
        // Orthographic projection (like 2D view)
        return glm::ortho(
            -width * 0.5f / cameraZoom, width * 0.5f / cameraZoom,
            -height * 0.5f / cameraZoom, height * 0.5f / cameraZoom,
            nearPlane, farPlane
        );
    } else {
        // Perspective projection (3D view)
        return glm::perspective(
            glm::radians(fieldOfView),
            (float)width / (float)height,
            nearPlane, farPlane
        );
    }
}

// Fungsi untuk menggambar grid di 3D
void SceneRenderer3D::DrawGrid(const glm::mat4& projection, const glm::mat4& view) {
    // Gunakan shader untuk grid lines
    glUseProgram(gridShaderProgram);
    
    // Set uniform untuk grid shader
    GLint projLoc = glGetUniformLocation(gridShaderProgram, "u_Projection");
    GLint viewLoc = glGetUniformLocation(gridShaderProgram, "u_View");
    GLint colorLoc = glGetUniformLocation(gridShaderProgram, "u_Color");
    
    glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    
    // Ukuran grid dan range
    float gridExtent = 50.0f; // Grid akan meluas 50 unit ke segala arah
    int linesCount = static_cast<int>(gridExtent / gridSize) * 2;
    
    // Batas grid untuk memastikan kita tidak membuat terlalu banyak garis
    linesCount = std::min(linesCount, m_MaxGridLines);
    
    // Set warna grid (abu-abu dengan transparansi)
    glUniform4f(colorLoc, 0.7f, 0.7f, 0.7f, 0.5f);
    
    // Enable blending untuk transparansi
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Enable line smoothing untuk penampilan yang lebih baik
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glLineWidth(1.0f);
    
    // Membuat semua vertex grid dalam satu batch
    std::vector<float> gridVertices;
    gridVertices.reserve(linesCount * 4 * 2); // 2 garis per arah * 2 vertices * 2 floats per vertex
    
    // Berdasarkan bidang yang dipilih, buat grid
    float startPos = -gridExtent;
    float endPos = gridExtent;
    
    switch (gridPlane) {
        case 0: // Bidang XY (Z konstan)
            // Garis-garis horizontal (sejajar sumbu X)
            for (float y = startPos; y <= endPos; y += gridSize) {
                gridVertices.push_back(startPos); // X1
                gridVertices.push_back(y);        // Y1
                gridVertices.push_back(0.0f);     // Z1 (konstan)
                
                gridVertices.push_back(endPos);   // X2
                gridVertices.push_back(y);        // Y2
                gridVertices.push_back(0.0f);     // Z2 (konstan)
            }
            
            // Garis-garis vertikal (sejajar sumbu Y)
            for (float x = startPos; x <= endPos; x += gridSize) {
                gridVertices.push_back(x);        // X1
                gridVertices.push_back(startPos); // Y1
                gridVertices.push_back(0.0f);     // Z1 (konstan)
                
                gridVertices.push_back(x);        // X2
                gridVertices.push_back(endPos);   // Y2
                gridVertices.push_back(0.0f);     // Z2 (konstan)
            }
            break;
            
        case 1: // Bidang XZ (Y konstan)
            // Garis-garis sejajar sumbu X
            for (float z = startPos; z <= endPos; z += gridSize) {
                gridVertices.push_back(startPos); // X1
                gridVertices.push_back(0.0f);     // Y1 (konstan)
                gridVertices.push_back(z);        // Z1
                
                gridVertices.push_back(endPos);   // X2
                gridVertices.push_back(0.0f);     // Y2 (konstan)
                gridVertices.push_back(z);        // Z2
            }
            
            // Garis-garis sejajar sumbu Z
            for (float x = startPos; x <= endPos; x += gridSize) {
                gridVertices.push_back(x);        // X1
                gridVertices.push_back(0.0f);     // Y1 (konstan)
                gridVertices.push_back(startPos); // Z1
                
                gridVertices.push_back(x);        // X2
                gridVertices.push_back(0.0f);     // Y2 (konstan)
                gridVertices.push_back(endPos);   // Z2
            }
            break;
            
        case 2: // Bidang YZ (X konstan)
            // Garis-garis sejajar sumbu Y
            for (float z = startPos; z <= endPos; z += gridSize) {
                gridVertices.push_back(0.0f);     // X1 (konstan)
                gridVertices.push_back(startPos); // Y1
                gridVertices.push_back(z);        // Z1
                
                gridVertices.push_back(0.0f);     // X2 (konstan)
                gridVertices.push_back(endPos);   // Y2
                gridVertices.push_back(z);        // Z2
            }
            
            // Garis-garis sejajar sumbu Z
            for (float y = startPos; y <= endPos; y += gridSize) {
                gridVertices.push_back(0.0f);     // X1 (konstan)
                gridVertices.push_back(y);        // Y1
                gridVertices.push_back(startPos); // Z1
                
                gridVertices.push_back(0.0f);     // X2 (konstan)
                gridVertices.push_back(y);        // Y2
                gridVertices.push_back(endPos);   // Z2
            }
            break;
    }
    
    // Bind and update the grid VBO with the new vertices
    glBindVertexArray(m_GridVAO);
    
    // Update buffer data
    glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, gridVertices.size() * sizeof(float), gridVertices.data());
    
    // Draw all lines in one call
    glDrawArrays(GL_LINES, 0, gridVertices.size() / 3);
    
    // Cleanup state
    glBindVertexArray(0);
    glDisable(GL_LINE_SMOOTH);
    glDisable(GL_BLEND);
}

// Initialize grid buffers for 3D
void SceneRenderer3D::InitGridBuffers() {
    glGenVertexArrays(1, &m_GridVAO);
    glGenBuffers(1, &m_GridVBO);
    
    glBindVertexArray(m_GridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
    
    // Pre-allocate buffer for max grid lines (2 vertices per line, 3 coords per vertex)
    glBufferData(GL_ARRAY_BUFFER, m_MaxGridLines * 2 * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    // Configure vertex attributes for 3D position
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

// Initialize and draw coordinate axes
void SceneRenderer3D::InitializeAxes() {
    if (m_AxesVAO != 0) {
        // Already initialized
        return;
    }
    
    glGenVertexArrays(1, &m_AxesVAO);
    glGenBuffers(1, &m_AxesVBO);
    
    glBindVertexArray(m_AxesVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_AxesVBO);
    
    // Define axes lines and colors
    // X-axis: Red, Y-axis: Green, Z-axis: Blue
    float axesData[] = {
        // Positions        // Colors
        0.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f, // X start, Red
        1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f, // X end, Red
        
        0.0f, 0.0f, 0.0f,   0.0f, 1.0f, 0.0f, // Y start, Green
        0.0f, 1.0f, 0.0f,   0.0f, 1.0f, 0.0f, // Y end, Green
        
        0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 1.0f, // Z start, Blue
        0.0f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f  // Z end, Blue
    };
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(axesData), axesData, GL_STATIC_DRAW);
    
    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    // Color attribute
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
}

void SceneRenderer3D::DrawAxes(const glm::mat4& projection, const glm::mat4& view) {
    if (m_AxesVAO == 0) {
        InitializeAxes();
    }
    
    glUseProgram(axisShaderProgram);
    
    // Set projection and view matrices
    glUniformMatrix4fv(glGetUniformLocation(axisShaderProgram, "projection"), 
                        1, GL_FALSE, glm::value_ptr(projection));
    glUniformMatrix4fv(glGetUniformLocation(axisShaderProgram, "view"), 
                        1, GL_FALSE, glm::value_ptr(view));
    
    // Draw axes
    glBindVertexArray(m_AxesVAO);
    glLineWidth(3.0f); // Make axes lines thicker
    glDrawArrays(GL_LINES, 0, 6);
    glLineWidth(1.0f); // Reset line width
    glBindVertexArray(0);
}

// Convert screen coordinates to 3D world ray
glm::vec3 SceneRenderer3D::ViewportToWorldPosition(float viewX, float viewY, float viewDepth) const {
    // Normalize device coordinates (-1 to 1)
    float ndcX = (2.0f * viewX) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * viewY) / height; // Y is inverted
    float ndcZ = 2.0f * viewDepth - 1.0f;        // Assume depth in [0,1] range
    
    // Create homogeneous clip coordinates
    glm::vec4 clipCoords(ndcX, ndcY, ndcZ, 1.0f);
    
    // Get inverse matrices
    glm::mat4 invProjection = glm::inverse(CalculateProjectionMatrix());
    glm::mat4 invView = glm::inverse(CalculateViewMatrix());
    
    // Transform back to world coordinates
    glm::vec4 worldCoords = invView * invProjection * clipCoords;
    
    if (worldCoords.w != 0.0f) {
        // Perspective divide
        worldCoords /= worldCoords.w;
    }
    
    return glm::vec3(worldCoords);
}

// Cast ray for 3D object picking
bool SceneRenderer3D::CastRay(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, 
                             GameObject** hitObject, float* hitDistance) {
    *hitObject = nullptr;
    *hitDistance = std::numeric_limits<float>::max();
    bool hit = false;
    
    // Cek setiap GameObject di scene
    for (size_t i = 0; i < currentScene.GetObjectCount(); i++) {
        GameObject* obj = currentScene.GetObjectAt(i);
        if (!obj) continue;
        
        // Ambil bounding sphere (sederhana) dari objek
        glm::vec3 objPos = currentScene.GetPosition();
        float objRadius = glm::length(currentScene.GetScale()) * 0.5f; // Approximate radius
        
        // Ray-sphere intersection test
        glm::vec3 oc = rayOrigin - objPos;
        float a = glm::dot(rayDirection, rayDirection);
        float b = 2.0f * glm::dot(oc, rayDirection);
        float c = glm::dot(oc, oc) - objRadius * objRadius;
        float discriminant = b * b - 4 * a * c;
        
        if (discriminant > 0) {
            float t = (-b - sqrt(discriminant)) / (2.0f * a);
            if (t > 0 && t < *hitDistance) {
                *hitDistance = t;
                *hitObject = obj;
                hit = true;
            }
        }
    }
    
    return hit;
}

// Handle mouse click in 3D
void SceneRenderer3D::HandleClick(float viewX, float viewY) {
    // Konversi koordinat viewport menjadi ray
    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    
    if (projType == ProjectionType::PERSPECTIVE) {
        // For perspective projection
        rayOrigin = cameraPosition;
        
        // Calculate ray direction
        glm::vec3 rayNds = glm::vec3(
            (2.0f * viewX) / width - 1.0f,
            1.0f - (2.0f * viewY) / height,
            1.0f
        );
        
        glm::vec4 rayClip = glm::vec4(rayNds.x, rayNds.y, -1.0f, 1.0f);
        glm::vec4 rayEye = glm::inverse(CalculateProjectionMatrix()) * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
        
        glm::vec4 rayWorldH = glm::inverse(CalculateViewMatrix()) * rayEye;
        rayDirection = glm::normalize(glm::vec3(rayWorldH));
    } else {
        // For orthographic projection
        // More straightforward ray cast from viewport position
        rayOrigin = ViewportToWorldPosition(viewX, viewY, 0.0f);
        rayDirection = glm::vec3(0.0f, 0.0f, -1.0f); // Looking along negative Z
    }
    
    // Cast ray to find object
    GameObject* hitObject = nullptr;
    float hitDistance = 0;
    
    if (CastRay(rayOrigin, rayDirection, &hitObject, &hitDistance)) {
        // Object hit, select it
        selectedObject = hitObject;
        selectedObjectIndex = currentScene.GetObjectIndex(hitObject);
    } else {
        // No hit, deselect
        selectedObject = nullptr;
        selectedObjectIndex = -1;
    }
}