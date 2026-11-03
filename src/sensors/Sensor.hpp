#pragma once
#include <optional>
#include "core/Span.hpp"
#include "perception/ObjectList.hpp"
#include "world/Obstacle.hpp"
#include "world/Vehicle.hpp"

// Area a sensor covers, relative to the centre of the player's car.
struct DetectionZone {
    float minLongitudinal;   // m, + = ahead
    float maxLongitudinal;
    float minLateral;        // lanes, + = to the right
    float maxLateral;

    bool contains(float longitudinal, float lateral) const {
        return longitudinal >= minLongitudinal && longitudinal <= maxLongitudinal &&
               lateral >= minLateral && lateral <= maxLateral;
    }
};

// Conditions that affect what sensors can see this cycle (filled from the road hazards).
struct SensorEnvironment {
    bool fog = false;                      // camera can't see
    std::optional<float> metalBridgeAhead; // m; radar waves bounce off it and create a ghost object
    Span<StaticObstacle> obstacles;        // static things on the road (barriers, debris)
    bool rain = false;                     // camera sees less far
    bool glare = false;                    // camera dazzled (tunnel entrance/exit)
};

// What a sensor reports for a static obstacle: its face, relative to the ego, standing still.
inline DetectedObject detectStatic(const Vehicle& ego, const StaticObstacle& o) {
    constexpr float kDepth = 1.0f;   // m: only the face matters for the gap
    DetectedObject d{o.start + kDepth / 2.0f - ego.position(), o.lateral - ego.lateral(), -ego.speed(), kDepth, 2.0f * o.halfWidth};
    return d;
}

// Interface for every sensor. The rest of the code only talks to Sensor, so a radar, a camera or a
// test double can be swapped in without changing the functions that use the object list.
//
// In a real car a sensor measures echoes or pixels; in the game a sensor model turns the
// simulation's ground truth into what that sensor would report (its field of view, later noise).
class Sensor {
public:
    // Virtual: sensors are owned as std::unique_ptr<Sensor>. Deleting a derived object through a base
    // pointer without a virtual destructor is undefined behaviour (the derived part is never destroyed).
    virtual ~Sensor() = default;

    // Polymorphic objects are not copied: copying through the base would "slice" off the derived part.
    Sensor(const Sensor&) = delete;
    Sensor& operator=(const Sensor&) = delete;

    // Every 20 ms: refresh objects() from the vehicles around the ego (a view, nothing is copied).
    virtual void update(const Vehicle& ego, Span<Vehicle> vehicles, const SensorEnvironment& env) = 0;
    virtual const char* name() const = 0;
    virtual const DetectionZone& fieldOfView() const = 0;

    const ObjectList& objects() const { return objects_; }

    // Health: false while the sensor can't work (e.g. a blinded camera). Its object list is then empty
    // and users must switch to a degraded mode instead of trusting "no objects" as "road is clear".
    bool available() const { return available_; }

protected:
    Sensor() = default;   // only derived classes can be created
    ObjectList objects_;
    bool available_ = true;
};
