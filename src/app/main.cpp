#include <iostream>
#include "platform/SdlContext.hpp"
#include "platform/Window.hpp"
#include "core/Config.hpp"

int main(int /*argc*/, char* /*argv*/[]) {
    SdlContext sdl;
    if (sdl.ok()) {
        std::cout << "SDL initialized successfully." << '\n';
    } else {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    WindowPtr window(SDL_CreateWindow("Lane Runner", SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, cfg::kScreenWidth, cfg::kScreenHeight, 0));
    if (!window) {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    RendererPtr renderer(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }


    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
        SDL_RenderClear(renderer.get());

        // Render game objects here...

        SDL_RenderPresent(renderer.get());
    }

    return 0;
}