#pragma once
#include <cmath>
#include "core/Config.hpp"
#include "world/Vehicle.hpp"

inline bool collides(const Vehicle& a, const Vehicle& b) {
    return (std::abs(a.lateral() - b.lateral()) < cfg::kCarWidthLanes) && (std::abs(a.position() - b.position()) < cfg::kCarLength);
}
