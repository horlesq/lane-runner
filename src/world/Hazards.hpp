#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <random>
#include "world/Vehicle.hpp"

// Fog banks and metal bridges along the road. Small fixed pools, recycled ahead of the player.
//  - Fog blinds the camera (and the player's view).
//  - A metal bridge reflects radar waves: the front radar reports a "ghost" object in the lane.
class RoadHazards {
public:
    static constexpr std::size_t kMaxFog = 2;
    static constexpr std::size_t kMaxBridges = 2;

    struct FogBank {
        float start;   // m along the road
        float end;
    };

    RoadHazards() {
        for (std::size_t i = 0; i < kMaxFog; ++i) {
            fog_[i] = makeFog(kFirstFog + kFogSpacing * static_cast<float>(i));
        }
        for (std::size_t i = 0; i < kMaxBridges; ++i) {
            bridges_[i] = kFirstBridge + kBridgeSpacing * static_cast<float>(i);
        }
    }

    // Recycle hazards the player has left behind.
    void step(const Vehicle& ego) {
        for (FogBank& f : fog_) {
            if (f.end < ego.position() - kRecycleBehind) {
                f = makeFog(furthestFog() + uniform(kFogSpacing * 0.6f, kFogSpacing * 1.4f));
            }
        }
        for (float& b : bridges_) {
            if (b < ego.position() - kRecycleBehind) {
                b = furthestBridge() + uniform(kBridgeSpacing * 0.6f, kBridgeSpacing * 1.4f);
            }
        }
    }

    bool inFog(float position) const {
        for (const FogBank& f : fog_) {
            if (position >= f.start && position <= f.end) {
                return true;
            }
        }
        return false;
    }

    // Distance from the ego to the nearest bridge ahead within [minAhead, maxAhead], if any.
    std::optional<float> bridgeAhead(const Vehicle& ego, float minAhead, float maxAhead) const {
        std::optional<float> nearest;
        for (float b : bridges_) {
            const float d = b - ego.position();
            if (d >= minAhead && d <= maxAhead && (!nearest || d < *nearest)) {
                nearest = d;
            }
        }
        return nearest;
    }

    const std::array<FogBank, kMaxFog>& fog() const { return fog_; }
    const std::array<float, kMaxBridges>& bridges() const { return bridges_; }

private:
    static constexpr float kFirstFog = 400.0f;      // m; the start of a run is clear
    static constexpr float kFogSpacing = 650.0f;
    static constexpr float kFirstBridge = 250.0f;
    static constexpr float kBridgeSpacing = 450.0f;
    static constexpr float kRecycleBehind = 30.0f;

    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng_); }

    FogBank makeFog(float start) { return FogBank{start, start + uniform(60.0f, 120.0f)}; }

    float furthestFog() const {
        float far = 0.0f;
        for (const FogBank& f : fog_) {
            far = std::max(far, f.end);
        }
        return far;
    }

    float furthestBridge() const {
        float far = 0.0f;
        for (float b : bridges_) {
            far = std::max(far, b);
        }
        return far;
    }

    std::array<FogBank, kMaxFog> fog_{};
    std::array<float, kMaxBridges> bridges_{};
    std::mt19937 rng_{2024};
};
