#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <SDL_image.h>
#include "audio/Audio.hpp"
#include "control/Arbiter.hpp"
#include "core/Config.hpp"
#include "functions/Acc.hpp"
#include "functions/Aeb.hpp"
#include "functions/Bsw.hpp"
#include "functions/Lka.hpp"
#include "game/CrashReport.hpp"
#include "game/NearMiss.hpp"
#include "game/Progress.hpp"
#include "platform/SdlContext.hpp"
#include "platform/Window.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"
#include "perception/Fusion.hpp"
#include "sensors/Camera.hpp"
#include "sensors/Radar.hpp"
#include "sensors/Sensor.hpp"
#include "world/Collision.hpp"
#include "world/Lead.hpp"
#include "world/Traffic.hpp"
#include "world/Vehicle.hpp"
#include "world/Hazards.hpp"
#include "world/Obstacle.hpp"
#include "world/Oil.hpp"
#include "world/PowerUps.hpp"
#include "world/Wind.hpp"

// ASSET_DIR is injected by CMake; wrap the macro in a typed constant once and use that.
constexpr const char* kAssetDir = ASSET_DIR;

enum class Screen : std::uint8_t { Menu, Garage, Shop, Guide, Playing, GameOver };

// Menu entries, in the order of MenuView::items.
constexpr std::size_t kMenuStart = 0;
constexpr std::size_t kMenuGarage = 1;
constexpr std::size_t kMenuShop = 2;
constexpr std::size_t kMenuAssists = 3;
constexpr std::size_t kMenuQuit = 4;
static_assert(kMenuQuit < MenuView{}.items.size(), "Menu indices out of range");

