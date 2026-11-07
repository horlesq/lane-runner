#include "app/Game.hpp"
#include <algorithm>
#include <optional>
#include <utility>
#include "control/Arbiter.hpp"
#include "core/Config.hpp"
#include "perception/Fusion.hpp"
#include "platform/Web.hpp"
#include "render/Layout.hpp"
#include "sensors/Camera.hpp"
#include "sensors/Radar.hpp"
#include "world/Collision.hpp"
#include "world/Lead.hpp"

namespace {

    // Menu entries, in the order of MenuView::items.
    constexpr std::size_t kMenuStart = 0;
    constexpr std::size_t kMenuGarage = 1;
    constexpr std::size_t kMenuShop = 2;
    constexpr std::size_t kMenuHowToPlay = 3;
    constexpr std::size_t kMenuAssists = 4;
    constexpr std::size_t kMenuQuit = 5;
    static_assert(kMenuAssists < MenuView{}.items.size(), "Menu indices out of range");
    static_assert(kIsWeb || kMenuQuit < MenuView{}.items.size(), "Menu indices out of range");   // no QUIT in a browser

    // Positions in Game::sensors_.
    constexpr std::size_t kFrontRadar = 0;
    constexpr std::size_t kRearLeftRadar = 1;
    constexpr std::size_t kRearRightRadar = 2;
    constexpr std::size_t kFrontCamera = 3;

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
    constexpr float kWarningChimeInterval = 0.5f;   // s between AEB warning chimes
    constexpr float kMaxBacklog = 0.25f;        // s; a long stall (e.g. dragging the window) can't trigger hundreds of steps

    constexpr std::array<CarSkin, 4> kNpcSkins{CarSkin::Red, CarSkin::Green, CarSkin::Yellow, CarSkin::Black};
    constexpr std::array<PowerUp, 4> kChargedAssists{PowerUp::Acc, PowerUp::Lka, PowerUp::Bsw, PowerUp::Fusion};

    bool isBraking(AebStage s) {
        return s == AebStage::PartialBrake || s == AebStage::FullBrake;
    }

    // Assist states as HUD lamp colours.
    AssistLamp lampOf(AccState s) {
        switch (s) {
            case AccState::Active:  return AssistLamp::Active;
            case AccState::Standby: return AssistLamp::Degraded;   // on, waiting to resume
            case AccState::Off:     return AssistLamp::Off;
        }
        return AssistLamp::Off;
    }

    AssistLamp lampOf(AebStage s) {   // green = armed, amber = warning, red = braking
        switch (s) {
            case AebStage::Inactive:     return AssistLamp::Active;
            case AebStage::Warning:      return AssistLamp::Degraded;
            case AebStage::PartialBrake:
            case AebStage::FullBrake:    return AssistLamp::Intervening;
        }
        return AssistLamp::Off;
    }

    AssistLamp lampOf(LkaState s) {   // amber while the driver steers over it or it can't see the lanes
        switch (s) {
            case LkaState::Active:     return AssistLamp::Active;
            case LkaState::Overridden: return AssistLamp::Degraded;
            case LkaState::Degraded:   return AssistLamp::Degraded;
            case LkaState::Off:        return AssistLamp::Off;
        }
        return AssistLamp::Off;
    }

    AssistLamp lampOf(BswState s) {   // amber = car in a blind spot, red = steering blocked
        switch (s) {
            case BswState::Clear:       return AssistLamp::Active;
            case BswState::Warning:     return AssistLamp::Degraded;
            case BswState::Intervening: return AssistLamp::Intervening;
            case BswState::Off:         return AssistLamp::Off;
        }
        return AssistLamp::Off;
    }

    SDL_Color colourOf(const PowerUpLook& look) {
        return SDL_Color{look.r, look.g, look.b, 255};
    }

} // namespace

