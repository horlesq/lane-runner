#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include "control/Pid.hpp"
#include "core/Config.hpp"
#include "world/Lead.hpp"
#include "world/Vehicle.hpp"

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

    
static bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }

// Test the proportional-only behavior of the PID controller.
static void testProportionalOnly() {
    Pid<float> pid({2.0f, 0.0f, 0.0f}, -100.0f, 100.0f);

    CHECK(near(pid.step(3.0f, 0.02f), 6.0f));
}

// Test that the PID output is clamped to the specified limits.
static void testOutputIsClamped() {
    Pid<float> pid({10.0f, 0.0f, 0.0f}, -8.0f, 2.0f);

    CHECK(near(pid.step(5.0f, 0.02f), 2.0f));
}

// Regression test for integrator windup.
//
// The PID is used as a simple cruise control: it must bring a car from 0 to 20 m/s.
// The output (acceleration) is limited to +2 m/s^2, so for the first ~10 s the controller is
// saturated at full throttle. Without anti-windup the integral keeps growing during that time,
// and the stored integral pushes the car far past the target (measured: 27.7 m/s).
// With anti-windup the peak stays close to 20 m/s (measured: 20.28 m/s).
static void testNoOvershoot() {
    Pid<float> pid({1.0f, 0.1f, 0.5f}, -8.0f, 2.0f);   // kp, ki, kd; brake -8 .. throttle +2 m/s^2

    constexpr float kTarget = 20.0f;   // m/s
    constexpr float kDt = 0.02f;       // the game's 20 ms cycle
    constexpr int kSteps = 1500;       // 1500 x 20 ms = 30 s of simulated driving

    float speed = 0.0f;      // m/s, the car starts standing still
    float maxSpeed = 0.0f;   // highest speed seen during the run = how far it overshoots

    for (int i = 0; i < kSteps; ++i) {
        const float accel = pid.step(kTarget - speed, kDt);   // error = how far below the target we are
        speed += accel * kDt;                                 // simplest vehicle model: v += a * dt
        maxSpeed = std::max(maxSpeed, speed);
    }

    CHECK(maxSpeed < kTarget * 1.05f);   // overshoot below 5 %
    CHECK(near(speed, kTarget, 0.5f));   // and after 30 s it has settled near the target
}

// --- findLead: which car ahead does ACC follow? ---
// Each test places cars by hand (lane, speed m/s, position m) around an ego in lane 1 at 0 m, 20 m/s.

// One car 20 m ahead in my lane: it is the lead. The gap is bumper to bumper, so the car
// length is subtracted, and the relative speed is negative because I'm faster (closing in).
static void testLeadInMyLane() {
    const Vehicle ego(1, 20.0f, 0.0f);
    const std::array<Vehicle, 1> cars{Vehicle(1, 15.0f, 20.0f)};

    const std::optional<Lead> lead = findLead(ego, cars);
    CHECK(lead.has_value());
    if (lead) {   // never dereference an empty optional, even in a test
        CHECK(near(lead->gap, 20.0f - cfg::kCarLength));
        CHECK(near(lead->relativeSpeed, 15.0f - 20.0f));
    }
}

// Cars that ACC must ignore: next lane, behind me, beyond sensor range. Result: no lead at all.
static void testNoLead() {
    const Vehicle ego(1, 20.0f, 0.0f);
    const std::array<Vehicle, 3> cars{
        Vehicle(2, 15.0f, 20.0f),                        // next lane
        Vehicle(1, 15.0f, -15.0f),                       // same lane, but behind
        Vehicle(1, 15.0f, cfg::kSensorRange + 10.0f),    // same lane, too far to detect
    };

    CHECK(!findLead(ego, cars).has_value());
}

// Two cars in my lane: ACC must follow the NEAREST one, whatever order they are stored in.
static void testNearestLeadWins() {
    const Vehicle ego(1, 20.0f, 0.0f);
    const std::array<Vehicle, 2> cars{Vehicle(1, 18.0f, 40.0f), Vehicle(1, 12.0f, 25.0f)};

    const std::optional<Lead> lead = findLead(ego, cars);
    CHECK(lead.has_value());
    if (lead) {
        CHECK(near(lead->gap, 25.0f - cfg::kCarLength));
        CHECK(near(lead->relativeSpeed, 12.0f - 20.0f));
    }
}

// A car cutting in from the next lane becomes the lead as soon as it overlaps me sideways,
// before it has fully arrived in my lane. This is why findLead uses lateral(), not lane().
static void testCutInIsDetected() {
    const Vehicle ego(1, 20.0f, 0.0f);
    std::array<Vehicle, 1> cars{Vehicle(2, 15.0f, 20.0f)};

    CHECK(!findLead(ego, cars).has_value());   // still fully in lane 2: not my lead

    // It moves 0.08 lanes per 20 ms cycle. It counts once it is closer than kCarWidthLanes (0.45) to me.
    cars[0].changeLane(-1);
    for (int i = 0; i < 4; ++i) {
        cars[0].step(0.02f);
    }
    CHECK(!findLead(ego, cars).has_value());   // lateral 1.68: 0.68 lanes away, not overlapping yet

    for (int i = 0; i < 4; ++i) {
        cars[0].step(0.02f);
    }
    CHECK(findLead(ego, cars).has_value());    // lateral 1.36: 0.36 lanes away, overlapping -> lead
}

int main() {
    testProportionalOnly();
    testOutputIsClamped();
    testNoOvershoot();
    testLeadInMyLane();
    testNoLead();
    testNearestLeadWins();
    testCutInIsDetected();

    std::printf(g_failures == 0 ? "All tests passed\n" : "%d test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;

}