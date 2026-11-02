#pragma once
#include <SDL.h>
#include <array>
#include <cstddef>
#include <string>
#include "render/HudView.hpp"
#include "render/Texture.hpp"

enum class CarSkin : std::uint8_t { Blue, Red, Green, Yellow, Black };
constexpr std::size_t kCarSkinCount = 5;

// Draws the game. Owns its textures; borrows the SDL renderer.
// Coordinates: x is measured from the road's left edge, y is screen pixels from the top.
class Renderer2D {
public:
    explicit Renderer2D(SDL_Renderer* renderer) : r_(renderer) {}

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    // Call once at startup. Returns false if a texture is missing (see SDL_GetError()).
    bool loadAssets(const std::string& assetDir);

    // scrollPx grows as the ego car drives forward; road markings and scenery move down with it.
    void drawRoad(float scrollPx);
    void drawCar(float centerX, float centerY, CarSkin skin);
    void drawPanel(const HudView& hud);

private:
    void drawScenery(float scrollPx);
    void drawSprite(SDL_Texture* tex, float centerX, float centerY, float w, float h, bool shadow);
    void fillRect(int x, int y, int w, int h, SDL_Color color);

    SDL_Renderer* r_;   // not owned: main() owns the renderer and outlives this object
    std::array<TexturePtr, kCarSkinCount> cars_;
    TexturePtr tree_;
    TexturePtr treeLarge_;
    TexturePtr rock_;
};
