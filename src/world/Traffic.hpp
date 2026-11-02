#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <random>
#include "world/Vehicle.hpp"
#include "core/Config.hpp"

class Traffic {
public:
    static constexpr std::size_t kMaxCars = 5;
    static constexpr std::size_t kVariantCount = 20;

    // initialize cars in lanes 0..3, spaced out along the road
    Traffic(){
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            cars_[i].reset(static_cast<int>(i % cfg::kLaneCount), 14.0f, 
                    30.0f + 25.0f * static_cast<float>(i), 
                    static_cast<int>(i % kVariantCount));
        }
    }

    // move cars, respawn the ones left behind
    void step(float dt, const Vehicle& ego) {
        for (Vehicle& car : cars_) {
            float nearestGap = 1000.0f;
            float leaderSpeed = 0.0f;

            // find the nearest car ahead in the same lane
            for (const Vehicle& other : cars_) {
                if (&other == &car) continue;              
                if (other.lane() != car.lane()) continue; 
                const float gap = other.position() - car.position();
                if (gap > 0.0f && gap < nearestGap) {  
                    nearestGap = gap;
                    leaderSpeed = other.speed();
                }
            }

            // brake only if the car ahead is close AND slower; otherwise keep cruising
            if (nearestGap < cfg::kFollowGap && car.speed() > leaderSpeed) {
                car.setAcceleration(-6.0f);
            } else {
                car.setAcceleration(0.0f);
            }

            car.step(dt);

            const float relative = car.position() - ego.position();   // + = ahead of me, - = behind
            if (relative < -cfg::kDespawnBehind || relative > cfg::kDespawnAhead) {
                respawn(car, ego.position());
            }
        }
    }


    const std::array<Vehicle, kMaxCars>& cars() const { return cars_; }

private:
    void respawn(Vehicle& car, float egoPosition) {
        std::uniform_int_distribution<int> laneDist(0, cfg::kLaneCount - 1);
        std::uniform_real_distribution<float> speedDist(10.0f, 18.0f);
        std::uniform_real_distribution<float> gapDist(35.0f, 50.0f);
        std::uniform_int_distribution<int> variantDist(0, kVariantCount - 1);
        
        const int lane = laneDist(rng_);
        const float speed = speedDist(rng_);
        const float position = egoPosition + gapDist(rng_);
        const int variant = variantDist(rng_);

        if (!isFree(car, lane, position)) {
            return;
        }

        car.reset(lane, speed, position, variant);

    }

    bool isFree(const Vehicle& self, int lane, float position) const {
        for (const Vehicle& other : cars_) {
            if (&other == &self) continue;
            if (other.lane() == lane && std::abs(other.position() - position) < cfg::kSpawnClearance) {
                return false;
            }
        }
        return true;
    }


    std::array<Vehicle, kMaxCars> cars_;
    std::mt19937 rng_{42};                        // random generator, fixed seed
};
