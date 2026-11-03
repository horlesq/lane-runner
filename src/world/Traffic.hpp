#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <random>
#include "core/Config.hpp"
#include "world/DriverModel.hpp"
#include "world/Vehicle.hpp"

// Everything a simulated driver remembers, kept next to its Vehicle (same index).
struct DriverState {
    driver::Profile profile;
    int signal = 0;              // turn indicator: -1 left, +1 right, 0 off
    float signalTime = 0.0f;     // s the indicator has been on
    bool changingLane = false;   // moving sideways into the signalled lane
    float cooldown = 0.0f;       // s before it looks for another lane change
};

// NPC traffic with simple but standard driver models:
//  - car following with IDM (smooth, realistic braking behind slower cars and behind the player),
//  - lane changes with MOBIL (only when faster AND safe for the car behind), announced by indicators,
//  - mixed personalities: calm drivers, slow pickups, and a few aggressive ones that cut in tight.
// A fixed pool of cars is recycled (no allocation): cars far behind or ahead are respawned.
class Traffic {
public:
    static constexpr std::size_t kMaxCars = 8;
    static constexpr std::size_t kVariantCount = 20;
    static constexpr int kColours = 4;        // variant = colour + kColours * model
    static constexpr int kPickupModel = 3;    // pickups are the slow vehicles

    Traffic() {
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            const int variant = static_cast<int>((i * 7) % kVariantCount);   // mix colours and models
            drivers_[i] = makeDriver(variant);
            cars_[i].reset(static_cast<int>(i % cfg::kLaneCount), drivers_[i].profile.desiredSpeed,
                           25.0f + 14.0f * static_cast<float>(i), variant);
        }
    }

    // One 20 ms cycle. egoIsTraffic = false on the title screen, where the "ego" is just a camera.
    void step(float dt, const Vehicle& ego, bool egoIsTraffic = true) {
        const Vehicle* egoCar = egoIsTraffic ? &ego : nullptr;

        // 1. Every driver decides from the same snapshot, so the result doesn't depend on update order.
        std::array<float, kMaxCars> accel{};
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            accel[i] = driver::idmAccel(cars_[i].speed(), drivers_[i].profile, leaderOf(i, egoCar));
        }

        // 2. Lane changes: indicate first, re-check, then move.
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            updateLaneChange(i, accel[i], dt, egoCar);
        }

        // 3. Move, then recycle cars that left the area around the player.
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            cars_[i].setAcceleration(accel[i]);
            cars_[i].step(dt);
            const float relative = cars_[i].position() - ego.position();   // + = ahead of me, - = behind
            if (relative < -cfg::kDespawnBehind || relative > cfg::kDespawnAhead) {
                respawn(i, ego);
            }
        }
    }

    const std::array<Vehicle, kMaxCars>& cars() const { return cars_; }
    const DriverState& driver(std::size_t i) const { return drivers_[i]; }
    int laneChanges() const { return laneChanges_; }   // total started, for tests and statistics

