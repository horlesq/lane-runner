#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

// What the player keeps between runs: best score, coins and shop upgrades.
// Saved as a small "key=value" text file, read at startup and written after a run or a purchase
// (file I/O happens outside the 20 ms game loop).
enum class ShopItem : std::uint8_t { LongerAssists, AebSaves, StartWithAcc, Magnet, Shield, CoinBonus, Count };

struct ShopOffer {
    const char* name;
    const char* description;
    int maxLevel;
    std::array<int, 3> prices;   // price of level 1, 2, 3
};

constexpr std::array<ShopOffer, static_cast<std::size_t>(ShopItem::Count)> kShopOffers{{
    {"LONGER ASSISTS", "+4 S PER POWER-UP", 3, {40, 90, 180}},
    {"AEB SAVES", "+1 SAVE AT START", 2, {60, 150, 0}},
    {"CRUISE START", "START WITH ACC", 1, {120, 0, 0}},
    {"MAGNET", "POWER-UPS DRIFT TO YOU", 1, {100, 0, 0}},
    {"SHIELD", "SURVIVE 1 CRASH PER RUN", 1, {220, 0, 0}},
    {"COIN BONUS", "+25% COINS PER LEVEL", 2, {80, 200, 0}},
}};

// Cars in the garage. Each is a body style from the sprite pack with its own trade-off.
struct CarOffer {
    const char* name;
    const char* strength;      // one line for the garage screen
    const char* weakness;
    int spriteModel;           // body style index in the sprite pack
    int price;                 // 0 = owned from the start
    float accelScale;          // throttle multiplier
    float topSpeedBonus;       // m/s added to the top speed
    float steerRate;           // lanes/s while steering
    float length;              // m, hitbox
    float width;               // lanes, hitbox
    int shields;               // crashes survived per run
};

constexpr std::array<CarOffer, 5> kCars{{
    {"SEDAN", "BALANCED", "-", 2, 0, 1.0f, 0.0f, 2.0f, 4.2f, 0.45f, 0},
    {"COUPE", "SHARP STEERING", "-", 1, 150, 1.0f, 0.0f, 2.6f, 4.0f, 0.45f, 0},
    {"COMPACT", "SMALL HITBOX", "-", 4, 150, 1.0f, -2.0f, 2.0f, 3.6f, 0.38f, 0},
    {"SPORTS", "FAST", "-", 0, 250, 1.35f, 6.0f, 2.2f, 4.2f, 0.45f, 0},
    {"PICKUP", "SURVIVES 1 CRASH", "SLOW", 3, 300, 0.8f, -4.0f, 1.8f, 4.4f, 0.47f, 1},
}};

struct Progress {
    int best = 0;
    int coins = 0;
    std::array<int, static_cast<std::size_t>(ShopItem::Count)> levels{};   // bought level per item
    unsigned ownedCars = 1u;   // bit i = car i owned; the sedan is free
    int selectedCar = 0;

    bool ownsCar(std::size_t i) const { return (ownedCars >> i) & 1u; }
    const CarOffer& car() const { return kCars[static_cast<std::size_t>(selectedCar)]; }

    // Garage: buy a car you don't own (and drive it), or switch to one you do. False if you can't afford it.
    bool chooseCar(std::size_t i) {
        if (i >= kCars.size()) {
            return false;
        }
        if (!ownsCar(i)) {
            if (coins < kCars[i].price) {
                return false;
            }
            coins -= kCars[i].price;
            ownedCars |= 1u << i;
        }
        selectedCar = static_cast<int>(i);
        return true;
    }

    int level(ShopItem item) const { return levels[static_cast<std::size_t>(item)]; }

    // Price of the next level, or 0 when the item is maxed out.
    int nextPrice(ShopItem item) const {
        const ShopOffer& offer = kShopOffers[static_cast<std::size_t>(item)];
        const int lvl = level(item);
        return lvl < offer.maxLevel ? offer.prices[static_cast<std::size_t>(lvl)] : 0;
    }

    // Spends coins on the next level. False when maxed out or not enough coins (nothing changes).
    bool buy(ShopItem item) {
        const int price = nextPrice(item);
        if (price == 0 || coins < price) {
            return false;
        }
        coins -= price;
        ++levels[static_cast<std::size_t>(item)];
        return true;
    }

    // Game rules derived from the upgrades.
    float powerUpSeconds() const { return 12.0f + 4.0f * static_cast<float>(level(ShopItem::LongerAssists)); }
    int startingAebSaves() const { return 1 + level(ShopItem::AebSaves); }
    bool startsWithAcc() const { return level(ShopItem::StartWithAcc) > 0; }
    bool hasMagnet() const { return level(ShopItem::Magnet) > 0; }
    int shieldsPerRun() const { return level(ShopItem::Shield) + car().shields; }
    float coinMultiplier() const { return 1.0f + 0.25f * static_cast<float>(level(ShopItem::CoinBonus)); }

    // End of a run: coins from the score, and the best score. Returns the coins earned.
    int finishRun(int score) {
        const int earned = static_cast<int>(static_cast<float>(score / kScorePerCoin) * coinMultiplier());
        coins += earned;
        if (score > best) {
            best = score;
        }
        return earned;
    }

    static constexpr int kScorePerCoin = 50;

    bool save(const std::string& path) const {
        std::ofstream out(path);
        if (!out) {
            return false;
        }
        out << "best=" << best << '\n' << "coins=" << coins << '\n';
        out << "cars=" << ownedCars << '\n' << "car=" << selectedCar << '\n';
        for (std::size_t i = 0; i < levels.size(); ++i) {
            out << "level" << i << '=' << levels[i] << '\n';
        }
        return static_cast<bool>(out);
    }

    // Missing or broken file: keep the defaults (a fresh player). Unknown keys are ignored.
    bool load(const std::string& path) {
        std::ifstream in(path);
        if (!in) {
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            const std::string key = line.substr(0, eq);
            const int value = std::atoi(line.c_str() + eq + 1);
            if (key == "best") {
                best = value;
            } else if (key == "coins") {
                coins = value;
            } else if (key == "cars") {
                ownedCars = (static_cast<unsigned>(value) | 1u) & ((1u << kCars.size()) - 1u);   // sedan always owned
            } else if (key == "car") {
                selectedCar = value;
            } else if (key.rfind("level", 0) == 0) {
                const std::size_t i = static_cast<std::size_t>(std::atoi(key.c_str() + 5));
                if (i < levels.size()) {
                    const int maxLevel = kShopOffers[i].maxLevel;
                    levels[i] = value < 0 ? 0 : (value > maxLevel ? maxLevel : value);   // never trust the file
                }
            }
        }
        if (selectedCar < 0 || static_cast<std::size_t>(selectedCar) >= kCars.size() ||
            !ownsCar(static_cast<std::size_t>(selectedCar))) {
            selectedCar = 0;   // never drive a car you don't own
        }
        return true;
    }
};
