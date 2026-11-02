#pragma once
#include <SDL.h>
#include <memory>

struct WindowDeleter {
    void operator()(SDL_Window* w) const { SDL_DestroyWindow(w); }
};

using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;

struct RendererDeleter {
    void operator()(SDL_Renderer* r) const { SDL_DestroyRenderer(r); }
};

using RendererPtr = std::unique_ptr<SDL_Renderer, RendererDeleter>;