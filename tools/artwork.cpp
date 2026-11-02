// Generates the game's cover art and app icon from the sprite pack.
// Usage: artwork <assetDir> <outDir>   ->  cover.png (640x480), icon.png (256x256), icon.ico
#include <SDL.h>
#include <SDL_image.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "render/BitmapFont.hpp"
#include "render/Texture.hpp"

namespace {

    constexpr float kPi = 3.14159265f;

    constexpr SDL_Color kNight{14, 18, 30, 255};
    constexpr SDL_Color kRadar{60, 230, 140, 255};
    constexpr SDL_Color kCamera{80, 200, 255, 255};
    constexpr SDL_Color kWarn{255, 176, 32, 255};
    constexpr SDL_Color kDanger{255, 72, 72, 255};
    constexpr SDL_Color kTitle{255, 204, 0, 255};
    constexpr SDL_Color kText{232, 236, 244, 255};
    constexpr SDL_Color kMuted{120, 134, 160, 255};

    SDL_Color alpha(SDL_Color c, Uint8 a) { return SDL_Color{c.r, c.g, c.b, a}; }

    void fill(SDL_Renderer* r, float x, float y, float w, float h, SDL_Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
        const SDL_FRect rect{x, y, w, h};
        SDL_RenderFillRectF(r, &rect);
    }

    void tri(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c, SDL_Color ca, SDL_Color cb, SDL_Color cc) {
        const SDL_Vertex v[3]{{a, ca, {0, 0}}, {b, cb, {0, 0}}, {c, cc, {0, 0}}};
        SDL_RenderGeometry(r, nullptr, v, 3, nullptr, 0);
    }

    // Point at distance len from (cx, cy); angle 0 points straight up the screen.
    SDL_FPoint polar(float cx, float cy, float len, float angle) {
        return SDL_FPoint{cx + len * std::sin(angle), cy - len * std::cos(angle)};
    }

    // Sensor field of view: a fan that fades out with distance.
    void cone(SDL_Renderer* r, float cx, float cy, float len, float halfAngle, SDL_Color c, Uint8 a0) {
        constexpr int kSegments = 32;
        for (int i = 0; i < kSegments; ++i) {
            const float t0 = -halfAngle + 2.0f * halfAngle * static_cast<float>(i) / kSegments;
            const float t1 = -halfAngle + 2.0f * halfAngle * static_cast<float>(i + 1) / kSegments;
            tri(r, {cx, cy}, polar(cx, cy, len, t0), polar(cx, cy, len, t1), alpha(c, a0), alpha(c, 0), alpha(c, 0));
        }
    }

    void arc(SDL_Renderer* r, float cx, float cy, float radius, float halfAngle, float thick, SDL_Color c) {
        constexpr int kSegments = 40;
        for (int i = 0; i < kSegments; ++i) {
            const float t0 = -halfAngle + 2.0f * halfAngle * static_cast<float>(i) / kSegments;
            const float t1 = -halfAngle + 2.0f * halfAngle * static_cast<float>(i + 1) / kSegments;
            const SDL_FPoint a = polar(cx, cy, radius - thick / 2, t0);
            const SDL_FPoint b = polar(cx, cy, radius + thick / 2, t0);
            const SDL_FPoint d = polar(cx, cy, radius - thick / 2, t1);
            const SDL_FPoint e = polar(cx, cy, radius + thick / 2, t1);
            tri(r, a, b, e, c, c, c);
            tri(r, a, e, d, c, c, c);
        }
    }

