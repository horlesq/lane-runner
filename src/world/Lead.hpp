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
    const Vehicle* nearest = nullptr;

    // find the nearest vehicle ahead that overlaps my path, within sensor range
    for (const Vehicle& car : cars) {
        const float distance = car.position() - ego.position();
        if (!overlapSideways(car, ego) || distance <= 0.0f || distance >= cfg::kSensorRange) {
            continue;   // not a candidate, next car
        }
        if (!nearest || distance < nearest->position() - ego.position()) {
            nearest = &car;
        }
    }

    if (!nearest) {
        return std::nullopt;
    }
    return Lead{bumperGap(ego, *nearest), nearest->speed() - ego.speed()};
}

// The game's version: search the live traffic.
inline std::optional<Lead> findLead(const Vehicle& ego, const Traffic& traffic) {
    return findLead(ego, traffic.cars());
}
