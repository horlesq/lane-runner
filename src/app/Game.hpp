#pragma once
#include <SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include "audio/Audio.hpp"
#include "core/Span.hpp"
#include "functions/Acc.hpp"
#include "functions/Aeb.hpp"
#include "functions/Bsw.hpp"
#include "functions/Lka.hpp"
#include "game/CrashReport.hpp"
#include "game/NearMiss.hpp"
#include "game/Progress.hpp"
#include "perception/ObjectList.hpp"
#include "render/HudView.hpp"
#include "render/MenuView.hpp"
#include "render/Renderer2D.hpp"
#include "sensors/Sensor.hpp"
#include "world/Hazards.hpp"
#include "world/Obstacle.hpp"
#include "world/Oil.hpp"
#include "world/PowerUps.hpp"
#include "world/Traffic.hpp"
#include "world/Vehicle.hpp"
#include "world/Wind.hpp"

// The whole game apart from the platform: screens, one run's state, the fixed-cycle logic and
// what goes on screen. main() owns the window and feeds it input and time.
class Game {
public:
    // savePath: where progress is loaded from now and saved to after every change.
    Game(Audio& audio, std::string savePath);

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    // A key was pressed (not repeated). Held keys (throttle, steering) are read in update().
    void handleKey(SDL_Keycode key);
    // The window lost focus: a run in progress pauses.
    void onFocusLost();
    // Advances the logic by frameTime seconds, in fixed 20 ms cycles.
    void update(float frameTime);
    void draw(Renderer2D& gfx) const;

    // QUIT was chosen in the menu.
    bool quitRequested() const { return quit_; }

private:
    enum class Screen : std::uint8_t { Menu, Garage, Shop, HowToPlay, Guide, Playing, GameOver };

    // --- Input, per screen ---
    void handleMenuKey(SDL_Keycode key, bool confirm);
    void handleGarageKey(SDL_Keycode key, bool confirm);
    void handleShopKey(SDL_Keycode key, bool confirm);
    void handlePlayingKey(SDL_Keycode key, bool confirm);

    // --- Screen changes ---
    void startRun();
    void backToMenu();
    void pause();

    // --- One 20 ms cycle and its parts ---
    void step();
    void stepPowerUps(Span<StaticObstacle> obstacles);
    void stepAssists(Span<StaticObstacle> obstacles);
    void stepScoring(Span<StaticObstacle> obstacles);

    // --- Power-ups ---
    float& chargeOf(PowerUp p) { return charge_[static_cast<std::size_t>(p)]; }
    float chargeOf(PowerUp p) const { return charge_[static_cast<std::size_t>(p)]; }
    void chargeAssist(PowerUp p);      // collected: refill its charge and switch it on
    void dischargeAssist(PowerUp p);   // charge ran out: switch it off

    int scoreOf(const Vehicle& v) const;
    void saveProgress();

    // --- Drawing ---
    HudView buildHud() const;
    void drawPickups(Renderer2D& gfx) const;
    void drawRoadHazards(Renderer2D& gfx) const;
    void drawTraffic(Renderer2D& gfx) const;
    void drawDriving(Renderer2D& gfx) const;   // ego, weather over it, debug view, HUD panel
    void drawSensorDebug(Renderer2D& gfx, float egoX) const;
    void drawScreen(Renderer2D& gfx) const;    // menu, garage, shop and help pages

    Audio& audio_;
    std::string savePath_;
    Progress progress_;
    std::random_device entropy_;   // a new seed for every run

    // Front radar, rear left/right radars, front camera; used only through the Sensor interface.
    std::array<std::unique_ptr<Sensor>, 4> sensors_;
    ObjectList frontObjects_;   // what ACC and AEB work with: raw front radar, or the fused list when fusion is on

    // --- Screens and menus ---
    Screen screen_ = Screen::Menu;
    MenuView menu_;
    std::size_t shopSelected_ = 0;
    std::size_t garageSelected_ = 0;
    bool debugMode_ = false;        // ASSISTS screen: every assist on for the whole run, no power-ups, no score
    bool rulesBeforeRun_ = false;   // HOW TO PLAY was opened by START on a fresh save: ENTER drives
    bool rulesSeen_ = false;        // shown once per session at most, so START isn't blocked by it twice
    bool paused_ = false;           // Esc while driving, or the window lost focus
    std::size_t pauseSelected_ = 0; // 0 = resume, 1 = quit to menu
    bool showSensorDebug_ = false;  // Tab: engineer's view of what the sensors detect
    bool quit_ = false;
    float accumulator_ = 0.0f;      // frame time not yet used up by 20 ms cycles

    // --- World ---
    Vehicle ego_;
    Traffic traffic_;
    RoadHazards hazards_;   // roadworks, accidents, fog, rain, tunnels, bridges
    Wind wind_;
    OilField oil_;
    PowerUpField powerUps_;
    NearMissTracker<Traffic::kMaxCars> nearMiss_;

    // --- Assists ---
    Acc acc_;   // adaptive cruise control
    Aeb aeb_;   // automatic emergency braking
    Lka lka_;   // lane keeping assist
    Bsw bsw_;   // blind spot warning
    bool fusionOn_ = false;
    std::array<float, 4> charge_{};   // s of charge left, indexed by PowerUp::Acc, Lka, Bsw, Fusion
    int aebSaves_ = 0;                // AEB works on saves instead of a charge
    float accResumeTimer_ = 0.0f;
    AebStage lastAebStage_ = AebStage::Inactive;
    float warningChimeTimer_ = 0.0f;  // s until the next AEB warning chime
    BswState lastBswState_ = BswState::Off;

    // --- This run ---
    int shields_ = 0;              // crashes the player can still survive
    float invulnerable_ = 0.0f;    // s left after a shield took a hit
    const char* pickupName_ = nullptr;
    float pickupNameTime_ = 0.0f;
    int bonus_ = 0;                // near-miss points
    int penalty_ = 0;              // points lost for cutting other drivers off
    float penaltyFlash_ = 0.0f;    // s left of the red score flash

    // --- Last run, for the game-over screen ---
    int lastScore_ = 0;
    bool newBest_ = false;
    int coinsEarned_ = 0;
    CrashReport lastCrash_{nullptr, nullptr};
};
