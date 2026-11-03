#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include "control/Pid.hpp"
#include "core/Config.hpp"
#include "functions/Acc.hpp"
#include "control/Arbiter.hpp"
#include "functions/Aeb.hpp"
#include "functions/Lka.hpp"
#include "world/Collision.hpp"
#include "world/DriverModel.hpp"
#include "world/Lead.hpp"
#include "world/Oil.hpp"
#include "world/Traffic.hpp"
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

    // It moves 0.025 lanes per 20 ms cycle (a lane change takes 0.8 s).
    // It counts once it is closer than kCarWidthLanes (0.45) to me.
    cars[0].changeLane(-1);
    for (int i = 0; i < 12; ++i) {
        cars[0].step(0.02f);
    }
    CHECK(!findLead(ego, cars).has_value());   // lateral 1.70: 0.70 lanes away, not overlapping yet

    for (int i = 0; i < 18; ++i) {
        cars[0].step(0.02f);
    }
    CHECK(findLead(ego, cars).has_value());    // lateral 1.25: 0.25 lanes away, overlapping -> lead
}

// --- ACC ---

// The state machine: A key toggles, the driver's brake cancels to Standby, A resumes the old set speed.
static void testAccStateMachine() {
    Acc acc;
    CHECK(acc.state() == AccState::Off);

    acc.cancel();                               // braking while Off changes nothing
    CHECK(acc.state() == AccState::Off);

    acc.toggle(20.0f);                          // switch on: set speed = current speed
    CHECK(acc.state() == AccState::Active);
    CHECK(near(acc.setSpeed(), 20.0f));

    acc.cancel();                               // driver brakes
    CHECK(acc.state() == AccState::Standby);

    acc.toggle(12.0f);                          // resume: back to Active, keeps the OLD set speed
    CHECK(acc.state() == AccState::Active);
    CHECK(near(acc.setSpeed(), 20.0f));

    acc.toggle(12.0f);                          // switch off
    CHECK(acc.state() == AccState::Off);
}

// When ACC is not Active it must never command anything, even with a car right ahead.
static void testAccInactiveCommandsNothing() {
    Acc acc;
    const std::optional<Lead> lead = Lead{3.0f, -10.0f};   // very close and closing fast
    CHECK(near(acc.step(0.02f, 20.0f, lead), 0.0f));
}

// Closed loop: ACC set at 20 m/s follows a 15 m/s car. It must slow to 15 m/s, settle at the
// desired gap (2 m + 0.5 s x 15 m/s = 9.5 m) and never touch the car on the way.
static void testAccFollowsSlowerCar() {
    Vehicle ego(1, 20.0f, 0.0f);
    std::array<Vehicle, 1> cars{};
    cars[0].reset(1, 15.0f, 40.0f, 0);   // 40 m ahead (centre to centre), 15 m/s
    Acc acc;
    acc.toggle(ego.speed());

    float minGap = 1000.0f;
    for (int i = 0; i < 2000; ++i) {   // 40 s
        ego.setAcceleration(acc.step(0.02f, ego.speed(), findLead(ego, cars)));
        ego.step(0.02f);
        cars[0].step(0.02f);
        minGap = std::min(minGap, cars[0].position() - ego.position() - cfg::kCarLength);
    }
    const float gap = cars[0].position() - ego.position() - cfg::kCarLength;

    CHECK(near(ego.speed(), 15.0f, 0.2f));
    CHECK(near(gap, 9.5f, 1.5f));
    CHECK(minGap > 5.0f);
}

// --- AEB ---

// TTC = gap / closing speed; no TTC when the gap isn't shrinking.
static void testTimeToCollision() {
    const std::optional<float> ttc = timeToCollision(Lead{10.0f, -5.0f});   // 10 m, closing at 5 m/s
    CHECK(ttc.has_value() && near(*ttc, 2.0f));
    CHECK(!timeToCollision(Lead{10.0f, 0.0f}).has_value());    // same speed
    CHECK(!timeToCollision(Lead{10.0f, 3.0f}).has_value());    // lead pulling away
}

