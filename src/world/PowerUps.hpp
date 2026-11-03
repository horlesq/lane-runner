#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include "core/Config.hpp"
#include "world/Vehicle.hpp"

// Pick-ups on the road. Driving through one charges an assist (or gives AEB one more save).
enum class PowerUp : std::uint8_t { Acc, Lka, Bsw, Fusion, AebSave };

class PowerUpField {
public:
    static constexpr std::size_t kMaxPickups = 2;

    struct Pickup {
        float position;   // m along the road
        float lateral;    // lanes (a lane centre)
        PowerUp type;
    };

    PowerUpField() {
        for (std::size_t i = 0; i < kMaxPickups; ++i) {
            pickups_[i] = make(kFirst + kSpacing * static_cast<float>(i));
        }
    }

    // Every 20 ms: returns the pick-up the player drove through this cycle, if any.
    // magnet: nearby pick-ups slide sideways towards the player's lane.
    std::optional<PowerUp> step(const Vehicle& ego, float dt = 0.02f, bool magnet = false) {
        std::optional<PowerUp> collected;
        for (Pickup& p : pickups_) {
            const float ahead = p.position - ego.position();
            if (magnet && ahead > 0.0f && ahead < kMagnetRange) {
                const float pull = std::clamp(ego.lateral() - p.lateral, -kMagnetRate * dt, kMagnetRate * dt);
                p.lateral += pull;
            }
            const bool touched = std::abs(ego.lateral() - p.lateral) < kReachLanes && std::abs(ego.position() - p.position) < kReachMetres;
            if (touched) {
                collected = p.type;
            }
            if (touched || p.position < ego.position() - kRecycleBehind) {
                p = make(furthest() + std::uniform_real_distribution<float>(kSpacing * 0.7f, kSpacing * 1.3f)(rng_));
            }
        }
        return collected;
    }

    const std::array<Pickup, kMaxPickups>& pickups() const { return pickups_; }

private:
    static constexpr float kFirst = 80.0f;      // m: the first one comes early
    static constexpr float kSpacing = 220.0f;
    static constexpr float kRecycleBehind = 20.0f;
    static constexpr float kReachLanes = 0.45f;
    static constexpr float kReachMetres = 3.0f;
    static constexpr float kMagnetRange = 40.0f;   // m ahead
    static constexpr float kMagnetRate = 1.5f;     // lanes/s

    Pickup make(float position) {
        const int lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);
        // AEB saves are rarer than assist charges.
        const int roll = std::uniform_int_distribution<int>(0, 8)(rng_);
        const PowerUp type = roll < 2 ? PowerUp::Acc : roll < 4 ? PowerUp::Lka : roll < 6 ? PowerUp::Bsw : roll < 8 ? PowerUp::Fusion : PowerUp::AebSave;
        return Pickup{position, static_cast<float>(lane), type};
    }

    float furthest() const {
        float far = 0.0f;
        for (const Pickup& p : pickups_) {
            far = std::max(far, p.position);
        }
        return far;
    }

    std::array<Pickup, kMaxPickups> pickups_{};
    std::mt19937 rng_{77};
};
