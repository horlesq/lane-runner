#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include "control/Pid.hpp"
#include "core/Config.hpp"
#include "functions/Acc.hpp"
#include "control/Arbiter.hpp"
#include "functions/Aeb.hpp"
#include "functions/Bsw.hpp"
#include "functions/Lka.hpp"
#include "world/Collision.hpp"
#include "world/DriverModel.hpp"
#include "world/Lead.hpp"
#include "world/Hazards.hpp"
#include "world/Obstacle.hpp"
#include "world/Oil.hpp"
#include "world/PowerUps.hpp"
#include "core/FixedVector.hpp"
#include "perception/ObjectList.hpp"
#include "game/CrashReport.hpp"
#include "game/NearMiss.hpp"
#include "game/Progress.hpp"
#include "perception/Fusion.hpp"
#include "sensors/Camera.hpp"
#include "sensors/Radar.hpp"
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
        CHECK(traffic.laneChanges() > 5);   // they do change lanes (measured: 4-10 per minute)
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

// --- FixedVector, sensors and BSW ---

// Fixed capacity: push_back refuses instead of allocating, and range-for only visits valid elements.
static void testFixedVector() {
    FixedVector<int, 3> v;
    CHECK(v.empty() && v.capacity() == 3);
    CHECK(v.push_back(1) && v.push_back(2) && v.push_back(3));
    CHECK(v.full());
    CHECK(!v.push_back(4));   // full: dropped, size unchanged
    CHECK(v.size() == 3);

    int sum = 0;
    for (int x : v) {
        sum += x;
    }
    CHECK(sum == 6);

    v.clear();
    CHECK(v.empty() && v.begin() == v.end());
}

// A radar reports only vehicles inside its zone, relative to the ego.
static void testRadarZones() {
    const Vehicle ego(1, 20.0f, 100.0f);
    const std::array<Vehicle, 4> cars{
        Vehicle(1, 15.0f, 130.0f),   // 30 m ahead, my lane    -> front radar
        Vehicle(0, 25.0f, 95.0f),    // 5 m behind, left lane  -> rear-left radar
        Vehicle(2, 20.0f, 100.0f),   // alongside, right lane  -> rear-right radar
        Vehicle(1, 20.0f, 200.0f),   // 100 m ahead            -> out of everyone's range
    };
    Radar front("FRONT", radar_zones::kFront);
    Radar rearLeft("REAR LEFT", radar_zones::kRearLeft);
    Radar rearRight("REAR RIGHT", radar_zones::kRearRight);
    front.update(ego, cars, SensorEnvironment{});
    rearLeft.update(ego, cars, SensorEnvironment{});
    rearRight.update(ego, cars, SensorEnvironment{});

    CHECK(front.objects().size() == 1);
    CHECK(near(front.objects()[0].longitudinal, 30.0f) && near(front.objects()[0].relativeSpeed, -5.0f));
    CHECK(rearLeft.objects().size() == 1 && near(rearLeft.objects()[0].lateral, -1.0f));
    CHECK(rearRight.objects().size() == 1 && near(rearRight.objects()[0].longitudinal, 0.0f));
}

// Used through the interface (std::unique_ptr<Sensor>), a radar behaves the same, and lead selection
// on its object list gives exactly what the ground-truth findLead() gives.
static void testLeadFromRadarMatchesGroundTruth() {
    const Vehicle ego(1, 20.0f, 0.0f);
    const std::array<Vehicle, 3> cars{Vehicle(1, 12.0f, 40.0f), Vehicle(1, 18.0f, 22.0f), Vehicle(2, 5.0f, 10.0f)};
    const std::unique_ptr<Sensor> radar = std::make_unique<Radar>("FRONT", radar_zones::kFront);
    radar->update(ego, cars, SensorEnvironment{});

    const std::optional<Lead> fromRadar = selectLead(radar->objects());
    const std::optional<Lead> truth = findLead(ego, cars);
    CHECK(fromRadar.has_value() && truth.has_value());
    if (fromRadar && truth) {
        CHECK(near(fromRadar->gap, truth->gap) && near(fromRadar->relativeSpeed, truth->relativeSpeed));
    }
    CHECK(std::string(radar->name()) == "FRONT");
}