    void line(SDL_Renderer* r, SDL_FPoint a, SDL_FPoint b, float thick, SDL_Color c) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / len * thick / 2;
        const float ny = dx / len * thick / 2;
        const SDL_FPoint p1{a.x + nx, a.y + ny}, p2{a.x - nx, a.y - ny}, p3{b.x - nx, b.y - ny}, p4{b.x + nx, b.y + ny};
        tri(r, p1, p2, p3, c, c, c);
        tri(r, p1, p3, p4, c, c, c);
    }

    // Detection box drawn as four corner brackets, like a perception debug view.
    void brackets(SDL_Renderer* r, float cx, float cy, float w, float h, SDL_Color c) {
        const float x0 = cx - w / 2, y0 = cy - h / 2, x1 = cx + w / 2, y1 = cy + h / 2;
        constexpr float k = 11.0f, t = 2.0f;
        fill(r, x0, y0, k, t, c);      fill(r, x0, y0, t, k, c);
        fill(r, x1 - k, y0, k, t, c);  fill(r, x1 - t, y0, t, k, c);
        fill(r, x0, y1 - t, k, t, c);  fill(r, x0, y1 - k, t, k, c);
        fill(r, x1 - k, y1 - t, k, t, c); fill(r, x1 - t, y1 - k, t, k, c);
    }

    void tag(SDL_Renderer* r, const char* text, float x, float y, SDL_Color c) {
        const int w = font::textWidth(text, 1);
        fill(r, x, y, static_cast<float>(w + 6), 11, alpha(kNight, 210));
        fill(r, x, y, 2, 11, c);
        font::drawText(r, text, static_cast<int>(x) + 5, static_cast<int>(y) + 2, 1, c);
    }

    void sprite(SDL_Renderer* r, SDL_Texture* tex, float cx, float cy, float w, bool shadow = true) {
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        const float h = w * static_cast<float>(th) / static_cast<float>(tw);
        if (shadow) {
            const SDL_FRect s{cx - w / 2 + 4, cy - h / 2 + 6, w, h};
            SDL_SetTextureColorMod(tex, 0, 0, 0);
            SDL_SetTextureAlphaMod(tex, 80);
            SDL_RenderCopyF(r, tex, nullptr, &s);
            SDL_SetTextureColorMod(tex, 255, 255, 255);
            SDL_SetTextureAlphaMod(tex, 255);
        }
        const SDL_FRect d{cx - w / 2, cy - h / 2, w, h};
        SDL_RenderCopyF(r, tex, nullptr, &d);
    }

    void road(SDL_Renderer* r, float x0, float laneW, int lanes, float top, float bottom, float dashOffset) {
        const float w = laneW * static_cast<float>(lanes);
        fill(r, x0, top, w, bottom - top, SDL_Color{74, 84, 100, 255});
        for (float y = top - 40 + dashOffset; y < bottom; y += 40) {
            fill(r, x0 - 8, y, 8, 20, SDL_Color{226, 76, 60, 255});
            fill(r, x0 - 8, y + 20, 8, 20, SDL_Color{242, 242, 246, 255});
            fill(r, x0 + w, y, 8, 20, SDL_Color{226, 76, 60, 255});
            fill(r, x0 + w, y + 20, 8, 20, SDL_Color{242, 242, 246, 255});
        }
        fill(r, x0 + 6, top, 3, bottom - top, SDL_Color{236, 238, 242, 255});
        fill(r, x0 + w - 9, top, 3, bottom - top, SDL_Color{236, 238, 242, 255});
        for (int lane = 1; lane < lanes; ++lane) {
            for (float y = top - 70 + dashOffset; y < bottom; y += 70) {
                fill(r, x0 + laneW * static_cast<float>(lane) - 2, y, 4, 32, SDL_Color{236, 238, 242, 255});
            }
        }
    }

    struct Art {
        TexturePtr blue, yellow, red, green, black, tree, treeLarge, logo;
    };

    // Rasterises an SVG at the given width (height follows the aspect ratio).
    TexturePtr loadSvg(SDL_Renderer* r, const std::string& path, int width) {
        SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
        if (!rw) {
            return TexturePtr();
        }
        SDL_Surface* surface = IMG_LoadSizedSVG_RW(rw, width, 0);
        SDL_RWclose(rw);
        if (!surface) {
            return TexturePtr();
        }
        TexturePtr texture(SDL_CreateTextureFromSurface(r, surface));
        SDL_FreeSurface(surface);
        return texture;
    }

    bool load(SDL_Renderer* r, const std::string& assetDir, Art& art) {
        const std::string png = assetDir + "racing-pack/PNG/";
        art.blue = loadTexture(r, png + "Cars/car_blue_1.png");
        art.yellow = loadTexture(r, png + "Cars/car_yellow_4.png");
        art.red = loadTexture(r, png + "Cars/car_red_1.png");
        art.green = loadTexture(r, png + "Cars/car_green_3.png");
        art.black = loadTexture(r, png + "Cars/car_black_5.png");
        art.tree = loadTexture(r, png + "Objects/tree_small.png");
        art.treeLarge = loadTexture(r, png + "Objects/tree_large.png");
        // Optional, local only (gitignored): a PNG, or else an SVG rendered at twice the card width.
        art.logo = loadTexture(r, assetDir + "art/logo.png");
        if (!art.logo) {
            art.logo = loadSvg(r, assetDir + "art/logo.svg", 312);
        }
        return art.blue && art.yellow && art.red && art.green && art.black && art.tree && art.treeLarge;
    }

    void drawCoverImpl(SDL_Renderer* r, const Art& art, bool withLogo) {
        constexpr float kLaneW = 84.0f;
        constexpr float kRoadX = 152.0f;
        constexpr float kCarW = 44.0f;
        const auto laneX = [](int lane) { return kRoadX + kLaneW * (static_cast<float>(lane) + 0.5f); };

        // Scene: daytime highway, then dimmed so the sensor overlay reads like an engineering view.
        fill(r, 0, 0, 640, 480, SDL_Color{39, 170, 96, 255});
        for (float y = -20; y < 480; y += 96) {
            fill(r, 0, y, 640, 48, SDL_Color{46, 184, 104, 255});
        }
        const float treesLeft[][3] = {{40, 60, 70}, {110, 150, 52}, {30, 230, 78}, {100, 330, 56}, {50, 420, 70}};
        const float treesRight[][3] = {{600, 40, 74}, {530, 130, 52}, {610, 220, 64}, {540, 320, 70}, {600, 410, 56}};
        for (const auto& t : treesLeft) sprite(r, (t[2] > 60 ? art.treeLarge : art.tree).get(), t[0], t[1], t[2]);
        for (const auto& t : treesRight) sprite(r, (t[2] > 60 ? art.treeLarge : art.tree).get(), t[0], t[1], t[2]);
        road(r, kRoadX, kLaneW, 4, 0, 480, 14);

        const float egoX = laneX(1), egoY = 352;
        sprite(r, art.green.get(), laneX(3), 96, kCarW);
        sprite(r, art.yellow.get(), laneX(1), 160, kCarW);
        sprite(r, art.red.get(), laneX(2) - 22, 236, kCarW);
        sprite(r, art.black.get(), laneX(0), 362, kCarW);
        fill(r, 0, 0, 640, 480, SDL_Color{8, 12, 26, 120});

        // Sensors: long-range radar (narrow, green) and front camera (wide, cyan).
        const float noseY = egoY - 40;
        cone(r, egoX, noseY, 300, 0.30f, kCamera, 40);
        cone(r, egoX, noseY, 330, 0.13f, kRadar, 90);
        for (float range : {90.0f, 180.0f, 270.0f}) {
            arc(r, egoX, noseY, range, 0.13f, 1.5f, alpha(kRadar, 160));
        }
        line(r, {egoX, noseY}, polar(egoX, noseY, 300, -0.30f), 1.2f, alpha(kCamera, 150));
        line(r, {egoX, noseY}, polar(egoX, noseY, 300, 0.30f), 1.2f, alpha(kCamera, 150));
        tag(r, "20M", egoX - 52, noseY - 96, kRadar);

        // Blind-spot zone on the occupied side.
        fill(r, kRoadX + 4, egoY - 50, kLaneW - 8, 118, alpha(kWarn, 46));

        sprite(r, art.blue.get(), egoX, egoY, kCarW);

        // Perception boxes with the values each assist cares about.
        brackets(r, laneX(1), 160, 66, 104, kRadar);
        tag(r, "ACC LEAD 18.2M", laneX(1) + 36, 116, kRadar);
        brackets(r, laneX(2) - 22, 236, 66, 104, kDanger);
        tag(r, "CUT-IN  TTC 1.4S", laneX(2) + 14, 196, kDanger);
        brackets(r, laneX(0), 362, 62, 96, kWarn);
        tag(r, "BSW", laneX(0) - 30, 404, kWarn);
        brackets(r, laneX(3), 96, 62, 100, alpha(kCamera, 200));
        tag(r, "OBJ 4  0.97", laneX(3) - 34, 150, kCamera);

        // Title.
        for (int i = 0; i < 70; ++i) {
            fill(r, 0, static_cast<float>(i), 640, 1, alpha(kNight, static_cast<Uint8>(230 - i * 3)));
        }
        font::drawTextShadow(r, "LANE RUNNER", 320 - font::textWidth("LANE RUNNER", 5) / 2, 12, 5, kTitle);
        font::drawTextShadow(r, "ADAS HIGHWAY CHALLENGE", 320 - font::textWidth("ADAS HIGHWAY CHALLENGE", 1) / 2, 52, 1,
                             kText);

        // Side status cards.
        fill(r, 8, 118, 112, 62, alpha(kNight, 200));
        fill(r, 8, 118, 2, 62, kRadar);
        font::drawText(r, "RADAR  77GHZ", 16, 126, 1, kRadar);
        font::drawText(r, "CAMERA  OK", 16, 140, 1, kCamera);
        font::drawText(r, "FUSION  4 OBJ", 16, 154, 1, kText);
        font::drawText(r, "CYCLE  20MS", 16, 168, 1, kMuted);

        fill(r, 520, 196, 112, 62, alpha(kNight, 200));
        fill(r, 630, 196, 2, 62, kDanger);
        font::drawText(r, "AEB  ARMED", 528, 204, 1, kDanger);
        font::drawText(r, "ACC  25M GAP", 528, 218, 1, kRadar);
        font::drawText(r, "LKA  ON", 528, 232, 1, kCamera);
        font::drawText(r, "BSW  WARN", 528, 246, 1, kWarn);

        // Bottom band: optional logo card, CAN frames and the ECU.
        constexpr float kScopeY = 420;
        fill(r, 0, kScopeY, 640, 60, alpha(kNight, 235));
        fill(r, 0, kScopeY, 640, 1, alpha(kMuted, 160));

        float textX = 16;
        if (withLogo && art.logo) {
            // White card, logo fitted inside with its aspect ratio kept.
            constexpr float kCardX = 12, kCardY = kScopeY + 8, kCardW = 168, kCardH = 44, kPad = 6;
            fill(r, kCardX, kCardY, kCardW, kCardH, SDL_Color{255, 255, 255, 255});
            int lw = 0, lh = 0;
            SDL_QueryTexture(art.logo.get(), nullptr, nullptr, &lw, &lh);
            const float scale = std::min((kCardW - 2 * kPad) / static_cast<float>(lw),
                                         (kCardH - 2 * kPad) / static_cast<float>(lh));
            const float w = static_cast<float>(lw) * scale, h = static_cast<float>(lh) * scale;
            const SDL_FRect dst{kCardX + (kCardW - w) / 2, kCardY + (kCardH - h) / 2, w, h};
            SDL_RenderCopyF(r, art.logo.get(), nullptr, &dst);
            textX = kCardX + kCardW + 14;
        }
        font::drawText(r, "CAN 0X1A0  08 3C 00 FF 12 7E", static_cast<int>(textX), static_cast<int>(kScopeY) + 16, 1,
                       alpha(kMuted, 220));
        font::drawText(r, "CAN 0X2B4  41 00 7F 03 C8 00", static_cast<int>(textX), static_cast<int>(kScopeY) + 30, 1,
                       alpha(kMuted, 160));

        // ECU chip with pins.
        constexpr float kChipX = 500, kChipY = kScopeY + 10, kChipW = 120, kChipH = 40;
        for (float x = kChipX + 8; x < kChipX + kChipW - 4; x += 10) {
            fill(r, x, kChipY - 5, 4, 5, kMuted);
            fill(r, x, kChipY + kChipH, 4, 5, kMuted);
        }
        fill(r, kChipX, kChipY, kChipW, kChipH, SDL_Color{34, 40, 56, 255});
        fill(r, kChipX, kChipY, kChipW, 2, alpha(kMuted, 200));
        font::drawText(r, "ECU", static_cast<int>(kChipX) + 10, static_cast<int>(kChipY) + 9, 2, kText);
        font::drawText(r, "50 HZ TASK", static_cast<int>(kChipX) + 52, static_cast<int>(kChipY) + 12, 1, kRadar);
        font::drawText(r, "C++17", static_cast<int>(kChipX) + 52, static_cast<int>(kChipY) + 24, 1, kMuted);
    }

    bool savePng(SDL_Surface* s, const std::string& path);

    // Hand-made pixel art from a text grid ('#' = pixel, anything else = empty) to a PNG:
    // white pixels with a 1 px dark outline, the classic 8-bit sprite look.
    bool writePixelArt(const std::string& txtPath, const std::string& outPath) {
        std::vector<std::string> rows;
        if (FILE* f = std::fopen(txtPath.c_str(), "rb")) {
            std::string line;
            int ch = 0;
            while ((ch = std::fgetc(f)) != EOF) {
                if (ch == '\n') {
                    rows.push_back(line);
                    line.clear();
                } else if (ch != '\r') {
                    line.push_back(static_cast<char>(ch));
                }
            }
            if (!line.empty()) {
                rows.push_back(line);
            }
            std::fclose(f);
        }
        std::size_t cols = 0;
        for (const std::string& row : rows) {
            cols = std::max(cols, row.size());
        }
        if (rows.empty() || cols == 0) {
            return false;
        }

        const auto set = [&](int r, int c) {
            return r >= 0 && c >= 0 && r < static_cast<int>(rows.size()) && c < static_cast<int>(rows[r].size()) &&
                   rows[r][c] == '#';
        };
        const int w = static_cast<int>(cols) + 2;   // 1 px border for the outline
        const int h = static_cast<int>(rows.size()) + 2;
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_LockSurface(s);
        for (int y = 0; y < h; ++y) {
            auto* out = static_cast<std::uint8_t*>(s->pixels) + y * s->pitch;
            for (int x = 0; x < w; ++x) {
                const int r = y - 1, c = x - 1;
                bool edge = false;
                for (int dy = -1; dy <= 1 && !edge; ++dy) {
                    for (int dx = -1; dx <= 1 && !edge; ++dx) {
                        edge = set(r + dy, c + dx);
                    }
                }
                std::uint8_t* px = out + x * 4;
                if (set(r, c)) {
                    px[0] = px[1] = px[2] = px[3] = 255;
                } else if (edge) {
                    px[0] = kNight.r; px[1] = kNight.g; px[2] = kNight.b; px[3] = 255;
                } else {
                    px[0] = px[1] = px[2] = px[3] = 0;
                }
            }
        }
        SDL_UnlockSurface(s);
        const bool ok = savePng(s, outPath);
        SDL_FreeSurface(s);
        return ok;
    }

    // Pixel-art version of the SVG logo for the in-game menu: small, white, hard edges.
    // The game loads the PNG because the handheld's SDL_image cannot read SVG.
    //
    // The SVG is rendered at kSuper x the target size and each target pixel is kept when enough
    // of its block is covered. A plain threshold on a small render breaks thin strokes apart;
    // measuring coverage keeps rings and stems continuous.
    bool writeMenuLogo(const std::string& svgPath, const std::string& outPath) {
        constexpr int kWidth = 100;         // drawn at 2x in the menu, matching the 2x pixel font
        constexpr int kSuper = 6;
        constexpr float kSymbolEnd = 0.24f; // the round symbol spans the left ~22% of the SVG
        constexpr float kSymbolCoverage = 0.28f;   // thin lines: keep pixels that are partly covered
        constexpr float kTextCoverage = 0.45f;     // solid letters: about half covered

        SDL_RWops* rw = SDL_RWFromFile(svgPath.c_str(), "rb");
        if (!rw) {
            return false;
        }
        SDL_Surface* raw = IMG_LoadSizedSVG_RW(rw, kWidth * kSuper, 0);
        SDL_RWclose(rw);
        if (!raw) {
            return false;
        }
        SDL_Surface* big = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(raw);

        const int height = (big->h + kSuper - 1) / kSuper;
        SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, kWidth, height, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_LockSurface(big);
        SDL_LockSurface(s);
        for (int y = 0; y < height; ++y) {
            auto* out = static_cast<std::uint8_t*>(s->pixels) + y * s->pitch;
            for (int x = 0; x < kWidth; ++x) {
                float sum = 0.0f;
                int count = 0;
                for (int sy = y * kSuper; sy < std::min((y + 1) * kSuper, big->h); ++sy) {
                    const auto* row = static_cast<const std::uint8_t*>(big->pixels) + sy * big->pitch;
                    for (int sx = x * kSuper; sx < std::min((x + 1) * kSuper, big->w); ++sx) {
                        sum += static_cast<float>(row[sx * 4 + 3]) / 255.0f;
                        ++count;
                    }
                }
                const float coverage = count > 0 ? sum / static_cast<float>(count) : 0.0f;
                const bool inSymbol = static_cast<float>(x) < kSymbolEnd * kWidth;
                const bool solid = coverage >= (inSymbol ? kSymbolCoverage : kTextCoverage);
                std::uint8_t* px = out + x * 4;
                px[0] = px[1] = px[2] = 255;
                px[3] = solid ? 255 : 0;
            }
        }
        SDL_UnlockSurface(s);
        SDL_UnlockSurface(big);
        SDL_FreeSurface(big);
        const bool ok = savePng(s, outPath);
        SDL_FreeSurface(s);
        return ok;
    }

    void drawCover(SDL_Renderer* r, const Art& art) { drawCoverImpl(r, art, false); }
    void drawCoverWithLogo(SDL_Renderer* r, const Art& art) { drawCoverImpl(r, art, true); }

    void drawIcon(SDL_Renderer* r, const Art& art) {
        // Rounded dark tile.
        constexpr float kSize = 256, kRadius = 48;
        for (int y = 0; y < 256; ++y) {
            const float fy = static_cast<float>(y) + 0.5f;
            float inset = 0;
            const float d = fy < kRadius ? kRadius - fy : (fy > kSize - kRadius ? fy - (kSize - kRadius) : 0);
            if (d > 0) {
                inset = kRadius - std::sqrt(kRadius * kRadius - d * d);
            }
            fill(r, inset, static_cast<float>(y), kSize - 2 * inset, 1, SDL_Color{20, 26, 42, 255});
        }

        road(r, 58, 70, 2, 14, 242, 0);
        fill(r, 50, 14, 156, 228, SDL_Color{10, 14, 26, 70});

        const float egoX = 93, egoY = 176, noseY = 176 - 48;
        cone(r, egoX, noseY, 150, 0.30f, kCamera, 50);
        cone(r, egoX, noseY, 160, 0.16f, kRadar, 130);
        for (float range : {50.0f, 95.0f}) {
            arc(r, egoX, noseY, range, 0.16f, 3.0f, alpha(kRadar, 210));
        }
        sprite(r, art.red.get(), 163, 70, 44);
        brackets(r, 163, 70, 64, 100, kDanger);
        sprite(r, art.blue.get(), egoX, egoY, 52);
    }

    bool savePng(SDL_Surface* s, const std::string& path) {
        if (IMG_SavePNG(s, path.c_str()) != 0) {
            std::fprintf(stderr, "Cannot write %s: %s\n", path.c_str(), SDL_GetError());
            return false;
        }
        return true;
    }

    std::vector<std::uint8_t> readFile(const std::string& path) {
        std::vector<std::uint8_t> data;
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            std::uint8_t buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
                data.insert(data.end(), buf, buf + n);
            }
            std::fclose(f);
        }
        return data;
    }

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v & 0xFFFF));
        put16(out, static_cast<std::uint16_t>(v >> 16));
    }

    // Windows .ico containing PNG-compressed images (supported since Windows Vista).
    bool writeIco(SDL_Surface* icon256, const std::string& outDir) {
        const int sizes[] = {256, 64, 48, 32, 16};
        std::vector<std::vector<std::uint8_t>> pngs;
        for (int size : sizes) {
            SDL_Surface* scaled = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
            SDL_SetSurfaceBlendMode(icon256, SDL_BLENDMODE_NONE);
            SDL_Rect dst{0, 0, size, size};
            SDL_BlitScaled(icon256, nullptr, scaled, &dst);
            const std::string tmp = outDir + "icon_tmp.png";
            savePng(scaled, tmp);
            SDL_FreeSurface(scaled);
            pngs.push_back(readFile(tmp));
            std::remove(tmp.c_str());
        }

        std::vector<std::uint8_t> ico;
        put16(ico, 0);
        put16(ico, 1);   // type: icon
        put16(ico, static_cast<std::uint16_t>(pngs.size()));
        std::uint32_t offset = 6 + 16 * static_cast<std::uint32_t>(pngs.size());
        for (std::size_t i = 0; i < pngs.size(); ++i) {
            ico.push_back(static_cast<std::uint8_t>(sizes[i] == 256 ? 0 : sizes[i]));   // 0 means 256
            ico.push_back(static_cast<std::uint8_t>(sizes[i] == 256 ? 0 : sizes[i]));
            ico.push_back(0);
            ico.push_back(0);
            put16(ico, 1);
            put16(ico, 32);
            put32(ico, static_cast<std::uint32_t>(pngs[i].size()));
            put32(ico, offset);
            offset += static_cast<std::uint32_t>(pngs[i].size());
        }
        for (const auto& png : pngs) {
            ico.insert(ico.end(), png.begin(), png.end());
        }

        FILE* f = std::fopen((outDir + "icon.ico").c_str(), "wb");
        if (!f) {
            return false;
        }
        std::fwrite(ico.data(), 1, ico.size(), f);
        std::fclose(f);
        return true;
    }

    bool render(int w, int h, void (*draw)(SDL_Renderer*, const Art&), const std::string& assetDir, SDL_Surface*& out) {
        out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
        SDL_Renderer* r = SDL_CreateSoftwareRenderer(out);
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
        SDL_RenderClear(r);
        Art art;
        if (!load(r, assetDir, art)) {
            std::fprintf(stderr, "Missing sprites: %s\n", SDL_GetError());
            return false;
        }
        draw(r, art);
        SDL_RenderPresent(r);
        art = Art{};   // textures must go before their renderer
        SDL_DestroyRenderer(r);
        return true;
    }

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: artwork <assetDir/> <outDir/>\n");
        return 1;
    }
    const std::string assetDir = argv[1];
    const std::string outDir = argv[2];
    SDL_Init(0);

    SDL_Surface* cover = nullptr;
    SDL_Surface* icon = nullptr;
    const bool ok = render(640, 480, drawCover, assetDir, cover) &&
                    render(256, 256, drawIcon, assetDir, icon) &&
                    savePng(cover, outDir + "cover.png") && savePng(icon, outDir + "icon.png") && writeIco(icon, outDir);

    // Second cover with a logo, only when assets/art/logo.png or logo.svg exists. Stays out of git.
    const auto exists = [](const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            std::fclose(f);
        }
        return f != nullptr;
    };
    bool logoOk = true;
    if (exists(assetDir + "art/logo.png") || exists(assetDir + "art/logo.svg")) {
        SDL_Surface* logoCover = nullptr;
        logoOk = render(640, 480, drawCoverWithLogo, assetDir, logoCover) &&
                 savePng(logoCover, outDir + "cover-logo.png");
        // Menu badge: hand-made pixel grid if present, otherwise derived from the SVG.
        if (exists(assetDir + "art/logo-pixels.txt")) {
            logoOk = writePixelArt(assetDir + "art/logo-pixels.txt", outDir + "menu-logo.png") && logoOk;
        } else if (exists(assetDir + "art/logo.svg")) {
            logoOk = writeMenuLogo(assetDir + "art/logo.svg", outDir + "menu-logo.png") && logoOk;
        }
        SDL_FreeSurface(logoCover);
        std::printf(logoOk ? "Logo cover written\n" : "Logo cover failed\n");
    }

    SDL_FreeSurface(cover);
    SDL_FreeSurface(icon);
    SDL_Quit();
    std::printf(ok ? "Artwork written to %s\n" : "Artwork failed\n", outDir.c_str());
    return ok ? 0 : 1;
}
