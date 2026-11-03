#pragma once
#include "core/Config.hpp"
#include "sensors/Sensor.hpp"

// Front camera: shorter range than the radar, but it recognises what it sees (a ghost reflection is
// not a car to a camera) and it sees the lane markings LKA needs. Fog blinds it completely.
class Camera final : public Sensor {
public:
    Camera(const char* name, const DetectionZone& zone) : name_(name), zone_(zone) {}

    void update(const Vehicle& ego, Span<Vehicle> vehicles, const SensorEnvironment& env) override {
        objects_.clear();
        available_ = !env.fog;
        if (!available_) {
            return;   // blind: report nothing, and say so through available()
        }
        for (const Vehicle& car : vehicles) {
            const DetectedObject o{car.position() - ego.position(), car.lateral() - ego.lateral(),
                                   car.speed() - ego.speed(), car.length(), car.width()};
            if (zone_.contains(o.longitudinal, o.lateral)) {
                objects_.push_back(o);
            }
        }
    }

    const char* name() const override { return name_; }
    const DetectionZone& fieldOfView() const override { return zone_; }

private:
    const char* name_;
    DetectionZone zone_;
};

namespace camera_zones {
    constexpr DetectionZone kFront{2.0f, 35.0f, -1.5f, 1.5f};   // m ahead; lanes either side
} // namespace camera_zones