// BSW: warns about a car in a blind spot, blocks steering towards it, lets steering away through,
// and ignores a car that is behind and falling back. Off = no effect at all.
static void testBsw() {
    ObjectList left;
    ObjectList right;
    left.push_back(DetectedObject{-3.0f, -1.0f, 2.0f});   // left lane, just behind, catching up

    Bsw bsw;
    CHECK(bsw.step(left, right, -2.0f).has_value());     // Off: steering passes untouched
    bsw.toggle();

    CHECK(!bsw.step(left, right, -2.0f).has_value());    // steer left into it: blocked
    CHECK(bsw.state() == BswState::Intervening);

    const std::optional<float> away = bsw.step(left, right, 2.0f);   // steer right: allowed
    CHECK(away.has_value() && near(*away, 2.0f));
    CHECK(bsw.state() == BswState::Warning && bsw.leftOccupied() && !bsw.rightOccupied());

    ObjectList fallingBack;
    fallingBack.push_back(DetectedObject{-12.0f, -1.0f, -6.0f});   // far behind and slower: no threat
    CHECK(bsw.step(fallingBack, right, -2.0f).has_value());
    CHECK(bsw.state() == BswState::Clear);

    // Lane change assist: far behind but closing fast (reaches me in about 2 s) is a threat too.
    ObjectList fastFromBehind;
    fastFromBehind.push_back(DetectedObject{-30.0f, -1.0f, 12.0f});
    CHECK(!bsw.step(fastFromBehind, right, -2.0f).has_value());
}

// --- Cutting in front of a faster car ---

struct CutInResult {
    bool crashed = false;
    bool blocked = false;   // BSW refused the lane change at least once
    int cutOffs = 0;        // the other driver had to brake hard because of the player
};

// Player at 22 m/s in lane 1 steers into lane 2, where a car `faster` m/s quicker is `behind` metres back
// (driver reaction time 1 s). Optionally the steering goes through BSW. 8 simulated seconds.
static CutInResult cutIn(float behind, float faster, bool withBsw) {
    Vehicle ego(1, 22.0f, 1000.0f);
    ego.enableFreeSteering();
    Traffic traffic;
    driver::Profile cruise;
    cruise.desiredSpeed = 22.0f;
    for (std::size_t i = 1; i < Traffic::kMaxCars; ++i) {   // everyone else ahead in lane 3, out of the way
        traffic.place(i, Vehicle(3, 22.0f, 1040.0f + 3.0f * static_cast<float>(i)), cruise, 1.0f);
    }
    driver::Profile fast;
    fast.desiredSpeed = 22.0f + faster;
    fast.timeHeadway = 1.2f;
    traffic.place(0, Vehicle(2, 22.0f + faster, 1000.0f - behind), fast, 1.0f);

    Radar rearLeft("REAR LEFT", radar_zones::kRearLeft);
    Radar rearRight("REAR RIGHT", radar_zones::kRearRight);
    Bsw bsw;
    if (withBsw) {
        bsw.toggle();
    }
    CutInResult r;
    for (int step = 0; step < 50 * 8 && !r.crashed; ++step) {
        rearLeft.update(ego, traffic.cars(), SensorEnvironment{});
        rearRight.update(ego, traffic.cars(), SensorEnvironment{});
        const std::optional<float> want = ego.lateral() < 2.0f ? std::optional<float>(2.0f) : std::nullopt;
        const std::optional<float> allowed = bsw.step(rearLeft.objects(), rearRight.objects(), want);
        r.blocked = r.blocked || (want && !allowed);
        ego.steer(allowed ? *allowed : 0.0f);
        ego.step(0.02f);
        traffic.step(0.02f, ego);
        r.cutOffs += traffic.takeCutOffs();
        r.crashed = collides(ego, traffic.cars()[0]);
    }
    return r;
}

