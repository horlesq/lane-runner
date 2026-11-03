#pragma once
#include "core/Span.hpp"
#include "perception/ObjectList.hpp"
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
    virtual void update(const Vehicle& ego, Span<Vehicle> vehicles) = 0;
    virtual const char* name() const = 0;
    virtual const DetectionZone& fieldOfView() const = 0;

    const ObjectList& objects() const { return objects_; }

protected:
    Sensor() = default;   // only derived classes can be created
    ObjectList objects_;
};
