#pragma once
#include <SDL.h>
#include <SDL_image.h>
#include <memory>
#include <string>

struct TextureDeleter {
    void operator()(SDL_Texture* t) const { SDL_DestroyTexture(t); }
};

using TexturePtr = std::unique_ptr<SDL_Texture, TextureDeleter>;

// Loads a PNG into GPU memory. Returns an empty pointer on failure (see SDL_GetError()).
inline TexturePtr loadTexture(SDL_Renderer* renderer, const std::string& path) {
    return TexturePtr(IMG_LoadTexture(renderer, path.c_str()));
}
