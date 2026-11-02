#pragma once

namespace cfg {

    constexpr int kScreenWidth = 640;
    constexpr int kScreenHeight = 480;
    constexpr int kLaneCount = 4;
    constexpr int kLaneWidth = 100;
    constexpr int kRoadWidth = kLaneCount * kLaneWidth;
    constexpr float kCycleTime = 0.02f; 
    constexpr float kEgoScreenY = 380.0f;
    

    static_assert(kRoadWidth <= kScreenWidth, "Road does not fit on screen");

} // namespace cfg

