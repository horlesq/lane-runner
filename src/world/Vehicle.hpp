#pragma once
#include <algorithm>
#include <cmath>
#include "core/Config.hpp"

class Vehicle {
    public:
        Vehicle(int lane, float speed, float position = 0.0f)
            : lane_(lane), lateral_(static_cast<float>(lane)), position_(position), speed_(speed), accel_(0.0f), variant_(0),
              freeSteering_(false), steerCommand_(0.0f), lateralRate_(0.0f), push_(0.0f), hitCurb_(false), grip_(1.0f) {}

        Vehicle() : Vehicle(0, 0.0f) {}

        int lane() const { return lane_; }
        float lateral() const { return lateral_; }
        float position() const { return position_; }
        float speed() const { return speed_; }

        void setAcceleration(float accel) { accel_ = accel; }

        void changeLane(int direction) {
            lane_ = std::clamp(lane_ + direction, 0, cfg::kLaneCount - 1);
        }

        // Free steering (the player's car): instead of snapping to a target lane, the driver or LKA
        // commands a sideways speed, and outside forces like wind can push the car off course.
        void enableFreeSteering() { freeSteering_ = true; }
        void steer(float lanesPerSecond) { steerCommand_ = lanesPerSecond; }
        void push(float lanesPerSecond) { push_ = lanesPerSecond; }   // disturbance for the next step
        void setGrip(float grip) { grip_ = std::clamp(grip, 0.0f, 1.0f); }   // 1 = dry road, low = slippery
        float lateralRate() const { return lateralRate_; }
        bool hitCurb() const { return hitCurb_; }   // ran into the road-side barrier in the last step

        void step(float dt) {
            if (freeSteering_) {
                stepFreeSteering(dt);
            } else {
                constexpr float kLaneChangeRate = 1.25f;   // lanes/s: a lane change takes 0.8 s
                const float diff = static_cast<float>(lane_) - lateral_;
                const float maxMove = kLaneChangeRate * dt;
                lateral_ += std::clamp(diff, -maxMove, maxMove);
            }

            speed_ += accel_ * dt;
            speed_ = std::max(speed_, 0.0f);

            position_ += speed_ * dt;
        }

        void reset(int lane, float speed, float position, int variant) {
            lane_ = lane;
            lateral_ = static_cast<float>(lane);
            position_ = position;
            speed_ = speed;
            accel_ = 0.0f;
            variant_ = variant;
            lateralRate_ = 0.0f;
        }

        int variant() const { return variant_; }

    private:
        void stepFreeSteering(float dt) {
            constexpr float kSteerLag = 0.15f;   // s; the car can't change direction instantly
            // Low grip: the tyres barely respond, so the car keeps sliding the way it was going.
            lateralRate_ += (steerCommand_ - lateralRate_) * std::min(1.0f, dt / kSteerLag) * grip_;
            lateral_ += (lateralRate_ + push_) * dt;

            // The barrier at the road edge stops the car; the game treats touching it as a crash.
            constexpr float kMin = -0.5f + cfg::kCarWidthLanes / 2.0f;
            constexpr float kMax = static_cast<float>(cfg::kLaneCount) - 0.5f - cfg::kCarWidthLanes / 2.0f;
            hitCurb_ = lateral_ < kMin || lateral_ > kMax;
            if (hitCurb_) {
                lateral_ = std::clamp(lateral_, kMin, kMax);
                lateralRate_ = 0.0f;
            }
            lane_ = std::clamp(static_cast<int>(std::lround(lateral_)), 0, cfg::kLaneCount - 1);   // lane I'm mostly in
        }

        int lane_; // 0..3
        float lateral_;  // lanes, 1.0 = centre of lane 1
        float position_; // meters travelled
        float speed_; // meters per second
        float accel_; // meters per second squared

        int variant_;

        bool freeSteering_;
        float steerCommand_;   // lanes/s requested by the driver or LKA
        float lateralRate_;    // lanes/s actually moving sideways
        float push_;           // lanes/s from outside forces (wind)
        bool hitCurb_;
        float grip_;           // 0..1
};
