#pragma once
#include <cstdint>
#include <optional>
#include "core/Config.hpp"
#include "perception/ObjectList.hpp"

// Clear = on, nothing in the blind spots. Warning = a car in a blind spot. Intervening = the driver
// steered towards it and BSW blocked the steering.
enum class BswState : std::uint8_t { Off, Clear, Warning, Intervening };

// Blind Spot Warning with intervention, fed by the two rear corner radars.
class Bsw {
public:
    // B key: Off <-> on.
    void toggle() { state_ = (state_ == BswState::Off) ? BswState::Clear : BswState::Off; }

    // Every 20 ms. Returns the driver's steering request after BSW: steering towards an occupied
    // blind spot is removed (empty = no driver steering, so LKA, if on, can centre the car again).
    std::optional<float> step(const ObjectList& rearLeft, const ObjectList& rearRight, std::optional<float> driverSteer) {
        left_ = hasThreat(rearLeft);
        right_ = hasThreat(rearRight);
        if (state_ == BswState::Off) {
            left_ = right_ = false;
            return driverSteer;
        }

        const bool steeringIntoLeft = driverSteer && *driverSteer < 0.0f && left_;
        const bool steeringIntoRight = driverSteer && *driverSteer > 0.0f && right_;
        if (steeringIntoLeft || steeringIntoRight) {
            state_ = BswState::Intervening;
            return std::nullopt;
        }
        state_ = (left_ || right_) ? BswState::Warning : BswState::Clear;
        return driverSteer;
    }

    BswState state() const { return state_; }
    bool leftOccupied() const { return left_; }
    bool rightOccupied() const { return right_; }

    // A car in a rear zone is a threat if it is alongside (in the blind spot itself), or behind and
    // closing in so fast that it would reach me within kReachTime (the lane change assist part).
    // Judged by time, not distance: a car 30 m back is harmless at the same speed, deadly at +12 m/s.
    static bool isThreat(const DetectedObject& o) {
        const float bumperGap = -o.longitudinal - (cfg::kCarLength + o.length) / 2.0f;   // > 0 when fully behind me
        if (bumperGap < kAlongsideGap) {
            return true;
        }
        if (o.relativeSpeed <= 0.0f) {
            return false;   // behind and not catching up
        }
        return bumperGap / o.relativeSpeed < kReachTime;
    }

    static constexpr float kAlongsideGap = 3.0f;   // m; closer than this behind counts as "in the blind spot"
    static constexpr float kReachTime = 3.5f;      // s; a car that would reach me sooner is a threat

private:
    static bool hasThreat(const ObjectList& objects) {
        for (const DetectedObject& o : objects) {
            if (isThreat(o)) {
                return true;
            }
        }
        return false;
    }

    BswState state_ = BswState::Off;
    bool left_ = false;
    bool right_ = false;
};
