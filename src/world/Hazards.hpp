#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include "core/FixedVector.hpp"
#include "core/Config.hpp"
#include "world/Obstacle.hpp"
#include "world/Vehicle.hpp"

// Fog banks, metal bridges and roadworks along the road. Small fixed pools, recycled ahead of the player.
//  - Fog blinds the camera (and the player's view).
//  - A metal bridge reflects radar waves: the front radar reports a "ghost" object in the lane.
//  - Roadworks close a lane: everyone has to merge out before the barrier.
//  - Debris: small objects in a lane; radar sees them late, the camera early.
//  - Accidents: wrecked cars standing across a lane.
//  - Rain: less grip, and the camera sees less far.
//  - Tunnels: dark; at the entrance and exit the camera is dazzled for a moment.
class RoadHazards {
public:
    static constexpr std::size_t kMaxFog = 2;
    static constexpr std::size_t kMaxBridges = 2;

    static constexpr std::size_t kMaxRoadworks = 1;
    static constexpr std::size_t kMaxDebris = 2;
    static constexpr bool kDebrisEnabled = false;   // off: not on the road, not drawn, not an obstacle
    static constexpr std::size_t kMaxAccidents = 1;
    static constexpr std::size_t kMaxRain = 1;
    static constexpr std::size_t kMaxTunnels = 1;

    enum class DebrisKind : std::uint8_t { Tyres, Barrel, Rock };

    struct Debris {
        float lateral;     // lanes
        float position;    // m
        DebrisKind kind;
    };

    struct Accident {
        int lane;
        float position;    // m: first wreck
    };

    struct Zone {          // rain or tunnel
        float start;
        float end;
    };
    static constexpr std::size_t kMaxObstacles = 8;
    using Obstacles = FixedVector<StaticObstacle, kMaxObstacles>;

    struct FogBank {
        float start;   // m along the road
        float end;
    };

    struct Roadworks {
        int lane;        // the closed lane
        float start;     // m: the barrier
        float end;
        int mergeDir;    // -1 = merge left, +1 = merge right (what the arrows show)
    };

    // seed: the game passes a new one every run; tests use the default for reproducible layouts.
    explicit RoadHazards(std::uint32_t seed = kDefaultSeed) : rng_(seed) {
        const auto first = [this](float base) { return base * uniform(0.6f, 1.4f); };   // the opening differs too
        for (std::size_t i = 0; i < kMaxFog; ++i) {
            fog_[i] = makeFog(first(kFirstFog) + kFogSpacing * static_cast<float>(i));
        }
        for (std::size_t i = 0; i < kMaxBridges; ++i) {
            bridges_[i] = first(kFirstBridge) + kBridgeSpacing * static_cast<float>(i);
        }
        for (std::size_t i = 0; i < kMaxRoadworks; ++i) {
            roadworks_[i] = makeRoadworks(first(kFirstRoadworks) + kRoadworksSpacing * static_cast<float>(i));
        }
        for (std::size_t i = 0; i < kMaxDebris; ++i) {
            debris_[i] = makeDebris(first(kFirstDebris) + kDebrisSpacing * static_cast<float>(i));
        }
        accidents_[0] = makeAccident(first(kFirstAccident));
        const float rainStart = first(kFirstRain);
        rain_[0] = Zone{rainStart, rainStart + uniform(200.0f, 350.0f)};
        const float tunnelStart = first(kFirstTunnel);
        tunnels_[0] = Zone{tunnelStart, tunnelStart + uniform(150.0f, 260.0f)};
    }

    static constexpr std::uint32_t kDefaultSeed = 2024;

    // Recycle hazards the player has left behind.
    void step(const Vehicle& ego) {
        for (FogBank& f : fog_) {
            if (f.end < ego.position() - kRecycleBehind) {
                f = makeFog(furthestFog() + uniform(kFogSpacing * 0.6f, kFogSpacing * 1.4f));
            }
        }
        for (float& b : bridges_) {
            if (b < ego.position() - kRecycleBehind) {
                b = furthestBridge() + uniform(kBridgeSpacing * 0.6f, kBridgeSpacing * 1.4f);
            }
        }
        for (Roadworks& r : roadworks_) {
            if (r.end < ego.position() - kRecycleBehind) {
                r = makeRoadworks(ego.position() + uniform(kRoadworksSpacing * 0.7f, kRoadworksSpacing * 1.3f));
            }
        }
        for (Debris& d : debris_) {
            if (d.position < ego.position() - kRecycleBehind) {
                d = makeDebris(ego.position() + uniform(kDebrisSpacing * 0.7f, kDebrisSpacing * 1.3f));
            }
        }
        for (Accident& a : accidents_) {
            if (a.position + kAccidentLength < ego.position() - kRecycleBehind) {
                a = makeAccident(ego.position() + uniform(kAccidentSpacing * 0.7f, kAccidentSpacing * 1.3f));
            }
        }
        for (Zone& z : rain_) {
            if (z.end < ego.position() - kRecycleBehind) {
                const float start = ego.position() + uniform(kRainSpacing * 0.7f, kRainSpacing * 1.3f);
                z = Zone{start, start + uniform(200.0f, 350.0f)};
            }
        }
        for (Zone& z : tunnels_) {
            if (z.end < ego.position() - kRecycleBehind) {
                const float start = ego.position() + uniform(kTunnelSpacing * 0.7f, kTunnelSpacing * 1.3f);
                z = Zone{start, start + uniform(150.0f, 260.0f)};
            }
        }
    }

    bool inRain(float position) const { return inAny(rain_, position, 0.0f); }
    bool inTunnel(float position) const { return inAny(tunnels_, position, 0.0f); }

