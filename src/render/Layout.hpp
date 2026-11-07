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

    constexpr float kPixelsPerMeter = 20.0f; // 1 meter in the game world is 20 pixels on the screen
    constexpr float kEgoScreenY = 380.0f;    // ego is drawn at a fixed height; the world scrolls past it

    // Screen y of a point `position` metres along the road, with the ego drawn at kEgoScreenY.
    constexpr float screenY(float position, float egoPosition) {
        return kEgoScreenY - (position - egoPosition) * kPixelsPerMeter;
    }

    // Road-relative x of a lateral position in lanes (0 = centre of the leftmost lane).
    constexpr float laneX(float lateral) {
        return (lateral + 0.5f) * cfg::kLaneWidth;
    }

    static_assert(kPanelWidth >= 160, "Side panel too narrow for the HUD text");

} // namespace layout
