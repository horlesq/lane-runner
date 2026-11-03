#pragma once
#include <cmath>
#include "perception/ObjectList.hpp"
#include "sensors/Sensor.hpp"

// Radar + camera fusion for the front view. Each sensor has a weakness the other covers:
//  - radar: long range, works in fog, but reports ghosts (bridges),
//  - camera: knows a real car when it sees one, but short range and blind in fog.
// Rule per radar object:
//  - inside the camera's view and the camera works: keep it only if the camera confirms it (ghosts drop out),
//  - beyond the camera's range, or camera blind: keep it, with lower confidence (radar alone).
namespace fusion {

    constexpr float kMatchLongitudinal = 3.0f;   // m
    constexpr float kMatchLateral = 0.35f;       // lanes
    constexpr float kRadarOnlyConfidence = 0.6f;

    inline bool confirms(const ObjectList& camera, const DetectedObject& r) {
        for (const DetectedObject& c : camera) {
            if (std::abs(c.longitudinal - r.longitudinal) < kMatchLongitudinal && std::abs(c.lateral - r.lateral) < kMatchLateral) {
                return true;
            }
        }
        return false;
    }

    inline ObjectList fuse(const Sensor& radar, const Sensor& camera) {
        ObjectList fused;
        const DetectionZone& view = camera.fieldOfView();
        for (DetectedObject o : radar.objects()) {
            const bool cameraCanCheck = camera.available() && view.contains(o.longitudinal, o.lateral);
            if (cameraCanCheck) {
                if (!confirms(camera.objects(), o)) {
                    continue;   // radar-only object right in front of a working camera: a ghost
                }
                o.confidence = 1.0f;
            } else {
                o.confidence = kRadarOnlyConfidence;
            }
            fused.push_back(o);
        }
        return fused;
    }

} // namespace fusion