Game::Game(Audio& audio, std::string savePath)
    : audio_(audio),
      savePath_(std::move(savePath)),
      sensors_{
          std::make_unique<Radar>("FRONT RADAR", radar_zones::kFront, true),
          std::make_unique<Radar>("REAR LEFT RADAR", radar_zones::kRearLeft),
          std::make_unique<Radar>("REAR RIGHT RADAR", radar_zones::kRearRight),
          std::make_unique<Camera>("FRONT CAMERA", camera_zones::kFront),
      },
      ego_(1, kMenuSpeed) {
    progress_.load(savePath_);
    menu_.best = progress_.best;
    menu_.coins = progress_.coins;
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void Game::onFocusLost() {
    // Alt+Tab or a click elsewhere: don't keep driving blind.
    if (screen_ == Screen::Playing && !paused_) {
        pause();
    }
}

void Game::handleKey(SDL_Keycode key) {
    const bool confirm = (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE);

    switch (screen_) {
        case Screen::Menu:
            handleMenuKey(key, confirm);
            break;
        case Screen::Garage:
            handleGarageKey(key, confirm);
            break;
        case Screen::Shop:
            handleShopKey(key, confirm);
            break;
        case Screen::HowToPlay:
            if (key == SDLK_ESCAPE) {
                backToMenu();
            } else if (confirm && rulesBeforeRun_) {
                startRun();
            }
            break;
        case Screen::Guide:
            if (key == SDLK_ESCAPE) {
                backToMenu();
            } else if (confirm) {
                debugMode_ = !debugMode_;
                audio_.play(Sfx::MenuSelect);
            }
            break;
        case Screen::Playing:
            handlePlayingKey(key, confirm);
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

void Game::handleMenuKey(SDL_Keycode key, bool confirm) {
    const std::size_t count = menu_.items.size();
    if (key == SDLK_UP) {
        menu_.selected = (menu_.selected + count - 1) % count;   // wrap around
        audio_.play(Sfx::MenuMove);
    } else if (key == SDLK_DOWN) {
        menu_.selected = (menu_.selected + 1) % count;
        audio_.play(Sfx::MenuMove);
    } else if (confirm) {
        if (!menu_.items[menu_.selected].enabled) {
            audio_.play(Sfx::Denied);
        } else if (menu_.selected == kMenuStart && progress_.best == 0 && progress_.coins == 0 && !rulesSeen_) {
            // Very first run on this save: explain the game before it starts.
            rulesBeforeRun_ = true;
            rulesSeen_ = true;
            screen_ = Screen::HowToPlay;
            audio_.play(Sfx::MenuSelect);
        } else if (menu_.selected == kMenuStart) {
            startRun();
        } else if (menu_.selected == kMenuShop) {
            shopSelected_ = 0;
            screen_ = Screen::Shop;
            audio_.play(Sfx::MenuSelect);
        } else if (menu_.selected == kMenuGarage) {
            garageSelected_ = static_cast<std::size_t>(progress_.selectedCar);
            screen_ = Screen::Garage;
            audio_.play(Sfx::MenuSelect);
        } else if (menu_.selected == kMenuHowToPlay) {
            rulesBeforeRun_ = false;
            rulesSeen_ = true;
            screen_ = Screen::HowToPlay;
            audio_.play(Sfx::MenuSelect);
        } else if (menu_.selected == kMenuAssists) {
            screen_ = Screen::Guide;
            audio_.play(Sfx::MenuSelect);
        } else if (menu_.selected == kMenuQuit) {
            quit_ = true;
        }
    } else if (key == SDLK_ESCAPE && !kIsWeb) {   // a browser tab is closed by the browser
        quit_ = true;
    }
}

void Game::handleGarageKey(SDL_Keycode key, bool confirm) {
    const std::size_t count = kCars.size();
    if (key == SDLK_LEFT) {
        garageSelected_ = (garageSelected_ + count - 1) % count;
        audio_.play(Sfx::MenuMove);
    } else if (key == SDLK_RIGHT) {
        garageSelected_ = (garageSelected_ + 1) % count;
        audio_.play(Sfx::MenuMove);
    } else if (key == SDLK_ESCAPE) {
        backToMenu();
    } else if (confirm) {
        if (progress_.chooseCar(garageSelected_)) {   // buys it if needed, then drives it
            saveProgress();
            audio_.play(Sfx::MenuSelect);
        } else {
            audio_.play(Sfx::Denied);
        }
    }
}

void Game::handleShopKey(SDL_Keycode key, bool confirm) {
    const std::size_t rows = static_cast<std::size_t>(ShopItem::Count) + 1;   // + BACK
    if (key == SDLK_UP) {
        shopSelected_ = (shopSelected_ + rows - 1) % rows;
        audio_.play(Sfx::MenuMove);
    } else if (key == SDLK_DOWN) {
        shopSelected_ = (shopSelected_ + 1) % rows;
        audio_.play(Sfx::MenuMove);
    } else if (key == SDLK_ESCAPE || (confirm && shopSelected_ == rows - 1)) {
        backToMenu();
    } else if (confirm) {
        if (progress_.buy(static_cast<ShopItem>(shopSelected_))) {
            saveProgress();
            audio_.play(Sfx::MenuSelect);
        } else {
            audio_.play(Sfx::Denied);   // maxed out or not enough coins
        }
    }
}

void Game::handlePlayingKey(SDL_Keycode key, bool confirm) {
    if (paused_) {
        if (key == SDLK_UP || key == SDLK_DOWN) {
            pauseSelected_ = 1 - pauseSelected_;   // two entries: up and down both switch
            audio_.play(Sfx::MenuMove);
        } else if (key == SDLK_ESCAPE || (confirm && pauseSelected_ == 0)) {
            paused_ = false;
            audio_.play(Sfx::MenuSelect);
        } else if (confirm) {
            backToMenu();
        }
        return;
    }
    // Steering is held, so it's read as keyboard state in the logic cycle, not here.
    // Assist keys only work while that assist has charge from a power-up.
    const auto toggleIfCharged = [&](PowerUp p, auto&& toggle) {
        if (chargeOf(p) > 0.0f) {
            toggle();
        } else {
            audio_.play(Sfx::Denied);
        }
    };
    if (key == SDLK_ESCAPE) {
        pause();
        audio_.play(Sfx::MenuSelect);
    } else if (key == SDLK_TAB) {
        showSensorDebug_ = !showSensorDebug_;
    } else if (key == SDLK_a) {
        toggleIfCharged(PowerUp::Acc, [&] { acc_.toggle(ego_.speed()); });
    } else if (key == SDLK_l) {
        toggleIfCharged(PowerUp::Lka, [&] { lka_.toggle(); });
    } else if (key == SDLK_f) {
        toggleIfCharged(PowerUp::Fusion, [&] { fusionOn_ = !fusionOn_; });
    } else if (key == SDLK_b) {
        toggleIfCharged(PowerUp::Bsw, [&] { bsw_.toggle(); });
    }
}

// ---------------------------------------------------------------------------------------------
// Screen changes
// ---------------------------------------------------------------------------------------------

// Every run gets its own seed, so traffic, hazards and power-ups are different each time.
// (Tests keep the default seeds, so their scenarios stay reproducible.)
void Game::startRun() {
    const std::uint32_t seed = entropy_();
    ego_ = Vehicle(1, kStartSpeed);
    ego_.enableFreeSteering();
    traffic_ = Traffic(seed);
    acc_ = Acc();
    aeb_ = Aeb();
    lka_ = Lka();
    bsw_ = Bsw();
    lastBswState_ = BswState::Off;
    hazards_ = RoadHazards(seed + 1);
    fusionOn_ = false;
    penalty_ = 0;
    penaltyFlash_ = 0.0f;
    bonus_ = 0;
    nearMiss_ = NearMissTracker<Traffic::kMaxCars>();
    powerUps_ = PowerUpField(seed + 2);
    charge_ = {};
    aebSaves_ = progress_.startingAebSaves();
    ego_.setDimensions(progress_.car().length, progress_.car().width);
    shields_ = progress_.shieldsPerRun();
    invulnerable_ = 0.0f;
    pickupName_ = nullptr;
    traffic_.setActiveCount(kStartTraffic);
    if (progress_.startsWithAcc()) {
        chargeAssist(PowerUp::Acc);
    }
    if (debugMode_) {
        for (PowerUp p : kChargedAssists) {
            chargeAssist(p);
        }
        aebSaves_ = kMaxAebSaves;
    }
    wind_ = Wind(seed + 3);
    oil_ = OilField(seed + 4);
    lastAebStage_ = AebStage::Inactive;
    paused_ = false;
    screen_ = Screen::Playing;
    audio_.play(Sfx::Start);
}

void Game::backToMenu() {
    ego_ = Vehicle(1, kMenuSpeed);
    traffic_ = Traffic();
    acc_ = Acc();
    aeb_ = Aeb();
    lka_ = Lka();
    bsw_ = Bsw();
    menu_.best = progress_.best;
    menu_.coins = progress_.coins;
    paused_ = false;
    screen_ = Screen::Menu;
}

void Game::pause() {
    paused_ = true;
    pauseSelected_ = 0;
}

void Game::saveProgress() {
    progress_.save(savePath_);
    webPersistSaves();   // browser: copy the file to IndexedDB (no-op elsewhere)
}

int Game::scoreOf(const Vehicle& v) const {
    return std::max(0, static_cast<int>(v.position() * layout::kPixelsPerMeter / 10.0f) + bonus_ - penalty_);
}

// ---------------------------------------------------------------------------------------------
// Power-ups
// ---------------------------------------------------------------------------------------------

void Game::chargeAssist(PowerUp p) {
    if (p == PowerUp::AebSave) {
        aebSaves_ = std::min(aebSaves_ + 1, kMaxAebSaves);
        return;
    }
    chargeOf(p) = progress_.powerUpSeconds();
    switch (p) {
        case PowerUp::Acc:     if (acc_.state() == AccState::Off) { acc_.toggle(ego_.speed()); }  break;
        case PowerUp::Lka:     if (lka_.state() == LkaState::Off) { lka_.toggle(); }              break;
        case PowerUp::Bsw:     if (bsw_.state() == BswState::Off) { bsw_.toggle(); }              break;
        case PowerUp::Fusion:  fusionOn_ = true;                                                  break;
        case PowerUp::AebSave:                                                                    break;
    }
}

// A fresh object is the "Off" state.
void Game::dischargeAssist(PowerUp p) {
    switch (p) {
        case PowerUp::Acc:     acc_ = Acc();       break;
        case PowerUp::Lka:     lka_ = Lka();       break;
        case PowerUp::Bsw:     bsw_ = Bsw();       break;
        case PowerUp::Fusion:  fusionOn_ = false;  break;
        case PowerUp::AebSave:                     break;
    }
}

// ---------------------------------------------------------------------------------------------
// Logic
// ---------------------------------------------------------------------------------------------

void Game::update(float frameTime) {
    accumulator_ = std::min(accumulator_ + frameTime, kMaxBacklog);
    if (screen_ == Screen::GameOver || paused_) {
        accumulator_ = 0.0f;   // the world is frozen after a crash or while paused
    }
    // Zero or more fixed cycles; a crash empties the accumulator, which ends the loop.
    while (accumulator_ >= cfg::kCycleTime) {
        accumulator_ -= cfg::kCycleTime;
        step();
    }
    audio_.setEngine(screen_ == Screen::Playing && !paused_, ego_.speed() / kTopSpeed);
}

void Game::step() {
    const bool playing = screen_ == Screen::Playing;

    // Static obstacles (roadworks) for this cycle: traffic, sensors and collisions all use the same list.
    if (playing) {
        hazards_.step(ego_);
    }
    const RoadHazards::Obstacles obstacles = playing ? hazards_.obstacles() : RoadHazards::Obstacles{};
    const Span<StaticObstacle> obstacleView(obstacles.begin(), obstacles.size());

    if (playing) {
        stepPowerUps(obstacleView);
        // Difficulty: more traffic the further you get.
        traffic_.setActiveCount(kStartTraffic + static_cast<std::size_t>(ego_.position() / kMetresPerExtraCar));
        stepAssists(obstacleView);
    } else {
        ego_.setAcceleration(0.0f);   // menu: an invisible camera cruises through traffic
    }

    const int laneBefore = ego_.lane();
    ego_.step(cfg::kCycleTime);
    if (playing && ego_.lane() != laneBefore) {
        audio_.play(Sfx::LaneChange);   // whoosh when crossing a lane line
    }
    traffic_.step(cfg::kCycleTime, ego_, playing, obstacleView);   // title screen: NPCs ignore the camera

    // Cutting someone off: they honk, you lose points.
    const int cutOffs = traffic_.takeCutOffs();
    if (cutOffs > 0 && playing) {
        audio_.play(Sfx::Horn);
        penalty_ += kCutOffPenalty * cutOffs;
        penaltyFlash_ = kPenaltyFlashTime;
    }
    penaltyFlash_ = std::max(0.0f, penaltyFlash_ - cfg::kCycleTime);

    if (playing) {
        stepScoring(obstacleView);
    }
}

// Collect power-ups, and drain the charge of active assists.
void Game::stepPowerUps(Span<StaticObstacle> obstacles) {
    const std::optional<PowerUp> got =
        debugMode_ ? std::nullopt : powerUps_.step(ego_, cfg::kCycleTime, progress_.hasMagnet(), obstacles);
    if (got) {
        chargeAssist(*got);
        audio_.play(Sfx::PowerUp);
        pickupName_ = lookOf(*got).collected;
        pickupNameTime_ = kPickupNameTime;
    }
    pickupNameTime_ = std::max(0.0f, pickupNameTime_ - cfg::kCycleTime);
    invulnerable_ = std::max(0.0f, invulnerable_ - cfg::kCycleTime);
    for (PowerUp p : kChargedAssists) {
        float& c = chargeOf(p);
        if (c > 0.0f && !debugMode_) {   // debug: charges never run out
            c -= cfg::kCycleTime;
            if (c <= 0.0f) {
                c = 0.0f;
                dischargeAssist(p);
            }
        }
    }
}

// Sensors, assists and the driver's input decide what the ego car does this cycle.
void Game::stepAssists(Span<StaticObstacle> obstacles) {
    // Perception: every sensor refreshes its object list through the same virtual call.
    SensorEnvironment env;
    env.obstacles = obstacles;
    env.rain = hazards_.inRain(ego_.position());
    env.glare = hazards_.cameraGlare(ego_.position());
    env.fog = hazards_.inFog(ego_.position());
    env.metalBridgeAhead = hazards_.bridgeAhead(ego_, kGhostMinDistance, kGhostMaxDistance);
    for (const std::unique_ptr<Sensor>& sensor : sensors_) {
        sensor->update(ego_, traffic_.cars(), env);
    }
    // Without fusion, ACC and AEB trust the radar alone, ghosts included (phantom braking).
    frontObjects_ = fusionOn_ ? fusion::fuse(*sensors_[kFrontRadar], *sensors_[kFrontCamera])
                              : sensors_[kFrontRadar]->objects();
    const std::optional<Lead> lead = selectLead(frontObjects_);   // perceive once, use everywhere

    // --- Longitudinal: every source puts its request in; the arbiter alone decides what reaches the car ---
    AccelRequests requests;

    // Held keys act every cycle, so they are read as state rather than events.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_DOWN]) {
        requests.driverBrake = -8.0f;
        acc_.cancel();                 // the driver's brake cancels ACC
    } else if (keys[SDL_SCANCODE_UP]) {
        // Full throttle fades out near top speed, like air drag catching up with the engine.
        const float topSpeed = kTopSpeed + progress_.car().topSpeedBonus;
        const float headroom = 1.0f - (ego_.speed() / topSpeed) * (ego_.speed() / topSpeed);
        requests.driverThrottle = kMaxThrottle * progress_.car().accelScale * std::max(headroom, 0.0f);
    }
    // No buttons needed on a handheld: after braking, ACC resumes by itself once the brake
    // has been released for a moment (and the driver isn't accelerating).
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_UP]) {
        accResumeTimer_ = 0.0f;
    } else if (acc_.state() == AccState::Standby) {
        accResumeTimer_ += cfg::kCycleTime;
        if (accResumeTimer_ >= kAccAutoResume) {
            acc_.toggle(ego_.speed());   // Standby -> Active, back to the old set speed
            accResumeTimer_ = 0.0f;
        }
    }
    if (acc_.state() == AccState::Active) {
        requests.acc = acc_.step(cfg::kCycleTime, ego_.speed(), lead);
    }
    // AEB runs while it has saves (and finishes a braking it already started).
    if (aebSaves_ > 0 || isBraking(aeb_.stage())) {
        requests.aeb = aeb_.step(lead);
    }

    ego_.setAcceleration(arbitrate(requests).accel);

    // AEB sounds: squeal when braking starts, repeating chime while warning.
    const AebStage stage = aeb_.stage();
    if (isBraking(stage) && !isBraking(lastAebStage_)) {
        audio_.play(Sfx::EmergencyBrake);
        if (!debugMode_) {
            aebSaves_ = std::max(aebSaves_ - 1, 0);   // each emergency braking uses one save
        }
    }
    if (stage == AebStage::Warning) {
        warningChimeTimer_ -= cfg::kCycleTime;
        if (lastAebStage_ != AebStage::Warning || warningChimeTimer_ <= 0.0f) {
            audio_.play(Sfx::CollisionWarning);
            warningChimeTimer_ = kWarningChimeInterval;
        }
    }
    lastAebStage_ = stage;

    // --- Lateral: driver steering beats LKA; wind pushes the car whatever anyone wants ---
    const bool left = keys[SDL_SCANCODE_LEFT];
    const bool right = keys[SDL_SCANCODE_RIGHT];
    std::optional<float> driverSteer;
    if (left != right) {   // both or neither held: no steering
        const float steerRate = progress_.car().steerRate;   // always stronger than LKA's 1.2
        driverSteer = left ? -steerRate : steerRate;
    }
    // BSW removes steering towards an occupied blind spot; LKA then sees "no driver" and re-centres.
    const std::optional<float> allowedSteer = bsw_.step(sensors_[kRearLeftRadar]->objects(),
                                                        sensors_[kRearRightRadar]->objects(), driverSteer);
    if (bsw_.state() == BswState::Intervening && lastBswState_ != BswState::Intervening) {
        audio_.play(Sfx::CollisionWarning);
    }
    lastBswState_ = bsw_.state();

    std::optional<float> lkaSteer;
    // LKA needs the camera to see the lane markings.
    const float lkaCommand = lka_.step(cfg::kCycleTime, ego_.lateral(), allowedSteer.has_value(),
                                       sensors_[kFrontCamera]->available());
    if (lka_.state() == LkaState::Active) {
        lkaSteer = lkaCommand;
    }
    ego_.steer(arbitrateSteer(allowedSteer, lkaSteer).rate);
    // Road surface: oil takes away grip (for the driver and LKA alike) and makes the car slide.
    const GripEffect surface = oil_.step(cfg::kCycleTime, ego_);
    if (surface.justHit) {
        audio_.play(Sfx::Skid);
    }
    ego_.setGrip(std::min(surface.grip, hazards_.inRain(ego_.position()) ? kRainGrip : 1.0f));
    // The wind keeps changing in a tunnel, it just can't reach the car there.
    const float gust = wind_.step(cfg::kCycleTime);
    ego_.push((hazards_.inTunnel(ego_.position()) ? 0.0f : gust) + surface.slide);
}

