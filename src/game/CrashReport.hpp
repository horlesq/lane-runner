#pragma once
#include <cmath>
#include <optional>
#include "core/Span.hpp"
#include "world/Collision.hpp"
#include "world/Obstacle.hpp"
#include "world/Vehicle.hpp"

// What the player crashed into, in words, plus a tip about the assist that would have helped.
struct CrashReport {
    const char* what;
    const char* tip;
};

// The first thing the ego is touching this cycle, if any. Hitting the road-side barrier comes last:
// if a car pushed you into it, the car is the more useful answer.
inline std::optional<CrashReport> describeCrash(const Vehicle& ego, Span<Vehicle> vehicles, Span<StaticObstacle> obstacles) {
    for (const Vehicle& v : vehicles) {
        if (!collides(ego, v)) {
            continue;
        }
        const bool bike = v.type() == VehicleType::Motorcycle;
        const float behind = ego.position() - v.position();          // > 0: it is behind me
        const float halfLengths = (ego.length() + v.length()) / 2.0f;
        if (behind > 0.6f * halfLengths) {
            return CrashReport{bike ? "REAR-ENDED BY A MOTORCYCLE" : "REAR-ENDED BY A CAR",
                               "BSW WARNS ABOUT FAST TRAFFIC COMING FROM BEHIND"};
        }
        if (std::abs(behind) < 0.6f * halfLengths) {
            return CrashReport{bike ? "SIDESWIPED A MOTORCYCLE" : "SIDESWIPED A CAR",
                               "BSW BLOCKS LANE CHANGES INTO CARS BESIDE YOU"};
        }
        return CrashReport{bike ? "INTO A MOTORCYCLE" : "INTO THE CAR AHEAD",
                           "ACC KEEPS A SAFE GAP - AEB BRAKES IF IT GETS TOO CLOSE"};
    }
    for (const StaticObstacle& o : obstacles) {
        if (!hits(o, ego)) {
            continue;
        }
        switch (o.kind) {
            case ObstacleKind::Debris:
                return CrashReport{"INTO DEBRIS", "FUSION: THE CAMERA SEES DEBRIS THE RADAR MISSES"};
            case ObstacleKind::Wreck:
                return CrashReport{"INTO A WRECK", "AEB BRAKES FOR STOPPED CARS IN YOUR LANE"};
            case ObstacleKind::Roadworks:
                return CrashReport{"INTO THE ROADWORKS", "WATCH THE ARROWS AND MERGE EARLY"};
        }
    }
    if (ego.hitCurb()) {
        return CrashReport{"INTO THE BARRIER", "LKA KEEPS YOU IN YOUR LANE AGAINST THE WIND"};
    }
    return std::nullopt;
}