// Each TTC band maps to its stage; only the two braking stages command a deceleration.
static void testAebStages() {
    Aeb aeb;
    CHECK(!aeb.step(Lead{30.0f, -5.0f}).has_value() && aeb.stage() == AebStage::Inactive);   // TTC 6.0
    CHECK(!aeb.step(Lead{10.0f, -5.0f}).has_value() && aeb.stage() == AebStage::Warning);    // TTC 2.0

    const std::optional<float> partial = aeb.step(Lead{6.0f, -5.0f});                        // TTC 1.2
    CHECK(aeb.stage() == AebStage::PartialBrake && partial.has_value() && near(*partial, -4.0f));

    const std::optional<float> full = aeb.step(Lead{3.0f, -5.0f});                           // TTC 0.6
    CHECK(aeb.stage() == AebStage::FullBrake && full.has_value() && near(*full, -9.0f));
}

// Latch: once braking, AEB never steps down while still closing in, only releases when the gap
// stops shrinking. Without the latch it would pulse between -9 and -4 as braking raises the TTC.
static void testAebLatch() {
    Aeb aeb;
    aeb.step(Lead{3.0f, -5.0f});                          // TTC 0.6 -> FullBrake
    aeb.step(Lead{5.0f, -5.0f});                          // TTC 1.0 would be PartialBrake alone
    CHECK(aeb.stage() == AebStage::FullBrake);
    aeb.step(Lead{20.0f, -5.0f});                         // TTC 4.0 would be Inactive alone
    CHECK(aeb.stage() == AebStage::FullBrake);

    CHECK(!aeb.step(Lead{5.0f, 0.0f}).has_value());       // no longer closing in -> release
    CHECK(aeb.stage() == AebStage::Inactive);
}

// --- Arbiter ---

// Base priority without AEB: driver brake > driver throttle > ACC > coasting.
static void testArbiterPriority() {
    AccelRequests none;
    CHECK(arbitrate(none).source == AccelSource::Coast);

    AccelRequests accOnly;
    accOnly.acc = 1.5f;
    CHECK(arbitrate(accOnly).source == AccelSource::Acc && near(arbitrate(accOnly).accel, 1.5f));

    AccelRequests throttleOverAcc;                 // pressing the throttle overrides ACC
    throttleOverAcc.acc = -2.0f;
    throttleOverAcc.driverThrottle = 4.0f;
    CHECK(arbitrate(throttleOverAcc).source == AccelSource::Driver && near(arbitrate(throttleOverAcc).accel, 4.0f));

    AccelRequests brakeOverAll;                    // the driver's brake beats throttle and ACC
    brakeOverAll.acc = 2.0f;
    brakeOverAll.driverThrottle = 4.0f;
    brakeOverAll.driverBrake = -8.0f;
    CHECK(arbitrate(brakeOverAll).source == AccelSource::Driver && near(arbitrate(brakeOverAll).accel, -8.0f));
}

// AEB can only make braking stronger: it overrides ACC and throttle, never weakens a harder request.
static void testArbiterAebOverride() {
    AccelRequests overAcc;
    overAcc.acc = 2.0f;
    overAcc.aeb = -9.0f;
    CHECK(arbitrate(overAcc).source == AccelSource::Aeb && near(arbitrate(overAcc).accel, -9.0f));

    AccelRequests overThrottle;                    // can't accelerate into a crash
    overThrottle.driverThrottle = 4.0f;
    overThrottle.aeb = -4.0f;
    CHECK(near(arbitrate(overThrottle).accel, -4.0f));

    AccelRequests driverBrakesHarder;              // partial AEB must not weaken the driver's full brake
    driverBrakesHarder.driverBrake = -8.0f;
    driverBrakesHarder.aeb = -4.0f;
    CHECK(arbitrate(driverBrakesHarder).source == AccelSource::Driver && near(arbitrate(driverBrakesHarder).accel, -8.0f));
}

// --- Free steering & LKA ---

// Runs LKA in closed loop for `seconds`, with an optional constant sideways push (wind).
// Returns the largest offset from the lane centre seen along the way.
static float runLka(Vehicle& car, Lka& lka, float seconds, float push) {
    float maxOffset = 0.0f;
    for (int i = 0; i < static_cast<int>(seconds / 0.02f); ++i) {
        car.steer(lka.step(0.02f, car.lateral(), false));
        car.push(push);
        car.step(0.02f);
        maxOffset = std::max(maxOffset, std::abs(car.lateral() - static_cast<float>(car.lane())));
    }
    return maxOffset;
}

