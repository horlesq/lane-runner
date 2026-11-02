#pragma once
#include "core/Config.hpp"

// Screen layout in pixels (logical 640x480). Game logic never needs these.
namespace layout {

    constexpr int kRoadX = 40;                          // grass verge left of the road
    constexpr int kRoadRight = kRoadX + cfg::kRoadWidth;
    constexpr int kPanelX = kRoadRight + 24;            // thin grass verge, then the side panel
    constexpr int kPanelWidth = cfg::kScreenWidth - kPanelX;

    constexpr int kCarWidth = 50;
    constexpr int kCarHeight = 92;

    constexpr int kWindowWidth = cfg::kScreenWidth * 3 / 2;    // PC window; rendering is scaled
    constexpr int kWindowHeight = cfg::kScreenHeight * 3 / 2;

    static_assert(kPanelWidth >= 160, "Side panel too narrow for the HUD text");

} // namespace layout
