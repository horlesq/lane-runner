#pragma once
#include <algorithm>
#include "core/Config.hpp"
#include "sensors/Sensor.hpp"

// Front camera: shorter range than the radar, but it recognises what it sees (a ghost reflection is
// not a car to a camera) and it sees the lane markings LKA needs. Fog blinds it completely.
class Camera final : public Sensor {
public:
    Camera(const char* name, const DetectionZone& zone) : name_(name), zone_(zone), view_(zone) {}

    void update(const Vehicle& ego, Span<Vehicle> vehicles, const SensorEnvironment& env) override {
        objects_.clear();
        available_ = !env.fog && !env.glare;
        view_ = zone_;
        if (env.rain) {
            view_.maxLongitudinal = std::min(zone_.maxLongitudinal, kRainRange);   // rain shortens the view
        }
        if (!available_) {
            return;   // blind: report nothing, and say so through available()
        }
        for (const Vehicle& car : vehicles) {
            const DetectedObject o{car.position() - ego.position(), car.lateral() - ego.lateral(),
                                   car.speed() - ego.speed(), car.length(), car.width()};
            if (view_.contains(o.longitudinal, o.lateral)) {
                objects_.push_back(o);
            }
        }
        for (const StaticObstacle& s : env.obstacles) {   // the camera recognises barriers and debris
            const DetectedObject d = detectStatic(ego, s);
            if (view_.contains(d.longitudinal, d.lateral)) {
                objects_.push_back(d);
            }
        }
    }

    const char* name() const override { return name_; }
    // What it can see right now (shorter in rain), which is what fusion must use to judge it.
    const DetectionZone& fieldOfView() const override { return view_; }

private:
    static constexpr float kRainRange = 20.0f;   // m

    const char* name_;
    DetectionZone zone_;   // clear-weather view
    DetectionZone view_;   // this cycle's view
};

namespace camera_zones {
    constexpr DetectionZone kFront{2.0f, 35.0f, -1.5f, 1.5f};   // m ahead; lanes either side
} // namespace camera_zones
