#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "control/Pid.hpp"
#include "core/Config.hpp"

// Overridden = switched on, but the driver is steering; LKA steps back and resumes on release.
enum class LkaState : std::uint8_t { Off, Active, Overridden };

// Lane Keeping Assist: steers the car back to the centre of the lane it is mostly in.
// Lateral control: the error is the offset from the lane centre (in lanes), the output a
// sideways speed (lanes/s). Its authority is limited so the driver can always out-steer it.
class Lka {
public:
    // L key: Off -> Active, otherwise -> Off.
    void toggle() {
        state_ = (state_ == LkaState::Off) ? LkaState::Active : LkaState::Off;
        pid_.reset();
        targetLane_ = -1;
    }

    // Every 20 ms. Returns the steering correction in lanes/s (0 when Off or overridden).
    float step(float dt, float lateral, bool driverSteering) {
        if (state_ == LkaState::Off) {
            return 0.0f;
        }
        if (driverSteering) {
            state_ = LkaState::Overridden;
            pid_.reset();   // the driver moved the car; old integral and D-term memory are meaningless
            return 0.0f;
        }
        state_ = LkaState::Active;

        // Centre on the lane the car is mostly in. After a manual lane change that is the new lane.
        const int lane = std::clamp(static_cast<int>(std::lround(lateral)), 0, cfg::kLaneCount - 1);
        if (lane != targetLane_) {
            pid_.reset();   // new target: avoid a derivative kick from the jump in error
            targetLane_ = lane;
        }
        return pid_.step(static_cast<float>(lane) - lateral, dt);
    }

    LkaState state() const { return state_; }
    int targetLane() const { return targetLane_; }

private:
    static constexpr float kMaxCorrection = 1.2f;   // lanes/s; the driver steers at 2.0, so they always win

    LkaState state_ = LkaState::Off;
    int targetLane_ = -1;   // -1 = none yet
    Pid<float> pid_{{3.0f, 1.0f, 0.3f}, -kMaxCorrection, kMaxCorrection};
};