// Without BSW: pulling out 20 m in front of a car closing at 12 m/s ends in a rear-end crash, because its
// driver needs a second to react. With a smaller speed difference it brakes hard in time and honks.
static void testCutInWithoutBsw() {
    CHECK(cutIn(20.0f, 12.0f, false).crashed);
    const CutInResult closeCall = cutIn(20.0f, 6.0f, false);
    CHECK(!closeCall.crashed && closeCall.cutOffs > 0);
}

// With BSW: the same dangerous lane changes are blocked until the fast car has passed, so no crash.
static void testBswPreventsRearEnd() {
    for (const float behind : {10.0f, 20.0f, 28.0f}) {
        const CutInResult r = cutIn(behind, 12.0f, true);
        CHECK(!r.crashed && r.blocked);
    }
}

// A motorcycle is shorter and narrower: 0.40 lanes off a car's centre, another car still touches it
// (car + car need more than 0.45 lanes) but a motorcycle doesn't (car + bike need only 0.325).
static void testMotorcycleHitbox() {
    const Vehicle car(1, 0.0f, 100.0f);
    Vehicle otherCar;
    otherCar.reset(2, 0.0f, 100.0f, 0, VehicleType::Car);
    Vehicle bike;
    bike.reset(2, 0.0f, 100.0f, 0, VehicleType::Motorcycle);
    CHECK(bike.length() < car.length() && bike.width() < car.width());

    // Standing still (speed 0), both drift from lane 2 towards lane 1 the same way until 0.40 lanes away.
    otherCar.enableFreeSteering();
    bike.enableFreeSteering();
    otherCar.steer(-1.0f);
    bike.steer(-1.0f);
    for (int i = 0; i < 200 && otherCar.lateral() - car.lateral() > 0.40f; ++i) {
        otherCar.step(0.02f);
        bike.step(0.02f);
    }
    CHECK(near(otherCar.lateral(), bike.lateral()));
    CHECK(collides(car, otherCar));
    CHECK(!collides(car, bike));
}

// --- Camera, radar ghosts, fusion ---

// Fog blinds the camera: it reports nothing and says so through available().
static void testCameraBlindInFog() {
    const Vehicle ego(1, 20.0f, 0.0f);
    const std::array<Vehicle, 1> cars{Vehicle(1, 15.0f, 20.0f)};
    Camera camera("CAM", camera_zones::kFront);
    camera.update(ego, cars, SensorEnvironment{});
    CHECK(camera.available() && camera.objects().size() == 1);

    SensorEnvironment fog;
    fog.fog = true;
    camera.update(ego, cars, fog);
    CHECK(!camera.available() && camera.objects().empty());
}

// Near a metal bridge the front radar reports a ghost "stopped car" in my lane. Without fusion,
// ACC/AEB would see a lead there; with fusion the camera doesn't confirm it, so it is dropped.
static void testFusionRemovesBridgeGhost() {
    const Vehicle ego(1, 25.0f, 0.0f);
    const std::array<Vehicle, 0> noCars{};
    SensorEnvironment bridge;
    bridge.metalBridgeAhead = 20.0f;
    Radar radar("FRONT", radar_zones::kFront, true);
    Camera camera("CAM", camera_zones::kFront);
    radar.update(ego, noCars, bridge);
    camera.update(ego, noCars, bridge);

    const std::optional<Lead> raw = selectLead(radar.objects());
    CHECK(raw.has_value() && near(raw->relativeSpeed, -25.0f));   // phantom stopped car ahead
    CHECK(!selectLead(fusion::fuse(radar, camera)).has_value());   // fused: gone
}

