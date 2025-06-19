#include "assets.hpp"

// Untuk memuat gambar
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
// Fungsi untuk memuat tekstur dari file gambar
bool Assets::LoadTextureFromFile(const char* filename, TextureData* out_texture)
{
    // Flip image vertically during loading
    // stbi_set_flip_vertically_on_load(true);

    // Load image from file
    int width, height, channels;
    unsigned char* data = stbi_load(filename, &width, &height, &channels, 4);
    if (data == NULL) {
        std::cerr << "Failed to load texture: " << filename << std::endl;
        return false;
    }

    // Create OpenGL texture
    GLuint texture_id;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);
    
    // Setup texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Upload image data to GPU
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Store texture info and free image memory
    *out_texture = {texture_id, width, height};
    GetDominantColor(data, width, height, channels);
    stbi_image_free(data);
    
    // Reset flip setting to default
    stbi_set_flip_vertically_on_load(false);
    return true;
}

Color Assets::GetDominantColor(unsigned char* data, int width, int height, int channels) {
    const int borderSize = 50; // Sample from edges, adjust as needed
    std::vector<std::vector<float>> colorClusters;
    
    // Sample pixels from the borders of the image
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // Only process border pixels
            if (x < borderSize || x > width - borderSize || 
                y < borderSize || y > height - borderSize) {
                
                int idx = (y * width + x) * 4;
                float r = data[idx + 0] / 255.0f;
                float g = data[idx + 1] / 255.0f;
                float b = data[idx + 2] / 255.0f;
                
                // Skip very dark or very bright pixels
                float brightness = (r + g + b) / 3.0f;
                if (brightness < 0.15f || brightness > 0.95f) continue;
                
                // Calculate color vibrance
                float max = std::max({r, g, b});
                float min = std::min({r, g, b});
                float saturation = (max - min) / (max + 0.001f);
                
                // Only keep vibrant colors
                if (saturation > 0.2f) {
                    colorClusters.push_back({r, g, b});
                }
            }
        }
    }
    
    // If no viable colors found, return a default color
    if (colorClusters.empty()) {
        return Color{0.2f, 0.2f, 0.3f}; // Subtle blue-grey
    }
    
    // Find the most prominent color cluster
    std::vector<float> avgColor{0, 0, 0};
    float totalWeight = 0;
    
    for (const auto& color : colorClusters) {
        // Weight colors by their saturation and brightness
        float saturation = std::max({color[0], color[1], color[2]}) - 
                          std::min({color[0], color[1], color[2]});
        float brightness = (color[0] + color[1] + color[2]) / 3.0f;
        float weight = saturation * (1.0f - std::abs(brightness - 0.5f));
        
        avgColor[0] += color[0] * weight;
        avgColor[1] += color[1] * weight;
        avgColor[2] += color[2] * weight;
        totalWeight += weight;
    }
    
    // Normalize and create result
    Color result;
    if (totalWeight > 0) {
        result.r = avgColor[0] / totalWeight;
        result.g = avgColor[1] / totalWeight;
        result.b = avgColor[2] / totalWeight;
        
        // Enhance the mood by slightly adjusting saturation and brightness
        float maxComp = std::max({result.r, result.g, result.b});
        float minComp = std::min({result.r, result.g, result.b});
        float sat = (maxComp - minComp) / (maxComp + 0.001f);
        
        // Enhance colors while maintaining the mood
        float saturationTarget = 0.6f;
        float adjustment = (saturationTarget - sat) * 0.5f;
        if (adjustment > 0) {
            result.r = std::min(1.0f, result.r * (1.0f + adjustment));
            result.g = std::min(1.0f, result.g * (1.0f + adjustment));
            result.b = std::min(1.0f, result.b * (1.0f + adjustment));
        }
    }
    
    Debug::Logger::Log("Edge Dominant Color - R: " + std::to_string(result.r) + 
                      ", G: " + std::to_string(result.g) + 
                      ", B: " + std::to_string(result.b), 
                      Debug::LogLevel::INFO);
    
    r = result.r;
    g = result.g;
    b = result.b;
    
    return result;
}