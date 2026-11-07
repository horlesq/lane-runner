#include "render/Renderer2D.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "render/BitmapFont.hpp"
#include "render/Controls.hpp"
#include "render/DrawHelpers.hpp"
#include "render/Layout.hpp"

// HUD side panel and the full-screen overlays (menus, help pages, pause, game over).

using draw_util::blink;

void Renderer2D::drawPanel(const HudView& hud) {
    const int x0 = layout::kPanelX;
    const int left = x0 + 12;
    const int right = cfg::kScreenWidth - 12;
    char buf[32];

    fillRect(x0, 0, layout::kPanelWidth, cfg::kScreenHeight, color::kPanel);
    fillRect(x0, 0, 3, cfg::kScreenHeight, color::kPanelEdge);

    font::drawTextShadow(r_, "LANE RUNNER", left, 14, 2, color::kTitle);

    font::drawText(r_, "SCORE", left, 44, 1, color::kLabel);
    if (hud.combo > 1) {
        char comboText[24];
        std::snprintf(comboText, sizeof comboText, "COMBO X%d", hud.combo);
        font::drawText(r_, comboText, right - font::textWidth(comboText, 1), 44, 1, color::kTitle);
    }
    std::snprintf(buf, sizeof buf, "%06d", hud.score);
    font::drawTextShadow(r_, buf, left, 56, 3, hud.scorePenalty && blink(200) ? color::kCrash : color::kText);

    font::drawText(r_, "BEST", left, 88, 1, color::kLabel);
    std::snprintf(buf, sizeof buf, "%06d", hud.best);
    font::drawText(r_, buf, left, 100, 2, color::kLabel);

    font::drawText(r_, "SPEED", left, 126, 1, color::kLabel);
    if (hud.shields > 0) {
        char shieldText[24];
        std::snprintf(shieldText, sizeof shieldText, "SHIELD X%d", hud.shields);
        font::drawText(r_, shieldText, right - font::textWidth(shieldText, 1), 126, 1, color::kActive);
    }
    std::snprintf(buf, sizeof buf, "%3d", hud.speedKmh);
    font::drawTextShadow(r_, buf, left, 138, 3, color::kText);
    font::drawText(r_, "KM/H", left + font::textWidth(buf, 3) + 6, 152, 1, color::kLabel);

    fillRect(left, 172, right - left, 2, color::kPanelEdge);
    if (hud.pickupName) {
        font::drawText(r_, hud.pickupName, left, 182, 1, color::kTitle);   // just collected: full name
    } else {
        font::drawText(r_, "ASSISTS", left, 182, 1, color::kLabel);
    }

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
    const char* camera = hud.cameraText ? hud.cameraText : (hud.cameraOk ? "OK" : "BLIND");
    font::drawText(r_, "RADAR", left, 362, 2, color::kText);
    font::drawText(r_, radar, right - font::textWidth(radar, 2), 362, 2,
                   hud.radarOk ? color::kActive : color::kDegraded);
    font::drawText(r_, "CAMERA", left, 386, 2, color::kText);
    font::drawText(r_, camera, right - font::textWidth(camera, 2), 386, 2,
                   hud.cameraOk && !hud.cameraText ? color::kActive : color::kDegraded);

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

    font::drawText(r_, controls::kDriving, left, cfg::kScreenHeight - 18, 1, color::kLocked);
}

