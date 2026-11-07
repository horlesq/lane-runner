#include "render/Renderer2D.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "render/BitmapFont.hpp"
#include "render/DrawHelpers.hpp"
#include "render/Layout.hpp"

// Road, vehicles, hazards and debug views. The HUD panel and full-screen overlays are in Renderer2DScreens.cpp.

using draw_util::blink;
using draw_util::hash;
using draw_util::wrap;

bool Renderer2D::loadAssets(const std::string& assetDir) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);   // lets text shadows be translucent

    const std::string png = assetDir + "racing-pack/PNG/";
    const std::array<const char*, kCarSkinCount> colours{"blue", "red", "green", "yellow", "black"};
    constexpr float kLongestSpriteHeight = 131.0f;   // pixels, sports/sedan/pickup models

    for (std::size_t skin = 0; skin < kCarSkinCount; ++skin) {
        for (std::size_t model = 0; model < kCarModelCount; ++model) {
            const std::string file = png + "Cars/car_" + colours[skin] + "_" + std::to_string(model + 1) + ".png";
            CarSprite& sprite = cars_[skin][model];
            sprite.texture = loadTexture(r_, file);
            if (!sprite.texture) {
                return false;
            }
            int h = 0;
            SDL_QueryTexture(sprite.texture.get(), nullptr, nullptr, nullptr, &h);
            sprite.lengthScale = static_cast<float>(h) / kLongestSpriteHeight;
        }
    }
    tree_ = loadTexture(r_, png + "Objects/tree_small.png");
    treeLarge_ = loadTexture(r_, png + "Objects/tree_large.png");
    rock_ = loadTexture(r_, png + "Objects/rock2.png");
    oil_ = loadTexture(r_, png + "Objects/oil.png");
    cone_ = loadTexture(r_, png + "Objects/cone_straight.png");
    const std::array<const char*, 7> debrisFiles{"tires_white", "tires_white_alt", "barrel_red_down", "barrel_blue_down",
                                                 "rock1", "rock2", "rock3"};
    for (std::size_t i = 0; i < debrisFiles.size(); ++i) {
        debris_[i] = loadTexture(r_, png + "Objects/" + debrisFiles[i] + ".png");
        if (!debris_[i]) {
            return false;
        }
    }
    barrier_ = loadTexture(r_, png + "Objects/barrier_red.png");
    arrow_ = loadTexture(r_, png + "Objects/arrow_yellow.png");
    if (!cone_ || !barrier_ || !arrow_) {
        return false;
    }
    for (std::size_t skin = 0; skin < kCarSkinCount; ++skin) {
        motorcycles_[skin] = loadTexture(r_, png + "Motorcycles/motorcycle_" + colours[skin] + ".png");
        riders_[skin] = loadTexture(r_, png + "Characters/racer_" + colours[skin] + ".png");
        if (!motorcycles_[skin] || !riders_[skin]) {
            return false;
        }
    }
    if (!oil_) {
        return false;
    }

    menuBadge_ = loadTexture(r_, assetDir + "art/menu-logo.png");
    if (menuBadge_) {
        SDL_SetTextureScaleMode(menuBadge_.get(), SDL_ScaleModeNearest);   // blocky, like the pixel font
    }
    return tree_ && treeLarge_ && rock_;
}

void Renderer2D::drawRoad(float scrollPx) {
    // Mown-grass stripes give a sense of speed even on the verges.
    constexpr int kBand = 48;
    const int grassOffset = static_cast<int>(wrap(scrollPx, 2.0f * kBand));
    for (int y = grassOffset - 2 * kBand; y < cfg::kScreenHeight; y += 2 * kBand) {
        fillRect(0, y, cfg::kScreenWidth, kBand, color::kGrassLight);
        fillRect(0, y + kBand, cfg::kScreenWidth, kBand, color::kGrassDark);
    }

    drawScenery(scrollPx);

    // Asphalt with slightly darker shoulders.
    fillRect(layout::kRoadX, 0, cfg::kRoadWidth, cfg::kScreenHeight, color::kAsphalt);
    fillRect(layout::kRoadX, 0, 14, cfg::kScreenHeight, color::kAsphaltEdge);
    fillRect(layout::kRoadRight - 14, 0, 14, cfg::kScreenHeight, color::kAsphaltEdge);

    // Red/white curbs just outside the road.
    constexpr int kCurbWidth = 8;
    constexpr int kCurbBlock = 20;
    const int curbOffset = static_cast<int>(wrap(scrollPx, 2.0f * kCurbBlock));
    for (int y = curbOffset - 2 * kCurbBlock; y < cfg::kScreenHeight; y += 2 * kCurbBlock) {
        fillRect(layout::kRoadX - kCurbWidth, y, kCurbWidth, kCurbBlock, color::kCurbRed);
        fillRect(layout::kRoadX - kCurbWidth, y + kCurbBlock, kCurbWidth, kCurbBlock, color::kCurbWhite);
        fillRect(layout::kRoadRight, y, kCurbWidth, kCurbBlock, color::kCurbRed);
        fillRect(layout::kRoadRight, y + kCurbBlock, kCurbWidth, kCurbBlock, color::kCurbWhite);
    }

    // Solid edge lines and dashed lane lines.
    fillRect(layout::kRoadX + 6, 0, 3, cfg::kScreenHeight, color::kMarking);
    fillRect(layout::kRoadRight - 9, 0, 3, cfg::kScreenHeight, color::kMarking);

    constexpr int kDash = 34;
    constexpr int kGap = 38;
    const int dashOffset = static_cast<int>(wrap(scrollPx, kDash + kGap));
    for (int lane = 1; lane < cfg::kLaneCount; ++lane) {
        const int x = layout::kRoadX + lane * cfg::kLaneWidth - 2;
        for (int y = dashOffset - (kDash + kGap); y < cfg::kScreenHeight; y += kDash + kGap) {
            fillRect(x, y, 4, kDash, color::kMarking);
        }
    }
}

