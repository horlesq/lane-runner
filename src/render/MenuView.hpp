#pragma once
#include <array>
#include <cstddef>

// Plain data describing the title menu; the renderer only reads it.

struct MenuItemView {
    const char* label;
    bool enabled;     // disabled items are shown greyed out with a "SOON" tag
};

struct MenuView {
#ifdef __EMSCRIPTEN__
    static constexpr std::size_t kItemCount = 5;   // a browser tab has no QUIT
#else
    static constexpr std::size_t kItemCount = 6;
#endif
    std::array<MenuItemView, kItemCount> items{{
        {"START", true},
        {"GARAGE", true},
        {"SHOP", true},
        {"HOW TO PLAY", true},
        {"ASSISTS", true},
#ifndef __EMSCRIPTEN__
        {"QUIT", true},
#endif
    }};
    std::size_t selected = 0;
    int best = 0;
    int coins = 0;
};

// The shop screen: one row per upgrade, then BACK.
struct ShopRowView {
    const char* name;
    const char* description;
    int level;
    int maxLevel;
    int price;          // 0 = maxed out
    bool affordable;
};

struct ShopView {
    std::array<ShopRowView, 6> rows{};
    std::size_t selected = 0;   // rows.size() = BACK
    int coins = 0;
};

// The garage: every car side by side.
struct GarageCarView {
    const char* name;
    const char* strength;
    const char* weakness;
    int spriteModel;
    int price;
    bool owned;
    bool inUse;
    bool affordable;
};

struct GarageView {
    std::array<GarageCarView, 5> cars{};
    std::size_t selected = 0;
    int coins = 0;
};
