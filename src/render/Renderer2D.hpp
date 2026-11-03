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
    // signal: turn indicator, -1 left, +1 right, 0 off (blinks amber on the front and rear corner).
    // braking: red brake lights on the rear corners.
    void drawCar(float centerX, float centerY, CarSkin skin, std::size_t model = 0, int signal = 0, bool braking = false);
    void drawMotorcycle(float centerX, float centerY, CarSkin skin, int signal = 0, bool braking = false);
    void drawPanel(const HudView& hud);

    // Lane closure: tinted lane between the two screen heights, cones on both lane lines, a barrier
    // at the closure (startY, nearest the player) and merge arrows before it. mergeDir: -1 left, +1 right.
    void drawRoadworks(float laneCenterX, float startY, float endY, int mergeDir);

    // Debris on the road (kind 0 tyres, 1 barrel, 2 rock), road-relative x, screen y.
    void drawDebris(float centerX, float centerY, int kind);
    // Two wrecked cars across a lane with hazard lights, and a warning triangle before them.
    // centerY = the first wreck (nearest the player); the second stands lengthMetres further ahead.
    void drawWreck(float laneCenterX, float centerY, float lengthMetres);
    // Rain between two screen heights: darker light and falling streaks.
    void drawRain(float topY, float bottomY);
    // Tunnel between two screen heights: dark inside, walls over the verges, ceiling lights, portals.
    void drawTunnel(float topY, float bottomY);

    // Concrete overpass across the road at screen height centerY (drawn over the cars).
    void drawBridge(float centerY);
    // Fog bank between two screen heights; hides everything inside it.
    void drawFog(float topY, float bottomY);

    // Power-up badge on the road: a pulsing disc with a short label (road-relative x, screen y).
    void drawPickup(float centerX, float centerY, const char* label, SDL_Color color);
    // A pick-up still off-screen ahead: small badge with a down arrow at the top edge above its lane.
    void drawPickupHint(float centerX, const char* label, SDL_Color color);

    // Oil patch on the road (drawn under the cars). centerX road-relative, centerY screen pixels.
    void drawOil(float centerX, float centerY);

    // Wind streaks blowing across the road; wind in lanes/s (+ = to the right), 0 draws nothing.
    void drawWind(float wind, float scrollPx);

    // Sensor debug view: line from the ego's nose to the detected lead car, with gap and relative speed.
    // egoX is road-relative like drawCar; gap in m (bumper to bumper), relativeSpeed in m/s.
    void drawLeadMarker(float egoX, float gap, float relativeSpeed);

    // BSW: warning lights beside the ego, like the lamps in a real car's side mirrors.
    // Amber = car in that blind spot; red and flashing = BSW is blocking a steer towards it.
    void drawBlindSpotWarning(float egoX, bool left, bool right, bool intervening);

    // Sensor debug view. Zone in metres (+ = ahead) and lanes (+ = right) relative to the ego;
    // a detection is drawn as corner brackets around the car's position on screen.
    void drawSensorZone(float egoX, float minLong, float maxLong, float minLat, float maxLat, SDL_Color color);
    void drawDetection(float centerX, float centerY, SDL_Color color);

    // LKA debug view: the lane centre it steers towards (road-relative x).
    void drawLaneTarget(float centerX);

    // Pulsing border around the road: amber for a warning, red (thicker) while braking for the driver.
    void drawAlertBorder(bool danger);

    // Full-screen overlays, drawn on top of the road and cars.
    void drawMenu(const MenuView& menu);
    void drawShop(const ShopView& shop);
    void drawGarage(const GarageView& garage);
    void drawAssistGuide();   // what each power-up does, in plain words
    // what / tip: what the player crashed into and a hint about the assist that would have helped.
    void drawGameOver(int score, int best, bool newBest, int coinsEarned, const char* what, const char* tip);

private:
    void drawScenery(float scrollPx);
    void drawTextCentered(const char* text, int centerX, int y, int scale, SDL_Color color);
    void drawSprite(SDL_Texture* tex, float centerX, float centerY, float w, float h, bool shadow);
    void fillRect(int x, int y, int w, int h, SDL_Color color);
    void drawLamps(float x, float centerY, float halfWidth, float height, int signal, bool braking);

    SDL_Renderer* r_;   // not owned: main() owns the renderer and outlives this object
    struct CarSprite {
        TexturePtr texture;
        float lengthScale = 1.0f;   // sprite height relative to the longest model
    };
    std::array<std::array<CarSprite, kCarModelCount>, kCarSkinCount> cars_;   // [skin][model]
    TexturePtr tree_;
    TexturePtr treeLarge_;
    TexturePtr rock_;
    TexturePtr oil_;
    TexturePtr cone_;
    std::array<TexturePtr, 3> debris_;
    TexturePtr barrier_;
    TexturePtr arrow_;
    std::array<TexturePtr, kCarSkinCount> motorcycles_;
    std::array<TexturePtr, kCarSkinCount> riders_;   // racer sprites that sit on the motorcycles

    // Wind animation: distance the air has moved, integrated frame by frame (not time x speed,
    // which would jump backwards whenever the gust weakens).
    float windTravel_ = 0.0f;   // px, signed: + = to the right
    Uint32 windTicks_ = 0;      // time of the previous drawWind call
    TexturePtr menuBadge_;   // optional art/menu-logo.png; the menu skips it when absent
};
