#pragma once
#include "core/Config.hpp"
#include "sensors/Sensor.hpp"

// Radar: reports every vehicle inside its zone with position and relative speed.
// `final`: nothing derives from Radar, which also lets the compiler call its methods directly.
class Radar final : public Sensor {
public:
    Radar(const char* name, const DetectionZone& zone) : name_(name), zone_(zone) {}

    void update(const Vehicle& ego, Span<Vehicle> vehicles) override {
        objects_.clear();
        for (const Vehicle& car : vehicles) {
            const DetectedObject o{car.position() - ego.position(), car.lateral() - ego.lateral(),
                                   car.speed() - ego.speed()};
            if (zone_.contains(o.longitudinal, o.lateral)) {
                objects_.push_back(o);   // a full list drops the rest, like a real sensor's object limit
            }
        }
    }

    const char* name() const override { return name_; }
    const DetectionZone& fieldOfView() const override { return zone_; }

private:
    const char* name_;
    DetectionZone zone_;
};

// Mounting positions. Front: long range, covering the own and next lanes (for cut-ins).
// Rear corners: the blind spots, from beside the car to well behind it, one lane over.
namespace radar_zones {
    constexpr DetectionZone kFront{2.0f, cfg::kSensorRange, -1.5f, 1.5f};   // starts ahead of the bumper: a car alongside is the corner radars' job
    constexpr DetectionZone kRearLeft{-15.0f, 3.0f, -1.5f, -0.5f};
    constexpr DetectionZone kRearRight{-15.0f, 3.0f, 0.5f, 1.5f};
} // namespace radar_zones
