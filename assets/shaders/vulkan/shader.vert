#version 450

// Input layout dari vertex buffer
layout(location = 0) in vec2 inPosition; // posisi XY
layout(location = 1) in vec3 inColor;    // warna RGB

// Data ini bakal dilempar ke fragment shader
layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(inPosition, 0.0, 1.0); // konversi ke clip space
    fragColor = inColor; // teruskan warna ke frag shader
}