void Renderer2D::drawScenery(float scrollPx) {
    // Scenery sits in fixed "slots" along the road; slot n is n * kSpacing ahead of the start.
    constexpr float kSpacing = 90.0f;
    const auto first = static_cast<std::int64_t>(std::floor((scrollPx - cfg::kScreenHeight - 120.0f) / kSpacing));
    const auto last = static_cast<std::int64_t>(std::ceil((scrollPx + 120.0f) / kSpacing));

    for (std::int64_t n = first; n <= last; ++n) {
        const std::uint32_t h = hash(n);
        const float y = scrollPx - static_cast<float>(n) * kSpacing;
        const float jitter = static_cast<float>(h % 9u) - 4.0f;

        // Left verge: mostly trees, half off-screen so the narrow strip still looks wooded.
        if (h % 5u == 0u) {
            drawSprite(rock_.get(), 14.0f + jitter, y, 30.0f, 25.0f, true);
        } else if (h % 3u == 0u) {
            drawSprite(treeLarge_.get(), 8.0f + jitter, y, 72.0f, 72.0f, true);
        } else {
            drawSprite(tree_.get(), 12.0f + jitter, y, 54.0f, 54.0f, true);
        }

        // Right verge: an occasional bush peeking out from under the panel.
        if ((h >> 8) % 3u == 0u) {
            drawSprite(tree_.get(), static_cast<float>(layout::kPanelX) + 6.0f, y + 30.0f, 40.0f, 40.0f, true);
        }
    }
}

void Renderer2D::drawCar(float centerX, float centerY, CarSkin skin, std::size_t model, int signal, bool braking) {
    const CarSprite& sprite = cars_[static_cast<std::size_t>(skin)][model % kCarModelCount];
    const float height = static_cast<float>(layout::kCarHeight) * sprite.lengthScale;
    const float x = static_cast<float>(layout::kRoadX) + centerX;
    drawSprite(sprite.texture.get(), x, centerY, static_cast<float>(layout::kCarWidth), height, true);
    drawLamps(x, centerY, layout::kCarWidth / 2.0f - 2.0f, height, signal, braking);
}

void Renderer2D::drawMotorcycle(float centerX, float centerY, CarSkin skin, int signal, bool braking) {
    // Sprite is 44 x 100; drawn about 2.4 m long, the same scale as the cars.
    constexpr float kHeight = 48.0f;
    constexpr float kWidth = kHeight * 44.0f / 100.0f;
    const float x = static_cast<float>(layout::kRoadX) + centerX;
    drawSprite(motorcycles_[static_cast<std::size_t>(skin)].get(), x, centerY, kWidth, kHeight, true);

    // Rider (42 x 51 sprite, same scale as the bike): sits just behind the middle, arms on the handlebars.
    // Suit colour picked to contrast with the bike.
    const std::size_t suit = (static_cast<std::size_t>(skin) + 2) % kCarSkinCount;
    constexpr float kRiderHeight = 51.0f * kHeight / 100.0f;
    constexpr float kRiderWidth = 42.0f * kHeight / 100.0f;
    drawSprite(riders_[suit].get(), x, centerY + 2.0f, kRiderWidth, kRiderHeight, false);

    drawLamps(x, centerY, kWidth / 2.0f, kHeight, signal, braking);
}

void Renderer2D::drawLamps(float x, float centerY, float halfWidth, float height, int signal, bool braking) {
    // Each lamp: a soft glow reaching past the body, a dark outline so it reads on any paint, a bright core.
    const auto lamp = [this](int lx, int ly, SDL_Color glow, SDL_Color core) {
        fillRect(lx - 9, ly - 8, 18, 16, glow);
        fillRect(lx - 5, ly - 4, 10, 8, SDL_Color{20, 16, 10, 230});
        fillRect(lx - 4, ly - 3, 8, 6, core);
    };
    const int rear = static_cast<int>(centerY + height / 2.0f - 8.0f);

    if (braking) {
        for (int side : {-1, 1}) {
            lamp(static_cast<int>(x + static_cast<float>(side) * halfWidth), rear,
                 SDL_Color{255, 40, 30, 120}, SDL_Color{255, 70, 60, 255});
        }
    }
    if (signal != 0 && blink(500)) {
        const int side = static_cast<int>(x + static_cast<float>(signal) * halfWidth);
        const int front = static_cast<int>(centerY - height / 2.0f + 8.0f);
        for (int y : {front, rear}) {
            lamp(side, y, SDL_Color{255, 176, 0, 110}, SDL_Color{255, 214, 60, 255});
        }
    }
}