// The curb stops a freely steered car at the road edge instead of letting it leave the road.
static void testFreeSteeringStopsAtCurb() {
    Vehicle car(0, 20.0f);
    car.enableFreeSteering();
    car.steer(-2.0f);   // hold left for 2 s: far more than the half lane to the edge
    for (int i = 0; i < 100; ++i) {
        car.step(0.02f);
    }
    CHECK(near(car.lateral(), -0.5f + cfg::kCarWidthLanes / 2.0f));
    CHECK(car.lane() == 0);
    CHECK(car.hitCurb());   // the game turns this into a crash
}

// Off: no correction at all.
static void testLkaOffDoesNothing() {
    Lka lka;
    CHECK(near(lka.step(0.02f, 1.3f, false), 0.0f));
    CHECK(lka.state() == LkaState::Off);
}

// Car left 0.3 lanes off-centre: LKA steers it back to the middle of lane 1.
static void testLkaCentersCar() {
    Vehicle car(1, 20.0f);
    car.enableFreeSteering();
    car.steer(1.0f);
    for (int i = 0; i < 15; ++i) {   // 0.3 s drifting right
        car.step(0.02f);
    }
    car.steer(0.0f);
    for (int i = 0; i < 25; ++i) {   // let it come to a stop sideways
        car.step(0.02f);
    }
    CHECK(car.lateral() > 1.2f);     // really off-centre before LKA starts

    Lka lka;
    lka.toggle();
    runLka(car, lka, 3.0f, 0.0f);
    CHECK(near(car.lateral(), 1.0f, 0.05f));
}

// Steady crosswind for 4 s: LKA keeps the car inside its lane. Without LKA the same wind
// would push it 2.4 lanes sideways. In-lane = less than half a lane minus half a car off-centre.
static void testLkaHoldsLaneInWind() {
    Vehicle car(1, 20.0f);
    car.enableFreeSteering();
    Lka lka;
    lka.toggle();

    const float maxOffset = runLka(car, lka, 4.0f, 0.6f);
    CHECK(maxOffset < 0.5f - cfg::kCarWidthLanes / 2.0f);
    CHECK(car.lane() == 1);
}

// The driver always wins: while steering, LKA is Overridden and silent; on release it resumes.
static void testLkaDriverOverride() {
    Lka lka;
    lka.toggle();
    CHECK(near(lka.step(0.02f, 1.3f, true), 0.0f));
    CHECK(lka.state() == LkaState::Overridden);

    lka.step(0.02f, 1.3f, false);
    CHECK(lka.state() == LkaState::Active);

    const SteerDecision d = arbitrateSteer(2.0f, -1.2f);   // driver right vs LKA left
    CHECK(d.source == SteerSource::Driver && near(d.rate, 2.0f));
    CHECK(arbitrateSteer(std::nullopt, -1.2f).source == SteerSource::Lka);
}

// --- Traffic: driver models ---

// IDM on an empty road: speeds up below the desired speed, holds it, slows down above it.
static void testIdmFreeRoad() {
    driver::Profile p;
    p.desiredSpeed = 20.0f;
    CHECK(driver::idmAccel(10.0f, p, std::nullopt) > 1.0f);
    CHECK(near(driver::idmAccel(20.0f, p, std::nullopt), 0.0f, 0.01f));
    CHECK(driver::idmAccel(25.0f, p, std::nullopt) < 0.0f);
}

// IDM behind a leader: a far car at the same speed barely matters, a close car we're closing on
// means hard braking.
static void testIdmFollowsLeader() {
    driver::Profile p;
    p.desiredSpeed = 20.0f;
    const float farSameSpeed = driver::idmAccel(20.0f, p, driver::Neighbour{80.0f, 20.0f});
    const float closeClosing = driver::idmAccel(20.0f, p, driver::Neighbour{8.0f, 10.0f});
    CHECK(farSameSpeed > -0.3f);
    CHECK(closeClosing < -5.0f);
}

// MOBIL: unsafe when the new follower would have to brake harder than allowed; worth it only when
// my gain beats the threshold plus the (politeness-weighted) loss I cause the follower.
static void testMobilDecision() {
    driver::Profile p;   // politeness 0.3, safeDecel 3
    const driver::LaneChangeOption good{-1.0f, 1.0f, 0.5f, 0.2f};    // I gain 2.0, follower loses 0.3
    const driver::LaneChangeOption cutIn{-1.0f, 1.0f, 0.5f, -4.0f};  // follower would brake at -4
    const driver::LaneChangeOption tiny{0.0f, 0.1f, 0.0f, 0.0f};     // gain below the threshold
    CHECK(driver::isSafe(good, p) && driver::incentive(good, p) > 0.0f);
    CHECK(!driver::isSafe(cutIn, p));
    CHECK(driver::incentive(tiny, p) < 0.0f);
}

