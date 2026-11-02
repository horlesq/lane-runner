#include <iostream>
#include "platform/SdlContext.hpp"
#include "platform/Window.hpp"
#include "core/Config.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"

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

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }

        const Uint32 now = SDL_GetTicks();
        const float dt = static_cast<float>(now - lastTicks) / 1000.0f;
        lastTicks = now;
        scroll += 600.0f * dt;
        hud.score = static_cast<int>(scroll / 10.0f);
        hud.speedKmh = 128;

        const auto laneCenter = [](int lane) {
            return static_cast<float>(lane * cfg::kLaneWidth + cfg::kLaneWidth / 2);
        };

        gfx.drawRoad(scroll);
        gfx.drawCar(laneCenter(0), 120.0f, CarSkin::Red);
        gfx.drawCar(laneCenter(2), 260.0f, CarSkin::Yellow);
        gfx.drawCar(laneCenter(3), 40.0f, CarSkin::Green);
        gfx.drawCar(laneCenter(1), 380.0f, CarSkin::Blue);   // ego
        gfx.drawPanel(hud);

        SDL_RenderPresent(renderer.get());
    }

    return 0;
}
