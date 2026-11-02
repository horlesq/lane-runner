#pragma once
#include <cstddef>
#include <random>
#include <array>
#include "world/Vehicle.hpp"
#include "core/Config.hpp"

class Traffic {
public:
    static constexpr std::size_t kMaxCars = 5;

    // initialize cars in lanes 0..3, spaced out along the road
    Traffic(){
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            cars_[i].reset(static_cast<int>(i % cfg::kLaneCount), 14.0f, 30.0f + 25.0f * static_cast<float>(i));
        }
    }

    // move cars, respawn the ones left behind
    void step(float dt, const Vehicle& ego) {
        for (Vehicle& car : cars_) {
            car.step(dt);

            const float relative = car.position() - ego.position();   // + = ahead of me, - = behind
            if (relative < -10.0f || relative > 60.0f) {
                respawn(car, ego.position());
            }
        }
    }


    const std::array<Vehicle, kMaxCars>& cars() const { return cars_; }

private:
    void respawn(Vehicle& car, float egoPosition) {
        std::uniform_int_distribution<int> lane(0, cfg::kLaneCount - 1);
        std::uniform_real_distribution<float> speed(10.0f, 18.0f);
        std::uniform_real_distribution<float> gap(35.0f, 50.0f);
        car.reset(lane(rng_), speed(rng_), egoPosition + gap(rng_));
    }


    std::array<Vehicle, kMaxCars> cars_;
    std::mt19937 rng_{42};                        // random generator, fixed seed
};