// Real cars survive fusion: confirmed ones with full confidence; beyond camera range or in fog the
// radar alone is trusted, with lower confidence (better than seeing nothing).
static void testFusionKeepsRealCars() {
    const Vehicle ego(1, 25.0f, 0.0f);
    const std::array<Vehicle, 2> cars{Vehicle(1, 20.0f, 20.0f), Vehicle(2, 20.0f, 50.0f)};   // 20 m and 50 m ahead
    Radar radar("FRONT", radar_zones::kFront, true);
    Camera camera("CAM", camera_zones::kFront);
    radar.update(ego, cars, SensorEnvironment{});
    camera.update(ego, cars, SensorEnvironment{});
    const ObjectList clear = fusion::fuse(radar, camera);
    CHECK(clear.size() == 2);
    for (const DetectedObject& o : clear) {
        CHECK(near(o.confidence, o.longitudinal < 35.0f ? 1.0f : fusion::kRadarOnlyConfidence));
    }

    SensorEnvironment fog;
    fog.fog = true;
    radar.update(ego, cars, fog);
    camera.update(ego, cars, fog);
    const ObjectList foggy = fusion::fuse(radar, camera);
    CHECK(foggy.size() == 2);
    CHECK(near(foggy[0].confidence, fusion::kRadarOnlyConfidence));
}

// LKA can't keep the lane without seeing the markings: degraded, no steering, resumes when visible.
static void testLkaDegradedWithoutLanes() {
    Lka lka;
    lka.toggle();
    CHECK(near(lka.step(0.02f, 1.3f, false, false), 0.0f));
    CHECK(lka.state() == LkaState::Degraded);
    CHECK(lka.step(0.02f, 1.3f, false, true) < 0.0f);   // lanes back: steers towards lane 1 again
    CHECK(lka.state() == LkaState::Active);
}

// --- Gameplay: shop, saving, near misses, power-ups, difficulty ---

// Buying: coins are spent, levels go up, a maxed item or missing coins changes nothing.
static void testShopBuying() {
    Progress p;
    p.coins = 100;
    CHECK(p.buy(ShopItem::LongerAssists));                  // costs 40
    CHECK(p.coins == 60 && p.level(ShopItem::LongerAssists) == 1);
    CHECK(near(p.powerUpSeconds(), 16.0f));
    CHECK(!p.buy(ShopItem::LongerAssists));                 // next level costs 90: can't afford
    CHECK(p.coins == 60 && p.level(ShopItem::LongerAssists) == 1);

    p.coins = 1000;
    CHECK(p.buy(ShopItem::StartWithAcc) && p.startsWithAcc());
    CHECK(p.nextPrice(ShopItem::StartWithAcc) == 0);        // maxed
    CHECK(!p.buy(ShopItem::StartWithAcc));
}

// Saving and loading round-trips; a tampered level is clamped to the item's maximum.
static void testProgressSaveLoad() {
    const std::string path = "test-progress.txt";
    Progress saved;
    saved.coins = 123;
    CHECK(saved.finishRun(2600) == 52);    // 2600 / 50 coins
    saved.buy(ShopItem::AebSaves);
    CHECK(saved.save(path));

    Progress loaded;
    CHECK(loaded.load(path));
    CHECK(loaded.best == 2600 && loaded.coins == saved.coins && loaded.level(ShopItem::AebSaves) == 1);
    CHECK(loaded.startingAebSaves() == 2);

    std::ofstream(path) << "level1=99\n";
    Progress tampered;
    tampered.load(path);
    CHECK(tampered.level(ShopItem::AebSaves) == kShopOffers[1].maxLevel);
    std::remove(path.c_str());

    Progress fresh;
    CHECK(!fresh.load("does-not-exist.txt") && fresh.coins == 0);
}

