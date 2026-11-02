#pragma once
#include <algorithm>

class Vehicle {
    public:
        Vehicle(int lane, float speed)
            : lane_(lane), position_(0.0f), speed_(speed), accel_(0.0f) {}

        int lane() const { return lane_; }
        float position() const { return position_; }
        float speed() const { return speed_; }

        void setAcceleration(float accel) { accel_ = accel; }
        void step(float dt) {
            speed_ += accel_ * dt;
            speed_ = std::max(speed_, 0.0f);

            position_ += speed_ * dt;
        }

    private:
        int lane_; // 0..3
        float position_; // meters travelled
        float speed_; // meters per second
        float accel_; // meters per second squared
};