#pragma once
#include <SDL.h>
#include <string>
#include <vector>

class SimpleRenderer {
public:
    SimpleRenderer(SDL_Renderer* renderer);
    ~SimpleRenderer();

    void LoadPage(const std::string& url);
    void Render();
    void Update();

private:
    SDL_Renderer* renderer;
    std::string currentUrl;
    SDL_Texture* pageContent;
    int contentWidth;
    int contentHeight;
};