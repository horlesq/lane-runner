#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <random>
#include "core/Config.hpp"
#include "core/Span.hpp"
#include "world/DriverModel.hpp"
#include "world/Obstacle.hpp"
#include "world/Vehicle.hpp"

// Everything a simulated driver remembers, kept next to its Vehicle (same index).
struct DriverState {
    driver::Profile profile;
    int signal = 0;              // turn indicator: -1 left, +1 right, 0 off
    float signalTime = 0.0f;     // s the indicator has been on
    bool changingLane = false;   // moving sideways into the signalled lane
    float cooldown = 0.0f;       // s before it looks for another lane change
    float reactionTime = 1.0f;   // s before reacting when someone suddenly cuts in close
    float reactionLeft = 0.0f;   // s still to go before this driver notices
    bool followingPlayer = false;
    float accel = 0.0f;          // m/s^2 last cycle (brake lights; kept while not yet reacting)
    float honkCooldown = 0.0f;   // s before it can honk again
};

// NPC traffic with simple but standard driver models:
//  - car following with IDM (smooth, realistic braking behind slower cars and behind the player),
//  - lane changes with MOBIL (only when faster AND safe for the car behind), announced by indicators,
//  - mixed personalities: calm drivers, slow pickups, aggressive ones that cut in tight, fast motorcycles,
//  - human reaction time: when the player cuts in close, a driver keeps going for 0.6-1.4 s before
//    braking, so an unchecked lane change in front of a fast car can end in a rear-end crash.
// A fixed pool of cars is recycled (no allocation): cars far behind or ahead are respawned.
class Traffic {
public:
    static constexpr std::size_t kMaxCars = 8;
    static constexpr std::size_t kVariantCount = 20;
    static constexpr int kColours = 4;        // variant = colour + kColours * model
    static constexpr int kPickupModel = 3;    // pickups are the slow vehicles