void Renderer2D::drawRoadworks(float laneCenterX, float startY, float endY, int mergeDir) {
    constexpr float kArrowLead1 = 140.0f;   // px before the barrier (about 7 m)
    constexpr float kArrowLead2 = 300.0f;
    if (startY + kArrowLead2 + 40.0f < 0.0f || endY > static_cast<float>(cfg::kScreenHeight)) {
        return;   // nothing of it on screen
    }
    const float x = static_cast<float>(layout::kRoadX) + laneCenterX;
    const float halfLane = static_cast<float>(cfg::kLaneWidth) / 2.0f;

    // Closed stretch: warm tint so the lane reads as "not for driving".
    const int top = std::max(0, static_cast<int>(endY));
    const int bottom = std::min(cfg::kScreenHeight, static_cast<int>(startY));
    if (bottom > top) {
        fillRect(static_cast<int>(x - halfLane), top, cfg::kLaneWidth, bottom - top, SDL_Color{255, 170, 40, 26});
    }

    // Merge arrows on the closed lane before the barrier, tilted towards the lane to merge into.
    const double angle = 55.0 * static_cast<double>(mergeDir);
    for (float lead : {kArrowLead1, kArrowLead2}) {
        const SDL_FRect a{x - 26.0f, startY + lead - 16.0f, 52.0f, 32.0f};
        SDL_RenderCopyExF(r_, arrow_.get(), nullptr, &a, angle, nullptr, SDL_FLIP_NONE);
    }

    // Cones on both lane lines, every ~2.5 m, from the barrier to the end.
    for (float y = startY; y > endY; y -= 50.0f) {
        if (y < -20.0f || y > static_cast<float>(cfg::kScreenHeight) + 20.0f) {
            continue;
        }
        for (float side : {-1.0f, 1.0f}) {
            drawSprite(cone_.get(), x + side * (halfLane - 6.0f), y, 18.0f, 18.0f, true);
        }
    }
    // Barrier across the lane at the closure.
    drawSprite(barrier_.get(), x, startY, static_cast<float>(cfg::kLaneWidth) - 8.0f, 22.0f, true);
}

void Renderer2D::drawDebris(float centerX, float centerY, int kind, unsigned seed) {
    const float x = static_cast<float>(layout::kRoadX) + centerX;
    // Random number in [-1, 1] for item i of this pile.
    const auto jitter = [&](int i) { return static_cast<float>(hash(static_cast<std::int64_t>(seed) * 97 + i) % 2001u) / 1000.0f - 1.0f; };

    // A sprite scaled so its longest side is `longest` px, rotated, with a soft shadow.
    const auto piece = [&](SDL_Texture* tex, float px, float py, float longest, double angle, SDL_Color tint = {255, 255, 255, 255}) {
        int w = 0;
        int h = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
        const float scale = longest / static_cast<float>(std::max(w, h));
        const float sw = static_cast<float>(w) * scale;
        const float sh = static_cast<float>(h) * scale;
        const SDL_FRect shadow{px - sw / 2.0f + 3.0f, py - sh / 2.0f + 4.0f, sw, sh};
        SDL_SetTextureColorMod(tex, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex, 80);
        SDL_RenderCopyExF(r_, tex, nullptr, &shadow, angle, nullptr, SDL_FLIP_NONE);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
        const SDL_FRect body{px - sw / 2.0f, py - sh / 2.0f, sw, sh};
        SDL_RenderCopyExF(r_, tex, nullptr, &body, angle, nullptr, SDL_FLIP_NONE);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
    };

    // Light scuffs on the asphalt where it hit the road and slid, just before the pile.
    for (int i = 0; i < 3; ++i) {
        const int sx = static_cast<int>(x + jitter(i) * 12.0f);
        const int sy = static_cast<int>(centerY + 12.0f + jitter(i + 3) * 3.0f);
        fillRect(sx, sy, 1, 8 + static_cast<int>(jitter(i + 6) * 3.0f), SDL_Color{200, 200, 210, 60});
    }

    SDL_Color shard{};
    switch (kind) {
        case 0: {   // tyres: two lying across each other, chunks of rubber
            piece(debris_[0].get(), x - 7.0f, centerY + jitter(10) * 3.0f, 24.0f, jitter(11) * 40.0);
            piece(debris_[1].get(), x + 9.0f, centerY - 5.0f + jitter(12) * 3.0f, 20.0f, jitter(13) * 60.0);
            shard = SDL_Color{30, 30, 34, 255};
            break;
        }
        case 1: {   // barrel on its side, leaking
            SDL_SetTextureColorMod(oil_.get(), 40, 36, 30);
            SDL_SetTextureAlphaMod(oil_.get(), 200);
            const SDL_FRect spill{x - 22.0f + jitter(20) * 4.0f, centerY - 8.0f, 44.0f, 30.0f};
            SDL_RenderCopyF(r_, oil_.get(), nullptr, &spill);
            SDL_SetTextureColorMod(oil_.get(), 255, 255, 255);
            SDL_SetTextureAlphaMod(oil_.get(), 255);
            piece(debris_[(seed & 1u) ? 2 : 3].get(), x, centerY, 30.0f, 90.0 + jitter(21) * 35.0);
            shard = SDL_Color{150, 150, 160, 255};   // bits of the lid and hoops
            break;
        }
        default: {   // rocks: one big, two small, gravel around
            const SDL_Color stone{170, 150, 125, 255};   // warm grey-brown, not the sprite's icy blue
            piece(debris_[4].get(), x - 3.0f, centerY, 26.0f, jitter(30) * 180.0, stone);
            piece(debris_[5].get(), x + 12.0f, centerY - 8.0f, 15.0f, jitter(31) * 180.0, stone);
            piece(debris_[6].get(), x - 13.0f + jitter(32) * 2.0f, centerY - 10.0f, 12.0f, jitter(33) * 180.0, stone);
            shard = SDL_Color{125, 110, 92, 255};
            break;
        }
    }
    // Small fragments scattered around the pile, a few further down the road.
    for (int i = 0; i < 9; ++i) {
        const int fx = static_cast<int>(x + jitter(40 + i) * 24.0f);
        const int fy = static_cast<int>(centerY + jitter(60 + i) * 14.0f + (i > 6 ? 18.0f : 0.0f));
        const int size = 2 + static_cast<int>(hash(static_cast<std::int64_t>(seed) + i) % 3u);
        fillRect(fx, fy, size, size, shard);
    }
}

