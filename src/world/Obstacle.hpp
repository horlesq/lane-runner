#pragma once
#include <cmath>
#include <cstdint>
#include "world/Vehicle.hpp"

enum class ObstacleKind : std::uint8_t { Roadworks, Debris, Wreck };

// Something on the road that doesn't move: a closed lane at roadworks, debris, a crashed car.
// Everyone uses the same description: NPC drivers brake and merge for it, sensors report it,
// and touching it is a crash.
struct StaticObstacle {
    float lateral;     // lanes, centre
    float halfWidth;   // lanes
    float start;       // m along the road: the face traffic drives towards
    float end;         // m
    bool weakRadarEcho = false;   // small or soft objects (debris) are hard for a radar to see
    ObstacleKind kind = ObstacleKind::Roadworks;
};

// Does the obstacle overlap the vehicle sideways?
inline bool overlapsSideways(const StaticObstacle& o, const Vehicle& v) {
    return std::abs(v.lateral() - o.lateral) < o.halfWidth + v.width() / 2.0f;
}

// Does the vehicle touch the obstacle?
inline bool hits(const StaticObstacle& o, const Vehicle& v) {
    const float front = v.position() + v.length() / 2.0f;
    const float back = v.position() - v.length() / 2.0f;
    return overlapsSideways(o, v) && front > o.start && back < o.end;
}
