#pragma once
#include <algorithm>
#include "core/Config.hpp"

class Vehicle {
    public:
        Vehicle(int lane, float speed)
            : lane_(lane), lateral_(static_cast<float>(lane)), position_(0.0f), speed_(speed), accel_(0.0f) {}

        int lane() const { return lane_; }
        float lateral() const { return lateral_; }
        float position() const { return position_; }
        float speed() const { return speed_; }

        void setAcceleration(float accel) { accel_ = accel; }

        void changeLane(int direction) {
            lane_ = std::clamp(lane_ + direction, 0, cfg::kLaneCount - 1);
        }

        void step(float dt) {
            constexpr float kLaneChangeRate = 4.0f;
            const float diff = static_cast<float>(lane_) - lateral_;
            const float maxMove = kLaneChangeRate * dt;

            lateral_ += std::clamp(diff, -maxMove, maxMove);

            speed_ += accel_ * dt;
            speed_ = std::max(speed_, 0.0f);

            position_ += speed_ * dt;
        }

    private:
        int lane_; // 0..3
        float lateral_;
        float position_; // meters travelled
        float speed_; // meters per second
        float accel_; // meters per second squared
};