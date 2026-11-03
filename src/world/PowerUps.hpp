#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include "core/Config.hpp"
#include "core/Span.hpp"
#include "world/Obstacle.hpp"
#include "world/Vehicle.hpp"

// Pick-ups on the road. Driving through one charges an assist (or gives AEB one more save).
enum class PowerUp : std::uint8_t { Acc, Lka, Bsw, Fusion, AebSave };

class PowerUpField {
public:
    static constexpr std::size_t kMaxPickups = 3;

    struct Pickup {
        float position;   // m along the road
        float lateral;    // lanes (a lane centre)
        PowerUp type;
    };

    explicit PowerUpField(std::uint32_t seed = 77) : rng_(seed) {
        const float first = kFirst * std::uniform_real_distribution<float>(0.7f, 1.3f)(rng_);
        for (std::size_t i = 0; i < kMaxPickups; ++i) {
            pickups_[i] = make(first + kSpacing * static_cast<float>(i), 1, {});
        }
    }

    // Every 20 ms: returns the pick-up the player drove through this cycle, if any.
    // magnet: a pick-up close to the player, in any lane, flies into the car.
    // obstacles: closed lanes, so a new pick-up never lands where it can't be reached.
    std::optional<PowerUp> step(const Vehicle& ego, float dt = 0.02f, bool magnet = false, Span<StaticObstacle> obstacles = {}) {
        std::optional<PowerUp> collected;
        for (Pickup& p : pickups_) {
            const float ahead = p.position - ego.position();
            if (magnet && std::abs(ahead) < kMagnetRange) {
                // Home in on the car: sideways, and along the road faster than the car drives.
                const float lateralStep = kMagnetLateralRate * dt;
                const float longitudinalStep = (ego.speed() + kMagnetCatchUp) * dt;
                p.lateral += std::clamp(ego.lateral() - p.lateral, -lateralStep, lateralStep);
                p.position += std::clamp(-ahead, -longitudinalStep, longitudinalStep);
            }
            // Collected as soon as any part of the car touches the badge.
            const bool touched = std::abs(ego.lateral() - p.lateral) < kBadgeHalfWidth + ego.width() / 2.0f &&
                                 std::abs(ego.position() - p.position) < kBadgeHalfLength + ego.length() / 2.0f;
            if (touched) {
                collected = p.type;
            }
            if (touched || p.position < ego.position() - kRecycleBehind) {
                p = make(furthest() + std::uniform_real_distribution<float>(kSpacing * 0.7f, kSpacing * 1.3f)(rng_),
                         ego.lane(), obstacles);
            }
        }
        return collected;
    }

    const std::array<Pickup, kMaxPickups>& pickups() const { return pickups_; }

private:
    static constexpr float kFirst = 60.0f;      // m: the first one comes early
    static constexpr float kSpacing = 140.0f;
    static constexpr float kRecycleBehind = 20.0f;
    static constexpr float kBadgeHalfWidth = 0.25f;   // lanes; the drawn badge is about half a lane wide
    static constexpr float kBadgeHalfLength = 1.5f;   // m
    static constexpr float kMagnetRange = 10.0f;         // m ahead or behind
    static constexpr float kMagnetLateralRate = 2.5f;    // lanes/s
    static constexpr float kMagnetCatchUp = 8.0f;        // m/s on top of the car's speed

    // Mostly where the player can reach it: their lane (40 %), a neighbouring lane (40 %), anywhere (20 %).
    // Never inside or just before a closed lane.
    Pickup make(float position, int playerLane, Span<StaticObstacle> obstacles) {
        const int roll = std::uniform_int_distribution<int>(0, 9)(rng_);
        int lane = playerLane;
        if (roll >= 8) {
            lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);
        } else if (roll >= 4) {
            lane = playerLane + (std::bernoulli_distribution(0.5)(rng_) ? 1 : -1);
        }
        lane = std::clamp(lane, 0, cfg::kLaneCount - 1);
        for (int tries = 0; tries < cfg::kLaneCount && blocked(lane, position, obstacles); ++tries) {
            lane = (lane + 1) % cfg::kLaneCount;
        }
        return Pickup{position, static_cast<float>(lane), randomType()};
    }

    static bool blocked(int lane, float position, Span<StaticObstacle> obstacles) {
        for (const StaticObstacle& o : obstacles) {
            if (std::abs(o.lateral - static_cast<float>(lane)) < o.halfWidth && position > o.start - 30.0f && position < o.end + 5.0f) {
                return true;
            }
        }
        return false;
    }

    PowerUp randomType() {
        // AEB saves are rarer than assist charges.
        const int roll = std::uniform_int_distribution<int>(0, 8)(rng_);
        return roll < 2 ? PowerUp::Acc : roll < 4 ? PowerUp::Lka : roll < 6 ? PowerUp::Bsw : roll < 8 ? PowerUp::Fusion : PowerUp::AebSave;
    }

    float furthest() const {
        float far = 0.0f;
        for (const Pickup& p : pickups_) {
            far = std::max(far, p.position);
        }
        return far;
    }

    std::array<Pickup, kMaxPickups> pickups_{};
    std::mt19937 rng_;
};