// Passing a car close and fast scores; passing it far away doesn't. Chained passes raise the combo.
static void testNearMiss() {
    NearMissTracker<2> tracker;
    Vehicle ego(1, 30.0f, 0.0f);
    std::array<Vehicle, 2> cars{};
    cars[0].reset(2, 20.0f, 6.0f, 0);   // next lane (edge gap 0.55 lanes): not close enough
    cars[1].reset(1, 20.0f, 20.0f, 0);  // will be moved next to the ego below
    int points = 0;
    for (int i = 0; i < 60; ++i) {      // 1.2 s at +10 m/s: the ego passes car 0
        ego.step(0.02f);
        cars[0].step(0.02f);
        points += tracker.update(0.02f, ego, cars);
    }
    CHECK(points == 0);

    // Now the player drifts towards the next lane before overtaking a car there: about 0.2 lanes
    // between the two bodies when passing. That's a near miss.
    NearMissTracker<1> close;
    Vehicle me(1, 30.0f, 0.0f);
    me.enableFreeSteering();
    std::array<Vehicle, 1> one{};
    one[0].reset(2, 20.0f, 12.0f, 0);   // car in the next lane, 12 m ahead, 10 m/s slower
    int scored = 0;
    for (int i = 0; i < 150; ++i) {
        me.steer(me.lateral() < 1.25f ? 1.0f : 0.0f);
        me.step(0.02f);
        one[0].step(0.02f);
        scored += close.update(0.02f, me, one);
    }
    const float edgeGap = std::abs(one[0].lateral() - me.lateral()) - (one[0].width() + me.width()) / 2.0f;
    CHECK(edgeGap > 0.0f && edgeGap < NearMissTracker<1>::kMaxEdgeGap);   // the setup really is a close pass
    CHECK(scored == NearMissTracker<1>::kBasePoints && close.combo() == 1);
}

// Driving through a pick-up collects it.
static void testPowerUpCollected() {
    PowerUpField field;
    const PowerUpField::Pickup first = field.pickups()[0];
    const Vehicle ego(static_cast<int>(first.lateral), 20.0f, first.position);
    const std::optional<PowerUp> got = field.step(ego);
    CHECK(got.has_value() && *got == first.type);
    CHECK(!field.step(ego).has_value());   // it's gone (recycled further ahead)
}

// Difficulty: only the active vehicles are on the road; the rest wait far behind, out of reach.
static void testTrafficActiveCount() {
    Vehicle ego(1, 20.0f, 0.0f);
    Traffic traffic;
    traffic.setActiveCount(3);
    for (int i = 0; i < 10; ++i) {
        ego.step(0.02f);
        traffic.step(0.02f, ego);
    }
    int nearby = 0;
    for (const Vehicle& v : traffic.cars()) {
        nearby += std::abs(v.position() - ego.position()) < 200.0f ? 1 : 0;
    }
    CHECK(nearby == 3);
}

// Garage: buying a car spends coins and drives it; owned cars switch for free; the choice is saved,
// and a save file can't select a car you don't own.
static void testGarage() {
    Progress p;
    CHECK(p.car().name == kCars[0].name && p.ownsCar(0));
    CHECK(!p.chooseCar(3));                       // sports car: 250 coins, have 0
    p.coins = 400;
    CHECK(p.chooseCar(4));                        // pickup: 300
    CHECK(p.coins == 100 && p.ownsCar(4) && p.selectedCar == 4);
    CHECK(p.shieldsPerRun() == 1);                // the pickup survives one crash
    CHECK(p.chooseCar(0) && p.coins == 100);      // back to the sedan: owned, free

    const std::string path = "test-garage.txt";
    p.chooseCar(4);
    p.save(path);
    Progress loaded;
    loaded.load(path);
    CHECK(loaded.ownsCar(4) && loaded.selectedCar == 4);

    std::ofstream(path) << "cars=1\ncar=3\n";     // claims to drive a car it doesn't own
    Progress cheat;
    cheat.load(path);
    CHECK(cheat.selectedCar == 0);
    std::remove(path.c_str());
}

// New shop items: coin bonus multiplies the coins of a run; shield adds to the car's shields.
static void testNewShopItems() {
    Progress p;
    p.coins = 10000;
    CHECK(p.buy(ShopItem::CoinBonus) && p.buy(ShopItem::CoinBonus));
    const int before = p.coins;
    CHECK(p.finishRun(1000) == 30);               // 1000 / 50 = 20 coins x 1.5
    CHECK(p.coins == before + 30);
    CHECK(p.buy(ShopItem::Shield) && p.shieldsPerRun() == 1);
    CHECK(p.buy(ShopItem::Magnet) && p.hasMagnet());
}

