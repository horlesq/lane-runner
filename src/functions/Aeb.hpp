#pragma once
#include <optional>
#include <algorithm>
#include <cstdint>
#include "world/Lead.hpp"

// Seconds until impact if nothing changes; empty when not closing in.
inline std::optional<float> timeToCollision(const Lead& lead) {
    const float closingSpeed = -lead.relativeSpeed;   // positive when catching up
    if (closingSpeed <= 0.0f) {
        return std::nullopt;   // not closing in: the gap isn't shrinking
    }
    return lead.gap / closingSpeed;
}


enum class AebStage : std::uint8_t { Inactive, Warning, PartialBrake, FullBrake };

class Aeb {
public:
    // Every 20 ms. Returns a brake command (m/s^2, negative) when AEB brakes, empty otherwise.
    std::optional<float> step(const std::optional<Lead>& lead) {
        // 1. No car, or not closing in: release (the only way out of braking).
        const std::optional<float> ttc = lead ? timeToCollision(*lead) : std::nullopt;
        if (!ttc) {
            stage_ = AebStage::Inactive;
            return std::nullopt;
        }

        // 2. Stage from the TTC table alone.
        AebStage newStage = AebStage::Inactive;
        if (*ttc < kFullTtc) {
            newStage = AebStage::FullBrake;
        } else if (*ttc < kPartialTtc) {
            newStage = AebStage::PartialBrake;
        } else if (*ttc < kWarnTtc) {
            newStage = AebStage::Warning;
        }

        // 3. Latch: while braking, never step down to a weaker stage.
        const bool braking = (stage_ == AebStage::PartialBrake || stage_ == AebStage::FullBrake);
        stage_ = braking ? std::max(stage_, newStage) : newStage;

        // 4. The brake command follows from the stage.
        switch (stage_) {
            case AebStage::FullBrake:    return kFullDecel;
            case AebStage::PartialBrake: return kPartialDecel;
            default:                     return std::nullopt;   // Warning and Inactive don't brake
        }
    }

    AebStage stage() const { return stage_; }

private:
    static constexpr float kWarnTtc = 2.6f;      // s
    static constexpr float kPartialTtc = 1.6f;   // s
    static constexpr float kFullTtc = 0.9f;      // s
    static constexpr float kPartialDecel = -4.0f;
    static constexpr float kFullDecel = -9.0f;

    AebStage stage_ = AebStage::Inactive;   // default member initializer: no constructor needed
};
