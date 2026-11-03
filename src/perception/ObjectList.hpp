#pragma once
#include <cstddef>
#include <optional>
#include "core/Config.hpp"
#include "core/FixedVector.hpp"
#include "world/Lead.hpp"

// One object reported by a sensor, relative to the player's car.
// Member order: all floats, so there is no padding between them.
struct DetectedObject {
    float longitudinal = 0.0f;    // m, centre to centre, + = ahead
    float lateral = 0.0f;         // lanes, + = to the right
    float relativeSpeed = 0.0f;   // m/s, object - ego (negative = closing in from ahead / falling back from behind)
    float length = cfg::kCarLength;      // m, estimated size: a radar can tell a motorcycle from a car
    float width = cfg::kCarWidthLanes;   // lanes
};

constexpr std::size_t kMaxObjects = 16;
using ObjectList = FixedVector<DetectedObject, kMaxObjects>;

// Lead selection: the nearest object ahead that overlaps the ego's path.
// Same rule as findLead(), but working on what a sensor reported instead of the simulation's truth.
inline std::optional<Lead> selectLead(const ObjectList& objects) {
    const DetectedObject* nearest = nullptr;
    for (const DetectedObject& o : objects) {
        const float halfWidths = (cfg::kCarWidthLanes + o.width) / 2.0f;   // the ego is a car
        const bool inPath = o.lateral > -halfWidths && o.lateral < halfWidths;
        if (inPath && o.longitudinal > 0.0f && (!nearest || o.longitudinal < nearest->longitudinal)) {
            nearest = &o;
        }
    }
    if (!nearest) {
        return std::nullopt;
    }
    return Lead{nearest->longitudinal - (cfg::kCarLength + nearest->length) / 2.0f, nearest->relativeSpeed};
}
