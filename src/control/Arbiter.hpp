#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

enum class AccelSource : std::uint8_t { Coast, Driver, Acc, Aeb };

// Every request that wants to move the car this cycle. Empty = that source has no request.
struct AccelRequests {
    std::optional<float> driverBrake;      // m/s^2, negative, while the brake is held
    std::optional<float> driverThrottle;   // m/s^2, positive, while the throttle is held
    std::optional<float> acc;              // m/s^2, while ACC is Active
    std::optional<float> aeb;              // m/s^2, negative, while AEB brakes
};

struct AccelDecision {
    float accel;          // the one acceleration sent to the car
    AccelSource source;   // who won
};

// Fixed rules, checked every cycle:
//   1. Base request by priority: driver brake > driver throttle > ACC > coasting.
//   2. Safety override: AEB may only make the result stronger braking (minimum wins).
inline AccelDecision arbitrate(const AccelRequests& in) {
    constexpr float kCoastDecel = -0.5f;   // m/s^2, rolling resistance with nothing pressed

    AccelDecision base{kCoastDecel, AccelSource::Coast};
    if (in.driverBrake) {
        base = {*in.driverBrake, AccelSource::Driver};
    } else if (in.driverThrottle) {
        base = {*in.driverThrottle, AccelSource::Driver};
    } else if (in.acc) {
        base = {*in.acc, AccelSource::Acc};
    }

    if (in.aeb && *in.aeb < base.accel) {
        return {*in.aeb, AccelSource::Aeb};
    }
    return base;
}

// --- Lateral (steering) ---

enum class SteerSource : std::uint8_t { None, Driver, Lka };

struct SteerDecision {
    float rate;           // lanes/s sideways
    SteerSource source;   // who won
};

// The driver always has final authority over steering; LKA only acts when the driver doesn't steer.
inline SteerDecision arbitrateSteer(std::optional<float> driver, std::optional<float> lka) {
    if (driver) {
        return {*driver, SteerSource::Driver};
    }
    if (lka) {
        return {*lka, SteerSource::Lka};
    }
    return {0.0f, SteerSource::None};
}