void Renderer2D::drawWreck(float laneCenterX, float centerY, float lengthMetres) {
    const float x = static_cast<float>(layout::kRoadX) + laneCenterX;
    const float ahead = lengthMetres * layout::kPixelsPerMeter * 0.55f;
    if (centerY + 360.0f < 0.0f || centerY - ahead - 60.0f > static_cast<float>(cfg::kScreenHeight)) {
        return;
    }
    // Warning triangle on the road, well before the wrecks.
    const float ty = centerY + 300.0f;
    const SDL_Color red{226, 60, 50, 255};
    const SDL_Vertex outer[3]{{{x, ty - 12.0f}, red, {0, 0}}, {{x - 11.0f, ty + 8.0f}, red, {0, 0}}, {{x + 11.0f, ty + 8.0f}, red, {0, 0}}};
    SDL_RenderGeometry(r_, nullptr, outer, 3, nullptr, 0);
    const SDL_Color white{245, 245, 245, 255};
    const SDL_Vertex inner[3]{{{x, ty - 5.0f}, white, {0, 0}}, {{x - 5.0f, ty + 4.0f}, white, {0, 0}}, {{x + 5.0f, ty + 4.0f}, white, {0, 0}}};
    SDL_RenderGeometry(r_, nullptr, inner, 3, nullptr, 0);

    // Two cars standing skewed across the lane, shadows on, hazard lights blinking on all corners.
    struct Wreck { CarSkin skin; std::size_t model; float dx; float dy; double angle; };
    const Wreck wrecks[2]{{CarSkin::Black, 1, -8.0f, 0.0f, 28.0}, {CarSkin::Red, 4, 10.0f, -ahead, -38.0}};
    const bool on = blink(600);
    for (const Wreck& w : wrecks) {
        const CarSprite& sprite = cars_[static_cast<std::size_t>(w.skin)][w.model];
        const float h = static_cast<float>(layout::kCarHeight) * sprite.lengthScale;
        const float cx = x + w.dx;
        const float cy = centerY + w.dy;
        const SDL_FRect shadow{cx - 25.0f + 4.0f, cy - h / 2.0f + 6.0f, 50.0f, h};
        SDL_SetTextureColorMod(sprite.texture.get(), 0, 0, 0);
        SDL_SetTextureAlphaMod(sprite.texture.get(), 70);
        SDL_RenderCopyExF(r_, sprite.texture.get(), nullptr, &shadow, w.angle, nullptr, SDL_FLIP_NONE);
        SDL_SetTextureColorMod(sprite.texture.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(sprite.texture.get(), 255);
        const SDL_FRect body{cx - 25.0f, cy - h / 2.0f, 50.0f, h};
        SDL_RenderCopyExF(r_, sprite.texture.get(), nullptr, &body, w.angle, nullptr, SDL_FLIP_NONE);
        if (on) {
            const double a = w.angle * 3.14159265 / 180.0;
            const float c = static_cast<float>(std::cos(a));
            const float s = static_cast<float>(std::sin(a));
            for (float sx : {-21.0f, 21.0f}) {
                for (float sy : {-h / 2.0f + 8.0f, h / 2.0f - 8.0f}) {
                    const int lx = static_cast<int>(cx + sx * c - sy * s);
                    const int ly = static_cast<int>(cy + sx * s + sy * c);
                    fillRect(lx - 8, ly - 7, 16, 14, SDL_Color{255, 176, 0, 110});
                    fillRect(lx - 3, ly - 2, 6, 5, SDL_Color{255, 214, 60, 255});
                }
            }
        }
    }
}

void Renderer2D::drawRain(float topY, float bottomY) {
    const int top = std::max(0, static_cast<int>(topY));
    const int bottom = std::min(cfg::kScreenHeight, static_cast<int>(bottomY));
    if (bottom <= top) {
        return;
    }
    const int width = layout::kPanelX;
    fillRect(0, top, width, bottom - top, SDL_Color{30, 44, 70, 70});   // grey, overcast light
    // Falling streaks: each has a fixed column; rain falls at a constant speed, so time x speed is fine here.
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    constexpr int kDrops = 90;
    for (int i = 0; i < kDrops; ++i) {
        const std::uint32_t h = hash(i + 991);
        const float x = static_cast<float>(h % static_cast<std::uint32_t>(width));
        const float y = wrap(static_cast<float>((h >> 9) % 997u) + t * (520.0f + static_cast<float>(h % 120u)), 480.0f);
        if (y < static_cast<float>(top) || y > static_cast<float>(bottom)) {
            continue;
        }
        const auto len = static_cast<int>(10 + h % 8u);
        for (int k = 0; k < len; k += 2) {   // slight slant
            fillRect(static_cast<int>(x) - k / 4, static_cast<int>(y) + k, 1, 2, SDL_Color{190, 210, 240, 150});
        }
    }
}

void Renderer2D::drawTunnel(float topY, float bottomY) {
    constexpr int kPortal = 28;
    const int top = std::max(0, static_cast<int>(topY));
    const int bottom = std::min(cfg::kScreenHeight, static_cast<int>(bottomY));
    if (bottom + kPortal <= 0 || topY - kPortal > static_cast<float>(cfg::kScreenHeight)) {
        return;
    }
    const int width = layout::kPanelX;
    if (bottom > top) {
        fillRect(0, top, width, bottom - top, SDL_Color{6, 8, 18, 165});                                   // darkness
        fillRect(0, top, layout::kRoadX - 8, bottom - top, SDL_Color{92, 96, 108, 255});                    // walls
        fillRect(layout::kRoadRight + 8, top, width - layout::kRoadRight - 8, bottom - top, SDL_Color{92, 96, 108, 255});
        // Ceiling lights above the lane lines, moving with the road.
        const int first = static_cast<int>(topY) + static_cast<int>(wrap(-topY, 60.0f));
        for (int y = first; y < bottom; y += 60) {
            if (y < top) {
                continue;
            }
            for (int lane = 1; lane < cfg::kLaneCount; ++lane) {
                const int lx = layout::kRoadX + lane * cfg::kLaneWidth;
                fillRect(lx - 9, y - 6, 18, 12, SDL_Color{255, 210, 120, 50});
                fillRect(lx - 4, y - 2, 8, 4, SDL_Color{255, 230, 160, 255});
            }
        }
    }
    // Concrete portals at both ends.
    for (float py : {topY, bottomY}) {
        const int y = static_cast<int>(py) - kPortal / 2;
        fillRect(0, y, width, kPortal, SDL_Color{120, 124, 134, 255});
        fillRect(0, y, width, 4, SDL_Color{160, 164, 174, 255});
        fillRect(0, y + kPortal - 4, width, 4, SDL_Color{80, 84, 94, 255});
    }
}

void Renderer2D::drawBridge(float centerY) {
    constexpr int kDeck = 44;   // px, about 2 m of deck seen from above
    const int top = static_cast<int>(centerY) - kDeck / 2;
    if (top > cfg::kScreenHeight || top + kDeck + 24 < 0) {
        return;
    }
    const int width = layout::kPanelX;
    fillRect(0, top + kDeck, width, 22, SDL_Color{10, 12, 20, 90});        // shadow on the road below
    fillRect(0, top, width, kDeck, SDL_Color{150, 156, 166, 255});          // deck
    fillRect(0, top, width, 4, SDL_Color{196, 202, 210, 255});              // lit edge
    fillRect(0, top + kDeck - 4, width, 4, SDL_Color{104, 110, 122, 255});  // shaded edge
    for (int x = 6; x < width; x += 18) {                                   // steel railing posts
        fillRect(x, top + 6, 4, 4, SDL_Color{70, 76, 90, 255});
        fillRect(x, top + kDeck - 10, 4, 4, SDL_Color{70, 76, 90, 255});
    }
    fillRect(0, top + 8, width, 2, SDL_Color{70, 76, 90, 255});             // rails
    fillRect(0, top + kDeck - 8, width, 2, SDL_Color{70, 76, 90, 255});
}

void Renderer2D::drawFog(float topY, float bottomY) {
    // A dense core with long, smooth edges, plus soft overlapping "puffs" along each edge so the fog
    // boundary is cloudy and irregular instead of a straight line.
    constexpr int kFeather = 150;      // px over which the fog builds up (about 7 m)
    constexpr float kMaxAlpha = 225.0f;
    constexpr SDL_Color kFogColour{214, 219, 228, 255};
    const int top = static_cast<int>(topY);
    const int bottom = static_cast<int>(bottomY);
    if (bottom + kFeather < 0 || top - kFeather > cfg::kScreenHeight) {
        return;
    }
    const int width = layout::kPanelX;
    const auto smooth = [](float x) { return x * x * (3.0f - 2.0f * x); };   // smoothstep: no visible kink

    // 1. Base layer: density rises smoothly from 0 at the outer edge of the feather to full in the core.
    for (int y = std::max(top - kFeather, 0); y < std::min(bottom + kFeather, cfg::kScreenHeight); y += 2) {
        float density = 1.0f;
        if (y < top) {
            density = smooth(1.0f - static_cast<float>(top - y) / kFeather);
        } else if (y > bottom) {
            density = smooth(1.0f - static_cast<float>(y - bottom) / kFeather);
        }
        density *= 0.75f;   // the puffs add the rest, so the edge gets lumpy
        fillRect(0, y, width, 2, SDL_Color{kFogColour.r, kFogColour.g, kFogColour.b, static_cast<Uint8>(kMaxAlpha * density)});
    }

    // 2. Puffs: soft discs (alpha fades towards the rim) scattered along both edges, drifting sideways.
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const auto puff = [&](float cx, float cy, float radius, float strength) {
        for (float dy = -radius; dy <= radius; dy += 2.0f) {
            const float y = cy + dy;
            if (y < 0.0f || y >= static_cast<float>(cfg::kScreenHeight)) {
                continue;
            }
            const float k = 1.0f - (dy / radius) * (dy / radius);
            const float halfWidth = radius * std::sqrt(k);
            const auto alpha = static_cast<Uint8>(strength * k * k);
            fillRect(static_cast<int>(cx - halfWidth), static_cast<int>(y), static_cast<int>(2.0f * halfWidth), 2,
                     SDL_Color{kFogColour.r, kFogColour.g, kFogColour.b, alpha});
        }
    };
    constexpr int kPuffsPerEdge = 9;
    for (int edge = 0; edge < 2; ++edge) {
        const float edgeY = edge == 0 ? static_cast<float>(top) : static_cast<float>(bottom);
        const float outward = edge == 0 ? -1.0f : 1.0f;   // puffs reach out of the fog into clear air
        for (int i = 0; i < kPuffsPerEdge; ++i) {
            const std::uint32_t h = hash(i * 31 + edge * 7 + 5);
            const float radius = 36.0f + static_cast<float>(h % 50u);
            const float drift = t * (6.0f + static_cast<float>((h >> 6) % 10u));
            const float cx = wrap(static_cast<float>(h % 997u) + drift, static_cast<float>(width) + 2.0f * radius) - radius;
            const float cy = edgeY + outward * static_cast<float>((h >> 10) % 70u);
            puff(cx, cy, radius, 120.0f);
        }
    }
}

void Renderer2D::drawOil(float centerX, float centerY) {
    // ~2.8 m x 2.4 m slick, flat on the road (no drop shadow): darkened so it stands out from the
    // asphalt, with a smaller bluish sheen on top like light on a wet film.
    SDL_Texture* tex = oil_.get();
    const float x = static_cast<float>(layout::kRoadX) + centerX;
    SDL_SetTextureColorMod(tex, 70, 62, 80);
    drawSprite(tex, x, centerY, 60.0f, 52.0f, false);
    SDL_SetTextureColorMod(tex, 150, 190, 255);
    SDL_SetTextureAlphaMod(tex, 70);
    drawSprite(tex, x - 6.0f, centerY - 6.0f, 26.0f, 20.0f, false);
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
}

void Renderer2D::drawWind(float wind, float scrollPx) {
    // Integrate the air's movement every frame (also when calm, so nothing jumps when a gust starts).
    constexpr float kPxPerWind = 420.0f;   // screen px/s per lane/s of wind
    const Uint32 now = SDL_GetTicks();
    const float frameDt = windTicks_ == 0 ? 0.0f : std::min(static_cast<float>(now - windTicks_) / 1000.0f, 0.1f);
    windTicks_ = now;
    windTravel_ += wind * kPxPerWind * frameDt;

    const float strength = std::min(std::fabs(wind), 1.0f);
    if (strength < 0.05f) {
        return;
    }
    // Flat, smooth shapes like the rest of the art:
    //  - gust glyphs: a tapering white stroke that curls up at its head (the weather-map wind symbol),
    //  - leaves: pointed ovals in the trees' greens with a darker midrib, spinning as they tumble.
    // Each item has a fixed random row; it travels with the wind and scrolls down with the road.
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const float dir = wind > 0.0f ? 1.0f : -1.0f;
    const float width = static_cast<float>(layout::kPanelX);
    const auto rowY = [&](std::uint32_t h, float parallax) {
        return wrap(static_cast<float>((h >> 12) % 480u) + scrollPx * parallax, static_cast<float>(cfg::kScreenHeight));
    };

    // One segment of a stroke: a quad from a to b whose width and colour change along it.
    const auto segment = [this](SDL_FPoint a, SDL_FPoint b, float wa, float wb, SDL_Color ca, SDL_Color cb) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::max(std::sqrt(dx * dx + dy * dy), 0.001f);
        const float nx = -dy / len;
        const float ny = dx / len;
        const SDL_Vertex v[4]{
            {{a.x + nx * wa / 2, a.y + ny * wa / 2}, ca, {0, 0}}, {{a.x - nx * wa / 2, a.y - ny * wa / 2}, ca, {0, 0}},
            {{b.x - nx * wb / 2, b.y - ny * wb / 2}, cb, {0, 0}}, {{b.x + nx * wb / 2, b.y + ny * wb / 2}, cb, {0, 0}},
        };
        const int indices[6]{0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r_, nullptr, v, 4, indices, 6);
    };

    const int gusts = static_cast<int>(9.0f * strength) + 1;
    for (int i = 0; i < gusts; ++i) {
        const std::uint32_t h = hash(i + 101);
        const float pace = 0.8f + static_cast<float>((h >> 8) % 140u) / 350.0f;   // each gust a bit faster or slower
        constexpr float kTail = 64.0f;   // px of straight tail behind the curl
        constexpr float kRadius = 8.0f;
        const float headX = wrap(static_cast<float>(h % 997u) + windTravel_ * pace, width + 3.0f * kTail) - kTail;
        const float baseY = rowY(h, 0.6f);
        const float flipY = (h % 2u == 0u) ? 1.0f : -1.0f;   // half curl upwards, half downwards
        const float maxAlpha = 150.0f + 90.0f * strength;

        // Path from the faint tail (s = 0) to the head, which rolls into a circle (s > kCurlStart).
        constexpr int kPoints = 28;
        constexpr float kCurlStart = 0.6f;
        SDL_FPoint prev{};
        float prevW = 0.0f;
        SDL_Color prevC{};
        for (int p = 0; p < kPoints; ++p) {
            const float s = static_cast<float>(p) / static_cast<float>(kPoints - 1);
            float lx = 0.0f;
            float ly = 0.0f;
            if (s < kCurlStart) {
                lx = -kTail * (1.0f - s / kCurlStart);
                ly = 1.5f * std::sin(s * 9.0f);
            } else {
                const float angle = (s - kCurlStart) / (1.0f - kCurlStart) * 1.6f * 3.14159f;
                lx = kRadius * std::sin(angle);
                ly = -kRadius + kRadius * std::cos(angle);
            }
            const SDL_FPoint pt{headX + dir * lx, baseY + flipY * ly};
            const float w = 1.0f + 3.0f * std::sin(3.14159f * std::min(s, 0.95f));
            const auto a = static_cast<Uint8>(maxAlpha * std::min(1.0f, s * 2.5f));
            const SDL_Color c{245, 249, 255, a};
            if (p > 0) {
                segment(prev, pt, prevW, w, prevC, c);
            }
            prev = pt;
            prevW = w;
            prevC = c;
        }
    }

    constexpr SDL_Color kLeafGreen{88, 196, 110, 255};
    constexpr SDL_Color kLeafAutumn{232, 192, 70, 255};
    constexpr SDL_Color kMidrib{30, 110, 62, 255};
    const int leaves = static_cast<int>(12.0f * strength);
    for (int i = 0; i < leaves; ++i) {
        const std::uint32_t h = hash(i + 4099);
        const float pace = 1.1f + static_cast<float>((h >> 8) % 200u) / 400.0f;   // leaves fly a little faster than the gusts
        const float cx = wrap(static_cast<float>(h % 991u) + windTravel_ * pace, width + 24.0f) - 12.0f;
        const float cy = rowY(h, 0.8f) + 7.0f * std::sin(t * 6.0f + static_cast<float>(h % 50u));
        const float spin = t * (4.0f + static_cast<float>(h % 4u)) * dir + static_cast<float>(h % 31u);
        const float halfLen = 9.0f;
        const float halfWidth = 4.5f * (0.35f + 0.65f * std::fabs(std::cos(t * 5.0f + static_cast<float>(h % 13u))));   // tumbling
        const float cs = std::cos(spin);
        const float sn = std::sin(spin);
        const auto at = [&](float along, float across) {
            return SDL_FPoint{cx + along * cs - across * sn, cy + along * sn + across * cs};
        };
        const SDL_Color body = (h % 5u == 0u) ? kLeafAutumn : kLeafGreen;
        const SDL_Vertex v[4]{{at(halfLen, 0), body, {0, 0}}, {at(0, halfWidth), body, {0, 0}},
                              {at(-halfLen, 0), body, {0, 0}}, {at(0, -halfWidth), body, {0, 0}}};
        const int indices[6]{0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r_, nullptr, v, 4, indices, 6);
        segment(at(-halfLen - 2.0f, 0), at(halfLen - 1.0f, 0), 1.2f, 1.2f, kMidrib, kMidrib);   // midrib and stem
    }
}

