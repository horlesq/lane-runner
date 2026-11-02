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

    constexpr float kFollowGap = 12.0f;      // NPC brakes if closer than this to the car ahead
    constexpr float kSpawnClearance = 15.0f; // minimum free space around a respawned car
    constexpr float kDespawnBehind = 10.0f;  // NPC this far behind the ego is recycled
    constexpr float kDespawnAhead = 60.0f;   // NPC this far ahead of the ego is recycled


    static_assert(kRoadWidth <= kScreenWidth, "Road does not fit on screen");

} // namespace cfg

