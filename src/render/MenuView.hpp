#pragma once
#include <array>
#include <cstddef>

// Plain data describing the title menu; the renderer only reads it.

struct MenuItemView {
    const char* label;
    bool enabled;     // disabled items are shown greyed out with a "SOON" tag
};

struct MenuView {
    std::array<MenuItemView, 3> items{{
        {"START", true},
        {"SHOP", false},
        {"QUIT", true},
    }};
    std::size_t selected = 0;
    int best = 0;
};
