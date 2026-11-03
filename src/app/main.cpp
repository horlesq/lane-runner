#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <SDL_image.h>
#include "audio/Audio.hpp"
#include "control/Arbiter.hpp"
#include "core/Config.hpp"
#include "functions/Acc.hpp"
#include "functions/Aeb.hpp"
#include "functions/Lka.hpp"
#include "platform/SdlContext.hpp"
#include "platform/Window.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"
#include "world/Collision.hpp"
#include "world/Lead.hpp"
#include "world/Traffic.hpp"
#include "world/Vehicle.hpp"
#include "world/Wind.hpp"

// ASSET_DIR is injected by CMake; wrap the macro in a typed constant once and use that.
constexpr const char* kAssetDir = ASSET_DIR;

enum class Screen : std::uint8_t { Menu, Playing, GameOver };

// Menu entries, in the order of MenuView::items.
constexpr std::size_t kMenuStart = 0;
constexpr std::size_t kMenuShop = 1;
constexpr std::size_t kMenuQuit = 2;
static_assert(kMenuQuit < MenuView{}.items.size(), "Menu indices out of range");
static_assert(kMenuShop != kMenuStart, "Menu indices must be distinct");

constexpr float kStartSpeed = 20.0f;   // m/s
constexpr float kMenuSpeed = 22.0f;    // m/s, camera speed behind the title menu
constexpr float kTopSpeed = 40.0f;     // m/s, only used to scale the engine sound
constexpr float kDriverSteerRate = 2.0f;   // lanes/s while ← or → is held; stronger than LKA's 1.2

