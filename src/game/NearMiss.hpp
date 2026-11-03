#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include "core/Span.hpp"
#include "world/Vehicle.hpp"

// Near misses: passing (or being passed by) a vehicle very close sideways, at speed.
// Each one scores kBasePoints x combo; the combo grows while near misses follow each other within
// kComboWindow seconds and resets otherwise.
template <std::size_t N>
class NearMissTracker {
public:
    static constexpr int kBasePoints = 50;
    static constexpr float kComboWindow = 4.0f;    // s
    static constexpr float kMaxEdgeGap = 0.3f;     // lanes between the two bodies' sides
    static constexpr float kMinPassSpeed = 4.0f;   // m/s relative speed

    // Every 20 ms. Returns the points scored this cycle (0 most of the time).
    int update(float dt, const Vehicle& ego, Span<Vehicle> vehicles) {
        sinceLast_ += dt;
        if (sinceLast_ > kComboWindow) {
            combo_ = 0;
        }
        int points = 0;
        std::size_t i = 0;
        for (const Vehicle& v : vehicles) {
            if (i >= N) {
                break;
            }
            const float relative = v.position() - ego.position();
            const bool wasAhead = lastRelative_[i] > 0.0f;
            const bool sameVehicle = std::abs(relative - lastRelative_[i]) < kRespawnJump;   // not recycled meanwhile
            if (sameVehicle && wasAhead != (relative > 0.0f) && isClosePass(ego, v)) {
                combo_ = combo_ + 1;
                sinceLast_ = 0.0f;
                points += kBasePoints * combo_;
            }
            lastRelative_[i] = relative;
            ++i;
        }
        return points;
    }

    int combo() const { return combo_; }
    bool comboActive() const { return combo_ > 0 && sinceLast_ <= kComboWindow; }

private:
    static constexpr float kRespawnJump = 5.0f;   // m in one cycle: the vehicle was recycled, not passed

    static bool isClosePass(const Vehicle& ego, const Vehicle& v) {
        const float edgeGap = std::abs(v.lateral() - ego.lateral()) - (v.width() + ego.width()) / 2.0f;
        return edgeGap >= 0.0f && edgeGap < kMaxEdgeGap && std::abs(v.speed() - ego.speed()) >= kMinPassSpeed;
    }

    std::array<float, N> lastRelative_{};
    int combo_ = 0;
    float sinceLast_ = 1000.0f;
};