void Renderer2D::drawBlindSpotWarning(float egoX, bool left, bool right, bool intervening) {
    if (intervening && !blink(250)) {
        return;
    }
    const SDL_Color lamp = intervening ? color::kCrash : color::kDegraded;
    const int x = layout::kRoadX + static_cast<int>(egoX);
    const int y = static_cast<int>(layout::kEgoScreenY) - 26;   // about where the mirrors are
    const auto badge = [&](int cx, int side) {
        // Rounded amber/red badge with a dark car pictogram and a "wave" towards the occupied side.
        fillRect(cx - 7, y - 9, 14, 18, color::kDim);
        fillRect(cx - 6, y - 8, 12, 16, lamp);
        fillRect(cx - 7, y - 7, 1, 14, lamp);
        fillRect(cx + 6, y - 7, 1, 14, lamp);
        fillRect(cx - 2, y - 5, 4, 9, color::kPanel);          // car body
        fillRect(cx - 3, y - 3, 6, 2, color::kPanel);          // wheels
        fillRect(cx - 3, y + 1, 6, 2, color::kPanel);
        fillRect(cx + side * 9, y - 4, 2, 8, lamp);            // signal wave
        fillRect(cx + side * 12, y - 6, 2, 12, lamp);
    };
    if (left) {
        badge(x - 40, -1);
    }
    if (right) {
        badge(x + 40, 1);
    }
}