void Renderer2D::drawMenu(const MenuView& menu) {
    constexpr int kCenterX = cfg::kScreenWidth / 2;
    fillRect(0, 0, cfg::kScreenWidth, cfg::kScreenHeight, color::kDim);

    font::drawTextShadow(r_, "LANE RUNNER", kCenterX - font::textWidth("LANE RUNNER", 6) / 2, 70, 6, color::kTitle);
    drawTextCentered("ADAS HIGHWAY CHALLENGE", kCenterX, 126, 2, color::kLabel);

    int y = 158;
    for (std::size_t i = 0; i < menu.items.size(); ++i) {
        const MenuItemView& item = menu.items[i];
        const bool selected = (i == menu.selected);
        const int width = font::textWidth(item.label, 3);

        if (selected) {
            fillRect(kCenterX - 150, y - 7, 300, 35, color::kHighlight);
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
        y += 33;
    }

    char buf[32];
    std::snprintf(buf, sizeof buf, "BEST %06d   COINS %d", menu.best, menu.coins);
    drawTextCentered(buf, kCenterX, 362, 2, color::kText);

    if (menuBadge_) {
        // Pixel-art badge with its own dark outline, drawn at the menu text's 2x pixel size.
        constexpr int kScale = 2;
        constexpr int kTop = 392;
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

    drawTextCentered(controls::kMenu, kCenterX, 456, 1, color::kLabel);
}

void Renderer2D::drawGameOver(int score, int best, bool newBest, int coinsEarned, const char* what, const char* tip) {
    constexpr int kCenterX = layout::kPanelX / 2;   // centred over the road, the panel stays visible
    fillRect(0, 0, layout::kPanelX, cfg::kScreenHeight, color::kDim);

    font::drawTextShadow(r_, "CRASHED!", kCenterX - font::textWidth("CRASHED!", 5) / 2, 70, 5, color::kCrash);
    if (what) {
        drawTextCentered(what, kCenterX, 118, 2, color::kText);
    }
    if (tip) {
        // Tip in a small card so it reads as advice, not as part of the score.
        const int w = font::textWidth(tip, 1) + 20;
        fillRect(kCenterX - w / 2, 142, w, 20, color::kPanel);
        fillRect(kCenterX - w / 2, 142, 2, 20, color::kTitle);
        drawTextCentered(tip, kCenterX, 149, 1, color::kTitle);
    }

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

    std::snprintf(buf, sizeof buf, "+%d COINS", coinsEarned);
    drawTextCentered(buf, kCenterX, 278, 2, color::kTitle);

    if (blink(1000)) {
        drawTextCentered(controls::kRetry, kCenterX, 318, 2, color::kText);
    }
    drawTextCentered(controls::kToMenu, kCenterX, 348, 2, color::kLabel);
}

void Renderer2D::drawPause(std::size_t selected) {
    constexpr int kCenterX = layout::kPanelX / 2;   // over the road, like the game-over screen
    fillRect(0, 0, layout::kPanelX, cfg::kScreenHeight, color::kDim);

    font::drawTextShadow(r_, "PAUSED", kCenterX - font::textWidth("PAUSED", 5) / 2, 120, 5, color::kTitle);

    const std::array<const char*, 2> items{"RESUME", "QUIT TO MENU"};
    int y = 200;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const bool isSelected = i == selected;
        if (isSelected) {
            const int w = font::textWidth(items[i], 3) + 30;
            fillRect(kCenterX - w / 2, y - 8, w, 36, color::kHighlight);
        }
        drawTextCentered(items[i], kCenterX, y, 3, isSelected ? color::kTitle : color::kText);
        y += 50;
    }

    drawTextCentered(controls::kPaused, kCenterX, 330, 1, color::kLabel);
}

void Renderer2D::drawShop(const ShopView& shop) {
    constexpr int kCenterX = cfg::kScreenWidth / 2;
    fillRect(0, 0, cfg::kScreenWidth, cfg::kScreenHeight, color::kDim);
    font::drawTextShadow(r_, "SHOP", kCenterX - font::textWidth("SHOP", 4) / 2, 22, 4, color::kTitle);

    char buf[48];
    std::snprintf(buf, sizeof buf, "COINS %d", shop.coins);
    drawTextCentered(buf, kCenterX, 60, 2, color::kText);

    constexpr int kRowX = 70;
    constexpr int kRowW = cfg::kScreenWidth - 2 * kRowX;
    constexpr int kRowH = 50;
    int y = 92;
    for (std::size_t i = 0; i <= shop.rows.size(); ++i) {
        const bool selected = i == shop.selected;
        if (selected) {
            fillRect(kRowX - 10, y - 6, kRowW + 20, kRowH - 4, color::kHighlight);
        }
        if (i == shop.rows.size()) {
            drawTextCentered("BACK", kCenterX, y + 6, 3, selected ? color::kTitle : color::kText);
            break;
        }
        const ShopRowView& row = shop.rows[i];
        font::drawTextShadow(r_, row.name, kRowX, y, 2, selected ? color::kTitle : color::kText);
        font::drawText(r_, row.description, kRowX, y + 20, 1, color::kLabel);
        for (int lvl = 0; lvl < row.maxLevel; ++lvl) {   // level pips: filled = bought
            fillRect(kRowX + 250 + lvl * 16, y + 4, 12, 8, lvl < row.level ? color::kActive : color::kBarBack);
        }
        const char* priceText = row.price == 0 ? "MAX" : buf;
        if (row.price != 0) {
            std::snprintf(buf, sizeof buf, "%d", row.price);
        }
        const SDL_Color priceColour = row.price == 0 ? color::kActive : (row.affordable ? color::kTitle : color::kLocked);
        font::drawTextShadow(r_, priceText, kRowX + kRowW - font::textWidth(priceText, 3), y + 4, 3, priceColour);
        y += kRowH;
    }
    drawTextCentered(controls::kShop, kCenterX, 456, 1, color::kLabel);
}

void Renderer2D::drawGarage(const GarageView& garage) {
    constexpr int kCenterX = cfg::kScreenWidth / 2;
    fillRect(0, 0, cfg::kScreenWidth, cfg::kScreenHeight, color::kDim);
    font::drawTextShadow(r_, "GARAGE", kCenterX - font::textWidth("GARAGE", 4) / 2, 30, 4, color::kTitle);

    char buf[32];
    std::snprintf(buf, sizeof buf, "COINS %d", garage.coins);
    drawTextCentered(buf, kCenterX, 72, 2, color::kText);

    constexpr int kSlotW = cfg::kScreenWidth / 5;
    for (std::size_t i = 0; i < garage.cars.size(); ++i) {
        const GarageCarView& car = garage.cars[i];
        const int cx = kSlotW * static_cast<int>(i) + kSlotW / 2;
        const bool selected = i == garage.selected;
        if (selected) {
            fillRect(cx - kSlotW / 2 + 4, 110, kSlotW - 8, 300, color::kHighlight);
        }
        // The car itself, in the player's blue, on a little plinth.
        fillRect(cx - 34, 236, 68, 6, color::kPanelEdge);
        const CarSprite& sprite = cars_[static_cast<std::size_t>(CarSkin::Blue)][static_cast<std::size_t>(car.spriteModel) % kCarModelCount];
        const float h = static_cast<float>(layout::kCarHeight) * sprite.lengthScale;
        const float carY = 236.0f - h / 2.0f - 4.0f;
        if (car.owned) {
            drawSprite(sprite.texture.get(), static_cast<float>(cx), carY, static_cast<float>(layout::kCarWidth), h, true);
        } else {
            SDL_SetTextureColorMod(sprite.texture.get(), 80, 86, 100);   // not yours yet: greyed out, no shadow
            drawSprite(sprite.texture.get(), static_cast<float>(cx), carY, static_cast<float>(layout::kCarWidth), h, false);
            SDL_SetTextureColorMod(sprite.texture.get(), 255, 255, 255);
        }

        drawTextCentered(car.name, cx, 256, 2, selected ? color::kTitle : color::kText);
        drawTextCentered(car.strength, cx, 282, 1, color::kActive);
        if (car.weakness[0] != '-') {
            drawTextCentered(car.weakness, cx, 296, 1, color::kDegraded);
        }
        if (car.inUse) {
            drawTextCentered("IN USE", cx, 330, 2, color::kActive);
        } else if (car.owned) {
            drawTextCentered("OWNED", cx, 330, 2, color::kText);
        } else {
            std::snprintf(buf, sizeof buf, "%d", car.price);
            drawTextCentered(buf, cx, 326, 3, car.affordable ? color::kTitle : color::kLocked);
        }
    }
    drawTextCentered(controls::kGarage, kCenterX, 440, 1, color::kLabel);
}

void Renderer2D::drawHowToPlay(bool firstRun) {
    constexpr int kCenterX = cfg::kScreenWidth / 2;
    fillRect(0, 0, cfg::kScreenWidth, cfg::kScreenHeight, color::kDim);

    struct Rule {
        SDL_Color colour;
        const char* name;
        const char* line1;
        const char* line2;
    };
    const std::array<Rule, 5> rules{{
        {color::kText, "DRIVE", controls::kDriveKeys, "TRAFFIC GETS HEAVIER THE FURTHER YOU GO."},
        {color::kTitle, "SCORE", "EVERY METRE SCORES. PASS CARS CLOSELY FOR A BONUS,",
         "SEVERAL IN A ROW MAKE A COMBO. CUTTING SOMEONE OFF: -250."},
        {SDL_Color{46, 204, 113, 255}, "POWER-UPS", "DRIVE THROUGH ONE TO SWITCH ON A DRIVER ASSIST",
         "FOR A WHILE. WHAT EACH ONE DOES: ASSISTS, IN THE MENU."},
        {SDL_Color{243, 156, 18, 255}, "HAZARDS", "ROADWORKS, ACCIDENTS, OIL, CROSSWIND, FOG, RAIN",
         "AND TUNNELS. SOME OF THEM BLIND THE CAMERA."},
        {SDL_Color{60, 170, 240, 255}, "COINS", "EVERY RUN EARNS COINS. SPEND THEM IN THE SHOP ON",
         "UPGRADES AND IN THE GARAGE ON FASTER CARS."},
    }};

    font::drawTextShadow(r_, "HOW TO PLAY", kCenterX - font::textWidth("HOW TO PLAY", 4) / 2, 18, 4, color::kTitle);
    drawTextCentered("DRIVE AS FAR AS YOU CAN WITHOUT CRASHING", kCenterX, 56, 1, color::kTitle);
    int y = 84;
    for (const Rule& rule : rules) {
        fillRect(56, y + 6, 8, 40, rule.colour);
        font::drawTextShadow(r_, rule.name, 80, y + 4, 2, rule.colour);
        font::drawText(r_, rule.line1, 80, y + 24, 1, color::kText);
        font::drawText(r_, rule.line2, 80, y + 36, 1, color::kText);
        y += 70;
    }
    drawTextCentered(firstRun ? controls::kHowToPlayFirstRun : controls::kHowToPlay, kCenterX, 456, 1,
                     firstRun ? color::kTitle : color::kLabel);
}

void Renderer2D::drawAssistGuide(bool debugMode) {
    constexpr int kCenterX = cfg::kScreenWidth / 2;
    fillRect(0, 0, cfg::kScreenWidth, cfg::kScreenHeight, color::kDim);

    struct Entry {
        const char* badge;
        SDL_Color colour;
        const char* name;
        const char* line1;
        const char* line2;
    };
    const std::array<Entry, 5> entries{{
        {"ACC", SDL_Color{46, 204, 113, 255}, "ADAPTIVE CRUISE CONTROL",
         "HOLDS YOUR SPEED AND KEEPS A SAFE GAP", "TO THE CAR AHEAD. RESUMES AFTER YOU BRAKE."},
        {"AEB", SDL_Color{230, 70, 60, 255}, "AUTOMATIC EMERGENCY BRAKING",
         "BRAKES HARD WHEN A CRASH IS SECONDS AWAY.", "EACH STOP USES ONE SAVE. ALWAYS ON."},
        {"LKA", SDL_Color{60, 170, 240, 255}, "LANE KEEPING ASSIST",
         "KEEPS YOU IN THE MIDDLE OF YOUR LANE", "AGAINST WIND. NEEDS THE CAMERA: NOT IN FOG."},
        {"BSW", SDL_Color{243, 156, 18, 255}, "BLIND SPOT WARNING",
         "WARNS ABOUT CARS BESIDE AND BEHIND YOU", "AND BLOCKS LANE CHANGES INTO THEM."},
        {"FUS", SDL_Color{170, 100, 230, 255}, "SENSOR FUSION",
         "RADAR + CAMERA: IGNORES FAKE RADAR ECHOES", "UNDER BRIDGES, SO NO SURPRISE BRAKING."},
    }};

    font::drawTextShadow(r_, "ASSISTS", kCenterX - font::textWidth("ASSISTS", 4) / 2, 18, 4, color::kTitle);
    drawTextCentered("DRIVE THROUGH A POWER-UP: THE ASSIST SWITCHES ON BY ITSELF", kCenterX, 56, 1, color::kTitle);

    int y = 84;
    for (const Entry& e : entries) {
        drawPickup(56.0f - static_cast<float>(layout::kRoadX), static_cast<float>(y + 22), e.badge, e.colour);
        font::drawTextShadow(r_, e.name, 100, y + 4, 2, e.colour);
        font::drawText(r_, e.line1, 100, y + 24, 1, color::kText);
        font::drawText(r_, e.line2, 100, y + 36, 1, color::kText);
        y += 70;
    }
    drawTextCentered(debugMode ? "DEBUG MODE ON: ALL ASSISTS, NO POWER-UPS, NO SCORE" : "DEBUG MODE OFF", kCenterX, 436, 1,
                     debugMode ? color::kCrash : color::kLabel);
    drawTextCentered(controls::kGuide, kCenterX, 456, 1, color::kLabel);
}
