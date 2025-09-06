#version 450

// Input dari vertex shader
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragPos;

// Output
layout(location = 0) out vec4 outColor;

void main() {
    // Basic lighting calculation
    vec3 lightPos = vec3(2.0, 2.0, 2.0);
    vec3 lightColor = vec3(1.0, 1.0, 1.0);
    
    // Ambient
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;
    
    // Diffuse
    vec3 norm = normalize(fragNormal);
    vec3 lightDir = normalize(lightPos - fragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // Combine
    vec3 result = (ambient + diffuse) * fragColor;
    outColor = vec4(result, 1.0);
}