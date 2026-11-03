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

    // A car in a blind-spot zone is a threat if it is alongside, or behind but keeping up or closing in.
    // One that is behind and falling back can't be hit by a lane change, so it doesn't count.
    static bool isThreat(const DetectedObject& o) {
        const bool alongside = o.longitudinal > -cfg::kCarLength;
        const bool keepingUp = o.relativeSpeed > -1.0f;
        return alongside || keepingUp;
    }

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
