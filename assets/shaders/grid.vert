#version 330 core
layout (location = 0) in vec2 aPos;

uniform mat4 u_Projection;
uniform mat4 u_View;
uniform vec4 u_Color;

void main()
{
    gl_Position = u_Projection * u_View * vec4(aPos, 0.0, 1.0);
}