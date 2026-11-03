#pragma once
#include <cmath>
#include "core/Config.hpp"
#include "world/Vehicle.hpp"

// Hitboxes overlap both sideways and lengthwise (each vehicle has its own size: car or motorcycle).
inline bool collides(const Vehicle& a, const Vehicle& b) {
    return overlapSideways(a, b) && std::abs(a.position() - b.position()) < (a.length() + b.length()) / 2.0f;
}