// Magnet: a pick-up two lanes over slides into the player's lane as they approach.
static void testMagnetPullsPickups() {
    PowerUpField field;
    const PowerUpField::Pickup target = field.pickups()[0];
    const int myLane = target.lateral < 1.5f ? 3 : 0;   // a far lane
    Vehicle ego(myLane, 20.0f, target.position - 35.0f);
    bool collected = false;
    for (int i = 0; i < 150 && !collected; ++i) {        // 3 s, driving towards it
        ego.step(0.02f);
        collected = field.step(ego, 0.02f, true).has_value();
    }
    CHECK(collected);

    PowerUpField noMagnet;
    Vehicle ego2(myLane, 20.0f, target.position - 35.0f);
    bool collected2 = false;
    for (int i = 0; i < 150 && !collected2; ++i) {
        ego2.step(0.02f);
        collected2 = noMagnet.step(ego2, 0.02f, false).has_value();
    }
    CHECK(!collected2);
}

// --- Roadworks (static obstacles) ---

// Touching a closed stretch of lane is a crash; driving past it in the next lane is not.
static void testObstacleHit() {
    const StaticObstacle closure{1.0f, 0.5f, 100.0f, 200.0f};
    CHECK(hits(closure, Vehicle(1, 20.0f, 99.0f)));     // front bumper past the barrier
    CHECK(!hits(closure, Vehicle(1, 20.0f, 95.0f)));    // still before it
    CHECK(!hits(closure, Vehicle(2, 20.0f, 150.0f)));   // next lane, alongside
}

// The game-over screen names what you hit: which vehicle, from which side, or which obstacle.
static void testCrashReport() {
    const Vehicle ego(1, 25.0f, 100.0f);
    const auto what = [&](const Vehicle& other, Span<StaticObstacle> obstacles = {}) {
        const std::array<Vehicle, 1> cars{other};
        const std::optional<CrashReport> r = describeCrash(ego, cars, obstacles);
        return r ? std::string(r->what) : std::string("NOTHING");
    };
    CHECK(what(Vehicle(1, 20.0f, 103.5f)) == "INTO THE CAR AHEAD");    // my front bumper in its back
    CHECK(what(Vehicle(1, 30.0f, 96.5f)) == "REAR-ENDED BY A CAR");    // it ran into my back
    CHECK(what(Vehicle(1, 25.0f, 100.5f)) == "SIDESWIPED A CAR");      // side by side
    CHECK(what(Vehicle(1, 20.0f, 120.0f)) == "NOTHING");               // 20 m ahead: no crash

    const std::array<StaticObstacle, 1> debris{{{1.0f, 0.2f, 101.0f, 103.0f, true, ObstacleKind::Debris}}};
    CHECK(what(Vehicle(3, 20.0f, 100.0f), debris) == "INTO DEBRIS");
    const std::array<StaticObstacle, 1> wreck{{{1.0f, 0.45f, 101.0f, 110.0f, false, ObstacleKind::Wreck}}};
    CHECK(what(Vehicle(3, 20.0f, 100.0f), wreck) == "INTO A WRECK");
}

// Radar and camera both report the barrier, so ACC/AEB see a stopped "object" in the lane ahead.
static void testSensorsSeeBarrier() {
    const Vehicle ego(1, 25.0f, 0.0f);
    const std::array<Vehicle, 0> noCars{};
    const std::array<StaticObstacle, 1> closure{{{1.0f, 0.5f, 30.0f, 120.0f}}};
    SensorEnvironment env;
    env.obstacles = closure;
    Radar radar("FRONT", radar_zones::kFront, true);
    Camera camera("CAM", camera_zones::kFront);
    radar.update(ego, noCars, env);
    camera.update(ego, noCars, env);
    const std::optional<Lead> lead = selectLead(fusion::fuse(radar, camera));   // confirmed by both
    CHECK(lead.has_value());
    if (lead) {
        CHECK(near(lead->gap, 30.0f - cfg::kCarLength / 2.0f, 0.1f) && near(lead->relativeSpeed, -25.0f));
    }
}

