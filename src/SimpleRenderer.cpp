#include "header/SimpleRenderer.hpp"
#include <SDL_ttf.h>

SimpleRenderer::SimpleRenderer(SDL_Renderer* r) : renderer(r), pageContent(nullptr) {
    contentWidth = 800;
    contentHeight = 600;
}

SimpleRenderer::~SimpleRenderer() {
    if (pageContent) {
        SDL_DestroyTexture(pageContent);
    }
}

void SimpleRenderer::LoadPage(const std::string& url) {
    currentUrl = url;
    
    // Create a surface to render text
    SDL_Surface* surface = SDL_CreateRGBSurface(0, contentWidth, contentHeight, 32, 0, 0, 0, 0);
    SDL_FillRect(surface, NULL, SDL_MapRGB(surface->format, 255, 255, 255));
    
    // Initialize TTF
    TTF_Init();
    TTF_Font* font = TTF_OpenFont("assets/fonts/OpenSans-Regular.ttf", 16);
    if (font) {
        SDL_Color textColor = {0, 0, 0, 255};
        SDL_Surface* textSurface = TTF_RenderText_Blended_Wrapped(font, 
            ("Loading: " + currentUrl).c_str(), 
            textColor, 
            contentWidth - 20);
            
        if (textSurface) {
            SDL_Rect position = {10, 10, textSurface->w, textSurface->h};
            SDL_BlitSurface(textSurface, NULL, surface, &position);
            SDL_FreeSurface(textSurface);
        }
        TTF_CloseFont(font);
    }
    TTF_Quit();

    // Create texture from surface
    if (pageContent) {
        SDL_DestroyTexture(pageContent);
    }
    pageContent = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
}

void SimpleRenderer::Update() {
    // Add any update logic here
}

void SimpleRenderer::Render() {
    if (pageContent) {
        SDL_Rect dst = {0, 40, contentWidth, contentHeight}; // 40px offset for navigation bar
        SDL_RenderCopy(renderer, pageContent, NULL, &dst);
    }
}