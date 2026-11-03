#pragma once

namespace cfg {

    constexpr int kScreenWidth = 640;
    constexpr int kScreenHeight = 480;
    constexpr int kLaneCount = 4;
    constexpr int kLaneWidth = 100;
    constexpr int kRoadWidth = kLaneCount * kLaneWidth;
    constexpr float kCycleTime = 0.02f;

    constexpr float kCarLength = 4.2f;        // metres (sprite is 4.6 m; slightly smaller hitbox feels fair)
    constexpr float kCarWidthLanes = 0.45f;   // car width in lanes (sprite is 0.5 lane)

    constexpr float kSpawnClearance = 15.0f; // minimum free space around a respawned car
    constexpr float kDespawnBehind = 30.0f;  // NPC this far behind the ego is recycled (far off-screen)
    constexpr float kDespawnAhead = 70.0f;   // NPC this far ahead of the ego is recycled

    constexpr float kSensorRange = 60.0f;   // metres; cars farther ahead are not detected


    static_assert(kRoadWidth <= kScreenWidth, "Road does not fit on screen");

} // namespace cfg