constexpr float kStartSpeed = 20.0f;   // m/s
constexpr float kMenuSpeed = 22.0f;    // m/s, camera speed behind the title menu
constexpr float kTopSpeed = 40.0f;     // m/s (144 km/h); throttle fades out towards it, also scales the engine sound
constexpr float kMaxThrottle = 4.0f;   // m/s^2 at low speed
constexpr int kCutOffPenalty = 250;    // points lost each time a driver has to brake hard because of you
constexpr float kPenaltyFlashTime = 1.2f;   // s the score flashes red
constexpr float kBrakeLightDecel = 1.5f;    // m/s^2; NPCs braking harder than this show brake lights
constexpr float kGhostMinDistance = 8.0f;    // m; the radar sees a bridge ghost from this close...
constexpr float kGhostMaxDistance = 50.0f;   // ...out to this far
constexpr std::size_t kStartTraffic = 5;     // vehicles on the road at the start of a run...
constexpr float kMetresPerExtraCar = 600.0f; // ...one more every 600 m, up to Traffic::kMaxCars
constexpr int kMaxAebSaves = 3;
constexpr float kShieldTime = 2.0f;         // s of invulnerability after a shield absorbs a crash
constexpr float kRainGrip = 0.6f;           // wet road: steering responds less
constexpr float kPickupHintRange = 70.0f;   // m: pick-ups this far ahead are announced at the top edge
constexpr float kAccAutoResume = 0.8f;      // s after releasing the brake before ACC takes over again
constexpr float kPickupNameTime = 2.5f;     // s the full name of a collected power-up stays in the panel

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

    // --- Sensors: created once at startup, then only used through the Sensor interface ---
    // unique_ptr<Sensor> owns a Radar or a Camera; destroying it through the base pointer is why ~Sensor is virtual.
    constexpr std::size_t kFrontRadar = 0;
    constexpr std::size_t kRearLeftRadar = 1;
    constexpr std::size_t kRearRightRadar = 2;
    constexpr std::size_t kFrontCamera = 3;
    const std::array<std::unique_ptr<Sensor>, 4> sensors{
        std::make_unique<Radar>("FRONT RADAR", radar_zones::kFront, true),
        std::make_unique<Radar>("REAR LEFT RADAR", radar_zones::kRearLeft),
        std::make_unique<Radar>("REAR RIGHT RADAR", radar_zones::kRearRight),
        std::make_unique<Camera>("FRONT CAMERA", camera_zones::kFront),
    };
    ObjectList frontObjects;   // what ACC and AEB work with: raw front radar, or the fused list when fusion is on
    bool fusionOn = false;     // F key

    // --- Game state ---
    Screen screen = Screen::Menu;
    Vehicle ego(1, kMenuSpeed);
    Traffic traffic;
    Acc acc;   // adaptive cruise control
    Aeb aeb;   // automatic emergency braking, always on
    Lka lka;   // lane keeping assist
    Bsw bsw;   // blind spot warning
    RoadHazards hazards;   // fog banks and metal bridges
    Wind wind;
    OilField oil;
    MenuView menu;
    HudView hud;
    // Progress (best score, coins, upgrades) lives in a small file in the user's settings folder.
    std::string savePath = "lanerunner-save.txt";
    if (char* prefDir = SDL_GetPrefPath("LaneRunner", "LaneRunner")) {
        savePath = std::string(prefDir) + "save.txt";
        SDL_free(prefDir);
    }
    Progress progress;
    progress.load(savePath);
    menu.best = progress.best;
    menu.coins = progress.coins;
    std::size_t shopSelected = 0;
    std::size_t garageSelected = 0;
    int shields = 0;              // crashes the player can still survive this run
    float invulnerable = 0.0f;    // s left after a shield took a hit
    const char* pickupName = nullptr;
    float pickupNameTime = 0.0f;
    float accResumeTimer = 0.0f;

    int lastScore = 0;
    bool newBest = false;
    int coinsEarned = 0;
    CrashReport lastCrash{nullptr, nullptr};
    int bonus = 0;                // near-miss points this run

    // Power-ups: seconds of charge left per assist; AEB works on saves instead.
    PowerUpField powerUps;
    std::array<float, 4> charge{};   // indexed by PowerUp::Acc, Lka, Bsw, Fusion
    const auto chargeOf = [&](PowerUp p) -> float& { return charge[static_cast<std::size_t>(p)]; };
    int aebSaves = 0;
    NearMissTracker<Traffic::kMaxCars> nearMiss;
    int penalty = 0;              // points lost for cutting other drivers off
    float penaltyFlash = 0.0f;    // s left of the red score flash

    bool showSensorDebug = false;   // Tab: engineer's view of what the sensors detect

    // AEB feedback: sounds play on stage changes, the warning chime repeats while it lasts.
    AebStage lastAebStage = AebStage::Inactive;
    float warningChimeTimer = 0.0f;   // s until the next warning chime
    constexpr float kWarningChimeInterval = 0.5f;
    const auto isBraking = [](AebStage s) { return s == AebStage::PartialBrake || s == AebStage::FullBrake; };
    BswState lastBswState = BswState::Off;

    const auto scoreOf = [&](const Vehicle& v) {
        return std::max(0, static_cast<int>(v.position() * layout::kPixelsPerMeter / 10.0f) + bonus - penalty);
    };

    // Switch an assist off when its charge runs out (a fresh object is the "Off" state).
    const auto dischargeAssist = [&](PowerUp p) {
        switch (p) {
            case PowerUp::Acc:     acc = Acc();       break;
            case PowerUp::Lka:     lka = Lka();       break;
            case PowerUp::Bsw:     bsw = Bsw();       break;
            case PowerUp::Fusion:  fusionOn = false;  break;
            case PowerUp::AebSave:                    break;
        }
    };

    // Collecting a power-up: refill its charge and switch it on.
    const auto chargeAssist = [&](PowerUp p) {
        if (p == PowerUp::AebSave) {
            aebSaves = std::min(aebSaves + 1, kMaxAebSaves);
            return;
        }
        chargeOf(p) = progress.powerUpSeconds();
        switch (p) {
            case PowerUp::Acc:     if (acc.state() == AccState::Off) { acc.toggle(ego.speed()); }  break;
            case PowerUp::Lka:     if (lka.state() == LkaState::Off) { lka.toggle(); }             break;
            case PowerUp::Bsw:     if (bsw.state() == BswState::Off) { bsw.toggle(); }             break;
            case PowerUp::Fusion:  fusionOn = true;                                                break;
            case PowerUp::AebSave:                                                                  break;
        }
    };

    // Every run gets its own seed, so traffic, hazards and power-ups are different each time.
    // (Tests keep the default seeds, so their scenarios stay reproducible.)
    std::random_device entropy;
    const auto startRun = [&]() {
        const std::uint32_t seed = entropy();
        ego = Vehicle(1, kStartSpeed);
        ego.enableFreeSteering();
        traffic = Traffic(seed);
        acc = Acc();
        aeb = Aeb();
        lka = Lka();
        bsw = Bsw();
        lastBswState = BswState::Off;
        hazards = RoadHazards(seed + 1);
        fusionOn = false;
        penalty = 0;
        penaltyFlash = 0.0f;
        bonus = 0;
        nearMiss = NearMissTracker<Traffic::kMaxCars>();
        powerUps = PowerUpField(seed + 2);
        charge = {};
        aebSaves = progress.startingAebSaves();
        ego.setDimensions(progress.car().length, progress.car().width);
        shields = progress.shieldsPerRun();
        invulnerable = 0.0f;
        pickupName = nullptr;
        traffic.setActiveCount(kStartTraffic);
        if (progress.startsWithAcc()) {
            chargeAssist(PowerUp::Acc);
        }
        wind = Wind(seed + 3);
        oil = OilField(seed + 4);
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
        bsw = Bsw();
        menu.best = progress.best;
        menu.coins = progress.coins;
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
                        } else if (menu.selected == kMenuShop) {
                            shopSelected = 0;
                            screen = Screen::Shop;
                            audio.play(Sfx::MenuSelect);
                        } else if (menu.selected == kMenuGarage) {
                            garageSelected = static_cast<std::size_t>(progress.selectedCar);
                            screen = Screen::Garage;
                            audio.play(Sfx::MenuSelect);
                        } else if (menu.selected == kMenuAssists) {
                            screen = Screen::Guide;
                            audio.play(Sfx::MenuSelect);
                        } else if (menu.selected == kMenuQuit) {
                            running = false;
                        }
                    } else if (key == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
                }
                case Screen::Garage: {
                    const std::size_t count = kCars.size();
                    if (key == SDLK_LEFT) {
                        garageSelected = (garageSelected + count - 1) % count;
                        audio.play(Sfx::MenuMove);
                    } else if (key == SDLK_RIGHT) {
                        garageSelected = (garageSelected + 1) % count;
                        audio.play(Sfx::MenuMove);
                    } else if (key == SDLK_ESCAPE) {
                        backToMenu();
                    } else if (confirm) {
                        if (progress.chooseCar(garageSelected)) {   // buys it if needed, then drives it
                            progress.save(savePath);
                            audio.play(Sfx::MenuSelect);
                        } else {
                            audio.play(Sfx::Denied);
                        }
                    }
                    break;
                }
                case Screen::Guide:
                    if (key == SDLK_ESCAPE || confirm) {
                        backToMenu();
                    }
                    break;
                case Screen::Shop: {
                    const std::size_t rows = static_cast<std::size_t>(ShopItem::Count) + 1;   // + BACK
                    if (key == SDLK_UP) {
                        shopSelected = (shopSelected + rows - 1) % rows;
                        audio.play(Sfx::MenuMove);
                    } else if (key == SDLK_DOWN) {
                        shopSelected = (shopSelected + 1) % rows;
                        audio.play(Sfx::MenuMove);
                    } else if (key == SDLK_ESCAPE || (confirm && shopSelected == rows - 1)) {
                        backToMenu();
                    } else if (confirm) {
                        if (progress.buy(static_cast<ShopItem>(shopSelected))) {
                            progress.save(savePath);
                            audio.play(Sfx::MenuSelect);
                        } else {
                            audio.play(Sfx::Denied);   // maxed out or not enough coins
                        }
                    }
                    break;
                }
                case Screen::Playing: {
                    // Steering is held, so it's read as keyboard state in the logic loop, not here.
                    // Assist keys only work while that assist has charge from a power-up.
                    const auto toggleIfCharged = [&](PowerUp p, auto&& toggle) {
                        if (chargeOf(p) > 0.0f) {
                            toggle();
                        } else {
                            audio.play(Sfx::Denied);
                        }
                    };
                    if (key == SDLK_ESCAPE) {
                        backToMenu();
                    } else if (key == SDLK_TAB) {
                        showSensorDebug = !showSensorDebug;
                    } else if (key == SDLK_a) {
                        toggleIfCharged(PowerUp::Acc, [&] { acc.toggle(ego.speed()); });
                    } else if (key == SDLK_l) {
                        toggleIfCharged(PowerUp::Lka, [&] { lka.toggle(); });
                    } else if (key == SDLK_f) {
                        toggleIfCharged(PowerUp::Fusion, [&] { fusionOn = !fusionOn; });
                    } else if (key == SDLK_b) {
                        toggleIfCharged(PowerUp::Bsw, [&] { bsw.toggle(); });
                    }
                    break;
                }
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
            // Static obstacles (roadworks) for this cycle: traffic, sensors and collisions all use the same list.
            if (screen == Screen::Playing) {
                hazards.step(ego);
            }
            const RoadHazards::Obstacles obstacles = screen == Screen::Playing ? hazards.obstacles() : RoadHazards::Obstacles{};
            const Span<StaticObstacle> obstacleView(obstacles.begin(), obstacles.size());

            if (screen == Screen::Playing) {
                // Power-ups: collect, and drain the charge of active assists.
                if (const std::optional<PowerUp> got = powerUps.step(ego, cfg::kCycleTime, progress.hasMagnet(), obstacleView)) {
                    chargeAssist(*got);
                    audio.play(Sfx::PowerUp);
                    pickupName = *got == PowerUp::Acc      ? "+ CRUISE CONTROL"
                                 : *got == PowerUp::Lka    ? "+ LANE KEEPING"
                                 : *got == PowerUp::Bsw    ? "+ BLIND SPOT WARNING"
                                 : *got == PowerUp::Fusion ? "+ SENSOR FUSION"
                                                           : "+ 1 AEB SAVE";
                    pickupNameTime = kPickupNameTime;
                }
                pickupNameTime = std::max(0.0f, pickupNameTime - cfg::kCycleTime);
                invulnerable = std::max(0.0f, invulnerable - cfg::kCycleTime);
                for (PowerUp p : {PowerUp::Acc, PowerUp::Lka, PowerUp::Bsw, PowerUp::Fusion}) {
                    float& c = chargeOf(p);
                    if (c > 0.0f) {
                        c -= cfg::kCycleTime;
                        if (c <= 0.0f) {
                            c = 0.0f;
                            dischargeAssist(p);
                        }
                    }
                }
                // Difficulty: more traffic the further you get.
                traffic.setActiveCount(kStartTraffic + static_cast<std::size_t>(ego.position() / kMetresPerExtraCar));

                // Perception: every sensor refreshes its object list through the same virtual call.
                SensorEnvironment env;
                env.obstacles = obstacleView;
                env.rain = hazards.inRain(ego.position());
                env.glare = hazards.cameraGlare(ego.position());
                env.fog = hazards.inFog(ego.position());
                env.metalBridgeAhead = hazards.bridgeAhead(ego, kGhostMinDistance, kGhostMaxDistance);
                for (const std::unique_ptr<Sensor>& sensor : sensors) {
                    sensor->update(ego, traffic.cars(), env);
                }
                // Without fusion, ACC and AEB trust the radar alone, ghosts included (phantom braking).
                frontObjects = fusionOn ? fusion::fuse(*sensors[kFrontRadar], *sensors[kFrontCamera])
                                        : sensors[kFrontRadar]->objects();
                const std::optional<Lead> lead = selectLead(frontObjects);   // perceive once, use everywhere

                // Every source puts its request in; the arbiter alone decides what reaches the car.
                AccelRequests requests;

                // Held keys act every cycle, so they are read as state rather than events.
                const Uint8* keys = SDL_GetKeyboardState(nullptr);
                if (keys[SDL_SCANCODE_DOWN]) {
                    requests.driverBrake = -8.0f;
                    acc.cancel();                 // the driver's brake cancels ACC
                } else if (keys[SDL_SCANCODE_UP]) {
                    // Full throttle fades out near top speed, like air drag catching up with the engine.
                    const float topSpeed = kTopSpeed + progress.car().topSpeedBonus;
                    const float headroom = 1.0f - (ego.speed() / topSpeed) * (ego.speed() / topSpeed);
                    requests.driverThrottle = kMaxThrottle * progress.car().accelScale * std::max(headroom, 0.0f);
                }
                // No buttons needed on a handheld: after braking, ACC resumes by itself once the brake
                // has been released for a moment (and the driver isn't accelerating).
                if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_UP]) {
                    accResumeTimer = 0.0f;
                } else if (acc.state() == AccState::Standby) {
                    accResumeTimer += cfg::kCycleTime;
                    if (accResumeTimer >= kAccAutoResume) {
                        acc.toggle(ego.speed());   // Standby -> Active, back to the old set speed
                        accResumeTimer = 0.0f;
                    }
                }
                if (acc.state() == AccState::Active) {
                    requests.acc = acc.step(cfg::kCycleTime, ego.speed(), lead);
                }
                // AEB runs while it has saves (and finishes a braking it already started).
                if (aebSaves > 0 || isBraking(aeb.stage())) {
                    requests.aeb = aeb.step(lead);
                }

                ego.setAcceleration(arbitrate(requests).accel);

                // AEB sounds: squeal when braking starts, repeating chime while warning.
                const AebStage stage = aeb.stage();
                if (isBraking(stage) && !isBraking(lastAebStage)) {
                    audio.play(Sfx::EmergencyBrake);
                    aebSaves = std::max(aebSaves - 1, 0);   // each emergency braking uses one save
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
                    const float steerRate = progress.car().steerRate;   // always stronger than LKA's 1.2
                    driverSteer = left ? -steerRate : steerRate;
                }
                // BSW removes steering towards an occupied blind spot; LKA then sees "no driver" and re-centres.
                const std::optional<float> allowedSteer = bsw.step(sensors[kRearLeftRadar]->objects(),
                                                                   sensors[kRearRightRadar]->objects(), driverSteer);
                if (bsw.state() == BswState::Intervening && lastBswState != BswState::Intervening) {
                    audio.play(Sfx::CollisionWarning);
                }
                lastBswState = bsw.state();

                std::optional<float> lkaSteer;
                // LKA needs the camera to see the lane markings.
                const float lkaCommand = lka.step(cfg::kCycleTime, ego.lateral(), allowedSteer.has_value(),
                                                  sensors[kFrontCamera]->available());
                if (lka.state() == LkaState::Active) {
                    lkaSteer = lkaCommand;
                }
                ego.steer(arbitrateSteer(allowedSteer, lkaSteer).rate);
                // Road surface: oil takes away grip (for the driver and LKA alike) and makes the car slide.
                const GripEffect surface = oil.step(cfg::kCycleTime, ego);
                if (surface.justHit) {
                    audio.play(Sfx::Skid);
                }
                ego.setGrip(std::min(surface.grip, hazards.inRain(ego.position()) ? kRainGrip : 1.0f));
                // The wind keeps changing in a tunnel, it just can't reach the car there.
                const float gust = wind.step(cfg::kCycleTime);
                ego.push((hazards.inTunnel(ego.position()) ? 0.0f : gust) + surface.slide);
            } else {
                ego.setAcceleration(0.0f);   // menu: an invisible camera cruises through traffic
            }

            const int laneBefore = ego.lane();
            ego.step(cfg::kCycleTime);
            if (screen == Screen::Playing && ego.lane() != laneBefore) {
                audio.play(Sfx::LaneChange);   // whoosh when crossing a lane line
            }
            traffic.step(cfg::kCycleTime, ego, screen == Screen::Playing, obstacleView);   // title screen: NPCs ignore the camera
            accumulator -= cfg::kCycleTime;

            // Cutting someone off: they honk, you lose points.
            const int cutOffs = traffic.takeCutOffs();
            if (cutOffs > 0 && screen == Screen::Playing) {
                audio.play(Sfx::Horn);
                penalty += kCutOffPenalty * cutOffs;
                penaltyFlash = kPenaltyFlashTime;
            }
            penaltyFlash = std::max(0.0f, penaltyFlash - cfg::kCycleTime);

            if (screen != Screen::Playing) {
                continue;
            }
            const int nearMissPoints = nearMiss.update(cfg::kCycleTime, ego, traffic.cars());
            if (nearMissPoints > 0) {
                bonus += nearMissPoints;
                audio.play(Sfx::NearMiss);
            }
            // Crash: into a vehicle (from any side), an obstacle, or the road-side barrier.
            const std::optional<CrashReport> crash = describeCrash(ego, traffic.cars(), obstacleView);
            if (crash && invulnerable <= 0.0f && shields > 0) {
                --shields;                    // the shield takes the hit
                invulnerable = kShieldTime;
                audio.play(Sfx::Crash);
            } else if (crash && invulnerable <= 0.0f) {
                audio.play(Sfx::Crash);
                lastCrash = *crash;
                lastScore = scoreOf(ego);
                newBest = lastScore > progress.best;
                coinsEarned = progress.finishRun(lastScore);
                progress.save(savePath);
                screen = Screen::GameOver;
                accumulator = 0.0f;
            }
        }

        audio.setEngine(screen == Screen::Playing, ego.speed() / kTopSpeed);

        // --- HUD ---
        const float scroll = ego.position() * layout::kPixelsPerMeter;
        hud.score = scoreOf(ego);
        hud.scorePenalty = penaltyFlash > 0.0f;
        hud.best = progress.best;
        hud.combo = nearMiss.comboActive() ? nearMiss.combo() : 0;
        hud.shields = screen == Screen::Playing ? shields : 0;
        hud.pickupName = pickupNameTime > 0.0f ? pickupName : nullptr;
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
            case LkaState::Degraded:   lkaLamp.lamp = AssistLamp::Degraded;  break;   // can't see the lanes
            case LkaState::Off:        lkaLamp.lamp = AssistLamp::Off;       break;
        }
        lkaLamp.level = 1.0f;

        AssistView& bswLamp = hud.assists[3];   // amber = car in a blind spot, red = steering blocked
        switch (bsw.state()) {
            case BswState::Clear:       bswLamp.lamp = AssistLamp::Active;       break;
            case BswState::Warning:     bswLamp.lamp = AssistLamp::Degraded;     break;
            case BswState::Intervening: bswLamp.lamp = AssistLamp::Intervening;  break;
            case BswState::Off:         bswLamp.lamp = AssistLamp::Off;          break;
        }
        bswLamp.level = 1.0f;
        const bool windBlows = screen == Screen::Playing && !hazards.inTunnel(ego.position());
        hud.wind = windBlows ? wind.current() : 0.0f;
        hud.cameraOk = screen != Screen::Playing || sensors[kFrontCamera]->available();
        hud.cameraText = nullptr;
        if (screen == Screen::Playing) {
            hud.cameraText = hazards.inFog(ego.position())         ? "BLIND"
                             : hazards.cameraGlare(ego.position()) ? "GLARE"
                             : hazards.inRain(ego.position())      ? "RAIN"
                                                                    : nullptr;
        }

        AssistView& fusionLamp = hud.assists[4];   // amber = camera blind, fusion runs on radar alone
        fusionLamp.lamp = !fusionOn ? AssistLamp::Off
                                    : (sensors[kFrontCamera]->available() ? AssistLamp::Active : AssistLamp::Degraded);
        fusionLamp.level = 1.0f;

        // Uncharged assists are locked (grey); the bar shows the charge left, or AEB's saves.
        const float fullCharge = progress.powerUpSeconds();
        const std::array<std::pair<std::size_t, PowerUp>, 4> chargedLamps{
            {{0, PowerUp::Acc}, {2, PowerUp::Lka}, {3, PowerUp::Bsw}, {4, PowerUp::Fusion}}};
        for (const auto& [lampIndex, p] : chargedLamps) {
            if (chargeOf(p) <= 0.0f) {
                hud.assists[lampIndex].lamp = AssistLamp::Locked;
            }
            hud.assists[lampIndex].level = chargeOf(p) / fullCharge;
        }
        if (aebSaves == 0 && !isBraking(aeb.stage())) {
            aebLamp.lamp = AssistLamp::Locked;
        }
        aebLamp.level = static_cast<float>(aebSaves) / static_cast<float>(kMaxAebSaves);

        // --- Render: back to front ---
        gfx.drawRoad(scroll);

        if (screen == Screen::Playing || screen == Screen::GameOver) {
            for (const PowerUpField::Pickup& p : powerUps.pickups()) {
                const float y = layout::kEgoScreenY - (p.position - ego.position()) * layout::kPixelsPerMeter;
                const float ahead = p.position - ego.position();
                const bool onScreen = y > -40.0f && y < static_cast<float>(cfg::kScreenHeight) + 40.0f;
                if (onScreen || (ahead > 0.0f && ahead < kPickupHintRange)) {
                    struct Look { const char* label; SDL_Color colour; };
                    const Look look = p.type == PowerUp::Acc      ? Look{"ACC", SDL_Color{46, 204, 113, 255}}
                                      : p.type == PowerUp::Lka    ? Look{"LKA", SDL_Color{60, 170, 240, 255}}
                                      : p.type == PowerUp::Bsw    ? Look{"BSW", SDL_Color{243, 156, 18, 255}}
                                      : p.type == PowerUp::Fusion ? Look{"FUS", SDL_Color{170, 100, 230, 255}}
                                                                  : Look{"AEB", SDL_Color{230, 70, 60, 255}};
                    if (y < -20.0f) {   // still off-screen: a small badge at the top edge above its lane
                        gfx.drawPickupHint((p.lateral + 0.5f) * cfg::kLaneWidth, look.label, look.colour);
                    } else {
                        gfx.drawPickup((p.lateral + 0.5f) * cfg::kLaneWidth, y, look.label, look.colour);
                    }
                }
            }
        }

        const bool inMenus = screen == Screen::Menu || screen == Screen::Shop || screen == Screen::Garage || screen == Screen::Guide;
        if (!inMenus) {
            const auto toScreenY = [&](float position) {
                return layout::kEgoScreenY - (position - ego.position()) * layout::kPixelsPerMeter;
            };
            for (const RoadHazards::Debris& d : hazards.debris()) {
                if (!RoadHazards::kDebrisEnabled) {
                    break;
                }
                gfx.drawDebris((d.lateral + 0.5f) * cfg::kLaneWidth, toScreenY(d.position), static_cast<int>(d.kind),
                                   static_cast<unsigned>(d.position));
            }
            for (const RoadHazards::Accident& a : hazards.accidents()) {
                gfx.drawWreck((static_cast<float>(a.lane) + 0.5f) * cfg::kLaneWidth, toScreenY(a.position),
                              RoadHazards::kAccidentLength);
            }
            for (const RoadHazards::Roadworks& r : hazards.roadworks()) {
                gfx.drawRoadworks((static_cast<float>(r.lane) + 0.5f) * cfg::kLaneWidth,
                                  layout::kEgoScreenY - (r.start - ego.position()) * layout::kPixelsPerMeter,
                                  layout::kEgoScreenY - (r.end - ego.position()) * layout::kPixelsPerMeter, r.mergeDir);
            }
            for (const OilField::Patch& p : oil.patches()) {
                const float y = layout::kEgoScreenY - (p.position - ego.position()) * layout::kPixelsPerMeter;
                if (y > -60.0f && y < static_cast<float>(cfg::kScreenHeight) + 60.0f) {
                    gfx.drawOil((p.lateral + 0.5f) * cfg::kLaneWidth, y);
                }
            }
        }

        for (std::size_t i = 0; i < Traffic::kMaxCars; ++i) {
            const Vehicle& car = traffic.cars()[i];
            // Traffic is drawn relative to the ego: 1 m ahead = kPixelsPerMeter px higher on screen.
            const float x = (car.lateral() + 0.5f) * cfg::kLaneWidth;
            const float y = layout::kEgoScreenY - (car.position() - ego.position()) * layout::kPixelsPerMeter;

            // One variant number encodes both colour and body style (cars) or just the colour (motorcycles).
            const auto variant = static_cast<std::size_t>(car.variant());
            const DriverState& driver = traffic.driver(i);
            const bool braking = driver.accel < -kBrakeLightDecel;
            if (car.type() == VehicleType::Motorcycle) {
                gfx.drawMotorcycle(x, y, kNpcSkins[variant % kNpcSkins.size()], driver.signal, braking);
            } else {
                gfx.drawCar(x, y, kNpcSkins[variant % kNpcSkins.size()], variant / kNpcSkins.size(), driver.signal, braking);
            }
        }

        if (screen == Screen::Menu) {
            gfx.drawMenu(menu);
        } else if (screen == Screen::Garage) {
            GarageView view;
            for (std::size_t i = 0; i < kCars.size(); ++i) {
                const CarOffer& car = kCars[i];
                view.cars[i] = GarageCarView{car.name, car.strength, car.weakness, car.spriteModel, car.price,
                                             progress.ownsCar(i), static_cast<int>(i) == progress.selectedCar,
                                             progress.coins >= car.price};
            }
            view.selected = garageSelected;
            view.coins = progress.coins;
            gfx.drawGarage(view);
        } else if (screen == Screen::Guide) {
            gfx.drawAssistGuide();
        } else if (screen == Screen::Shop) {
            ShopView view;
            for (std::size_t i = 0; i < view.rows.size(); ++i) {
                const auto item = static_cast<ShopItem>(i);
                const ShopOffer& offer = kShopOffers[i];
                const int price = progress.nextPrice(item);
                view.rows[i] = ShopRowView{offer.name, offer.description, progress.level(item), offer.maxLevel,
                                           price, price > 0 && progress.coins >= price};
            }
            view.selected = shopSelected;
            view.coins = progress.coins;
            gfx.drawShop(view);
        } else {
            const float egoX = (ego.lateral() + 0.5f) * cfg::kLaneWidth;
            const bool blinkOff = invulnerable > 0.0f && (SDL_GetTicks() / 100) % 2 == 0;   // shield just saved you
            if (!blinkOff) {
                gfx.drawCar(egoX, layout::kEgoScreenY, CarSkin::Blue, static_cast<std::size_t>(progress.car().spriteModel));
            }

            // Bridges pass over the cars; fog hides everything inside it, the player's view included.
            const auto screenY = [&](float position) {
                return layout::kEgoScreenY - (position - ego.position()) * layout::kPixelsPerMeter;
            };
            for (const RoadHazards::Zone& z : hazards.tunnels()) {
                gfx.drawTunnel(screenY(z.end), screenY(z.start));
            }
            for (const RoadHazards::Zone& z : hazards.rain()) {
                gfx.drawRain(screenY(z.end), screenY(z.start));
            }
            for (const float b : hazards.bridges()) {
                gfx.drawBridge(layout::kEgoScreenY - (b - ego.position()) * layout::kPixelsPerMeter);
            }
            for (const RoadHazards::FogBank& f : hazards.fog()) {
                gfx.drawFog(layout::kEgoScreenY - (f.end - ego.position()) * layout::kPixelsPerMeter,
                            layout::kEgoScreenY - (f.start - ego.position()) * layout::kPixelsPerMeter);
            }

            // Debug view of what the sensors detect: each radar's zone, a bracket on every object it
            // reports, and the lead ACC/AEB use (line with gap and relative speed).
            if (showSensorDebug && screen == Screen::Playing) {
                for (std::size_t s = 0; s < sensors.size(); ++s) {
                    const DetectionZone& zone = sensors[s]->fieldOfView();
                    const SDL_Color c = s == kFrontRadar    ? SDL_Color{60, 230, 140, 255}    // green
                                        : s == kFrontCamera ? SDL_Color{80, 200, 255, 255}    // cyan
                                                            : SDL_Color{255, 176, 32, 255};   // amber
                    gfx.drawSensorZone(egoX, zone.minLongitudinal, zone.maxLongitudinal, zone.minLateral, zone.maxLateral, c);
                    for (const DetectedObject& o : sensors[s]->objects()) {
                        gfx.drawDetection(egoX + o.lateral * cfg::kLaneWidth,
                                          layout::kEgoScreenY - o.longitudinal * layout::kPixelsPerMeter, c);
                    }
                }
                if (const std::optional<Lead> lead = selectLead(frontObjects)) {
                    gfx.drawLeadMarker(egoX, lead->gap, lead->relativeSpeed);
                }
                if (lka.state() == LkaState::Active) {
                    gfx.drawLaneTarget((static_cast<float>(lka.targetLane()) + 0.5f) * cfg::kLaneWidth);
                }
            }

            if (screen == Screen::Playing) {
                gfx.drawWind(windBlows ? wind.current() : 0.0f, scroll);
            }

            if (screen == Screen::Playing && bsw.state() != BswState::Off) {
                gfx.drawBlindSpotWarning(egoX, bsw.leftOccupied(), bsw.rightOccupied(),
                                         bsw.state() == BswState::Intervening);
            }

            if (screen == Screen::Playing && aeb.stage() != AebStage::Inactive) {
                gfx.drawAlertBorder(isBraking(aeb.stage()));   // red while braking, amber when warning
            }

            gfx.drawPanel(hud);
        }
        if (screen == Screen::GameOver) {
            gfx.drawGameOver(lastScore, progress.best, newBest, coinsEarned, lastCrash.what, lastCrash.tip);
        }

        SDL_RenderPresent(renderer.get());
    }

    return 0;
}
