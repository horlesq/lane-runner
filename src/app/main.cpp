#include <iostream>
#include <algorithm>
#include "platform/SdlContext.hpp"
#include "platform/Window.hpp"
#include "core/Config.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"
#include "world/Vehicle.hpp"
#include "world/Traffic.hpp"

// ASSET_DIR is injected by CMake; wrap the macro in a typed constant once and use that.
constexpr const char* kAssetDir = ASSET_DIR;

int main(int /*argc*/, char* /*argv*/[]) {
    SdlContext sdl;
    if (sdl.ok()) {
        std::cout << "SDL initialized successfully." << '\n';
    } else {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   // smooth sprite scaling

    WindowPtr window(SDL_CreateWindow("Lane Runner", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      layout::kWindowWidth, layout::kWindowHeight, SDL_WINDOW_RESIZABLE));
    if (!window) {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    RendererPtr renderer(SDL_CreateRenderer(window.get(), -1, SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    // Draw in 640x480 coordinates; SDL scales the result to whatever size the window has.
    SDL_RenderSetLogicalSize(renderer.get(), cfg::kScreenWidth, cfg::kScreenHeight);

    Renderer2D gfx(renderer.get());
    if (!gfx.loadAssets(kAssetDir)) {
        std::cerr << "Asset error: " << SDL_GetError() << '\n';
        return 1;
    }

    // --- Placeholder demo state: replaced by the game logic classes ---
    float scroll = 0.0f;
    HudView hud;
    hud.assists[0].lamp = AssistLamp::Active;
    hud.assists[0].level = 0.7f;
    hud.assists[1].lamp = AssistLamp::Off;
    Uint32 lastTicks = SDL_GetTicks();


    // --- Game state ---
    Vehicle ego(1, 20.0f);
    Traffic traffic;

    float accumulator = 0.0f;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_LEFT) {
                ego.changeLane(-1);
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0 && event.key.keysym.sym == SDLK_RIGHT) {
                ego.changeLane(1);
            }
        }

        const Uint32 now = SDL_GetTicks();
        const float dt = static_cast<float>(now - lastTicks) / 1000.0f;

        accumulator += dt;
        accumulator = std::min(accumulator, 0.25f);

        // Update the traffic cars at a fixed time step, independent of the frame rate.
        while (accumulator >= cfg::kCycleTime) {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_UP]) {
                ego.setAcceleration(4.0f);
            } else if (keys[SDL_SCANCODE_DOWN]) {
                ego.setAcceleration(-8.0f);
            } else {
                ego.setAcceleration(-0.5f);
            }

            ego.step(cfg::kCycleTime);
            traffic.step(cfg::kCycleTime, ego);
            accumulator -= cfg::kCycleTime;
        }

        lastTicks = now;
        scroll = ego.position() * layout::kPixelsPerMeter;
        hud.score = static_cast<int>(scroll / 10.0f);
        hud.speedKmh = static_cast<int>(ego.speed() * 3.6f); // m/s to km/h

        // --- Render ---
        gfx.drawRoad(scroll);

        // Draw the traffic cars
        for (const Vehicle& car : traffic.cars()) {
            const float carX = (car.lateral() + 0.5f) * cfg::kLaneWidth;
            const float carY = cfg::kEgoScreenY - (car.position() - ego.position()) * layout::kPixelsPerMeter;
            gfx.drawCar(carX, carY, CarSkin::Red);
        }

        // Draw the ego car
        const float egoX = (ego.lateral() + 0.5f) * cfg::kLaneWidth;
        gfx.drawCar(egoX, cfg::kEgoScreenY, CarSkin::Blue);   // ego

        // Draw the HUD panel
        gfx.drawPanel(hud);

        SDL_RenderPresent(renderer.get());

    }

    return 0;
}
