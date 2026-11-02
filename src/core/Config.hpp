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

    

    static_assert(kRoadWidth <= kScreenWidth, "Road does not fit on screen");

} // namespace cfg

