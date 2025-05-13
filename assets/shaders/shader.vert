#version 330 core

layout (location = 0) in vec3 aPos;      // Vertex position
layout (location = 1) in vec2 aTexCoord; // Texture coordinate

uniform mat4 uProjection;
uniform mat4 uModel;

out vec2 TexCoord;

void main()
{
    gl_Position = uProjection * uModel * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
