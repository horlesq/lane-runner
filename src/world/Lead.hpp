#pragma once
#include <optional>
#include <cmath>
#include "core/Config.hpp"
#include "world/Vehicle.hpp"
#include "world/Traffic.hpp"



struct Lead {
    float gap;             // metres, bumper to bumper
    float relativeSpeed;   // metres/second, lead speed - ego speed (negative = closing in)
};

// Works on any container of Vehicles (std::array, a test's hand-placed cars, ...).
template <typename Cars>
std::optional<Lead> findLead(const Vehicle& ego, const Cars& cars) {
    float nearestDistance = cfg::kSensorRange;
    float nearestSpeed = 0.0f;
    bool found = false;

    // find the nearest car ahead in the same lane, within sensor range
    for (const Vehicle& car : cars) {
        float distance = car.position() - ego.position();

        const bool inMyLane = std::abs(car.lateral() - ego.lateral()) < cfg::kCarWidthLanes;
        const bool ahead = distance > 0.0f;

        if (!inMyLane || !ahead) {
            continue;   // not a candidate, next car
        }
        
        if (distance < nearestDistance) {   // closer than the best so far (and within sensor range)
            nearestDistance = distance;
            nearestSpeed = car.speed();
            found = true;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return Lead{nearestDistance - cfg::kCarLength, nearestSpeed - ego.speed()};
}

// The game's version: search the live traffic.
inline std::optional<Lead> findLead(const Vehicle& ego, const Traffic& traffic) {
    return findLead(ego, traffic.cars());
}