    // Camera glare: within a few metres of a tunnel entrance or exit the light changes too fast to adapt.
    bool cameraGlare(float position) const {
        for (const Zone& t : tunnels_) {
            if (std::abs(position - t.start) < kGlareRange || std::abs(position - t.end) < kGlareRange) {
                return true;
            }
        }
        return false;
    }

    const std::array<Debris, kMaxDebris>& debris() const { return debris_; }
    const std::array<Accident, kMaxAccidents>& accidents() const { return accidents_; }
    const std::array<Zone, kMaxRain>& rain() const { return rain_; }
    const std::array<Zone, kMaxTunnels>& tunnels() const { return tunnels_; }

    static constexpr float kAccidentLength = 9.0f;   // m: two wrecks, one behind the other
    static constexpr float kGlareRange = 12.0f;      // m around a tunnel portal

    // Everything static on the road, in the one form traffic, sensors and collisions all understand.
    Obstacles obstacles() const {
        Obstacles list;
        for (const Roadworks& r : roadworks_) {
            list.push_back(StaticObstacle{static_cast<float>(r.lane), 0.5f, r.start, r.end});
        }
        for (const Debris& d : debris_) {
            if (!kDebrisEnabled) {
                break;
            }
            list.push_back(StaticObstacle{d.lateral, kDebrisHalfWidth, d.position - 0.6f, d.position + 0.6f, true, ObstacleKind::Debris});
        }
        for (const Accident& a : accidents_) {
            list.push_back(StaticObstacle{static_cast<float>(a.lane), 0.45f, a.position - 2.5f, a.position + kAccidentLength, false,
                                          ObstacleKind::Wreck});
        }
        return list;
    }

    const std::array<Roadworks, kMaxRoadworks>& roadworks() const { return roadworks_; }

    bool inFog(float position) const {
        for (const FogBank& f : fog_) {
            if (position >= f.start && position <= f.end) {
                return true;
            }
        }
        return false;
    }

    // Distance from the ego to the nearest bridge ahead within [minAhead, maxAhead], if any.
    std::optional<float> bridgeAhead(const Vehicle& ego, float minAhead, float maxAhead) const {
        std::optional<float> nearest;
        for (float b : bridges_) {
            const float d = b - ego.position();
            if (d >= minAhead && d <= maxAhead && (!nearest || d < *nearest)) {
                nearest = d;
            }
        }
        return nearest;
    }

    const std::array<FogBank, kMaxFog>& fog() const { return fog_; }
    const std::array<float, kMaxBridges>& bridges() const { return bridges_; }

private:
    static constexpr float kFirstFog = 400.0f;      // m; the start of a run is clear
    static constexpr float kFogSpacing = 650.0f;
    static constexpr float kFirstBridge = 250.0f;
    static constexpr float kBridgeSpacing = 450.0f;
    static constexpr float kRecycleBehind = 30.0f;
    static constexpr float kFirstRoadworks = 550.0f;
    static constexpr float kRoadworksSpacing = 900.0f;
    static constexpr float kFirstDebris = 300.0f;
    static constexpr float kDebrisSpacing = 380.0f;
    static constexpr float kDebrisHalfWidth = 0.18f;   // lanes
    static constexpr float kFirstAccident = 1100.0f;
    static constexpr float kAccidentSpacing = 1300.0f;
    static constexpr float kFirstRain = 700.0f;
    static constexpr float kRainSpacing = 1000.0f;
    static constexpr float kFirstTunnel = 900.0f;
    static constexpr float kTunnelSpacing = 1200.0f;

    template <std::size_t N>
    static bool inAny(const std::array<Zone, N>& zones, float position, float margin) {
        for (const Zone& z : zones) {
            if (position >= z.start - margin && position <= z.end + margin) {
                return true;
            }
        }
        return false;
    }

    // A lane that isn't closed by roadworks around `position` (so obstacles never pile up in one spot).
    int freeLane(float position) {
        int lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);
        for (const Roadworks& r : roadworks_) {
            if (lane == r.lane && position > r.start - 60.0f && position < r.end + 20.0f) {
                lane = (lane + 2) % cfg::kLaneCount;
            }
        }
        return lane;
    }

    Debris makeDebris(float position) {
        const int lane = freeLane(position);
        const auto kind = static_cast<DebrisKind>(std::uniform_int_distribution<int>(0, 2)(rng_));
        return Debris{static_cast<float>(lane) + uniform(-0.2f, 0.2f), position, kind};
    }

    Accident makeAccident(float position) { return Accident{freeLane(position), position}; }

    Roadworks makeRoadworks(float start) {
        const int lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);
        const int mergeDir = lane == 0 ? 1 : lane == cfg::kLaneCount - 1 ? -1 : (std::bernoulli_distribution(0.5)(rng_) ? 1 : -1);
        return Roadworks{lane, start, start + uniform(80.0f, 150.0f), mergeDir};
    }

    float uniform(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng_); }

    FogBank makeFog(float start) { return FogBank{start, start + uniform(60.0f, 120.0f)}; }

    float furthestFog() const {
        float far = 0.0f;
        for (const FogBank& f : fog_) {
            far = std::max(far, f.end);
        }
        return far;
    }

    float furthestBridge() const {
        float far = 0.0f;
        for (float b : bridges_) {
            far = std::max(far, b);
        }
        return far;
    }

    std::array<FogBank, kMaxFog> fog_{};
    std::array<float, kMaxBridges> bridges_{};
    std::array<Roadworks, kMaxRoadworks> roadworks_{};
    std::array<Debris, kMaxDebris> debris_{};
    std::array<Accident, kMaxAccidents> accidents_{};
    std::array<Zone, kMaxRain> rain_{};
    std::array<Zone, kMaxTunnels> tunnels_{};
    std::mt19937 rng_;
};