// Long run: 3 minutes of traffic around a player who brakes sensibly for slower cars, at a slow and
// a fast speed. No NPC may ever hit another NPC or the player, and lane changes must happen.
static void testTrafficNeverCrashes() {
    for (const float egoDesired : {8.0f, 26.0f}) {
        Vehicle ego(1, egoDesired, 0.0f);
        ego.enableFreeSteering();
        Traffic traffic;
        driver::Profile egoDriver;
        egoDriver.desiredSpeed = egoDesired;
        int crashes = 0;

        for (int step = 0; step < 50 * 180; ++step) {
            const std::optional<Lead> lead = findLead(ego, traffic);
            ego.setAcceleration(driver::idmAccel(ego.speed(), egoDriver,
                lead ? std::optional<driver::Neighbour>(driver::Neighbour{lead->gap, ego.speed() + lead->relativeSpeed})
                     : std::nullopt));
            ego.step(0.02f);
            traffic.step(0.02f, ego);

            const auto& cars = traffic.cars();
            for (std::size_t a = 0; a < cars.size(); ++a) {
                crashes += collides(ego, cars[a]) ? 1 : 0;
                for (std::size_t b = a + 1; b < cars.size(); ++b) {
                    crashes += collides(cars[a], cars[b]) ? 1 : 0;
                }
            }
        }
        CHECK(crashes == 0);
        CHECK(traffic.laneChanges() > 20);
    }
}

// --- Road surface ---

// Low grip: the same steering input moves the car sideways much less.
static void testLowGripWeakensSteering() {
    Vehicle dry(1, 20.0f);
    Vehicle oily(1, 20.0f);
    dry.enableFreeSteering();
    oily.enableFreeSteering();
    oily.setGrip(0.15f);
    dry.steer(2.0f);
    oily.steer(2.0f);
    for (int i = 0; i < 10; ++i) {   // 0.2 s of full steering
        dry.step(0.02f);
        oily.step(0.02f);
    }
    CHECK(oily.lateral() - 1.0f < 0.4f * (dry.lateral() - 1.0f));
}

// Driving onto a patch: grip drops, the car is pushed sideways, then the tyres clean up again.
static void testOilMakesCarSlide() {
    OilField oil;
    const OilField::Patch patch = oil.patches()[0];
    Vehicle car(0, 20.0f, patch.position);   // right on the first patch
    car.enableFreeSteering();
    for (int lane = 0; lane < cfg::kLaneCount; ++lane) {
        if (OilField::touches(patch, Vehicle(lane, 20.0f, patch.position))) {
            car = Vehicle(lane, 20.0f, patch.position);
            break;
        }
    }

    const GripEffect hit = oil.step(0.02f, car);
    CHECK(hit.justHit);
    CHECK(hit.grip < 0.3f);
    CHECK(std::abs(hit.slide) > 0.5f);

    const Vehicle away(car.lane(), 20.0f, patch.position + 30.0f);   // drove off the patch
    GripEffect later;
    for (int i = 0; i < 75; ++i) {   // 1.5 s
        later = oil.step(0.02f, away);
    }
    CHECK(near(later.grip, 1.0f) && near(later.slide, 0.0f));
}

int main() {
    testProportionalOnly();
    testOutputIsClamped();
    testNoOvershoot();
    testLeadInMyLane();
    testNoLead();
    testNearestLeadWins();
    testCutInIsDetected();
    testAccStateMachine();
    testAccInactiveCommandsNothing();
    testAccFollowsSlowerCar();
    testTimeToCollision();
    testAebStages();
    testAebLatch();
    testArbiterPriority();
    testArbiterAebOverride();
    testFreeSteeringStopsAtCurb();
    testLkaOffDoesNothing();
    testLkaCentersCar();
    testLkaHoldsLaneInWind();
    testLkaDriverOverride();
    testIdmFreeRoad();
    testIdmFollowsLeader();
    testMobilDecision();
    testTrafficNeverCrashes();
    testLowGripWeakensSteering();
    testOilMakesCarSlide();

    std::printf(g_failures == 0 ? "All tests passed\n" : "%d test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;

}