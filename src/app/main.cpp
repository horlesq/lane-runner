#include <algorithm>
#include <iostream>
#include "core/Config.hpp"
#include "platform/SdlContext.hpp"
#include "platform/Window.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"
#include "world/Collision.hpp"
#include "world/Traffic.hpp"
#include "world/Vehicle.hpp"

// ASSET_DIR is injected by CMake; wrap the macro in a typed constant once and use that.
constexpr const char* kAssetDir = ASSET_DIR;

int main(int /*argc*/, char* /*argv*/[]) {
    // --- Platform setup (destroyed in reverse order when main returns) ---
    SdlContext sdl;
    if (!sdl.ok()) {
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

    // --- Game state ---
    Vehicle ego(1, 20.0f);
    Traffic traffic;

    HudView hud;
    hud.assists[0].lamp = AssistLamp::Active;   // placeholder until the ADAS functions exist
    hud.assists[0].level = 0.7f;
    hud.assists[1].lamp = AssistLamp::Off;

    // Logic runs in fixed 20 ms cycles; the accumulator carries leftover frame time.
    float accumulator = 0.0f;
    Uint32 lastTicks = SDL_GetTicks();

    bool running = true;
    while (running) {
        // --- Input events (one-shot actions) ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false;     break;
                    case SDLK_LEFT:   ego.changeLane(-1);  break;
                    case SDLK_RIGHT:  ego.changeLane(+1);  break;
                    default:                               break;
                }
            }
        }

        // --- Timing ---
        const Uint32 now = SDL_GetTicks();
        const float frameTime = static_cast<float>(now - lastTicks) / 1000.0f;
        lastTicks = now;

        // Cap the backlog so a long stall (e.g. dragging the window) can't trigger hundreds of steps.
        accumulator = std::min(accumulator + frameTime, 0.25f);

        // --- Logic: zero or more fixed cycles per frame ---
        while (accumulator >= cfg::kCycleTime) {
            // Held keys act every cycle, so they are read as state rather than events.
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_UP]) {
                ego.setAcceleration(4.0f);
            } else if (keys[SDL_SCANCODE_DOWN]) {
                ego.setAcceleration(-8.0f);
            } else {
                ego.setAcceleration(-0.5f);   // rolling resistance
            }

            ego.step(cfg::kCycleTime);
            traffic.step(cfg::kCycleTime, ego);

            for (const Vehicle& car : traffic.cars()) {
                if (collides(ego, car)) {
                    std::cout << "Crash! Restarting run.\n";
                    ego = Vehicle(1, 20.0f);
                    traffic = Traffic();
                    break;   // traffic was just replaced; stop iterating the old cars
                }
            }

            accumulator -= cfg::kCycleTime;
        }

        // --- HUD ---
        const float scroll = ego.position() * layout::kPixelsPerMeter;
        hud.score = static_cast<int>(scroll / 10.0f);
        hud.speedKmh = static_cast<int>(ego.speed() * 3.6f);   // m/s -> km/h

        // --- Render: back to front ---
        gfx.drawRoad(scroll);

        for (const Vehicle& car : traffic.cars()) {
            // Traffic is drawn relative to the ego: 1 m ahead = kPixelsPerMeter px higher on screen.
            const float x = (car.lateral() + 0.5f) * cfg::kLaneWidth;
            const float y = layout::kEgoScreenY - (car.position() - ego.position()) * layout::kPixelsPerMeter;
            gfx.drawCar(x, y, CarSkin::Red);
        }

        const float egoX = (ego.lateral() + 0.5f) * cfg::kLaneWidth;
        gfx.drawCar(egoX, layout::kEgoScreenY, CarSkin::Blue);

        gfx.drawPanel(hud);
        SDL_RenderPresent(renderer.get());
    }

    return 0;
}
