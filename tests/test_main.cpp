#include <algorithm>
#include <cmath>
#include <cstdio>
#include "control/Pid.hpp"

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

int main() {
    testProportionalOnly();
    testOutputIsClamped();
    testNoOvershoot();

    std::printf(g_failures == 0 ? "All tests passed\n" : "%d test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;

}