// Traffic through roadworks, debris and accidents on several random layouts: NPCs merge out in time;
// nobody hits an obstacle or another car. The "player" here is only a camera the NPCs ignore
// (a test robot that drove through obstacles would hide them from the cars following it).
static void testTrafficMergesAtRoadworks() {
    for (std::uint32_t seed : {1u, 2u, 3u}) {
        RoadHazards hazards(seed);
        Vehicle ego(0, 25.0f, 0.0f);
        Traffic traffic(seed + 100);
        int crashes = 0;
        for (int step = 0; step < 50 * 240; ++step) {
            hazards.step(ego);
            const RoadHazards::Obstacles obstacles = hazards.obstacles();
            ego.step(0.02f);
            traffic.step(0.02f, ego, false, Span<StaticObstacle>(obstacles.begin(), obstacles.size()));
            const auto& cars = traffic.cars();
            for (std::size_t a = 0; a < cars.size(); ++a) {
                for (const StaticObstacle& o : obstacles) {
                    crashes += hits(o, cars[a]) ? 1 : 0;
                }
                for (std::size_t b = a + 1; b < cars.size(); ++b) {
                    crashes += collides(cars[a], cars[b]) ? 1 : 0;
                }
            }
        }
        CHECK(crashes == 0);
    }
}

// --- Debris, rain, tunnels ---

// Debris 30 m ahead: the radar barely reflects from it and doesn't report it yet; the camera does.
// Fusion adds camera-only objects, so with fusion the lead (and AEB) sees the debris much earlier.
static void testDebrisNeedsFusion() {
    const Vehicle ego(1, 25.0f, 0.0f);
    const std::array<Vehicle, 0> noCars{};
    const std::array<StaticObstacle, 1> debris{{{1.0f, 0.18f, 30.0f, 31.2f, true}}};
    SensorEnvironment env;
    env.obstacles = debris;
    Radar radar("FRONT", radar_zones::kFront, true);
    Camera camera("CAM", camera_zones::kFront);
    radar.update(ego, noCars, env);
    camera.update(ego, noCars, env);
    CHECK(!selectLead(radar.objects()).has_value());                 // radar alone: nothing
    CHECK(selectLead(fusion::fuse(radar, camera)).has_value());     // fused: debris ahead

    const Vehicle close(1, 25.0f, 20.0f);                            // 10 m before it: radar finally sees it
    radar.update(close, noCars, env);
    CHECK(selectLead(radar.objects()).has_value());
}

// Rain shortens the camera's view (a car 30 m ahead disappears); glare at a tunnel portal blinds it.
static void testCameraRainAndGlare() {
    const Vehicle ego(1, 20.0f, 0.0f);
    const std::array<Vehicle, 1> cars{Vehicle(1, 15.0f, 30.0f)};
    Camera camera("CAM", camera_zones::kFront);
    SensorEnvironment rain;
    rain.rain = true;
    camera.update(ego, cars, rain);
    CHECK(camera.available() && camera.objects().empty() && camera.fieldOfView().maxLongitudinal < 30.0f);

    SensorEnvironment glare;
    glare.glare = true;
    camera.update(ego, cars, glare);
    CHECK(!camera.available());
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
    testFixedVector();
    testRadarZones();
    testLeadFromRadarMatchesGroundTruth();
    testBsw();
    testCutInWithoutBsw();
    testBswPreventsRearEnd();
    testMotorcycleHitbox();
    testCameraBlindInFog();
    testFusionRemovesBridgeGhost();
    testFusionKeepsRealCars();
    testLkaDegradedWithoutLanes();
    testShopBuying();
    testProgressSaveLoad();
    testNearMiss();
    testPowerUpCollected();
    testTrafficActiveCount();
    testGarage();
    testNewShopItems();
    testMagnetPullsPickups();
    testObstacleHit();
    testCrashReport();
    testSensorsSeeBarrier();
    testTrafficMergesAtRoadworks();
    testDebrisNeedsFusion();
    testCameraRainAndGlare();

    std::printf(g_failures == 0 ? "All tests passed\n" : "%d test(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;

}