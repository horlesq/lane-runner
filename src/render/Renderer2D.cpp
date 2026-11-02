#include "render/Renderer2D.hpp"
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

void Renderer2D::drawCar(float centerX, float centerY, CarSkin skin, std::size_t model) {
    const CarSprite& sprite = cars_[static_cast<std::size_t>(skin)][model % kCarModelCount];
    const float height = static_cast<float>(layout::kCarHeight) * sprite.lengthScale;
    drawSprite(sprite.texture.get(), static_cast<float>(layout::kRoadX) + centerX, centerY,
               static_cast<float>(layout::kCarWidth), height, true);
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
    font::drawTextShadow(r_, buf, left, 56, 3, color::kText);

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
        if (a.lamp == AssistLamp::Active) {
            c = color::kActive;
        } else if (a.lamp == AssistLamp::Degraded) {
            c = color::kDegraded;
        } else if (a.lamp == AssistLamp::Off) {
            c = color::kLabel;
        }

        font::drawText(r_, a.name, left, y, 2, c);
        fillRect(right - 10, y + 2, 10, 10, c);

        const int barWidth = right - left;
        fillRect(left, y + 18, barWidth, 3, color::kBarBack);
        if (a.lamp == AssistLamp::Active) {
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

    font::drawText(r_, "ESC: MENU", left, cfg::kScreenHeight - 18, 1, color::kLocked);
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
