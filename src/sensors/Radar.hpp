#pragma once
#include "core/Config.hpp"
#include "sensors/Sensor.hpp"

// Radar: reports every vehicle inside its zone with position and relative speed. It sees through fog,
// but metal structures like bridges reflect its waves: the front radar then reports a "ghost" object.
// `final`: nothing derives from Radar, which also lets the compiler call its methods directly.
class Radar final : public Sensor {
public:
    Radar(const char* name, const DetectionZone& zone, bool seesBridgeGhosts = false)
        : name_(name), zone_(zone), seesBridgeGhosts_(seesBridgeGhosts) {}

    void update(const Vehicle& ego, Span<Vehicle> vehicles, const SensorEnvironment& env) override {
        objects_.clear();
        if (seesBridgeGhosts_ && env.metalBridgeAhead) {
            // Looks like a stopped car in my lane under the bridge: the classic cause of phantom braking.
            objects_.push_back(DetectedObject{*env.metalBridgeAhead, 0.0f, -ego.speed()});
        }
        for (const StaticObstacle& o : env.obstacles) {
            const DetectedObject d = detectStatic(ego, o);
            // Small, soft objects reflect little: the radar only picks them up when close.
            const bool visible = !o.weakRadarEcho || d.longitudinal < kWeakEchoRange;
            if (visible && zone_.contains(d.longitudinal, d.lateral)) {
                objects_.push_back(d);
            }
        }
        for (const Vehicle& car : vehicles) {
            const DetectedObject o{car.position() - ego.position(), car.lateral() - ego.lateral(),
                                   car.speed() - ego.speed(), car.length(), car.width()};
            if (zone_.contains(o.longitudinal, o.lateral)) {
                objects_.push_back(o);   // a full list drops the rest, like a real sensor's object limit
            }
        }
    }

    const char* name() const override { return name_; }
    const DetectionZone& fieldOfView() const override { return zone_; }

private:
    static constexpr float kWeakEchoRange = 15.0f;   // m

    const char* name_;
    DetectionZone zone_;
    bool seesBridgeGhosts_;
};

// Mounting positions. Front: long range, covering the own and next lanes (for cut-ins).
// Rear corners: the blind spots beside the car, and 40 m back for fast cars closing in (lane change assist).
namespace radar_zones {
    constexpr DetectionZone kFront{2.0f, cfg::kSensorRange, -1.5f, 1.5f};   // starts ahead of the bumper: a car alongside is the corner radars' job
    constexpr DetectionZone kRearLeft{-40.0f, 3.0f, -1.5f, -0.5f};
    constexpr DetectionZone kRearRight{-40.0f, 3.0f, 0.5f, 1.5f};
} // namespace radar_zones