void Renderer2D::drawSensorZone(float egoX, float minLong, float maxLong, float minLat, float maxLat, SDL_Color c) {
    const float ppm = layout::kPixelsPerMeter;
    const int left = layout::kRoadX + static_cast<int>(egoX + minLat * static_cast<float>(cfg::kLaneWidth));
    const int right = layout::kRoadX + static_cast<int>(egoX + maxLat * static_cast<float>(cfg::kLaneWidth));
    const int top = std::max(0, static_cast<int>(layout::kEgoScreenY - maxLong * ppm));
    const int bottom = std::min(cfg::kScreenHeight, static_cast<int>(layout::kEgoScreenY - minLong * ppm));
    if (bottom <= top || right <= left) {
        return;
    }
    fillRect(left, top, right - left, bottom - top, SDL_Color{c.r, c.g, c.b, 28});
    const SDL_Color edge{c.r, c.g, c.b, 110};
    fillRect(left, top, 1, bottom - top, edge);
    fillRect(right - 1, top, 1, bottom - top, edge);
    fillRect(left, top, right - left, 1, edge);
    fillRect(left, bottom - 1, right - left, 1, edge);
}

void Renderer2D::drawDetection(float centerX, float centerY, SDL_Color c) {
    // Corner brackets, like a perception debug view (and the cover art).
    const int x0 = layout::kRoadX + static_cast<int>(centerX) - 31;
    const int y0 = static_cast<int>(centerY) - 50;
    const int x1 = x0 + 62;
    const int y1 = y0 + 100;
    constexpr int k = 10;
    constexpr int t = 2;
    fillRect(x0, y0, k, t, c);          fillRect(x0, y0, t, k, c);
    fillRect(x1 - k, y0, k, t, c);      fillRect(x1 - t, y0, t, k, c);
    fillRect(x0, y1 - t, k, t, c);      fillRect(x0, y1 - k, t, k, c);
    fillRect(x1 - k, y1 - t, k, t, c);  fillRect(x1 - t, y1 - k, t, k, c);
}

