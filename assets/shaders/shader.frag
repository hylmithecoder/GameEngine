#version 330 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uTexture;
uniform vec4 uColor;

void main()
{
    vec4 texColor = texture(uTexture, TexCoord);
    FragColor = texColor * uColor; // apply tint color
}