// Near misses add points; a crash uses up a shield or ends the run.
void Game::stepScoring(Span<StaticObstacle> obstacles) {
    const int nearMissPoints = nearMiss_.update(cfg::kCycleTime, ego_, traffic_.cars());
    if (nearMissPoints > 0) {
        bonus_ += nearMissPoints;
        audio_.play(Sfx::NearMiss);
    }
    // Crash: into a vehicle (from any side), an obstacle, or the road-side barrier.
    const std::optional<CrashReport> crash = describeCrash(ego_, traffic_.cars(), obstacles);
    if (!crash || invulnerable_ > 0.0f) {
        return;
    }
    audio_.play(Sfx::Crash);
    if (shields_ > 0) {
        --shields_;                   // the shield takes the hit
        invulnerable_ = kShieldTime;
        return;
    }
    lastCrash_ = *crash;
    lastScore_ = scoreOf(ego_);
    newBest_ = !debugMode_ && lastScore_ > progress_.best;
    coinsEarned_ = 0;
    if (!debugMode_) {   // debug runs don't count
        coinsEarned_ = progress_.finishRun(lastScore_);
        saveProgress();
    }
    screen_ = Screen::GameOver;
    accumulator_ = 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------

HudView Game::buildHud() const {
    const bool playing = screen_ == Screen::Playing;
    HudView hud;
    hud.score = scoreOf(ego_);
    hud.scorePenalty = penaltyFlash_ > 0.0f;
    hud.best = progress_.best;
    hud.combo = nearMiss_.comboActive() ? nearMiss_.combo() : 0;
    hud.shields = playing ? shields_ : 0;
    hud.pickupName = pickupNameTime_ > 0.0f ? pickupName_ : nullptr;
    hud.speedKmh = static_cast<int>(ego_.speed() * 3.6f);   // m/s -> km/h

    const bool cameraOk = sensors_[kFrontCamera]->available();
    hud.wind = playing && !hazards_.inTunnel(ego_.position()) ? wind_.current() : 0.0f;
    hud.cameraOk = !playing || cameraOk;
    if (playing) {
        hud.cameraText = hazards_.inFog(ego_.position())         ? "BLIND"
                         : hazards_.cameraGlare(ego_.position()) ? "GLARE"
                         : hazards_.inRain(ego_.position())      ? "RAIN"
                                                                 : nullptr;
    }

    // Lamps in HudView order: ACC, AEB, LKA, BSW, FUSION.
    AssistView& accLamp = hud.assists[0];
    AssistView& aebLamp = hud.assists[1];
    AssistView& lkaLamp = hud.assists[2];
    AssistView& bswLamp = hud.assists[3];
    AssistView& fusionLamp = hud.assists[4];
    accLamp.lamp = lampOf(acc_.state());
    aebLamp.lamp = lampOf(aeb_.stage());
    lkaLamp.lamp = lampOf(lka_.state());
    bswLamp.lamp = lampOf(bsw_.state());
    // Amber = camera blind, fusion runs on radar alone.
    fusionLamp.lamp = !fusionOn_ ? AssistLamp::Off : (cameraOk ? AssistLamp::Active : AssistLamp::Degraded);

    // Uncharged assists are locked (grey); the bar shows the charge left, or AEB's saves.
    const float fullCharge = progress_.powerUpSeconds();
    const std::array<std::pair<AssistView*, PowerUp>, 4> chargedLamps{
        {{&accLamp, PowerUp::Acc}, {&lkaLamp, PowerUp::Lka}, {&bswLamp, PowerUp::Bsw}, {&fusionLamp, PowerUp::Fusion}}};
    for (const auto& [lamp, p] : chargedLamps) {
        if (chargeOf(p) <= 0.0f) {
            lamp->lamp = AssistLamp::Locked;
        }
        lamp->level = chargeOf(p) / fullCharge;
    }
    if (aebSaves_ == 0 && !isBraking(aeb_.stage())) {
        aebLamp.lamp = AssistLamp::Locked;
    }
    aebLamp.level = static_cast<float>(aebSaves_) / static_cast<float>(kMaxAebSaves);
    return hud;
}

// Back to front: road, power-ups, things on the road, traffic, then the screen on top.
void Game::draw(Renderer2D& gfx) const {
    gfx.drawRoad(ego_.position() * layout::kPixelsPerMeter);

    const bool inRun = screen_ == Screen::Playing || screen_ == Screen::GameOver;
    if (inRun && !debugMode_) {
        drawPickups(gfx);
    }
    if (inRun) {
        drawRoadHazards(gfx);
    }
    drawTraffic(gfx);

    if (inRun) {
        drawDriving(gfx);
    } else {
        drawScreen(gfx);
    }
    if (paused_) {
        gfx.drawPause(pauseSelected_);
    }
    if (screen_ == Screen::GameOver) {
        gfx.drawGameOver(lastScore_, progress_.best, newBest_, coinsEarned_, lastCrash_.what, lastCrash_.tip);
    }
}

void Game::drawPickups(Renderer2D& gfx) const {
    for (const PowerUpField::Pickup& p : powerUps_.pickups()) {
        const float y = layout::screenY(p.position, ego_.position());
        const float ahead = p.position - ego_.position();
        const bool onScreen = y > -40.0f && y < static_cast<float>(cfg::kScreenHeight) + 40.0f;
        if (!onScreen && !(ahead > 0.0f && ahead < kPickupHintRange)) {
            continue;
        }
        const PowerUpLook look = lookOf(p.type);
        if (y < -20.0f) {   // still off-screen: a small badge at the top edge above its lane
            gfx.drawPickupHint(layout::laneX(p.lateral), look.tag, colourOf(look));
        } else {
            gfx.drawPickup(layout::laneX(p.lateral), y, look.tag, colourOf(look));
        }
    }
}

// Things on the road surface, drawn under the cars.
void Game::drawRoadHazards(Renderer2D& gfx) const {
    const float egoPos = ego_.position();
    if (RoadHazards::kDebrisEnabled) {
        for (const RoadHazards::Debris& d : hazards_.debris()) {
            gfx.drawDebris(layout::laneX(d.lateral), layout::screenY(d.position, egoPos), static_cast<int>(d.kind),
                           static_cast<unsigned>(d.position));
        }
    }
    for (const RoadHazards::Accident& a : hazards_.accidents()) {
        gfx.drawWreck(layout::laneX(static_cast<float>(a.lane)), layout::screenY(a.position, egoPos),
                      RoadHazards::kAccidentLength);
    }
    for (const RoadHazards::Roadworks& r : hazards_.roadworks()) {
        gfx.drawRoadworks(layout::laneX(static_cast<float>(r.lane)), layout::screenY(r.start, egoPos),
                          layout::screenY(r.end, egoPos), r.mergeDir);
    }
    for (const OilField::Patch& p : oil_.patches()) {
        const float y = layout::screenY(p.position, egoPos);
        if (y > -60.0f && y < static_cast<float>(cfg::kScreenHeight) + 60.0f) {
            gfx.drawOil(layout::laneX(p.lateral), y);
        }
    }
}

void Game::drawTraffic(Renderer2D& gfx) const {
    for (std::size_t i = 0; i < Traffic::kMaxCars; ++i) {
        const Vehicle& car = traffic_.cars()[i];
        const float x = layout::laneX(car.lateral());
        const float y = layout::screenY(car.position(), ego_.position());

        // One variant number encodes both colour and body style (cars) or just the colour (motorcycles).
        const auto variant = static_cast<std::size_t>(car.variant());
        const DriverState& driver = traffic_.driver(i);
        const bool braking = driver.accel < -kBrakeLightDecel;
        const CarSkin skin = kNpcSkins[variant % kNpcSkins.size()];
        if (car.type() == VehicleType::Motorcycle) {
            gfx.drawMotorcycle(x, y, skin, driver.signal, braking);
        } else {
            gfx.drawCar(x, y, skin, variant / kNpcSkins.size(), driver.signal, braking);
        }
    }
}

void Game::drawDriving(Renderer2D& gfx) const {
    const bool playing = screen_ == Screen::Playing;
    const float egoPos = ego_.position();
    const float egoX = layout::laneX(ego_.lateral());
    const bool blinkOff = invulnerable_ > 0.0f && (SDL_GetTicks() / 100) % 2 == 0;   // shield just saved you
    if (!blinkOff) {
        gfx.drawCar(egoX, layout::kEgoScreenY, CarSkin::Blue, static_cast<std::size_t>(progress_.car().spriteModel));
    }

    // Bridges pass over the cars; fog hides everything inside it, the player's view included.
    for (const RoadHazards::Zone& z : hazards_.tunnels()) {
        gfx.drawTunnel(layout::screenY(z.end, egoPos), layout::screenY(z.start, egoPos));
    }
    for (const RoadHazards::Zone& z : hazards_.rain()) {
        gfx.drawRain(layout::screenY(z.end, egoPos), layout::screenY(z.start, egoPos));
    }
    for (const float b : hazards_.bridges()) {
        gfx.drawBridge(layout::screenY(b, egoPos));
    }
    for (const RoadHazards::FogBank& f : hazards_.fog()) {
        gfx.drawFog(layout::screenY(f.end, egoPos), layout::screenY(f.start, egoPos));
    }

    if (playing && showSensorDebug_) {
        drawSensorDebug(gfx, egoX);
    }
    if (playing) {
        const bool windBlows = !hazards_.inTunnel(egoPos);
        gfx.drawWind(windBlows ? wind_.current() : 0.0f, egoPos * layout::kPixelsPerMeter);
    }
    if (playing && bsw_.state() != BswState::Off) {
        gfx.drawBlindSpotWarning(egoX, bsw_.leftOccupied(), bsw_.rightOccupied(), bsw_.state() == BswState::Intervening);
    }
    if (playing && aeb_.stage() != AebStage::Inactive) {
        gfx.drawAlertBorder(isBraking(aeb_.stage()));   // red while braking, amber when warning
    }
    gfx.drawPanel(buildHud());
}

// Engineer's view of what the sensors detect: each sensor's zone, a bracket on every object it
// reports, and the lead ACC/AEB use (line with gap and relative speed).
void Game::drawSensorDebug(Renderer2D& gfx, float egoX) const {
    for (std::size_t s = 0; s < sensors_.size(); ++s) {
        const DetectionZone& zone = sensors_[s]->fieldOfView();
        const SDL_Color c = s == kFrontRadar    ? SDL_Color{60, 230, 140, 255}    // green
                            : s == kFrontCamera ? SDL_Color{80, 200, 255, 255}    // cyan
                                                : SDL_Color{255, 176, 32, 255};   // amber
        gfx.drawSensorZone(egoX, zone.minLongitudinal, zone.maxLongitudinal, zone.minLateral, zone.maxLateral, c);
        for (const DetectedObject& o : sensors_[s]->objects()) {
            // Objects are reported relative to the ego.
            gfx.drawDetection(egoX + o.lateral * cfg::kLaneWidth, layout::screenY(o.longitudinal, 0.0f), c);
        }
    }
    if (const std::optional<Lead> lead = selectLead(frontObjects_)) {
        gfx.drawLeadMarker(egoX, lead->gap, lead->relativeSpeed);
    }
    if (lka_.state() == LkaState::Active) {
        gfx.drawLaneTarget(layout::laneX(static_cast<float>(lka_.targetLane())));
    }
}

void Game::drawScreen(Renderer2D& gfx) const {
    switch (screen_) {
        case Screen::Menu:
            gfx.drawMenu(menu_);
            break;
        case Screen::Garage: {
            GarageView view;
            for (std::size_t i = 0; i < kCars.size(); ++i) {
                const CarOffer& car = kCars[i];
                view.cars[i] = GarageCarView{car.name, car.strength, car.weakness, car.spriteModel, car.price,
                                             progress_.ownsCar(i), static_cast<int>(i) == progress_.selectedCar,
                                             progress_.coins >= car.price};
            }
            view.selected = garageSelected_;
            view.coins = progress_.coins;
            gfx.drawGarage(view);
            break;
        }
        case Screen::Shop: {
            ShopView view;
            for (std::size_t i = 0; i < view.rows.size(); ++i) {
                const auto item = static_cast<ShopItem>(i);
                const ShopOffer& offer = kShopOffers[i];
                const int price = progress_.nextPrice(item);
                view.rows[i] = ShopRowView{offer.name, offer.description, progress_.level(item), offer.maxLevel,
                                           price, price > 0 && progress_.coins >= price};
            }
            view.selected = shopSelected_;
            view.coins = progress_.coins;
            gfx.drawShop(view);
            break;
        }
        case Screen::HowToPlay:
            gfx.drawHowToPlay(rulesBeforeRun_);
            break;
        case Screen::Guide:
            gfx.drawAssistGuide(debugMode_);
            break;
        case Screen::Playing:
        case Screen::GameOver:
            break;   // drawn by drawDriving
    }
}
