#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include "core/Config.hpp"
#include "world/Vehicle.hpp"

// Effect of the road surface on the player's car this cycle.
struct GripEffect {
    float grip = 1.0f;       // 1 = normal, small = slippery (steering barely responds)
    float slide = 0.0f;      // lanes/s sideways slide while the tyres are oily
    bool justHit = false;    // entered a patch this cycle (for the sound)
};

// Oil patches on the road. A small fixed pool is recycled ahead of the player (no allocation).
// Touching one makes the tyres slip: steering loses authority and the car slides sideways for a moment.
class OilField {
public:
    static constexpr std::size_t kMaxPatches = 3;

    struct Patch {
        float position;   // m along the road (centre)
        float lateral;    // lanes (centre)
    };

    OilField() {
        for (std::size_t i = 0; i < kMaxPatches; ++i) {
            patches_[i] = Patch{kFirstPatch + kSpacing * static_cast<float>(i), randomLateral()};
        }
    }

    GripEffect step(float dt, const Vehicle& ego) {
        GripEffect effect;
        bool onOil = false;
        for (Patch& p : patches_) {
            if (p.position < ego.position() - kRecycleBehind) {
                p = Patch{ego.position() + std::uniform_real_distribution<float>(kAheadMin, kAheadMax)(rng_), randomLateral()};
            }
            onOil = onOil || touches(p, ego);
        }

        if (onOil && !wasOnOil_) {
            effect.justHit = true;
            slipLeft_ = kSlipTime;
            // Slide the way the car was already drifting; straight-line hits go a random way.
            const float drift = ego.lateralRate();
            const float dir = std::abs(drift) > 0.05f ? (drift > 0.0f ? 1.0f : -1.0f)
                                                     : (std::bernoulli_distribution(0.5)(rng_) ? 1.0f : -1.0f);
            slideDir_ = dir;
        }
        wasOnOil_ = onOil;

        if (slipLeft_ > 0.0f) {
            const float fade = slipLeft_ / kSlipTime;   // 1 -> 0 as the tyres clean up
            effect.grip = kSlipGrip + (1.0f - kSlipGrip) * (1.0f - fade);
            effect.slide = slideDir_ * kSlideRate * fade;
            slipLeft_ -= dt;
        }
        return effect;
    }

    const std::array<Patch, kMaxPatches>& patches() const { return patches_; }

    static bool touches(const Patch& p, const Vehicle& car) {
        return std::abs(car.lateral() - p.lateral) < kHalfWidth + cfg::kCarWidthLanes / 2.0f &&
               std::abs(car.position() - p.position) < kHalfLength + cfg::kCarLength / 2.0f;
    }

private:
    static constexpr float kHalfWidth = 0.25f;     // lanes
    static constexpr float kHalfLength = 1.2f;     // m
    static constexpr float kSlipTime = 1.0f;       // s the tyres stay slippery after touching oil
    static constexpr float kSlipGrip = 0.15f;      // grip right after hitting the oil
    static constexpr float kSlideRate = 0.7f;      // lanes/s sideways slide at its strongest
    static constexpr float kFirstPatch = 120.0f;   // m; no oil right at the start
    static constexpr float kSpacing = 70.0f;
    static constexpr float kAheadMin = 60.0f;      // m ahead where a recycled patch appears
    static constexpr float kAheadMax = 160.0f;
    static constexpr float kRecycleBehind = 15.0f;

    float randomLateral() {
        const int lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);
        return static_cast<float>(lane) + std::uniform_real_distribution<float>(-0.2f, 0.2f)(rng_);
    }

    std::array<Patch, kMaxPatches> patches_{};
    std::mt19937 rng_{99};
    bool wasOnOil_ = false;
    float slipLeft_ = 0.0f;
    float slideDir_ = 1.0f;
};