void Renderer2D::drawLaneTarget(float centerX) {
    // Short dashed cyan line ahead of the car on the lane centre LKA steers towards.
    constexpr SDL_Color kTarget{80, 200, 255, 170};
    const int x = layout::kRoadX + static_cast<int>(centerX);
    const int top = static_cast<int>(layout::kEgoScreenY) - layout::kCarHeight / 2 - 90;
    for (int y = top; y < top + 80; y += 12) {
        fillRect(x - 1, y, 2, 7, kTarget);
    }
    fillRect(x - 6, top + 80, 12, 2, kTarget);
}

void Renderer2D::drawLeadMarker(float egoX, float gap, float relativeSpeed) {
    // Positions are car centres; the sprite's half length turns them into nose and tail on screen.
    const float halfCar = static_cast<float>(layout::kCarHeight) / 2.0f;
    const float centreDistance = gap + cfg::kCarLength;
    const int x = layout::kRoadX + static_cast<int>(egoX);
    const int noseY = static_cast<int>(layout::kEgoScreenY - halfCar);
    const int tailY = static_cast<int>(layout::kEgoScreenY - centreDistance * layout::kPixelsPerMeter + halfCar);
    if (tailY >= noseY) {
        return;   // overlapping: nothing to draw between the cars
    }

    const bool closing = relativeSpeed < 0.0f;
    const SDL_Color c = closing ? color::kDegraded : color::kActive;

    // Dashed range line with end ticks, like a radar debug view.
    for (int y = noseY; y > tailY; y -= 10) {
        fillRect(x - 1, std::max(tailY, y - 6), 2, std::min(6, y - tailY), c);
    }
    fillRect(x - 10, tailY, 20, 2, c);
    fillRect(x - 6, noseY - 2, 12, 2, c);

    // Label next to the middle of the line.
    char gapText[16];
    char speedText[16];
    std::snprintf(gapText, sizeof gapText, "%.1fM", static_cast<double>(gap));
    std::snprintf(speedText, sizeof speedText, "%+.1f M/S", static_cast<double>(relativeSpeed));
    const int labelW = std::max(font::textWidth(gapText, 1), font::textWidth(speedText, 1)) + 8;
    const int labelX = x + 8;
    const int labelY = (noseY + tailY) / 2 - 10;
    fillRect(labelX, labelY, labelW, 21, color::kDim);
    fillRect(labelX, labelY, 2, 21, c);
    font::drawText(r_, gapText, labelX + 5, labelY + 3, 1, color::kText);
    font::drawText(r_, speedText, labelX + 5, labelY + 12, 1, c);
}