private:
    static constexpr float kFollowWidth = 0.6f;    // lanes; a car this close sideways counts as "in front of me"
    static constexpr float kSignalLead = 0.8f;     // s of indicating before starting to move over
    static constexpr float kBesideMargin = 2.0f;   // m of extra room needed next to me to move over
    static constexpr float kMatchSpeedRange = 60.0f;   // m; a new car adopts the speed of a car this close ahead
    static constexpr float kSpawnBrakeCheck = 3.0f;    // m/s^2; spawn gaps must allow a calm stop, not an emergency one

    // Another road user found in a lane, with what is needed to predict its reaction.
    struct Other {
        float position;
        float speed;
        driver::Profile profile;
    };

    // The player is treated as a cautious driver when NPCs check whether cutting in is safe.
    static driver::Profile egoProfile(const Vehicle& ego) {
        driver::Profile p;
        p.desiredSpeed = std::max(ego.speed(), 5.0f);
        p.timeHeadway = 1.0f;
        return p;
    }

    // Leader for car following: nearest vehicle ahead that overlaps me sideways (also mid lane change).
    std::optional<driver::Neighbour> leaderOf(std::size_t i, const Vehicle* ego) const {
        const Vehicle& me = cars_[i];
        std::optional<driver::Neighbour> best;
        const auto consider = [&](const Vehicle& other) {
            const float distance = other.position() - me.position();
            if (distance <= 0.0f || std::abs(other.lateral() - me.lateral()) >= kFollowWidth) {
                return;
            }
            const float gap = distance - cfg::kCarLength;
            if (!best || gap < best->gap) {
                best = driver::Neighbour{gap, other.speed()};
            }
        };
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != i) {
                consider(cars_[j]);
            }
        }
        if (ego) {
            consider(*ego);
        }
        return best;
    }

    // Is NPC j in `lane`, moving into it, or about to (indicating towards it)?
    bool occupies(std::size_t j, int lane) const {
        const Vehicle& c = cars_[j];
        const DriverState& d = drivers_[j];
        return c.lane() == lane || std::abs(c.lateral() - static_cast<float>(lane)) < kFollowWidth ||
               (d.signal != 0 && c.lane() + d.signal == lane);
    }

    // Nearest road user ahead of (or behind) car i in `lane`, the player included.
    std::optional<Other> nearestInLane(std::size_t i, int lane, bool ahead, const Vehicle* ego) const {
        const float myPos = cars_[i].position();
        std::optional<Other> best;
        const auto consider = [&](float position, float speed, const driver::Profile& profile) {
            const float distance = ahead ? position - myPos : myPos - position;
            const bool onThisSide = ahead ? distance > 0.0f : distance >= 0.0f;   // exactly level counts as behind
            if (onThisSide && (!best || distance < std::abs(best->position - myPos))) {
                best = Other{position, speed, profile};
            }
        };
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != i && occupies(j, lane)) {
                consider(cars_[j].position(), cars_[j].speed(), drivers_[j].profile);
            }
        }
        if (ego && std::abs(ego->lateral() - static_cast<float>(lane)) < kFollowWidth) {
            consider(ego->position(), ego->speed(), egoProfile(*ego));
        }
        return best;
    }

    // MOBIL evaluation of moving car i into `lane`. Empty when there is no room beside me.
    std::optional<driver::LaneChangeOption> evaluate(std::size_t i, int lane, float myAccelNow, const Vehicle* ego) const {
        const Vehicle& me = cars_[i];
        const std::optional<Other> leader = nearestInLane(i, lane, true, ego);
        const std::optional<Other> follower = nearestInLane(i, lane, false, ego);

        const float minRoom = cfg::kCarLength + kBesideMargin;
        if ((leader && leader->position - me.position() < minRoom) ||
            (follower && me.position() - follower->position < minRoom)) {
            return std::nullopt;   // someone is right beside me
        }

        const auto gapBetween = [](float frontPos, float backPos) { return frontPos - backPos - cfg::kCarLength; };

        driver::LaneChangeOption o{};
        o.myAccelNow = myAccelNow;
        o.myAccelAfter = driver::idmAccel(
            me.speed(), drivers_[i].profile,
            leader ? std::optional<driver::Neighbour>(driver::Neighbour{gapBetween(leader->position, me.position()), leader->speed})
                   : std::nullopt);
        if (follower) {
            o.followerAccelNow = driver::idmAccel(
                follower->speed, follower->profile,
                leader ? std::optional<driver::Neighbour>(driver::Neighbour{gapBetween(leader->position, follower->position), leader->speed})
                       : std::nullopt);
            o.followerAccelAfter = driver::idmAccel(
                follower->speed, follower->profile, driver::Neighbour{gapBetween(me.position(), follower->position), me.speed()});
        }
        return o;
    }

    void updateLaneChange(std::size_t i, float myAccelNow, float dt, const Vehicle* ego) {
        Vehicle& car = cars_[i];
        DriverState& d = drivers_[i];
        d.cooldown -= dt;

        if (d.signal != 0) {
            d.signalTime += dt;
            if (!d.changingLane && d.signalTime >= kSignalLead) {
                // Last look before moving over: the gap may have closed while indicating.
                const std::optional<driver::LaneChangeOption> o = evaluate(i, car.lane() + d.signal, myAccelNow, ego);
                if (o && driver::isSafe(*o, d.profile)) {
                    car.changeLane(d.signal);
                    d.changingLane = true;
                    ++laneChanges_;
                } else {
                    d.signal = 0;          // give up this time
                    d.cooldown = 1.0f;
                }
            } else if (d.changingLane && std::abs(car.lateral() - static_cast<float>(car.lane())) < 0.02f) {
                d.signal = 0;              // arrived in the new lane
                d.changingLane = false;
                d.cooldown = std::uniform_real_distribution<float>(2.0f, 5.0f)(rng_);
            }
            return;
        }
        if (d.cooldown > 0.0f) {
            return;
        }

        // Look left and right, keep the most attractive safe option.
        int bestDir = 0;
        float bestIncentive = 0.0f;
        for (int dir : {-1, +1}) {
            const int lane = car.lane() + dir;
            if (lane < 0 || lane >= cfg::kLaneCount) {
                continue;
            }
            const std::optional<driver::LaneChangeOption> o = evaluate(i, lane, myAccelNow, ego);
            if (!o || !driver::isSafe(*o, d.profile)) {
                continue;
            }
            const float gain = driver::incentive(*o, d.profile);
            if (gain > bestIncentive) {
                bestIncentive = gain;
                bestDir = dir;
            }
        }
        if (bestDir != 0) {
            d.signal = bestDir;
            d.signalTime = 0.0f;
        } else {
            d.cooldown = 0.5f;   // look again in half a second, not every cycle: calmer and cheaper
        }
    }

    DriverState makeDriver(int variant) {
        auto uniform = [this](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng_); };
        DriverState d;
        const bool pickup = variant / kColours == kPickupModel;
        const bool aggressive = !pickup && std::bernoulli_distribution(0.15)(rng_);
        if (pickup) {
            d.profile = {uniform(11.0f, 15.0f), uniform(1.6f, 2.2f), 0.5f, 2.0f};   // slow and careful
        } else if (aggressive) {
            d.profile = {uniform(22.0f, 28.0f), uniform(0.8f, 1.0f), 0.0f, 6.0f};   // fast, tailgates, cuts in
        } else {
            d.profile = {uniform(14.0f, 22.0f), uniform(1.2f, 1.8f), 0.3f, 3.0f};
        }
        d.cooldown = uniform(1.0f, 4.0f);
        return d;
    }

    void respawn(std::size_t i, const Vehicle& ego) {
        const int variant = std::uniform_int_distribution<int>(0, kVariantCount - 1)(rng_);
        const DriverState fresh = makeDriver(variant);

        // Fast drivers sometimes come up from behind (out of view), the rest appear ahead.
        const bool fromBehind = fresh.profile.desiredSpeed > ego.speed() + 4.0f && std::bernoulli_distribution(0.35)(rng_);
        const float position = fromBehind ? ego.position() - std::uniform_real_distribution<float>(20.0f, 28.0f)(rng_)
                                          : ego.position() + std::uniform_real_distribution<float>(35.0f, 50.0f)(rng_);
        const int lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);

        // Merge in at the speed of the car ahead in that lane, like real traffic: a new car must not
        // appear doing 28 m/s right behind a queue at 12 m/s.
        float speed = fresh.profile.desiredSpeed;
        const auto slowDownFor = [&](const Vehicle& other) {
            const float ahead = other.position() - position;
            if (ahead > 0.0f && ahead < kMatchSpeedRange) {
                speed = std::min(speed, other.speed());
            }
        };
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != i && occupies(j, lane)) {
                slowDownFor(cars_[j]);
            }
        }
        if (inLane(ego, lane)) {
            slowDownFor(ego);
        }

        if (!isFree(i, lane, position, speed, ego)) {
            return;   // try again next cycle with new random values
        }
        cars_[i].reset(lane, speed, position, variant);
        drivers_[i] = fresh;
    }

    static bool inLane(const Vehicle& v, int lane) {
        return std::abs(v.lateral() - static_cast<float>(lane)) < kFollowWidth;
    }

    // Enough room to appear at `position` with `speed`: the fixed clearance, plus the distance the
    // faster of each pair would need to brake down to the slower one's speed.
    static bool roomBetween(float position, float speed, const Vehicle& other) {
        const float distance = std::abs(other.position() - position);
        const bool otherAhead = other.position() > position;
        const float closing = otherAhead ? speed - other.speed() : other.speed() - speed;
        const float brakingDistance = closing > 0.0f ? closing * closing / (2.0f * kSpawnBrakeCheck) : 0.0f;
        return distance - cfg::kCarLength >= std::max(cfg::kSpawnClearance, driver::kMinGap + brakingDistance);
    }

    bool isFree(std::size_t self, int lane, float position, float speed, const Vehicle& ego) const {
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != self && occupies(j, lane) && !roomBetween(position, speed, cars_[j])) {
                return false;
            }
        }
        return !(inLane(ego, lane) && !roomBetween(position, speed, ego));
    }

    std::array<Vehicle, kMaxCars> cars_;
    std::array<DriverState, kMaxCars> drivers_;
    std::mt19937 rng_{42};   // fixed seed: the same traffic every run, so behaviour is reproducible
    int laneChanges_ = 0;
};
