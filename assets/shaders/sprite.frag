#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D u_Texture;

void main()
{
    // Sample the texture at the current texture coordinates
    vec4 texColor = texture(u_Texture, TexCoord);
    
    // Discard fragments with very low alpha
    if(texColor.a < 0.1)
        discard;
        
    // Output the final color
    FragColor = texColor;
}