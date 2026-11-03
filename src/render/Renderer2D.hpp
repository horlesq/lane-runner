#pragma once
#include <SDL.h>
#include <array>
#include <cstddef>
#include <string>
#include "render/HudView.hpp"
#include "render/MenuView.hpp"
#include "render/Texture.hpp"

enum class CarSkin : std::uint8_t { Blue, Red, Green, Yellow, Black };
constexpr std::size_t kCarSkinCount = 5;

// Body styles from the sprite pack: 0 sports, 1 coupe, 2 sedan, 3 pickup, 4 compact.
constexpr std::size_t kCarModelCount = 5;

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
    // model must be < kCarModelCount. Shorter models keep their real proportions.
    void drawCar(float centerX, float centerY, CarSkin skin, std::size_t model = 0);
    void drawPanel(const HudView& hud);

    // Sensor debug view: line from the ego's nose to the detected lead car, with gap and relative speed.
    // egoX is road-relative like drawCar; gap in m (bumper to bumper), relativeSpeed in m/s.
    void drawLeadMarker(float egoX, float gap, float relativeSpeed);

    // Full-screen overlays, drawn on top of the road and cars.
    void drawMenu(const MenuView& menu);
    void drawGameOver(int score, int best, bool newBest);

private:
    void drawScenery(float scrollPx);
    void drawTextCentered(const char* text, int centerX, int y, int scale, SDL_Color color);
    void drawSprite(SDL_Texture* tex, float centerX, float centerY, float w, float h, bool shadow);
    void fillRect(int x, int y, int w, int h, SDL_Color color);

    SDL_Renderer* r_;   // not owned: main() owns the renderer and outlives this object
    struct CarSprite {
        TexturePtr texture;
        float lengthScale = 1.0f;   // sprite height relative to the longest model
    };
    std::array<std::array<CarSprite, kCarModelCount>, kCarSkinCount> cars_;   // [skin][model]
    TexturePtr tree_;
    TexturePtr treeLarge_;
    TexturePtr rock_;
    TexturePtr menuBadge_;   // optional art/menu-logo.png; the menu skips it when absent
};
