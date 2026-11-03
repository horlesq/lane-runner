#pragma once
#include <cmath>
#include <cstdint>
#include <random>

// Crosswind gusts that push the car sideways: the disturbance LKA has to correct.
// A gust swells and fades over a few seconds, then it is calm for a while.
class Wind {
public:
    explicit Wind(std::uint32_t seed = 7) : rng_(seed) {}

    // Every 20 ms. Returns the sideways push in lanes/s (+ = to the right).
    float step(float dt) {
        if (!inGust_) {
            calmLeft_ -= dt;
            if (calmLeft_ <= 0.0f) {
                startGust();
            }
            return 0.0f;
        }

        gustTime_ += dt;
        if (gustTime_ >= gustDuration_) {
            inGust_ = false;
            current_ = 0.0f;
            calmLeft_ = std::uniform_real_distribution<float>(3.0f, 7.0f)(rng_);
            return 0.0f;
        }
        current_ = peak_ * std::sin(kPi * gustTime_ / gustDuration_);   // smooth rise and fall
        return current_;
    }

    float current() const { return current_; }

private:
    static constexpr float kPi = 3.14159265f;

    void startGust() {
        inGust_ = true;
        gustTime_ = 0.0f;
        gustDuration_ = std::uniform_real_distribution<float>(1.5f, 3.5f)(rng_);
        const float strength = std::uniform_real_distribution<float>(0.4f, 0.9f)(rng_);
        peak_ = std::bernoulli_distribution(0.5)(rng_) ? strength : -strength;
    }

    std::mt19937 rng_;
    bool inGust_ = false;
    float calmLeft_ = 5.0f;   // s of calm at the start of a run
    float gustTime_ = 0.0f;
    float gustDuration_ = 0.0f;
    float peak_ = 0.0f;
    float current_ = 0.0f;
};
