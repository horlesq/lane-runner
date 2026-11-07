#pragma once
// Colours and small helpers shared by the Renderer2D source files. Internal to render/.
#include <SDL.h>
#include <cmath>
#include <cstdint>

namespace color {
    constexpr SDL_Color kGrassLight{46, 184, 104, 255};
    constexpr SDL_Color kGrassDark{39, 170, 96, 255};
    constexpr SDL_Color kAsphalt{74, 84, 100, 255};
    constexpr SDL_Color kAsphaltEdge{64, 72, 86, 255};
    constexpr SDL_Color kMarking{236, 238, 242, 255};
    constexpr SDL_Color kCurbRed{226, 76, 60, 255};
    constexpr SDL_Color kCurbWhite{242, 242, 246, 255};

    constexpr SDL_Color kPanel{24, 28, 40, 255};
    constexpr SDL_Color kPanelEdge{52, 60, 82, 255};
    constexpr SDL_Color kLabel{132, 144, 168, 255};
    constexpr SDL_Color kText{240, 242, 248, 255};
    constexpr SDL_Color kTitle{255, 204, 0, 255};
    constexpr SDL_Color kActive{46, 204, 113, 255};
    constexpr SDL_Color kDegraded{243, 156, 18, 255};
    constexpr SDL_Color kLocked{68, 76, 96, 255};
    constexpr SDL_Color kBarBack{40, 46, 62, 255};

    constexpr SDL_Color kDim{12, 14, 22, 175};
    constexpr SDL_Color kHighlight{255, 204, 0, 36};
    constexpr SDL_Color kCrash{236, 80, 64, 255};
} // namespace color

namespace draw_util {

// True for half of every period: used for blinking prompts.
inline bool blink(Uint32 periodMs) {
    return (SDL_GetTicks() / (periodMs / 2)) % 2 == 0;
}

// Positive remainder, so patterns keep moving smoothly for any scroll value.
inline float wrap(float value, float period) {
    const float m = std::fmod(value, period);
    return m < 0.0f ? m + period : m;
}

// Cheap deterministic hash: the same scenery slot always gets the same tree.
inline std::uint32_t hash(std::int64_t n) {
    auto x = static_cast<std::uint32_t>(n) * 2654435761u;
    x ^= x >> 15;
    return x * 2246822519u;
}

} // namespace draw_util
