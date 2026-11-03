#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

// Models for simulated drivers, as used in traffic simulation:
//  - IDM (Intelligent Driver Model, Treiber 2000): how hard to accelerate or brake behind a leader.
//  - MOBIL (Kesting 2007): whether a lane change is worth it and safe for the car behind.
namespace driver {

    // Personality of one simulated driver.
    struct Profile {
        float desiredSpeed = 15.0f;   // m/s on an empty road
        float timeHeadway = 1.5f;     // s, following distance it likes to keep
        float politeness = 0.3f;      // 0 = ignores others, 1 = weighs their braking as much as its own gain
        float safeDecel = 3.0f;       // m/s^2, hardest braking it will force on the car behind when cutting in
    };

    // The vehicle directly ahead (or behind) in a lane.
    struct Neighbour {
        float gap;     // m, bumper to bumper
        float speed;   // m/s
    };

    constexpr float kMaxAccel = 1.5f;       // m/s^2
    constexpr float kComfortDecel = 2.5f;   // m/s^2, the braking IDM aims for in normal traffic
    constexpr float kMaxBrake = 9.0f;       // m/s^2, physical limit
    constexpr float kMinGap = 3.0f;         // m, gap kept even when standing still

    // IDM: acceleration for a car at `speed` with an optional leader.
    //   a = aMax * [1 - (v/v0)^4 - (s*/s)^2],   s* = s0 + v*T + v*dv / (2*sqrt(aMax*b))
    // The first term pulls towards the desired speed, the second pushes back when the gap gets
    // smaller than the desired gap s*, which grows with speed and with how fast we are closing in.
    inline float idmAccel(float speed, const Profile& p, const std::optional<Neighbour>& leader) {
        const float freeRoad = 1.0f - std::pow(speed / std::max(p.desiredSpeed, 0.1f), 4.0f);
        float interaction = 0.0f;
        if (leader) {
            const float closing = speed - leader->speed;
            const float desiredGap =
                kMinGap + std::max(0.0f, speed * p.timeHeadway + speed * closing / (2.0f * std::sqrt(kMaxAccel * kComfortDecel)));
            const float ratio = desiredGap / std::max(leader->gap, 0.1f);
            interaction = ratio * ratio;
        }
        return std::clamp(kMaxAccel * (freeRoad - interaction), -kMaxBrake, kMaxAccel);
    }

    // MOBIL lane-change decision.
    // Safety: after the change, the new follower must not need to brake harder than `safeDecel`.
    // Incentive: my gain in acceleration must beat a threshold plus `politeness` x the follower's loss.
    struct LaneChangeOption {
        float myAccelNow;        // with my current leader
        float myAccelAfter;      // with the leader in the target lane
        float followerAccelNow;  // the target lane's follower, before I move in (0 if none)
        float followerAccelAfter;// the same follower with me as its new leader (0 if none)
    };

    constexpr float kChangeThreshold = 0.15f;   // m/s^2, minimum gain so cars don't change lanes for nothing

    inline bool isSafe(const LaneChangeOption& o, const Profile& p) {
        return o.followerAccelAfter >= -p.safeDecel;
    }

    inline float incentive(const LaneChangeOption& o, const Profile& p) {
        const float myGain = o.myAccelAfter - o.myAccelNow;
        const float followerLoss = o.followerAccelNow - o.followerAccelAfter;
        return myGain - p.politeness * followerLoss - kChangeThreshold;
    }

} // namespace driver
