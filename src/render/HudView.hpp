#pragma once
#include <array>
#include <cstdint>

// Plain data the game fills in every frame so the renderer can draw the side panel.
// The renderer only reads it; it knows nothing about the classes that produce it.

enum class AssistLamp : std::uint8_t { Locked, Off, Active, Degraded };

struct AssistView {
    const char* name;
    AssistLamp lamp;
    float level;      // 0..1, remaining power-up time
};

struct HudView {
    int score = 0;
    int best = 0;
    int speedKmh = 0;
    std::array<AssistView, 5> assists{{
        {"ACC", AssistLamp::Locked, 0.0f},
        {"AEB", AssistLamp::Locked, 0.0f},
        {"LKA", AssistLamp::Locked, 0.0f},
        {"BSW", AssistLamp::Locked, 0.0f},
        {"FUSION", AssistLamp::Locked, 0.0f},
    }};
    bool radarOk = true;
    bool cameraOk = true;
};