    // seed: the game passes a new one every run; tests use the default so their traffic is reproducible.
    explicit Traffic(std::uint32_t seed = 42) : rng_(seed) {
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            const int variant = static_cast<int>((i * 7) % kVariantCount);   // mix colours and models
            drivers_[i] = makeDriver(variant);
            cars_[i].reset(static_cast<int>(i % cfg::kLaneCount), drivers_[i].profile.desiredSpeed,
                           25.0f + 14.0f * static_cast<float>(i), variant);
        }
    }

    // One 20 ms cycle. egoIsTraffic = false on the title screen, where the "ego" is just a camera.
    // obstacles: static things on the road (closed lanes...) that drivers brake for and merge around.
    void step(float dt, const Vehicle& ego, bool egoIsTraffic = true, Span<StaticObstacle> obstacles = {}) {
        const Vehicle* egoCar = egoIsTraffic ? &ego : nullptr;
        obstacles_ = obstacles;   // only used during this call

        // 1. Every driver decides from the same snapshot, so the result doesn't depend on update order.
        std::array<float, kMaxCars> accel{};
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            accel[i] = decideAccel(i, dt, egoCar);
        }

        // 2. Lane changes: indicate first, re-check, then move.
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            updateLaneChange(i, accel[i], dt, egoCar);
        }

        // 3. Move, then recycle cars that left the area around the player.
        for (std::size_t i = 0; i < kMaxCars; ++i) {
            if (i >= activeCount_) {
                // Not in play yet (difficulty): parked far behind, out of every sensor and out of view.
                cars_[i].reset(0, ego.speed(), ego.position() - kParkedBehind, cars_[i].variant(), cars_[i].type());
                continue;
            }
            drivers_[i].accel = accel[i];
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

    // Difficulty: how many of the kMaxCars vehicles are on the road. Parked ones join in when raised.
    void setActiveCount(std::size_t n) { activeCount_ = std::min(n, kMaxCars); }

    // How many times since the last call a driver had to brake hard because the player cut them off.
    int takeCutOffs() {
        const int n = cutOffs_;
        cutOffs_ = 0;
        return n;
    }

    // Put vehicle i in a scripted state (tests and scenarios); its driver starts fresh with this profile.
    void place(std::size_t i, const Vehicle& vehicle, const driver::Profile& profile, float reactionTime) {
        cars_[i] = vehicle;
        drivers_[i] = DriverState{};
        drivers_[i].profile = profile;
        drivers_[i].reactionTime = reactionTime;
        drivers_[i].cooldown = 1000.0f;   // scripted vehicles keep their lane
    }

private:
    static constexpr float kFollowWidth = 0.6f;    // lanes; a car this close sideways counts as "in front of me"
    static constexpr float kSignalLead = 0.8f;     // s of indicating before starting to move over
    static constexpr float kLaneChangeTime = 0.8f; // s to slide over one lane (Vehicle: 1.25 lanes/s)
    static constexpr float kBesideMargin = 2.0f;   // m of extra room needed next to me to move over
    static constexpr float kMatchSpeedRange = 60.0f;   // m; a new car adopts the speed of a car this close ahead
    static constexpr float kSpawnBrakeCheck = 3.0f;    // m/s^2; spawn gaps must allow a calm stop, not an emergency one
    static constexpr float kFasterFromBehindChance = 0.85f;   // a driver faster than the player usually comes from behind
    static constexpr float kRubberBandChance = 0.35f;         // some slower ones too, sped up to overtake
    static constexpr float kMinSpeedForOvertakers = 2.0f;     // m/s; a stopped player gets no overtakers
    static constexpr float kMaxNpcSpeed = 46.0f;           // m/s, 165 km/h
    static constexpr float kMotorcycleChance = 0.12f;      // share of new vehicles that are motorcycles
    static constexpr float kCutOffDecel = 3.5f;            // m/s^2; braking this hard because of the player = cut off
    static constexpr float kHonkCooldown = 4.0f;           // s
    static constexpr float kParkedBehind = 5000.0f;        // m; where inactive vehicles wait
    static constexpr float kStuckSpeed = 5.0f;             // m/s; slower than this, a driver merges more assertively
    static constexpr float kObstacleSpawnClearance = 20.0f; // m, plus braking distance, before a closure

    // Another road user found in a lane, with what is needed to predict its reaction.
    struct Other {
        float position;
        float speed;
        float length;
        driver::Profile profile;
    };

    struct Leader {
        driver::Neighbour neighbour;
        bool isPlayer;
    };

    // The player is treated as a cautious driver when NPCs check whether cutting in is safe.
    static driver::Profile egoProfile(const Vehicle& ego) {
        driver::Profile p;
        p.desiredSpeed = std::max(ego.speed(), 5.0f);
        p.timeHeadway = 1.0f;
        return p;
    }

    // Acceleration for driver i: IDM behind its leader, but with a human reaction time when the player
    // suddenly appears inside its following distance (a cut-in): until it notices, it keeps doing what it did.
    float decideAccel(std::size_t i, float dt, const Vehicle* ego) {
        DriverState& d = drivers_[i];
        d.honkCooldown -= dt;
        const std::optional<Leader> leader = leaderOf(i, ego);
        const bool behindPlayer = leader && leader->isPlayer;
        const float comfortableGap = cars_[i].speed() * d.profile.timeHeadway;
        if (behindPlayer && !d.followingPlayer && leader->neighbour.gap < comfortableGap) {
            d.reactionLeft = d.reactionTime;   // the player appeared out of nowhere
        }
        d.followingPlayer = behindPlayer;

        float a = driver::idmAccel(cars_[i].speed(), d.profile,
                                   leader ? std::optional<driver::Neighbour>(leader->neighbour) : std::nullopt);
        if (d.reactionLeft > 0.0f) {
            d.reactionLeft -= dt;
            a = d.accel;   // hasn't reacted yet
        } else if (behindPlayer && a < -kCutOffDecel && d.honkCooldown <= 0.0f) {
            ++cutOffs_;    // forced to brake hard because of the player
            d.honkCooldown = kHonkCooldown;
        }
        return a;
    }

    // Leader for car following: nearest vehicle ahead that overlaps me sideways (also mid lane change).
    std::optional<Leader> leaderOf(std::size_t i, const Vehicle* ego) const {
        const Vehicle& me = cars_[i];
        std::optional<Leader> best;
        const auto consider = [&](const Vehicle& other, bool isPlayer) {
            const float distance = other.position() - me.position();
            if (distance <= 0.0f || std::abs(other.lateral() - me.lateral()) >= kFollowWidth) {
                return;
            }
            const float gap = bumperGap(me, other);
            if (!best || gap < best->neighbour.gap) {
                best = Leader{driver::Neighbour{gap, other.speed()}, isPlayer};
            }
        };
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != i) {
                consider(cars_[j], false);
            }
        }
        if (ego) {
            consider(*ego, true);
        }
        // A static obstacle in my path is a "leader" standing still: IDM brakes for it, MOBIL merges away.
        for (const StaticObstacle& o : obstacles_) {
            if (o.start > me.position() && std::abs(o.lateral - me.lateral()) < o.halfWidth + me.width() / 2.0f) {
                const float gap = o.start - me.position() - me.length() / 2.0f;
                if (!best || gap < best->neighbour.gap) {
                    best = Leader{driver::Neighbour{gap, 0.0f}, false};
                }
            }
        }
        return best;
    }

    static bool coversLane(const StaticObstacle& o, int lane) {
        return std::abs(o.lateral - static_cast<float>(lane)) < o.halfWidth + 0.25f;
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
        const auto consider = [&](const Vehicle& v, const driver::Profile& profile) {
            const float distance = ahead ? v.position() - myPos : myPos - v.position();
            const bool onThisSide = ahead ? distance > 0.0f : distance >= 0.0f;   // exactly level counts as behind
            if (onThisSide && (!best || distance < std::abs(best->position - myPos))) {
                best = Other{v.position(), v.speed(), v.length(), profile};
            }
        };
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != i && occupies(j, lane)) {
                consider(cars_[j], drivers_[j].profile);
            }
        }
        if (ego && std::abs(ego->lateral() - static_cast<float>(lane)) < kFollowWidth) {
            consider(*ego, egoProfile(*ego));
        }
        if (ahead) {
            for (const StaticObstacle& o : obstacles_) {
                const float distance = o.start - myPos;
                if (coversLane(o, lane) && distance > 0.0f && (!best || distance < std::abs(best->position - myPos))) {
                    best = Other{o.start, 0.0f, 0.0f, driver::Profile{}};   // stands still; its face is at `start`
                }
            }
        }
        return best;
    }

    // MOBIL evaluation of moving car i into `lane`. Empty when there is no room beside me.
    std::optional<driver::LaneChangeOption> evaluate(std::size_t i, int lane, float myAccelNow, const Vehicle* ego) const {
        const Vehicle& me = cars_[i];
        const std::optional<Other> leader = nearestInLane(i, lane, true, ego);
        const std::optional<Other> follower = nearestInLane(i, lane, false, ego);

        for (const StaticObstacle& o : obstacles_) {   // never move into a closed stretch of lane
            // Ahead, count the distance covered while indicating and sliding over, not just the car's length.
            const float reachAhead = me.length() / 2.0f + kBesideMargin + me.speed() * (kSignalLead + kLaneChangeTime);
            const float reachBehind = me.length() / 2.0f + kBesideMargin;
            if (coversLane(o, lane) && me.position() + reachAhead > o.start && me.position() - reachBehind < o.end) {
                return std::nullopt;
            }
        }
        const auto minRoom = [&](const Other& o) { return (me.length() + o.length) / 2.0f + kBesideMargin; };
        if ((leader && leader->position - me.position() < minRoom(*leader)) ||
            (follower && me.position() - follower->position < minRoom(*follower))) {
            return std::nullopt;   // someone is right beside me
        }

        // Bumper gap between a front and a back road user, from their centres and lengths.
        const auto gapBetween = [](float frontPos, float frontLen, float backPos, float backLen) {
            return frontPos - backPos - (frontLen + backLen) / 2.0f;
        };

        driver::LaneChangeOption o{};
        o.myAccelNow = myAccelNow;
        o.myAccelAfter = driver::idmAccel(
            me.speed(), drivers_[i].profile,
            leader ? std::optional<driver::Neighbour>(driver::Neighbour{gapBetween(leader->position, leader->length, me.position(), me.length()), leader->speed})
                   : std::nullopt);
        if (follower) {
            o.followerAccelNow = driver::idmAccel(
                follower->speed, follower->profile,
                leader ? std::optional<driver::Neighbour>(driver::Neighbour{gapBetween(leader->position, leader->length, follower->position, follower->length), leader->speed})
                       : std::nullopt);
            o.followerAccelAfter = driver::idmAccel(
                follower->speed, follower->profile, driver::Neighbour{gapBetween(me.position(), me.length(), follower->position, follower->length), me.speed()});
        }
        return o;
    }

    void updateLaneChange(std::size_t i, float myAccelNow, float dt, const Vehicle* ego) {
        Vehicle& car = cars_[i];
        DriverState& d = drivers_[i];
        d.cooldown -= dt;
        // Stuck (almost stopped, e.g. at a lane closure): a driver merges more assertively, like a zipper merge.
        driver::Profile p = d.profile;
        if (car.speed() < kStuckSpeed) {
            p.safeDecel = std::max(p.safeDecel, 6.0f);
            p.politeness = 0.0f;
        }

        if (d.signal != 0) {
            d.signalTime += dt;
            if (!d.changingLane && d.signalTime >= kSignalLead) {
                // Last look before moving over: the gap may have closed while indicating.
                const std::optional<driver::LaneChangeOption> o = evaluate(i, car.lane() + d.signal, myAccelNow, ego);
                if (o && driver::isSafe(*o, p)) {
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
            if (!o || !driver::isSafe(*o, p)) {
                continue;
            }
            const float gain = driver::incentive(*o, p);
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

    DriverState makeDriver(int variant, VehicleType type = VehicleType::Car) {
        auto uniform = [this](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng_); };
        DriverState d;
        const bool pickup = type == VehicleType::Car && variant / kColours == kPickupModel;
        const bool aggressive = type == VehicleType::Car && !pickup && std::bernoulli_distribution(0.15)(rng_);
        if (type == VehicleType::Motorcycle) {
            d.profile = {uniform(34.0f, 44.0f), uniform(0.7f, 1.0f), 0.1f, 5.0f};   // fast and nimble: 122-158 km/h
            d.reactionTime = uniform(0.6f, 0.9f);
        } else if (pickup) {
            d.profile = {uniform(14.0f, 19.0f), uniform(1.6f, 2.2f), 0.5f, 2.0f};   // slow and careful: 50-68 km/h
            d.reactionTime = uniform(1.0f, 1.4f);
        } else if (aggressive) {
            d.profile = {uniform(30.0f, 38.0f), uniform(0.8f, 1.0f), 0.0f, 6.0f};   // fast, tailgates, cuts in: 108-137 km/h
            d.reactionTime = uniform(0.7f, 1.0f);
        } else {
            d.profile = {uniform(20.0f, 30.0f), uniform(1.2f, 1.8f), 0.3f, 3.0f};   // 72-108 km/h
            d.reactionTime = uniform(0.8f, 1.2f);
        }
        d.cooldown = uniform(1.0f, 4.0f);
        return d;
    }

    void respawn(std::size_t i, const Vehicle& ego) {
        const VehicleType type = std::bernoulli_distribution(kMotorcycleChance)(rng_) ? VehicleType::Motorcycle : VehicleType::Car;
        const int variant = type == VehicleType::Motorcycle ? std::uniform_int_distribution<int>(0, kColours - 1)(rng_)
                                                             : std::uniform_int_distribution<int>(0, kVariantCount - 1)(rng_);
        DriverState fresh = makeDriver(variant, type);

        // Where a new car appears depends on who is faster:
        //  - faster than the player: ahead it would only drive away, so it usually comes from behind and overtakes,
        //  - slower: it appears ahead and the player catches up.
        // So that cars still overtake a player near top speed, some slower drivers are also sent from behind
        // and then made 7-11 m/s faster than the player is going right now (game-design "rubber banding").
        // Slow pickups never come from behind.
        const bool pickup = type == VehicleType::Car && variant / kColours == kPickupModel;
        const bool fasterThanPlayer = fresh.profile.desiredSpeed > ego.speed() + 3.0f;
        const float behindChance = fasterThanPlayer ? kFasterFromBehindChance : kRubberBandChance;
        const bool fromBehind = !pickup && ego.speed() > kMinSpeedForOvertakers && std::bernoulli_distribution(behindChance)(rng_);
        if (fromBehind && !fasterThanPlayer) {
            const float faster = ego.speed() + std::uniform_real_distribution<float>(7.0f, 11.0f)(rng_);
            fresh.profile.desiredSpeed = std::max(fresh.profile.desiredSpeed, std::min(faster, kMaxNpcSpeed));
        }
        const float position = fromBehind ? ego.position() - std::uniform_real_distribution<float>(20.0f, 26.0f)(rng_)
                                          : ego.position() + std::uniform_real_distribution<float>(35.0f, 50.0f)(rng_);
        // Most overtakers come up in a lane next to the player (the blind spot), the rest anywhere.
        int lane = std::uniform_int_distribution<int>(0, cfg::kLaneCount - 1)(rng_);
        if (fromBehind && std::bernoulli_distribution(0.7)(rng_)) {
            const int side = std::bernoulli_distribution(0.5)(rng_) ? -1 : 1;
            lane = ego.lane() + side;
            if (lane < 0 || lane >= cfg::kLaneCount) {
                lane = ego.lane() - side;
            }
        }

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
        cars_[i].reset(lane, speed, position, variant, type);
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
        for (const StaticObstacle& o : obstacles_) {
            // Not inside a closed lane, and far enough before it to stop calmly from the spawn speed.
            const float stopping = kObstacleSpawnClearance + speed * speed / (2.0f * kSpawnBrakeCheck);
            if (coversLane(o, lane) && position > o.start - stopping && position < o.end + 5.0f) {
                return false;
            }
        }
        for (std::size_t j = 0; j < kMaxCars; ++j) {
            if (j != self && occupies(j, lane) && !roomBetween(position, speed, cars_[j])) {
                return false;
            }
        }
        return !(inLane(ego, lane) && !roomBetween(position, speed, ego));
    }

    std::array<Vehicle, kMaxCars> cars_;
    std::array<DriverState, kMaxCars> drivers_;
    std::mt19937 rng_;
    int laneChanges_ = 0;
    int cutOffs_ = 0;
    std::size_t activeCount_ = kMaxCars;
    Span<StaticObstacle> obstacles_;
};
