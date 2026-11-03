#include "render/Renderer2D.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "render/BitmapFont.hpp"
#include "render/Layout.hpp"

namespace {

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

    // True for half of every period: used for blinking prompts.
    bool blink(Uint32 periodMs) {
        return (SDL_GetTicks() / (periodMs / 2)) % 2 == 0;
    }

    // Positive remainder, so patterns keep moving smoothly for any scroll value.
    float wrap(float value, float period) {
        const float m = std::fmod(value, period);
        return m < 0.0f ? m + period : m;
    }

    // Cheap deterministic hash: the same scenery slot always gets the same tree.
    std::uint32_t hash(std::int64_t n) {
        auto x = static_cast<std::uint32_t>(n) * 2654435761u;
        x ^= x >> 15;
        return x * 2246822519u;
    }

} // namespace

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

void Renderer2D::drawPanel(const HudView& hud) {
    const int x0 = layout::kPanelX;
    const int left = x0 + 12;
    const int right = cfg::kScreenWidth - 12;
    char buf[32];

    fillRect(x0, 0, layout::kPanelWidth, cfg::kScreenHeight, color::kPanel);
    fillRect(x0, 0, 3, cfg::kScreenHeight, color::kPanelEdge);

    font::drawTextShadow(r_, "LANE RUNNER", left, 14, 2, color::kTitle);

    font::drawText(r_, "SCORE", left, 44, 1, color::kLabel);
    std::snprintf(buf, sizeof buf, "%06d", hud.score);
    font::drawTextShadow(r_, buf, left, 56, 3, hud.scorePenalty && blink(200) ? color::kCrash : color::kText);

    font::drawText(r_, "BEST", left, 88, 1, color::kLabel);
    std::snprintf(buf, sizeof buf, "%06d", hud.best);
    font::drawText(r_, buf, left, 100, 2, color::kLabel);

    font::drawText(r_, "SPEED", left, 126, 1, color::kLabel);
    std::snprintf(buf, sizeof buf, "%3d", hud.speedKmh);
    font::drawTextShadow(r_, buf, left, 138, 3, color::kText);
    font::drawText(r_, "KM/H", left + font::textWidth(buf, 3) + 6, 152, 1, color::kLabel);

    fillRect(left, 172, right - left, 2, color::kPanelEdge);
    font::drawText(r_, "ASSISTS", left, 182, 1, color::kLabel);

    int y = 196;
    for (const AssistView& a : hud.assists) {
        SDL_Color c = color::kLocked;
        switch (a.lamp) {
            case AssistLamp::Active:      c = color::kActive;    break;
            case AssistLamp::Degraded:    c = color::kDegraded;  break;
            case AssistLamp::Off:         c = color::kLabel;     break;
            case AssistLamp::Intervening: c = color::kCrash;     break;
            case AssistLamp::Locked:                             break;
        }

        font::drawText(r_, a.name, left, y, 2, c);
        const bool flashOff = a.lamp == AssistLamp::Intervening && !blink(200);
        if (!flashOff) {
            fillRect(right - 10, y + 2, 10, 10, c);   // intervening lamps flash
        }

        const int barWidth = right - left;
        fillRect(left, y + 18, barWidth, 3, color::kBarBack);
        if (a.lamp == AssistLamp::Active || a.lamp == AssistLamp::Intervening) {
            const float level = a.level < 0.0f ? 0.0f : (a.level > 1.0f ? 1.0f : a.level);
            fillRect(left, y + 18, static_cast<int>(static_cast<float>(barWidth) * level), 3, c);
        }
        y += 27;
    }

    fillRect(left, 336, right - left, 2, color::kPanelEdge);
    font::drawText(r_, "SENSORS", left, 346, 1, color::kLabel);

    const char* radar = hud.radarOk ? "OK" : "FAULT";
    const char* camera = hud.cameraOk ? "OK" : "BLIND";
    font::drawText(r_, "RADAR", left, 362, 2, color::kText);
    font::drawText(r_, radar, right - font::textWidth(radar, 2), 362, 2,
                   hud.radarOk ? color::kActive : color::kDegraded);
    font::drawText(r_, "CAMERA", left, 386, 2, color::kText);
    font::drawText(r_, camera, right - font::textWidth(camera, 2), 386, 2,
                   hud.cameraOk ? color::kActive : color::kDegraded);

    // Wind: arrows point where the car is being pushed; more arrows = stronger gust.
    font::drawText(r_, "WIND", left, 410, 2, color::kText);
    const float strength = std::fabs(hud.wind);
    if (strength < 0.05f) {
        font::drawText(r_, "CALM", right - font::textWidth("CALM", 2), 410, 2, color::kActive);
    } else {
        const int count = strength < 0.35f ? 1 : (strength < 0.65f ? 2 : 3);
        const char* arrows = hud.wind > 0.0f ? (count == 1 ? ">" : count == 2 ? ">>" : ">>>")
                                             : (count == 1 ? "<" : count == 2 ? "<<" : "<<<");
        font::drawText(r_, arrows, right - font::textWidth(arrows, 2), 410, 2, color::kDegraded);
    }

    font::drawText(r_, "A:ACC L:LKA B:BSW F:FUS", left, cfg::kScreenHeight - 30, 1, color::kLocked);
    font::drawText(r_, "TAB: DEBUG  ESC: MENU", left, cfg::kScreenHeight - 18, 1, color::kLocked);
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

void Renderer2D::drawMenu(const MenuView& menu) {
    constexpr int kCenterX = cfg::kScreenWidth / 2;
    fillRect(0, 0, cfg::kScreenWidth, cfg::kScreenHeight, color::kDim);

    font::drawTextShadow(r_, "LANE RUNNER", kCenterX - font::textWidth("LANE RUNNER", 6) / 2, 70, 6, color::kTitle);
    drawTextCentered("ADAS HIGHWAY CHALLENGE", kCenterX, 126, 2, color::kLabel);

    int y = 196;
    for (std::size_t i = 0; i < menu.items.size(); ++i) {
        const MenuItemView& item = menu.items[i];
        const bool selected = (i == menu.selected);
        const int width = font::textWidth(item.label, 3);

        if (selected) {
            fillRect(kCenterX - 150, y - 8, 300, 37, color::kHighlight);
            font::drawText(r_, ">", kCenterX - 130, y, 3, color::kTitle);
            font::drawText(r_, "<", kCenterX + 130 - font::textWidth("<", 3), y, 3, color::kTitle);
        }

        SDL_Color c = item.enabled ? color::kText : color::kLocked;
        if (selected && item.enabled) {
            c = color::kTitle;
        }
        font::drawTextShadow(r_, item.label, kCenterX - width / 2, y, 3, c);
        if (!item.enabled) {
            font::drawText(r_, "SOON", kCenterX + width / 2 + 8, y - 4, 1, color::kDegraded);
        }
        y += 46;
    }

    char buf[32];
    std::snprintf(buf, sizeof buf, "BEST %06d", menu.best);
    drawTextCentered(buf, kCenterX, 352, 2, color::kText);

    if (menuBadge_) {
        // Pixel-art badge with its own dark outline, drawn at the menu text's 2x pixel size.
        constexpr int kScale = 2;
        constexpr int kTop = 386;
        int w = 0, h = 0;
        SDL_QueryTexture(menuBadge_.get(), nullptr, nullptr, &w, &h);
        const int bw = w * kScale, bh = h * kScale;
        const int x = kCenterX - bw / 2;

        const SDL_Rect shadow{x + kScale, kTop + kScale, bw, bh};
        const SDL_Rect dst{x, kTop, bw, bh};
        SDL_SetTextureColorMod(menuBadge_.get(), 10, 12, 20);
        SDL_SetTextureAlphaMod(menuBadge_.get(), 200);
        SDL_RenderCopy(r_, menuBadge_.get(), nullptr, &shadow);
        SDL_SetTextureColorMod(menuBadge_.get(), color::kText.r, color::kText.g, color::kText.b);
        SDL_SetTextureAlphaMod(menuBadge_.get(), 255);
        SDL_RenderCopy(r_, menuBadge_.get(), nullptr, &dst);
    }

    drawTextCentered("UP/DOWN: CHOOSE   ENTER: SELECT   ESC: QUIT", kCenterX, 448, 1, color::kLabel);
}

void Renderer2D::drawGameOver(int score, int best, bool newBest) {
    constexpr int kCenterX = layout::kPanelX / 2;   // centred over the road, the panel stays visible
    fillRect(0, 0, layout::kPanelX, cfg::kScreenHeight, color::kDim);

    font::drawTextShadow(r_, "CRASHED!", kCenterX - font::textWidth("CRASHED!", 5) / 2, 120, 5, color::kCrash);

    char buf[32];
    drawTextCentered("SCORE", kCenterX, 190, 1, color::kLabel);
    std::snprintf(buf, sizeof buf, "%06d", score);
    font::drawTextShadow(r_, buf, kCenterX - font::textWidth(buf, 4) / 2, 204, 4, color::kText);

    if (newBest) {
        if (blink(700)) {
            drawTextCentered("NEW BEST!", kCenterX, 250, 2, color::kTitle);
        }
    } else {
        std::snprintf(buf, sizeof buf, "BEST %06d", best);
        drawTextCentered(buf, kCenterX, 250, 2, color::kLabel);
    }

    if (blink(1000)) {
        drawTextCentered("ENTER: RETRY", kCenterX, 310, 2, color::kText);
    }
    drawTextCentered("ESC: MENU", kCenterX, 340, 2, color::kLabel);
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
