#version 450

// Uniform buffer untuk transformasi matriks
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;      // transformasi model (rotasi, scale, translasi)
    mat4 view;       // posisi kamera
    mat4 proj;       // perspektif proyeksi
} ubo;

// Input vertex attributes
layout(location = 0) in vec3 inPosition;  // posisi XYZ (ubah ke vec3)
layout(location = 1) in vec3 inColor;     // warna RGB
layout(location = 2) in vec3 inNormal;    // normal untuk lighting

// Output ke fragment shader
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec3 fragPos;    // posisi di world space untuk lighting

void main() {
    // Transformasi posisi
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragPos = worldPos.xyz;
    
    // Transformasi normal
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    
    // Final position dengan MVP matrix
    gl_Position = ubo.proj * ubo.view * worldPos;
    
    // Pass warna ke fragment shader
    fragColor = inColor;
}