int main(int /*argc*/, char* /*argv*/[]) {
    // --- Platform setup (destroyed in reverse order when main returns) ---
    SdlContext sdl;
    if (!sdl.ok()) {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    Audio audio;   // declared after sdl, so it shuts down before SDL_Quit()
    if (!audio.ok()) {
        std::cerr << "No audio device, running silently: " << SDL_GetError() << '\n';
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");   // smooth sprite scaling

    WindowPtr window(SDL_CreateWindow("Lane Runner", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      layout::kWindowWidth, layout::kWindowHeight, SDL_WINDOW_RESIZABLE));
    if (!window) {
        std::cerr << "SDL error: " << SDL_GetError() << '\n';
        return 1;
    }

    // Title-bar icon; the game still runs if the file is missing.
    if (SDL_Surface* icon = IMG_Load((std::string(kAssetDir) + "art/icon.png").c_str())) {
        SDL_SetWindowIcon(window.get(), icon);
        SDL_FreeSurface(icon);
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

    constexpr std::array<CarSkin, 4> kNpcSkins{CarSkin::Red, CarSkin::Green, CarSkin::Yellow, CarSkin::Black};

    // --- Game state ---
    Screen screen = Screen::Menu;
    Vehicle ego(1, kMenuSpeed);
    Traffic traffic;
    Acc acc;   // adaptive cruise control
    Aeb aeb;   // automatic emergency braking, always on
    Lka lka;   // lane keeping assist
    Wind wind;
    MenuView menu;
    HudView hud;
    int best = 0;
    int lastScore = 0;
    bool newBest = false;

    bool showSensorDebug = true;   // Tab: engineer's view of what the sensors detect

    // AEB feedback: sounds play on stage changes, the warning chime repeats while it lasts.
    AebStage lastAebStage = AebStage::Inactive;
    float warningChimeTimer = 0.0f;   // s until the next warning chime
    constexpr float kWarningChimeInterval = 0.5f;
    const auto isBraking = [](AebStage s) { return s == AebStage::PartialBrake || s == AebStage::FullBrake; };

    const auto scoreOf = [](const Vehicle& v) {
        return static_cast<int>(v.position() * layout::kPixelsPerMeter / 10.0f);
    };

    const auto startRun = [&]() {
        ego = Vehicle(1, kStartSpeed);
        ego.enableFreeSteering();
        traffic = Traffic();
        acc = Acc();
        aeb = Aeb();
        lka = Lka();
        wind = Wind();
        lastAebStage = AebStage::Inactive;
        screen = Screen::Playing;
        audio.play(Sfx::Start);
    };

    const auto backToMenu = [&]() {
        ego = Vehicle(1, kMenuSpeed);
        traffic = Traffic();
        acc = Acc();
        aeb = Aeb();
        lka = Lka();
        menu.best = best;
        screen = Screen::Menu;
    };

    // Logic runs in fixed 20 ms cycles; the accumulator carries leftover frame time.
    float accumulator = 0.0f;
    Uint32 lastTicks = SDL_GetTicks();

    bool running = true;
    while (running) {
        // --- Input events (one-shot actions), handled per screen ---
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                continue;
            }
            if (event.type != SDL_KEYDOWN || event.key.repeat != 0) {
                continue;
            }
            const SDL_Keycode key = event.key.keysym.sym;
            const bool confirm = (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE);

            switch (screen) {
                case Screen::Menu: {
                    const std::size_t count = menu.items.size();
                    if (key == SDLK_UP) {
                        menu.selected = (menu.selected + count - 1) % count;   // wrap around
                        audio.play(Sfx::MenuMove);
                    } else if (key == SDLK_DOWN) {
                        menu.selected = (menu.selected + 1) % count;
                        audio.play(Sfx::MenuMove);
                    } else if (confirm) {
                        if (!menu.items[menu.selected].enabled) {
                            audio.play(Sfx::Denied);
                        } else if (menu.selected == kMenuStart) {
                            startRun();
                        } else if (menu.selected == kMenuQuit) {
                            running = false;
                        }
                    } else if (key == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
                }
                case Screen::Playing:
                    // Steering is held, so it's read as keyboard state in the logic loop, not here.
                    if (key == SDLK_ESCAPE) {
                        backToMenu();
                    } else if (key == SDLK_TAB) {
                        showSensorDebug = !showSensorDebug;
                    } else if (key == SDLK_a) {
                        acc.toggle(ego.speed());
                    } else if (key == SDLK_l) {
                        lka.toggle();
                    }
                    break;
                case Screen::GameOver:
                    if (confirm) {
                        startRun();
                    } else if (key == SDLK_ESCAPE) {
                        backToMenu();
                    }
                    break;
            }
        }

        // --- Timing ---
        const Uint32 now = SDL_GetTicks();
        const float frameTime = static_cast<float>(now - lastTicks) / 1000.0f;
        lastTicks = now;

        // Cap the backlog so a long stall (e.g. dragging the window) can't trigger hundreds of steps.
        accumulator = std::min(accumulator + frameTime, 0.25f);
        if (screen == Screen::GameOver) {
            accumulator = 0.0f;   // the world is frozen after a crash
        }

        // --- Logic: zero or more fixed cycles per frame ---
        while (accumulator >= cfg::kCycleTime) {
            if (screen == Screen::Playing) {
                // Every source puts its request in; the arbiter alone decides what reaches the car.
                const std::optional<Lead> lead = findLead(ego, traffic);   // perceive once, use everywhere
                AccelRequests requests;

                // Held keys act every cycle, so they are read as state rather than events.
                const Uint8* keys = SDL_GetKeyboardState(nullptr);
                if (keys[SDL_SCANCODE_DOWN]) {
                    requests.driverBrake = -8.0f;
                    acc.cancel();                 // the driver's brake cancels ACC
                } else if (keys[SDL_SCANCODE_UP]) {
                    requests.driverThrottle = 4.0f;
                }
                if (acc.state() == AccState::Active) {
                    requests.acc = acc.step(cfg::kCycleTime, ego.speed(), lead);
                }
                requests.aeb = aeb.step(lead);    // safety function: always on

                ego.setAcceleration(arbitrate(requests).accel);

                // AEB sounds: squeal when braking starts, repeating chime while warning.
                const AebStage stage = aeb.stage();
                if (isBraking(stage) && !isBraking(lastAebStage)) {
                    audio.play(Sfx::EmergencyBrake);
                }
                if (stage == AebStage::Warning) {
                    warningChimeTimer -= cfg::kCycleTime;
                    if (lastAebStage != AebStage::Warning || warningChimeTimer <= 0.0f) {
                        audio.play(Sfx::CollisionWarning);
                        warningChimeTimer = kWarningChimeInterval;
                    }
                }
                lastAebStage = stage;

                // Lateral: driver steering beats LKA; wind pushes the car whatever anyone wants.
                const bool left = keys[SDL_SCANCODE_LEFT];
                const bool right = keys[SDL_SCANCODE_RIGHT];
                std::optional<float> driverSteer;
                if (left != right) {   // both or neither held: no steering
                    driverSteer = left ? -kDriverSteerRate : kDriverSteerRate;
                }
                std::optional<float> lkaSteer;
                const float lkaCommand = lka.step(cfg::kCycleTime, ego.lateral(), driverSteer.has_value());
                if (lka.state() == LkaState::Active) {
                    lkaSteer = lkaCommand;
                }
                ego.steer(arbitrateSteer(driverSteer, lkaSteer).rate);
                ego.push(wind.step(cfg::kCycleTime));
            } else {
                ego.setAcceleration(0.0f);   // menu: an invisible camera cruises through traffic
            }

            const int laneBefore = ego.lane();
            ego.step(cfg::kCycleTime);
            if (screen == Screen::Playing && ego.lane() != laneBefore) {
                audio.play(Sfx::LaneChange);   // whoosh when crossing a lane line
            }
            traffic.step(cfg::kCycleTime, ego);
            accumulator -= cfg::kCycleTime;

            if (screen != Screen::Playing) {
                continue;
            }
            for (const Vehicle& car : traffic.cars()) {
                if (collides(ego, car)) {
                    audio.play(Sfx::Crash);
                    lastScore = scoreOf(ego);
                    newBest = lastScore > best;
                    best = std::max(best, lastScore);
                    screen = Screen::GameOver;
                    accumulator = 0.0f;
                    break;
                }
            }
        }

        audio.setEngine(screen == Screen::Playing, ego.speed() / kTopSpeed);

        // --- HUD ---
        const float scroll = ego.position() * layout::kPixelsPerMeter;
        hud.score = scoreOf(ego);
        hud.best = best;
        hud.speedKmh = static_cast<int>(ego.speed() * 3.6f);   // m/s -> km/h

        AssistView& accLamp = hud.assists[0];
        switch (acc.state()) {
            case AccState::Active:  accLamp.lamp = AssistLamp::Active;    break;
            case AccState::Standby: accLamp.lamp = AssistLamp::Degraded;  break;   // on, waiting to resume
            case AccState::Off:     accLamp.lamp = AssistLamp::Off;       break;
        }
        accLamp.level = 1.0f;

        AssistView& aebLamp = hud.assists[1];   // green = armed, amber = warning, red = braking
        switch (aeb.stage()) {
            case AebStage::Inactive:     aebLamp.lamp = AssistLamp::Active;       break;
            case AebStage::Warning:      aebLamp.lamp = AssistLamp::Degraded;     break;
            case AebStage::PartialBrake:
            case AebStage::FullBrake:    aebLamp.lamp = AssistLamp::Intervening;  break;
        }
        aebLamp.level = 1.0f;

        AssistView& lkaLamp = hud.assists[2];   // amber while the driver steers over it
        switch (lka.state()) {
            case LkaState::Active:     lkaLamp.lamp = AssistLamp::Active;    break;
            case LkaState::Overridden: lkaLamp.lamp = AssistLamp::Degraded;  break;
            case LkaState::Off:        lkaLamp.lamp = AssistLamp::Off;       break;
        }
        lkaLamp.level = 1.0f;
        hud.wind = screen == Screen::Playing ? wind.current() : 0.0f;

        // --- Render: back to front ---
        gfx.drawRoad(scroll);

        for (const Vehicle& car : traffic.cars()) {
            // Traffic is drawn relative to the ego: 1 m ahead = kPixelsPerMeter px higher on screen.
            const float x = (car.lateral() + 0.5f) * cfg::kLaneWidth;
            const float y = layout::kEgoScreenY - (car.position() - ego.position()) * layout::kPixelsPerMeter;

            // One variant number encodes both colour and body style.
            const auto variant = static_cast<std::size_t>(car.variant());
            gfx.drawCar(x, y, kNpcSkins[variant % kNpcSkins.size()], variant / kNpcSkins.size());
        }

        if (screen == Screen::Menu) {
            gfx.drawMenu(menu);
        } else {
            const float egoX = (ego.lateral() + 0.5f) * cfg::kLaneWidth;
            gfx.drawCar(egoX, layout::kEgoScreenY, CarSkin::Blue);

            // Debug view of what the sensors detect: 
            //       - a line from the ego's nose to the lead car, with gap and relative speed.
            if (showSensorDebug) {
                if (const std::optional<Lead> lead = findLead(ego, traffic)) {
                    gfx.drawLeadMarker(egoX, lead->gap, lead->relativeSpeed);
                }
                if (lka.state() == LkaState::Active) {
                    gfx.drawLaneTarget((static_cast<float>(lka.targetLane()) + 0.5f) * cfg::kLaneWidth);
                }
            }

            if (screen == Screen::Playing && aeb.stage() != AebStage::Inactive) {
                gfx.drawAlertBorder(isBraking(aeb.stage()));   // red while braking, amber when warning
            }

            gfx.drawPanel(hud);
        }
        if (screen == Screen::GameOver) {
            gfx.drawGameOver(lastScore, best, newBest);
        }

        SDL_RenderPresent(renderer.get());
    }

    return 0;
}
