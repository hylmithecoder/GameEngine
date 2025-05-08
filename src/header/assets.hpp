#include <stb_image.h>
#include <GL/gl.h> // Include OpenGL header
#include <SDL_opengl.h>
#include <GL/glext.h>
#include <iostream>

class Assets
{
    public:
        void load_assets();
        GLuint backgroundTexture;
};