void Renderer2D::drawAlertBorder(bool danger) {
    // Pulse between 40 % and 100 % strength; faster when braking.
    const float period = danger ? 250.0f : 600.0f;
    const float phase = static_cast<float>(SDL_GetTicks() % static_cast<Uint32>(period)) / period;
    const float pulse = 0.4f + 0.6f * (0.5f + 0.5f * std::cos(phase * 2.0f * 3.14159265f));

    const SDL_Color base = danger ? color::kCrash : color::kDegraded;
    const int thickness = danger ? 18 : 8;
    const int w = layout::kPanelX;   // the road area; the panel stays clean
    const int h = cfg::kScreenHeight;

    // Concentric bands fading towards the centre: a glow rather than a hard frame.
    for (int i = 0; i < thickness; ++i) {
        const float fade = 1.0f - static_cast<float>(i) / static_cast<float>(thickness);
        const auto a = static_cast<Uint8>(150.0f * pulse * fade * fade);
        const SDL_Color c{base.r, base.g, base.b, a};
        fillRect(i, i, w - 2 * i, 1, c);
        fillRect(i, h - 1 - i, w - 2 * i, 1, c);
        fillRect(i, i + 1, 1, h - 2 * i - 2, c);
        fillRect(w - 1 - i, i + 1, 1, h - 2 * i - 2, c);
    }
}

void Renderer2D::drawPickupHint(float centerX, const char* label, SDL_Color c) {
    const int x = layout::kRoadX + static_cast<int>(centerX);
    const bool on = blink(400);
    fillRect(x - 17, 4, 34, 15, color::kPanel);
    fillRect(x - 16, 5, 32, 13, on ? c : SDL_Color{c.r, c.g, c.b, 150});
    font::drawText(r_, label, x - font::textWidth(label, 1) / 2, 8, 1, color::kText);
    for (int i = 0; i < 5; ++i) {   // small arrow pointing down at the lane
        fillRect(x - 5 + i, 20 + i, 10 - 2 * i, 1, c);
    }
}

void Renderer2D::drawPickup(float centerX, float centerY, const char* label, SDL_Color c) {
    // Pulsing glow, then a solid disc with a dark rim and the label: readable at speed.
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks()) / 160.0f);
    const float cx = static_cast<float>(layout::kRoadX) + centerX;
    const auto disc = [&](float radius, SDL_Color colour) {
        for (float dy = -radius; dy <= radius; dy += 1.0f) {
            const float half = std::sqrt(std::max(radius * radius - dy * dy, 0.0f));
            fillRect(static_cast<int>(cx - half), static_cast<int>(centerY + dy), static_cast<int>(2.0f * half), 1, colour);
        }
    };
    disc(26.0f + 4.0f * pulse, SDL_Color{c.r, c.g, c.b, static_cast<Uint8>(50 + 50 * pulse)});
    disc(20.0f, color::kPanel);
    disc(18.0f, c);
    const int scale = std::char_traits<char>::length(label) > 3 ? 1 : 2;
    font::drawTextShadow(r_, label, static_cast<int>(cx) - font::textWidth(label, scale) / 2,
                         static_cast<int>(centerY) - 7 * scale / 2, scale, color::kText);
}

void Renderer2D::drawTextCentered(const char* text, int centerX, int y, int scale, SDL_Color c) {
    font::drawTextShadow(r_, text, centerX - font::textWidth(text, scale) / 2, y, scale, c);
}

void Renderer2D::drawSprite(SDL_Texture* tex, float centerX, float centerY, float w, float h, bool shadow) {
    if (shadow) {
        const SDL_FRect shadowRect{centerX - w / 2.0f + 4.0f, centerY - h / 2.0f + 6.0f, w, h};
        SDL_SetTextureColorMod(tex, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex, 70);
        SDL_RenderCopyF(r_, tex, nullptr, &shadowRect);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }
    const SDL_FRect dst{centerX - w / 2.0f, centerY - h / 2.0f, w, h};
    SDL_RenderCopyF(r_, tex, nullptr, &dst);
}

void Renderer2D::fillRect(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    const SDL_Rect rect{x, y, w, h};
    SDL_RenderFillRect(r_, &rect);
}
