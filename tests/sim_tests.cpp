// SKYFALL unit / integration tests — no external framework, runs headless via ctest.
#include "aircraft/AircraftProfile.hpp"
#include "app/CameraMode.hpp"
#include "app/Settings.hpp"
#include "app/FrameGovernor.hpp"
#include "combat/Combat.hpp"
#include "mission/Campaign.hpp"
#include "mission/Mission.hpp"
#include "mission/Voice.hpp"
#include "sim/Locations.hpp"
#include "sim/Atmosphere.hpp"
#include "sim/FlightModel.hpp"
#include "telemetry/Replay.hpp"
#include "telemetry/FrameStats.hpp"
#include "telemetry/Replay.hpp"
#include "CombatBot.hpp"
#include "CampaignPlaythrough.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

using namespace sky;

namespace {
int g_failed = 0, g_checks = 0;
std::string g_current;
#define CHECK(cond)                                                                             \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(cond)) {                                                                          \
            ++g_failed;                                                                         \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                                       \
    } while (0)
#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

const Terrain& terrain() { static Terrain t; return t; }
Terrain& carrierTerrain();
const WorldLayout& layout() { static WorldLayout l = WorldLayout::build(terrain()); return l; }

/// Scripted pilot used by several tests: cold start -> takeoff -> climb -> gear up.
struct ScriptedTakeoff {
    FlightModel& fm;
    bool gearCmd = false;
    ControlInputs step(double t) {
        ControlInputs in;
        const FlightState& s = fm.state();
        if (t > 0.1 && s.engine == EngineState::Off) fm.requestEngineStart();
        if (s.engine == EngineState::Running && s.parkingBrake) fm.toggleParkingBrake();
        if (s.engine == EngineState::Running) in.throttleRate = s.throttle < 0.88 ? 1.0 : 0.0;
        if (s.airspeed > fm.profile().rotateSpeedMs) in.pitch = toEuler(s.q).pitch < 10 * kDeg ? 0.6 : 0.15;
        if (!s.onGround) in.pitch = toEuler(s.q).pitch < 12 * kDeg ? 0.25 : 0.0;
        if (!s.onGround && s.agl > 40 && s.gearDown && !gearCmd) { fm.toggleGear(); gearCmd = true; }
        return in;
    }
};
}  // namespace

TEST(isa_atmosphere_matches_standard) {
    CHECK_NEAR(isa(0).density, 1.225, 0.001);
    CHECK_NEAR(isa(0).temperatureK, 288.15, 0.01);
    CHECK_NEAR(isa(11000).density, 0.3639, 0.002);
    CHECK_NEAR(isa(0).speedOfSound, 340.3, 0.5);
    CHECK(isa(5000).density < isa(1000).density);
}

TEST(quaternion_euler_roundtrip) {
    for (double h : {-2.0, -0.3, 0.0, 1.0, 2.8})
        for (double p : {-0.8, 0.0, 0.4})
            for (double b : {-1.2, 0.0, 0.7}) {
                Quat q = Quat::fromEuler(h, p, b);
                Euler e = toEuler(q);
                CHECK_NEAR(wrapPi(e.heading - h), 0.0, 1e-6);
                CHECK_NEAR(e.pitch, p, 1e-6);
                CHECK_NEAR(e.bank, b, 1e-6);
            }
    Quat q = Quat::fromEuler(0, 0, 0);
    CHECK_NEAR(q.forward().x, 1.0, 1e-9);
    CHECK_NEAR(q.right().z, 1.0, 1e-9);
    // positive heading turns the nose towards +Z (east -> south on the compass)
    CHECK(Quat::fromEuler(0.3, 0, 0).forward().z > 0);
    CHECK_NEAR(compassHeadingDeg(Quat::fromEuler(0, 0, 0)), 90.0, 1e-6);
}

TEST(lift_curve_shape) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    const AircraftProfile& p = fm.profile();
    CHECK_NEAR(fm.liftCoefficient(0.0), p.cl0, 1e-9);
    CHECK(fm.liftCoefficient(5 * kDeg) > fm.liftCoefficient(0));
    CHECK(fm.liftCoefficient(p.alphaStallRad) <= p.clMax + 1e-9);
    CHECK(fm.liftCoefficient(p.alphaStallRad + 20 * kDeg) < fm.liftCoefficient(p.alphaStallRad));  // post-stall loss
    CHECK(fm.dragCoefficient(0, 0) >= p.cd0);
}

namespace {
/// Level flight at `alt` from 150 m/s: seconds to reach 95 % of the arcade top speed in full
/// afterburner, then seconds at idle + airbrake to the controllable speed, and the lowest speed.
struct ArcadeRun { double t95 = -1, tBrake = -1, vMin = 1e9, vTop = 0, vmax = 0; bool reversed = false; };
ArcadeRun arcadeRun(AircraftId id, double alt, HandlingMode mode, double fuelFrac = 1.0) {
    static Terrain t;
    FlightModel fm(profile(id), t);
    fm.setHandling(mode);
    fm.resetAirborne({-4000, alt, 0}, 0.0, 150.0, 0.5);
    fm.mutableState().fuelKg = profile(id).fuelMassKg * fuelFrac;
    ArcadeRun r;
    r.vmax = fm.arcadeTopSpeed(alt);
    ControlInputs in;
    in.throttleRate = 1.0;
    double tt = 0;
    for (int i = 0; i < 120 * 10; ++i) {
        const FlightState& s = fm.state();
        in.pitch = clampd((alt - s.pos.y) * 0.01 - s.vel.y * 0.05, -0.5, 0.5);
        fm.step(in, FlightModel::kDt);
        tt += FlightModel::kDt;
        if (r.t95 < 0 && s.airspeed >= 0.95 * r.vmax) r.t95 = tt;
    }
    r.vTop = fm.state().airspeed;
    double vctrl = std::max(profile(id).approachSpeedMs * 1.6, 0.36 * r.vmax);
    in.throttleRate = -1.0;
    in.airbrake = true;
    tt = 0;
    for (int i = 0; i < 120 * 12; ++i) {
        const FlightState& s = fm.state();
        in.pitch = clampd((alt - s.pos.y) * 0.01 - s.vel.y * 0.05, -0.5, 0.5);
        fm.step(in, FlightModel::kDt);
        tt += FlightModel::kDt;
        r.vMin = std::min(r.vMin, s.airspeed);
        if (dot(s.vel, s.q.forward()) < 0) r.reversed = true;
        if (r.tBrake < 0 && s.airspeed <= vctrl * 1.05) r.tBrake = tt;
    }
    return r;
}
}  // namespace

TEST(arcade_profile_accelerates_in_3_to_4_s_and_brakes_in_4_to_5_s) {
    for (int ac = 0; ac < kFighterCount; ++ac) {
        for (double alt : {1500.0, 9000.0}) {
            ArcadeRun r = arcadeRun(static_cast<AircraftId>(ac), alt, HandlingMode::Arcade);
            CHECK(r.t95 >= 2.9 && r.t95 <= 4.2);
            CHECK(r.vTop >= 0.97 * r.vmax && r.vTop <= 1.03 * r.vmax);
            CHECK(r.tBrake >= 3.8 && r.tBrake <= 5.0);
            CHECK(r.vMin > 95.0);          // still flying: no stop in mid-air
            CHECK(!r.reversed);
            // the weapons / fuel load does not change the arcade performance
            ArcadeRun light = arcadeRun(static_cast<AircraftId>(ac), alt, HandlingMode::Arcade, 0.2);
            CHECK(std::fabs(light.t95 - r.t95) < 0.25);
        }
    }
    // the F-22 is Mach 2 class up high, each aircraft keeps its own top speed
    Terrain t;
    FlightModel f22(profile(AircraftId::F22), t), f35(profile(AircraftId::F35), t);
    CHECK(f22.arcadeTopSpeed(11000) / isa(11000).speedOfSound >= 1.95);
    CHECK(f35.arcadeTopSpeed(11000) < f22.arcadeTopSpeed(11000));
    // Assisted and Simulation keep the physics: nowhere near top speed after 4 s
    ArcadeRun a = arcadeRun(AircraftId::F22, 1500, HandlingMode::Assisted);
    CHECK(a.t95 < 0 || a.t95 > 6.0);
    ArcadeRun sim = arcadeRun(AircraftId::F22, 1500, HandlingMode::Simulation);
    CHECK(sim.t95 < 0 || sim.t95 > 6.0);
}

TEST(engine_off_produces_no_thrust_and_start_requires_command) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway();
    ControlInputs in; in.throttleRate = 1.0;
    for (int i = 0; i < 120 * 10; ++i) fm.step(in);
    CHECK(fm.state().engine == EngineState::Off);
    CHECK(fm.state().thrustN == 0.0);
    CHECK_NEAR(fm.state().airspeed, 0.0, 1e-6);
    fm.requestEngineStart();
    CHECK(fm.state().engine == EngineState::Starting);
    for (int i = 0; i < 120 * 8; ++i) fm.step(ControlInputs{});
    CHECK(fm.state().engine == EngineState::Running);
    CHECK(fm.state().thrustN > 0.0);
}

TEST(parking_brake_holds_at_military_power_and_release_accelerates) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway(true);
    ControlInputs in; in.throttleRate = 1.0;
    for (int i = 0; i < 120 * 2; ++i) fm.step(in);        // throttle lever to ~0.88 region
    in.throttleRate = 0;
    fm.mutableState().throttle = 0.88;                    // full dry power, no afterburner
    for (int i = 0; i < 120 * 5; ++i) fm.step(in);
    CHECK(fm.state().airspeed < 0.2);
    fm.setParkingBrake(false);
    for (int i = 0; i < 120 * 5; ++i) fm.step(in);
    CHECK(fm.state().airspeed > 10.0);
    // wheel brakes decelerate
    double v = fm.state().airspeed;
    in.brake = true;
    fm.mutableState().throttle = 0.0;
    for (int i = 0; i < 120 * 4; ++i) fm.step(in);
    CHECK(fm.state().airspeed < v - 5.0);
}

TEST(gear_cannot_retract_on_ground) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway();
    CHECK(!fm.toggleGear());
    CHECK(fm.state().gearDown);
    fm.step(ControlInputs{});
    CHECK_NEAR(fm.state().gearPos, 1.0, 1e-12);
}

TEST(scripted_takeoff_climb_and_gear_up) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway();
    ScriptedTakeoff pilot{fm};
    double t = 0, liftoffX = 0;
    bool lifted = false;
    Vec3 start = fm.state().pos;
    for (int i = 0; i < 120 * 70; ++i) {
        fm.step(pilot.step(t));
        t += FlightModel::kDt;
        if (!lifted && !fm.state().onGround) { lifted = true; liftoffX = length(fm.state().pos - start); }
        CHECK(!fm.state().crashed);
        if (fm.state().crashed) { std::printf("    crash: %s\n", crashReasonText(fm.state().crashReason)); break; }
    }
    CHECK(lifted);
    CHECK(liftoffX < terrain().runway().length - 200.0);   // airborne before the end of the runway
    CHECK(fm.state().agl > 200.0);
    CHECK(!fm.state().gearDown);
    CHECK_NEAR(fm.state().gearPos, 0.0, 1e-9);
    std::printf("    liftoff after %.0f m, agl at 70 s = %.0f m, speed %.0f m/s\n", liftoffX, fm.state().agl, fm.state().airspeed);
}

TEST(simulation_is_deterministic) {
    auto run = [] {
        FlightModel fm(profile(AircraftId::F22), terrain());
        fm.resetOnRunway();
        ScriptedTakeoff pilot{fm};
        double t = 0;
        for (int i = 0; i < 120 * 40; ++i) { ControlInputs in = pilot.step(t); in.roll = std::sin(t * 0.3) * 0.2; fm.step(in); t += FlightModel::kDt; }
        return fm.state();
    };
    FlightState a = run(), b = run();
    CHECK(a.pos == b.pos);
    CHECK(a.vel == b.vel);
    CHECK(a.q.w == b.q.w && a.q.x == b.q.x && a.q.y == b.q.y && a.q.z == b.q.z);
}

TEST(fixed_stepper_is_bounded) {
    FixedStepper fs;
    CHECK(fs.advance(1.0 / 60.0) == 2);
    CHECK(fs.advance(0.0) == 0);
    int n = fs.advance(2.0);  // long stall (e.g. window drag)
    CHECK(n == fs.maxStepsPerFrame);
    CHECK(fs.accumulator == 0.0);
    CHECK(fs.droppedSteps > 0);
    FixedStepper g;
    int total = 0;
    for (int i = 0; i < 600; ++i) total += g.advance(1.0 / 60.0);
    CHECK(std::abs(total - 1200) <= 1);
}

TEST(level_flight_is_held_with_neutral_stick) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetAirborne({0, 2500, 0}, 0.0, 200.0, 0.72);
    double alt0 = fm.state().pos.y;
    for (int i = 0; i < 120 * 20; ++i) fm.step(ControlInputs{});
    CHECK(!fm.state().crashed);
    CHECK_NEAR(fm.state().pos.y, alt0, 80.0);
    CHECK_NEAR(fm.state().gLoad, 1.0, 0.15);
}

TEST(banked_pull_turns_and_respects_g_limit) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetAirborne({0, 3000, 0}, 0.0, 230.0, 0.95);
    double maxG = 0;
    double prevH = toEuler(fm.state().q).heading, dh = 0, maxAlpha = 0;
    for (int i = 0; i < 120 * 6; ++i) {
        ControlInputs in;
        double bank = toEuler(fm.state().q).bank;
        in.roll = clampd((75 * kDeg - bank) * 2.0, -1, 1);
        in.pitch = i > 120 ? 1.0 : 0.0;
        fm.step(in);
        maxG = std::max(maxG, fm.state().gLoad);
        maxAlpha = std::max(maxAlpha, fm.state().alpha);
        double h = toEuler(fm.state().q).heading;
        dh += wrapPi(h - prevH);
        prevH = h;
    }
    CHECK(maxAlpha <= fm.profile().alphaLimitRad + 2.0 * kDeg);  // AoA limiter (Assisted)
    CHECK(!fm.state().crashed);
    CHECK(dh > 60 * kDeg);                        // turned right substantially
    CHECK(maxG <= fm.profile().gLimit + 0.6);     // FBW limiter
    std::printf("    heading change %.0f deg in 6 s, max G %.2f\n", dh / kDeg, maxG);
}

TEST(arcade_turns_sharply_and_keeps_energy_sane) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.setHandling(HandlingMode::Arcade);
    fm.resetAirborne({0, 3000, 0}, 0.0, 200.0, 0.85);
    double prevH = toEuler(fm.state().q).heading, dh = 0;
    for (int i = 0; i < 120 * 4; ++i) {
        ControlInputs in;
        double bank = toEuler(fm.state().q).bank;
        in.roll = clampd((80 * kDeg - bank) * 3.0, -1, 1);
        in.pitch = i > 30 ? 1.0 : 0.0;
        fm.step(in);
        double h = toEuler(fm.state().q).heading;
        dh += wrapPi(h - prevH);
        prevH = h;
    }
    CHECK(!fm.state().crashed);
    CHECK(dh > 150 * kDeg);                        // ~180 degrees in 4 s: dogfight-grade turn
    CHECK(fm.state().airspeed > 90.0);            // bleeds energy but keeps flying
    std::printf("    arcade: %.0f deg in 4 s, speed %.0f m/s\n", dh / kDeg, fm.state().airspeed);
}

TEST(arcade_pull_up_recovers_from_low_altitude_dive) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.setHandling(HandlingMode::Arcade);
    // 25 degree dive at 400 m over the sea, full back stick + afterburner
    fm.resetAirborne({12000, 400, 0}, 0.0, 180.0, 1.0);
    fm.mutableState().q = Quat::fromEuler(0, -25 * kDeg, 0);
    fm.mutableState().vel = fm.state().q.forward() * 180.0;
    double minAlt = 1e9;
    for (int i = 0; i < 120 * 10; ++i) {
        ControlInputs in;
        in.pitch = toEuler(fm.state().q).pitch < 30 * kDeg ? 1.0 : 0.0;
        in.throttleRate = 1.0;
        fm.step(in);
        minAlt = std::min(minAlt, fm.state().pos.y);
    }
    CHECK(!fm.state().crashed);
    CHECK(fm.state().pos.y > 600.0);   // climbs away
    std::printf("    lowest point %.0f m, after 10 s %.0f m at %.0f m/s\n", minAlt, fm.state().pos.y, fm.state().airspeed);
}

TEST(every_aircraft_is_flyable_in_arcade) {
    for (const auto& prof : allProfiles()) {
        CHECK(prof.playableNow);
        const bool fighter = prof.role == AircraftRole::Fighter;
        FlightModel fm(prof, terrain());
        fm.setHandling(HandlingMode::Arcade);
        fm.resetAirborne({12000, 450, 0}, 0.0, 170.0, 1.0);
        fm.mutableState().q = Quat::fromEuler(0, -20 * kDeg, 0);
        fm.mutableState().vel = fm.state().q.forward() * 170.0;
        double prevH = 0, dh = 0;
        for (int i = 0; i < 120 * 12; ++i) {
            ControlInputs in;
            Euler e = toEuler(fm.state().q);
            if (i < 120 * 6) in.pitch = e.pitch < 25 * kDeg ? 1.0 : 0.0;             // recover and climb
            else { in.roll = clampd((75 * kDeg - e.bank) * 3.0, -1, 1); in.pitch = 1.0; }  // then turn hard
            in.throttleRate = 1.0;
            fm.step(in);
            double h = toEuler(fm.state().q).heading;
            if (i == 120 * 6) prevH = h;
            if (i > 120 * 6) { dh += wrapPi(h - prevH); prevH = h; }
        }
        CHECK(!fm.state().crashed);
        CHECK(fm.state().pos.y > 700.0);
        CHECK(dh > (fighter ? 180 : 35) * kDeg);   // a transport turns, but gently
        if (!fighter) CHECK(dh < 120 * kDeg);
        std::printf("    %-22s alt %.0f m, turned %.0f deg in 6 s, %.0f m/s\n", prof.name, fm.state().pos.y, dh / kDeg, fm.state().airspeed);
    }
}

TEST(diving_into_terrain_crashes) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetAirborne({0, 600, 0}, 0.0, 200.0, 0.8);
    fm.mutableState().q = Quat::fromEuler(0, -40 * kDeg, 0);
    fm.mutableState().vel = fm.state().q.forward() * 200.0;
    for (int i = 0; i < 120 * 20 && !fm.state().crashed; ++i) fm.step(ControlInputs{});
    CHECK(fm.state().crashed);
    CHECK(fm.state().crashReason == CrashReason::Terrain || fm.state().crashReason == CrashReason::Water);
}

TEST(mission01_does_not_advance_on_time_alone) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway();
    Mission m = makeMission01();
    MissionContext ctx{&fm.state(), &fm.profile(), &terrain(), 0};
    for (int i = 0; i < 120 * 120; ++i) { fm.step(ControlInputs{}); m.update(ctx, FlightModel::kDt); }
    CHECK(m.stepIndex() == 0);
    CHECK(m.status() == MissionStatus::Running);
}

TEST(mission01_completes_with_real_flying) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway();
    Mission m = makeMission01();
    MissionContext ctx{&fm.state(), &fm.profile(), &terrain(), 0};
    ScriptedTakeoff pilot{fm};
    double t = 0;
    for (int i = 0; i < 120 * 120 && m.status() == MissionStatus::Running; ++i) {
        ControlInputs in = pilot.step(t);
        if (fm.state().agl > 320) in.pitch = 0.0;
        // hold heading 090 (runway heading) and wings level
        Euler e = toEuler(fm.state().q);
        in.roll = clampd(-e.bank * 2.0, -1, 1);
        fm.step(in);
        m.update(ctx, FlightModel::kDt);
        t += FlightModel::kDt;
    }
    CHECK(m.status() == MissionStatus::Complete);
    CHECK(m.completionTimes().size() == m.stepCount());
    std::printf("    mission 01 completed at t = %.1f s\n", m.elapsed());
}

TEST(mission03_scripted_approach_lands_and_stops) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    const RunwayInfo& rw = terrain().runway();
    Vec3 th = rw.threshold();
    double d0 = 7000.0;
    fm.resetAirborne({th.x - d0, rw.center.y + glidePathHeight(d0), th.z}, 0.0, 95.0, 0.45);
    fm.mutableState().vel = Quat::fromEuler(0.0, -3.0 * kDeg, 0.0).forward() * 95.0;
    Mission m = makeMission03();
    MissionContext ctx{&fm.state(), &fm.profile(), &terrain(), 0};
    fm.toggleGear();  // gear was up after resetAirborne -> lower it
    for (int i = 0; i < 120 * 240 && m.status() == MissionStatus::Running; ++i) {
        const FlightState& s = fm.state();
        ControlInputs in;
        double d = th.x - s.pos.x;
        double h = s.pos.y - rw.center.y;
        double target = glidePathHeight(d);
        Euler e = toEuler(s.q);
        in.roll = clampd(-e.bank * 2.0 - (s.pos.z - th.z) * 0.002, -1, 1);
        if (!s.onGround) {
            // glide-path tracking: flight-path command from the height error, flare below 12 m
            double vsCmd = -std::tan(3.0 * kDeg) * s.airspeed + (target - h) * 0.15;
            if (h < 14.0) vsCmd = -1.2;
            in.pitch = clampd((vsCmd - s.vel.y) * 0.08, -0.6, 0.6);
            double spd = h < 14.0 ? 70.0 : 88.0;
            double lever = clampd(0.45 + (spd - s.airspeed) * 0.03, 0.0, 0.85);
            in.throttleRate = clampd((lever - s.throttle) * 8.0, -1, 1);
        } else {
            in.throttleRate = -1;
            in.brake = true;
        }
        fm.step(in);
        m.update(ctx, FlightModel::kDt);
    }
    if (fm.state().crashed) std::printf("    crash: %s\n", crashReasonText(fm.state().crashReason));
    CHECK(!fm.state().crashed);
    CHECK(m.status() == MissionStatus::Complete);
    std::printf("    landed and stopped at x=%.0f (runway %.0f..%.0f), t=%.0f s\n", fm.state().pos.x,
                rw.center.x - rw.length / 2, rw.center.x + rw.length / 2, m.elapsed());
}

TEST(mission02_does_not_complete_without_flying) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetAirborne({0, 1250, 0}, 0.0, 200.0, 0.62);
    Mission m = makeMission02();
    MissionContext ctx{&fm.state(), &fm.profile(), &terrain(), 0};
    for (int i = 0; i < 120 * 60; ++i) { fm.step(ControlInputs{}); m.update(ctx, FlightModel::kDt); }
    CHECK(m.stepIndex() <= 1);   // straight & level can pass on its own, banking cannot
}

TEST(mission_fails_on_crash) {
    FlightModel fm(profile(AircraftId::F22), terrain());
    fm.resetOnRunway();
    Mission m = makeMission01();
    fm.forceCrash(CrashReason::HardLanding);
    MissionContext ctx{&fm.state(), &fm.profile(), &terrain(), 0};
    m.update(ctx, 0.1);
    CHECK(m.status() == MissionStatus::Failed);
    CHECK(!m.failure().empty());
}

TEST(strike_aircraft_and_attack_helicopters_destroy_ground_units) {
    // operations AI: a strike jet runs in with air-to-surface missiles, an attack helicopter
    // hunts the convoy with its cannon; both kill friendly ground units
    for (int kind = 0; kind < 2; ++kind) {
        Battle b(terrain());
        Jet& p = b.createPlayer(AircraftId::F22);
        p.fm->resetAirborne({-14000, 3000, -14000}, 0.0, 220.0, 0.8);   // far away, not involved
        b.startScripted(1);
        Vec3 base{6000, 0, 6000};
        for (int k = 0; k < 4; ++k) {
            GroundTarget g;
            g.kind = k % 2 ? GroundKind::Truck : GroundKind::Tank;
            g.team = Team::Blue;
            g.armed = false;
            g.pos = base + Vec3{k * 30.0, 0, k * 12.0};
            b.addGround(g);
        }
        if (kind == 0) {
            Jet& j = b.debugAddJet(Team::Red, AircraftId::Su30, base + Vec3{-12000, 1500, 0}, 0.0, 230.0);
            j.strike = true;
            j.agms = 4;
        } else {
            GroundTarget h;
            h.kind = GroundKind::Helicopter;
            h.team = Team::Red;
            h.attack = true;
            h.armed = true;
            h.huntGround = true;
            h.flyAgl = 70;
            h.flySpeed = 45;
            h.pathCenter = base + Vec3{-5000, 0, 1000};
            h.pathRadius = 600;
            h.pos = h.pathCenter;
            b.addGround(h);
        }
        int before = b.groundAliveOfTeam(Team::Blue);
        for (int i = 0; i < 120 * 240 && b.groundAliveOfTeam(Team::Blue) > 1; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
        int after = b.groundAliveOfTeam(Team::Blue);
        std::printf("    %s: friendly ground units %d -> %d\n", kind == 0 ? "strike jet" : "attack helicopter", before, after);
        CHECK(before == 4);
        CHECK(after <= 2);
    }
}

TEST(convoys_follow_their_road_and_rescue_helicopters_hold_on_the_pickup) {
    Battle b(terrain());
    b.createPlayer(AircraftId::F22).fm->resetAirborne({0, 3000, 0}, 0.0, 220.0, 0.8);
    b.startScripted(1);
    GroundTarget t;
    t.kind = GroundKind::Truck;
    t.team = Team::Blue;
    t.armed = false;
    t.route = {{1000, 0, 1000}, {3000, 0, 1000}, {3000, 0, 3000}};
    t.routeSpeed = 12.0;
    t.pos = t.route.front();
    int ti = b.addGround(t);
    GroundTarget h;
    h.kind = GroundKind::Helicopter;
    h.team = Team::Blue;
    h.armed = false;
    h.flyAgl = 120;
    h.flySpeed = 50;
    h.route = {{-2000, 0, -2000}, {-2000, 0, 0}};
    h.routeSpeed = 50;
    h.hold = 30.0;
    h.pos = h.route.front();
    int hi = b.addGround(h);
    for (int i = 0; i < 120 * 20; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(length(b.ground()[hi].pos - Vec3{-2000, b.ground()[hi].pos.y, -2000}) < 2.0);   // still holding
    CHECK(b.ground()[hi].pos.y - terrain().surfaceHeight(-2000, -2000) < 12.0);              // hovering low
    for (int i = 0; i < 120 * 340; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(b.ground()[ti].arrived());
    CHECK(std::hypot(b.ground()[ti].pos.x - 3000, b.ground()[ti].pos.z - 3000) < 2.0);
    CHECK(b.ground()[hi].arrived());
}

TEST(escort_target_is_hunted_first_and_armour_makes_it_survive_longer) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({-9000, 3000, -9000}, 0.0, 220.0, 0.8);
    b.startScripted(1);
    Jet& c17 = b.debugAddJet(Team::Blue, AircraftId::C17, {4000, 3000, 0}, 0.0, 170.0);
    c17.kinematic = true;
    c17.damageScale = 0.34;
    c17.missiles = 0;
    c17.rounds = 0;
    Jet& red = b.debugAddJet(Team::Red, AircraftId::MiG29, {-1000, 3100, 0}, 0.0, 230.0);
    red.aiPrefer = c17.id;
    for (int i = 0; i < 120 * 4; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(red.aiTarget == c17.id);   // the transport, although the player is a little closer by weight
    // a single missile does not bring the C-17 down any more
    double h0 = c17.health;
    Missile m;
    m.owner = red.id;
    m.team = Team::Red;
    m.targetJet = c17.id;
    m.pos = c17.fm->state().pos - Vec3{60, 0, 0};
    m.vel = Vec3{900, 0, 0};
    b.debugAddMissile(m);
    for (int i = 0; i < 30; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(c17.alive);
    CHECK(c17.health < h0);
    CHECK(c17.health > 50.0);
}

TEST(damaged_bandits_break_off_and_wingmen_hold_formation) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({0, 3500, 0}, 0.0, 230.0, 0.8);
    b.startScripted(1);
    Jet& wing = b.debugAddJet(Team::Blue, AircraftId::F16, {-1500, 3500, 800}, 0.0, 230.0);
    wing.followId = p.id;
    double far = 0;
    for (int i = 0; i < 120 * 60; ++i) {
        ControlInputs in;
        in.throttleRate = p.fm->state().throttle < 0.8 ? 1.0 : 0.0;
        b.update(FlightModel::kDt, in, PlayerCommands{});
        if (i > 120 * 20) far = std::max(far, length(wing.fm->state().pos - p.fm->state().pos));
    }
    std::printf("    wingman stays within %.0f m of the lead\n", far);
    CHECK(far < 1500.0);
    Jet& red = b.debugAddJet(Team::Red, AircraftId::Su35, p.fm->state().pos + Vec3{3000, 0, 0}, kPi, 230.0);
    red.health = 40.0;
    bool broke = false;
    for (int i = 0; i < 120 * 3; ++i) { b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{}); broke = broke || red.ai == AiState::Extend; }
    CHECK(broke);
}

TEST(battle_roster_version_changes_when_jets_are_created_or_destroyed) {
    // the renderer keeps Jet* across a frame (smooth motion); a menu that rebuilds the battle
    // mid-frame (the HANGAR button) must be detectable, or the saved pointers dangle
    Battle b(terrain());
    b.createPlayer(AircraftId::F22);
    auto v0 = b.rosterVersion();
    int id = b.debugAddJet(Team::Red, AircraftId::Su35, {2500, 3100, 400}, 1.2, 200.0).id;
    auto v1 = b.rosterVersion();
    CHECK(v1 != v0);
    b.removeJet(id);
    auto v2 = b.rosterVersion();
    CHECK(v2 != v1);
    b.debugAddJet(Team::Red, AircraftId::Su35, {2500, 3100, 400}, 1.2, 200.0);
    auto v3 = b.rosterVersion();
    b.clearCombatants();
    CHECK(b.rosterVersion() != v3);
    auto v4 = b.rosterVersion();
    b.createPlayer(AircraftId::F16);
    CHECK(b.rosterVersion() != v4);
    // a second Battle never reuses a version number
    Battle c(terrain());
    c.createPlayer(AircraftId::F22);
    CHECK(c.rosterVersion() != b.rosterVersion());
}

TEST(proportional_navigation_missile_hits_non_manoeuvring_target) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({0, 3000, 0}, 0.0, 220.0, 0.8);
    b.setAiEnabled(false);
    Jet& t = b.debugAddJet(Team::Red, AircraftId::Su35, {2500, 3100, 400}, 1.2, 200.0);
    int tid = t.id;
    p.lockTarget = tid;
    p.lockProgress = 1.0;
    b.fireMissile(p);
    CHECK(b.missiles().size() == 1);
    bool killed = false;
    for (int i = 0; i < 120 * 15 && !killed; ++i) {
        b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
        const Jet* tj = b.jetById(tid);
        killed = !tj || !tj->alive;
    }
    CHECK(killed);
}

TEST(flares_can_decoy_missiles) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({0, 3000, 0}, 0.0, 220.0, 0.8);
    b.setAiEnabled(false);
    Missile m;
    m.team = Team::Red;
    m.owner = -1;
    m.targetJet = p.id;
    m.pos = {-2500, 3000, 300};
    m.vel = {600, 0, 0};
    b.debugAddMissile(m);
    PlayerCommands cmd;
    double hp = p.health;
    for (int i = 0; i < 120 * 10; ++i) {
        cmd.flares = (i == 120);
        b.update(FlightModel::kDt, ControlInputs{}, cmd);
    }
    CHECK(b.player().alive);
    CHECK(b.player().health == hp);
    CHECK(b.player().flareCharges == 2 || b.player().flareCharges == 3);
}

TEST(ai_does_not_fly_into_terrain_when_patrolling) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({-5000, 4000, -5000}, 0.0, 200.0, 0.8);
    std::vector<int> ids;
    for (int k = 0; k < 4; ++k) ids.push_back(b.debugAddJet(Team::Blue, AircraftId::F22, {k * 2000.0 - 3000, 900 + k * 300.0, 1000.0 * k - 6000}, k * 1.5, 200).id);
    for (int i = 0; i < 120 * 90; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    int alive = 0;
    for (int id : ids) { const Jet* j = b.jetById(id); if (j && j->alive) ++alive; }
    CHECK(alive == 4);
}

TEST(dogfight_arena_spawns_waves_and_ai_engages) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({-4000, 2200, 2000}, 0.0, 220.0, 0.8);
    BattleConfig cfg;
    cfg.playerInvulnerable = true;
    b.start(cfg);
    CHECK(b.wave() == 1);
    CHECK(b.enemiesAlive() == cfg.enemyJets);
    CHECK(!b.ground().empty());
    int launches = 0, shots = 0;
    for (int i = 0; i < 120 * 60; ++i) {
        b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
        for (auto& e : b.drainEvents()) if (e.type == EventType::MissileLaunch) ++launches;
        shots += static_cast<int>(b.bullets().size() > 0);
    }
    CHECK(b.player().alive);
    CHECK(launches + shots > 0);   // the AI actually fights
    std::printf("    60 s arena: %d missile launches, score %d, enemies alive %d\n", launches, b.score(), b.enemiesAlive());
}

TEST(terrain_runway_is_flat_and_sea_is_east) {
    const Terrain& t = terrain();
    const RunwayInfo& r = t.runway();
    CHECK(t.onRunway(r.center.x, r.center.z));
    CHECK(t.onRunway(r.threshold().x, r.threshold().z));
    CHECK(std::fabs(t.height(r.center.x, r.center.z) - (r.center.y - 0.6)) < 0.5);
    CHECK(t.isWater(14000, 0));
    CHECK(!t.isWater(r.center.x, r.center.z));
    CHECK(t.height(1234.5, -678.9) == t.height(1234.5, -678.9));
}

TEST(camera_cycles_through_three_modes) {
    CameraMode m = CameraMode::Cockpit;
    m = nextCamera(m); CHECK(m == CameraMode::Chase);
    m = nextCamera(m); CHECK(m == CameraMode::External);
    m = nextCamera(m); CHECK(m == CameraMode::Cockpit);
}

TEST(settings_round_trip_preserves_unknown_keys) {
    std::string path = (std::filesystem::temp_directory_path() / "skyfall_test_settings.ini").string();
    Settings s;
    s.master = 0.3; s.musicEnabled = true; s.invertPitch = true; s.fovDeg = 82;
    s.extra["future_option"] = "42";
    CHECK(s.save(path));
    Settings r;
    CHECK(r.load(path));
    CHECK_NEAR(r.master, 0.3, 1e-9);
    CHECK(r.musicEnabled);
    CHECK(r.invertPitch);
    CHECK_NEAR(r.fovDeg, 82, 1e-9);
    CHECK(r.extra["future_option"] == "42");
    Settings d;
    CHECK(!d.musicEnabled);  // music is OFF by default
    std::filesystem::remove(path);
}

TEST(skin_choices_survive_a_settings_round_trip) {
    // skins are stored one character per aircraft: 0-9, a-z, A-Z (62 schemes)
    std::string path = (std::filesystem::temp_directory_path() / "skyfall_test_skins.ini").string();
    Settings s;
    const int picks[] = {0, 9, 10, 35, 36, 39, 61};
    for (int i = 0; i < kAircraftCount; ++i) s.setSkin(i, picks[i % 7]);
    CHECK(s.save(path));
    Settings r;
    CHECK(r.load(path));
    for (int i = 0; i < kAircraftCount; ++i) CHECK(r.skinFor(i) == picks[i % 7]);
    s.setSkin(0, 62);                    // out of range: ignored
    CHECK(s.skinFor(0) == 0);
    std::filesystem::remove(path);
}

TEST(frame_governor_lowers_resolution_then_detail_and_restores_in_reverse) {
    FrameGovernor g;
    const double budget = 8.33;   // Performance 120
    double t = 0;
    // a heavy scene at 14 ms: resolution steps down to the floor first, detail stays full
    for (int i = 0; i < 5; ++i) g.step(t += 1, 14.0, budget);
    CHECK_NEAR(g.scale(), 0.5, 1e-9);
    CHECK(g.detailDrop() == 0);
    // still too slow at 50 %: detail is shed, at most one level every 3 s, never below level 3
    for (int i = 0; i < 30; ++i) g.step(t += 1, 14.0, budget);
    CHECK(g.detailDrop() == FrameGovernor::kMaxDetailDrop);
    CHECK_NEAR(g.scale(), 0.5, 1e-9);
    // slight headroom (7.5 ms) is not enough to bring detail back (it costs more than a step)
    for (int i = 0; i < 30; ++i) g.step(t += 1, 7.5, budget);
    CHECK(g.detailDrop() == FrameGovernor::kMaxDetailDrop);
    // clear headroom: detail first, then resolution
    int lastDetail = g.detailDrop();
    bool scaleRoseBeforeDetailFull = false;
    for (int i = 0; i < 120; ++i) {
        g.step(t += 1, 5.0, budget);
        if (g.detailDrop() > 0 && g.scale() > 0.5 + 1e-9) scaleRoseBeforeDetailFull = true;
        CHECK(g.detailDrop() <= lastDetail);
        lastDetail = g.detailDrop();
    }
    CHECK(!scaleRoseBeforeDetailFull);
    CHECK(g.detailDrop() == 0);
    CHECK_NEAR(g.scale(), 1.0, 1e-9);
    // a scene inside the budget never changes anything
    FrameGovernor h;
    for (int i = 0; i < 60; ++i) h.step(i, 6.0, budget);
    CHECK(h.detailDrop() == 0 && h.scale() == 1.0);
    // no budget (uncapped, no preset budget): untouched
    h.step(100, 50.0, 0.0);
    CHECK(h.scale() == 1.0);
}

TEST(detail_drop_shortens_view_distances_and_drops_shadows) {
    for (int q = 0; q < kQualityCount; ++q) {
        QualityProfile base = qualityProfile(static_cast<GraphicsQuality>(q));
        QualityProfile same = qualityProfile(static_cast<GraphicsQuality>(q), 0);
        CHECK(same.treeDist == base.treeDist && same.shadows == base.shadows);
        float prev = base.sceneryDist;
        for (int d = 1; d <= 3; ++d) {
            QualityProfile p = qualityProfile(static_cast<GraphicsQuality>(q), d);
            CHECK(p.sceneryDist < prev && p.treeDist < base.treeDist && p.terrainHiDist < base.terrainHiDist);
            CHECK(p.sceneScale == base.sceneScale && p.maxWidth == base.maxWidth);   // resolution is the governor's other lever
            if (d >= 2) CHECK(!p.shadows);
            prev = p.sceneryDist;
        }
    }
}

TEST(frame_stats_percentiles) {
    FrameStats fs(1000);
    for (int i = 1; i <= 100; ++i) fs.push(i);
    Percentiles p = fs.compute();
    CHECK_NEAR(p.p50, 50.5, 1e-9);
    CHECK(p.p95 > 94 && p.p95 < 96.5);
    CHECK(p.p99 > 98 && p.p99 <= 100);
    CHECK_NEAR(p.mean, 50.5, 1e-9);
}

TEST(kill_replay_clip_is_a_private_copy_seen_from_the_victim) {
    // 6 s of a fight at 30 Hz: the player (id 0) closes on a bandit (id 7) from behind-left and
    // fires a missile at t = 3 s that hits at t = 6 s
    FlightRecorder rec;
    for (int i = 0; i <= 180; ++i) {
        double t = 100.0 + i / 30.0;
        RecFrame f;
        f.t = t;
        f.player.id = 0;
        f.player.pos = {-3000.0 + 250.0 * (t - 100.0), 2000, -800.0 + 120.0 * (t - 100.0)};
        f.player.q = Quat::fromEuler(0.4, 0, 0);
        RecJet v;
        v.id = 7; v.team = 1; v.type = AircraftId::Su35;
        v.pos = {0.0 + 200.0 * (t - 100.0), 2000, 0};
        v.q = Quat::fromEuler(0.0, 0, 0);
        f.others.push_back(v);
        if (t >= 103.0) {
            RecMissile m;
            m.id = 42; m.targetJet = 7; m.owner = 0; m.motor = true;
            double u = (t - 103.0) / 3.0;
            m.pos = lerp(f.player.pos, v.pos, u);
            m.dir = normalize(v.pos - f.player.pos);
            f.missiles.push_back(m);
        }
        CHECK(rec.offer(f) || i == 0 || true);
    }
    RecJet at = rec.frames().back().others[0];
    KillClip clip = makeKillClip(rec, 7, at, {200, 0, 0}, 106.0, "BANDIT 2", "ATA");
    CHECK(clip.valid());
    CHECK(clip.start() >= 106.0 - 4.5 - 1e-6 && clip.frames.back().t >= 106.0);
    // the crash replay's recorder can be cleared: the clip keeps its own copy
    rec.clear();
    CHECK(clip.frames.size() > 100);
    // poses interpolate; after the kill the wreck falls and rolls
    RecJet v0 = clip.victimAt(104.0), v1 = clip.victimAt(106.0), v2 = clip.victimAt(107.0);
    CHECK(std::fabs(v0.pos.x - 800.0) < 30.0);
    CHECK(v2.pos.y < v1.pos.y - 3.0 && v2.pos.x > v1.pos.x + 150.0);
    // the pilot looks at the missile guiding on it once it is close, from inside its own cockpit
    KillCamState look;
    KillView kv;
    for (double t = clip.start(); t < 105.5; t += 1.0 / 60.0) kv = killCamera(clip, t, look, 1.0 / 60.0);
    RecJet me = clip.victimAt(105.5);
    CHECK(length(kv.eye - me.pos) < 10.0);
    const RecFrame* f = clip.frameAt(105.5);
    CHECK(f && !f->missiles.empty());
    Vec3 toMissile = normalize(f->missiles[0].pos - kv.eye);
    CHECK(dot(normalize(kv.target - kv.eye), toMissile) > 0.9);
    CHECK(kv.threatVisible && !kv.afterKill);
    kv = killCamera(clip, 106.1, look, 1.0 / 60.0);
    CHECK(kv.afterKill && kv.impact > 0.5);
    // a kill with the victim barely recorded is not replayed
    FlightRecorder shortRec;
    RecFrame g; g.t = 1.0; g.player.id = 0; shortRec.offer(g);
    CHECK(!makeKillClip(shortRec, 7, at, {}, 1.0, "X", "GUN").valid());
}

TEST(flight_recorder_keeps_a_rolling_window_and_interpolates) {
    FlightRecorder rec;
    for (int i = 0; i < 30 * 15; ++i) {
        RecFrame f;
        f.t = i / 30.0;
        f.player.pos = {f.t * 100.0, 500.0, 0.0};
        f.player.q = Quat::fromEuler(0, 0, f.t * 0.1);
        CHECK(rec.offer(f));
        RecFrame dup = f;  // same timestamp is rate-limited away
        CHECK(!rec.offer(dup));
    }
    CHECK(rec.endTime() - rec.startTime() <= FlightRecorder::kWindow + 1e-9);
    CHECK(rec.endTime() > 14.9);
    RecJet mid = rec.playerAt(10.0 + 1.0 / 60.0);
    CHECK_NEAR(mid.pos.x, 1001.666, 0.01);
}

TEST(crash_analysis_explains_the_mistake) {
    FlightRecorder rec;
    for (int i = 0; i < 30 * 6; ++i) {
        RecFrame f;
        f.t = i / 30.0;
        f.speed = 260; f.vs = -110; f.pitchDeg = -25; f.bankDeg = 120; f.agl = 900 - i * 5.0f;
        f.gpws = f.t > 3.0;
        rec.offer(f);
    }
    LossReport r = analyzeLoss(rec, rec.endTime(), CrashReason::Terrain, LossCause::Crash, "");
    CHECK(!r.title.empty());
    bool mentionsRoll = false, mentionsGpws = false;
    for (auto& a : r.advice) {
        mentionsRoll |= a.find("rolled") != std::string::npos;
        mentionsGpws |= a.find("PULL UP") != std::string::npos;
    }
    CHECK(mentionsRoll);
    CHECK(mentionsGpws);
    // missile kill without flares
    FlightRecorder rec2;
    for (int i = 0; i < 90; ++i) { RecFrame f; f.t = i / 30.0; f.speed = 200; f.missileWarning = i > 30; rec2.offer(f); }
    LossReport m = analyzeLoss(rec2, rec2.endTime(), CrashReason::Destroyed, LossCause::ShotDownMissile, "COBRA 2");
    CHECK(m.title.find("MISSILE") != std::string::npos);
    CHECK(!m.advice.empty() && m.advice[0].find("flares") != std::string::npos);
    // pull-out height grows with speed and dive angle
    CHECK(pullOutAltitude(250, -40, 7) > pullOutAltitude(150, -40, 7));
    CHECK(pullOutAltitude(250, -60, 7) > pullOutAltitude(250, -20, 7));
    CHECK_NEAR(pullOutAltitude(250, 10, 7), 0.0, 1e-12);
}

TEST(all_aircraft_profiles_are_defined) {
    int playable = 0, fighters = 0;
    for (const auto& p : allProfiles()) {
        CHECK(p.name != nullptr && std::strlen(p.name) > 0);
        CHECK(p.wingAreaM2 > 10);
        if (p.role == AircraftRole::Fighter) {
            ++fighters;
            CHECK(p.afterburner && p.thrustAbN > p.thrustMilN);
            CHECK(p.missiles >= 4 && p.cannonRounds > 0);
            CHECK(isFighter(p.id));
        } else {
            CHECK(!p.afterburner && p.missiles == 0 && p.gLimit <= 3.0);
            CHECK(!isFighter(p.id));
        }
        playable += p.playableNow;
    }
    CHECK(allProfiles().size() == 11);
    CHECK(fighters == kFighterCount && fighters == 10);
    CHECK(playable == 11);
    CHECK(profile(AircraftId::FA18).carrierCapable);
    CHECK(combatAircraft(static_cast<int>(AircraftId::C17)) == 0);
    CHECK(combatAircraft(static_cast<int>(AircraftId::Typhoon)) == static_cast<int>(AircraftId::Typhoon));
}

TEST(c17_flies_like_a_heavy_transport) {
    // subsonic in every mode, no afterburner, takes off and climbs from a runway, and its
    // stall speed sits below its approach speed (a flyable approach)
    const AircraftProfile& p = profile(AircraftId::C17);
    for (HandlingMode mode : {HandlingMode::Arcade, HandlingMode::Assisted, HandlingMode::Simulation}) {
        FlightModel fm(p, terrain());
        fm.setHandling(mode);
        fm.resetAirborne({12000, 3000, 0}, 0.0, 180.0, 1.0);
        double vmax = 0;
        for (int i = 0; i < 120 * 40; ++i) {
            ControlInputs in;
            in.throttleRate = 1.0;
            Euler e = toEuler(fm.state().q);
            in.pitch = clampd((0.0 - e.pitch) * 2.0 - fm.state().vel.y * 0.02, -0.4, 0.4);
            fm.step(in);
            vmax = std::max(vmax, fm.state().airspeed);
        }
        CHECK(fm.state().abLevel == 0.0);
        CHECK(fm.state().mach < 0.9);
        CHECK(vmax > 180.0);
        CHECK(!fm.state().crashed);
        std::printf("    C-17 %s: top %.0f m/s, Mach %.2f\n", mode == HandlingMode::Arcade ? "arcade" : mode == HandlingMode::Assisted ? "assisted" : "simulation",
                    vmax, fm.state().mach);
    }
    double W = (p.emptyMassKg + p.fuelMassKg) * kG;
    double vs = std::sqrt(2.0 * W / (1.225 * p.wingAreaM2 * p.clMax));
    CHECK(vs < p.approachSpeedMs * 0.9);
    CHECK(vs < p.rotateSpeedMs);
    // takeoff roll
    FlightModel fm(p, terrain());
    fm.setHandling(HandlingMode::Assisted);
    fm.resetOnRunway(true);
    fm.mutableState().parkingBrake = false;
    double roll = 0;
    Vec3 start = fm.state().pos;
    for (int i = 0; i < 120 * 90 && fm.state().onGround; ++i) {
        ControlInputs in;
        in.throttleRate = 1.0;
        if (fm.state().airspeed > p.rotateSpeedMs) in.pitch = 0.6;
        fm.step(in);
        roll = length(fm.state().pos - start);
    }
    CHECK(!fm.state().onGround);
    CHECK(!fm.state().crashed);
    CHECK(roll < 2400.0);
    std::printf("    C-17 stall %.0f m/s, approach %.0f m/s, takeoff roll %.0f m\n", vs, p.approachSpeedMs, roll);
}

// ------------------------------------------------------------------------------------------
// Campaign 04-40
// ------------------------------------------------------------------------------------------
TEST(campaign_has_sixty_one_playable_missions) {
    const auto& cat = campaignCatalogue();
    CHECK(cat.size() == static_cast<size_t>(kCampaignMissions));
    CHECK(kCampaignMissions == 61);
    for (size_t i = 0; i < cat.size(); ++i) {
        CHECK(cat[i].number == static_cast<int>(i) + 1);
        CHECK(cat[i].playable);
        CHECK(!cat[i].title.empty());
    }
    for (int n = 4; n <= kCampaignMissions; ++n) {
        const CampaignMissionDef* d = campaignMission(n);
        CHECK(d != nullptr);
        if (d) CHECK(!d->objectives.empty());
    }
    CHECK(campaignMission(3) == nullptr);
    CHECK(campaignMission(kCampaignMissions + 1) == nullptr);
}

TEST(campaign_layouts_are_valid) {
    const Terrain& t = terrain();
    int bad = 0;
    auto fail = [&](const CampaignMissionDef& m, const std::string& what) {
        if (bad++ < 25) std::printf("    mission %02d: %s\n", m.number, what.c_str());
    };
    for (const CampaignMissionDef& m : campaignMissions()) {
        if (std::fabs(m.startX) > 14500 || std::fabs(m.startZ) > 14500) fail(m, "start outside the map");
        if (!m.carrier && m.startAlt < t.surfaceHeight(m.startX, m.startZ) + 300) fail(m, "start too close to the ground");
        for (const TargetDef& g : m.targets) {
            if (g.anchor == TargetDef::Anchor::Railway) {
                if (layout().railway.length() < 5000) fail(m, g.label + ": no railway");
                continue;
            }
            if (g.anchor == TargetDef::Anchor::Bridge) {
                if (layout().largestBridge(true) < 0 && layout().largestBridge(false) < 0) fail(m, g.label + ": no bridge");
                continue;
            }
            if (g.anchor == TargetDef::Anchor::Industrial) {
                if (g.index >= static_cast<int>(layout().factories.size())) fail(m, g.label + ": no industrial slot");
                continue;
            }
            auto checkPoint = [&](double x, double z) {
                if (std::fabs(x) > 15000 || std::fabs(z) > 15000) { fail(m, g.label + " outside the map"); return; }
                double h = t.height(x, z);
                if (groundIsNaval(g.kind)) {
                    if (h > -6.0) fail(m, g.label + " (naval) not in water, h=" + std::to_string(h));
                } else {
                    if (h < 3.0) fail(m, g.label + " (land) in water, h=" + std::to_string(h));
                    if (t.normal(x, z, 8.0).y < 0.88) fail(m, g.label + " on a steep slope");
                    if (t.onRunway(x, z, 60.0)) fail(m, g.label + " on the runway");
                }
            };
            if (g.pathRadius > 0)
                for (int k = 0; k < 36; ++k) checkPoint(g.x + std::cos(k * kPi / 18) * g.pathRadius, g.z + std::sin(k * kPi / 18) * g.pathRadius);
            else checkPoint(g.x, g.z);
        }
        // static targets must not overlap each other
        for (size_t a = 0; a < m.targets.size(); ++a)
            for (size_t b = a + 1; b < m.targets.size(); ++b) {
                const TargetDef &A = m.targets[a], &B = m.targets[b];
                if (A.pathRadius > 0 || B.pathRadius > 0) continue;
                if (A.anchor != TargetDef::Anchor::None || B.anchor != TargetDef::Anchor::None) continue;
                if (std::hypot(A.x - B.x, A.z - B.z) < 25.0) fail(m, A.label + " overlaps " + B.label);
            }
        for (const ObjectiveDef& o : m.objectives) {
            if (o.kind == ObjectiveKind::DestroyGroup) {
                int n = 0;
                for (const TargetDef& g : m.targets) if (g.group == o.group) ++n;
                if (n == 0) fail(m, "objective '" + o.title + "' has no targets");
            }
            if (o.kind == ObjectiveKind::Waypoint && (std::fabs(o.x) > 15000 || std::fabs(o.z) > 15000)) fail(m, "waypoint outside the map");
            for (int w : o.wavesOnStart) if (w < 0 || w >= static_cast<int>(m.waves.size())) fail(m, "bad wave index");
        }
        // every wave is released by some objective
        for (size_t w = 0; w < m.waves.size(); ++w) {
            bool used = false;
            for (const ObjectiveDef& o : m.objectives) for (int i : o.wavesOnStart) if (i == static_cast<int>(w)) used = true;
            if (!used) fail(m, "wave " + std::to_string(w) + " is never spawned");
        }
        if (m.protectGroup > 0) {
            int n = 0;
            for (const TargetDef& g : m.targets) if (g.group == m.protectGroup && g.team == Team::Blue) ++n;
            if (n == 0) fail(m, "protect group has no friendly assets");
        }
    }
    CHECK(bad == 0);
}

namespace {
/// Plays a campaign mission with an objective-aware "cheat" pilot: teleports to waypoints, destroys
/// targets and bandits through the test hooks, but flies the manoeuvre objectives for real.
struct DesertPlaythrough : sky::CampaignPlaythrough {
    explicit DesertPlaythrough(const CampaignMissionDef& d)
        : sky::CampaignPlaythrough(d, d.carrier ? static_cast<const Terrain&>(carrierTerrain()) : terrain(), &layout()) {}
};
}  // namespace

TEST(campaign_missions_do_not_complete_without_action) {
    for (const CampaignMissionDef& d : campaignMissions()) {
        DesertPlaythrough pt(d);
        ControlInputs in;
        for (int i = 0; i < 240; ++i) pt.step(in);   // 2 s of straight flight
        if (pt.mission.stepIndex() != 0 || pt.mission.status() != MissionStatus::Running)
            std::printf("    mission %02d advanced without action\n", d.number);
        CHECK(pt.mission.stepIndex() == 0);
        CHECK(pt.mission.status() == MissionStatus::Running);
    }
}

TEST(campaign_missions_complete_when_objectives_are_met) {
    for (const CampaignMissionDef& d : campaignMissions()) {
        DesertPlaythrough pt(d);
        bool ok = pt.run(240.0);
        if (!ok)
            std::printf("    mission %02d stuck at objective %zu/%zu (%s) status %d %s\n", d.number, pt.mission.stepIndex() + 1,
                        pt.mission.stepCount(), pt.mission.current() ? pt.mission.current()->title.c_str() : "-",
                        static_cast<int>(pt.mission.status()), pt.mission.failure().c_str());
        CHECK(ok);
        // every scripted wave was released
        int waves = 0;
        for (const ObjectiveDef& o : d.objectives) waves += static_cast<int>(o.wavesOnStart.size());
        CHECK(pt.battle.wave() == waves);
    }
}

TEST(protecting_a_friendly_rig_fails_mission_36_when_it_is_destroyed) {
    const CampaignMissionDef* d = campaignMission(36);
    CHECK(d != nullptr);
    if (!d) return;
    DesertPlaythrough pt(*d);
    for (int gi = 0; gi < static_cast<int>(pt.battle.ground().size()); ++gi)
        if (pt.battle.ground()[gi].team == Team::Blue) { pt.battle.debugDestroyGround(gi); break; }
    pt.step(ControlInputs{});
    CHECK(pt.mission.status() == MissionStatus::Failed);
}

TEST(ground_targets_sit_on_terrain_or_sea_and_boats_stay_afloat) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({0, 2000, 0}, 0, 220, 0.8);
    GroundTarget rig; rig.kind = GroundKind::OilRig; rig.pos = {11500, 0, -6000};
    int r = b.addGround(rig);
    GroundTarget boat; boat.kind = GroundKind::Ship; boat.pos = {11800, 0, -4600}; boat.pathCenter = boat.pos; boat.pathRadius = 500;
    int s = b.addGround(boat);
    CHECK_NEAR(b.ground()[r].pos.y, 0.0, 1e-9);
    CHECK_NEAR(b.ground()[r].health, groundMaxHealth(GroundKind::OilRig), 1e-9);
    BattleConfig cfg; cfg.enemyJets = 0; cfg.allyJets = 0; cfg.groundTargets = false; cfg.endlessWaves = false;
    // run without start(): ground update requires an active battle, so start then re-add
    b.start(cfg);
    r = b.addGround(rig); s = b.addGround(boat);
    Vec3 before = b.ground()[s].pos;
    for (int i = 0; i < 600; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(length(b.ground()[s].pos - before) > 10.0);      // the boat moved
    CHECK_NEAR(b.ground()[s].pos.y, 0.0, 1e-9);            // on the sea surface
    CHECK(b.enemiesAlive() == 0);                           // no endless waves in scripted battles
}

TEST(two_ats_hits_destroy_an_oil_rig) {
    Battle b(terrain());
    Jet& p = b.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({8000, 900, -6000}, 0.0, 230, 0.8);
    BattleConfig cfg; cfg.enemyJets = 0; cfg.allyJets = 0; cfg.groundTargets = false; cfg.endlessWaves = false;
    b.start(cfg);
    GroundTarget rig; rig.kind = GroundKind::OilRig; rig.pos = {11500, 0, -6000}; rig.label = "OIL RIG A"; rig.group = 1;
    int r = b.addGround(rig);
    int hits = 0;
    for (int shot = 0; shot < 3 && b.ground()[r].alive; ++shot) {
        Missile m;
        m.team = Team::Blue; m.owner = 0; m.airToSurface = true; m.targetGround = r;
        m.pos = {10200, 300, -6000};
        m.vel = normalize(Vec3{11500, 24, -6000} - m.pos) * 300.0;
        b.debugAddMissile(m);
        for (int i = 0; i < 1200 && !b.missiles().empty(); ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
        ++hits;
    }
    CHECK(!b.ground()[r].alive);
    CHECK(hits == 2);
}

TEST(campaign_battle_step_fits_the_frame_budget) {
    // Worst case: the biggest missions with every wave airborne at once and AI enabled.
    for (int n : {38, 40, 24, 31}) {
        const CampaignMissionDef* d = campaignMission(n);
        CHECK(d != nullptr);
        if (!d) continue;
        Battle b(terrain());
        Jet& p = b.createPlayer(AircraftId::F22);
        p.fm->resetAirborne({d->startX, d->startAlt, d->startZ}, campaignStartHeading(*d), d->startSpeed, 0.8);
        CampaignDirector dir;
        dir.begin(*d, b);
        for (const WaveDef& w : d->waves) b.spawnEnemies(w.count, w.skill, {w.x, 0, w.z}, w.radius, w.altitude);
        const int steps = 120 * 20;
        auto t0 = std::chrono::steady_clock::now();
        double worst = 0;
        for (int i = 0; i < steps; ++i) {
            auto a = std::chrono::steady_clock::now();
            b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
            worst = std::max(worst, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count());
        }
        double mean = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / steps;
        std::printf("    mission %02d: %zu jets, %zu ground targets, sim step mean %.3f ms, worst %.3f ms\n", n, b.jets().size(),
                    b.ground().size(), mean, worst);
        // 120 Hz physics leaves 8.3 ms per step on a 1:1 frame; a step must cost a small fraction of it
        CHECK(mean < 1.0);
    }
}

namespace {
/// Player jet pointed at a ground target from `dist` metres away and `agl` metres above it.
struct GroundAttackRange {
    Battle b{terrain()};
    int target = -1;
    bool valid = true;
    GroundAttackRange(GroundKind kind, double dist, double agl, double bearing, bool moving) {
        Jet& p = b.createPlayer(AircraftId::F22);
        Vec3 tpos{-7600, 0, 2500};   // open ground beside the airbase
        tpos.y = terrain().height(tpos.x, tpos.z);
        Vec3 from{tpos.x - std::cos(bearing) * dist, tpos.y + agl, tpos.z - std::sin(bearing) * dist};
        // the approach must be flyable: above the ground under the jet with a clear line of sight
        valid = terrain().surfaceHeight(from.x, from.z) < from.y - 80.0 && b.lineOfSight(from, tpos + Vec3{0, 2, 0});
        double pitch = -std::atan2(agl, dist);
        p.fm->resetAirborne(from, bearing, 220, 0.8);
        p.fm->mutableState().q = Quat::fromEuler(bearing, pitch, 0.0);
        p.fm->mutableState().vel = Quat::fromEuler(bearing, pitch, 0.0).forward() * 220.0;
        BattleConfig cfg; cfg.enemyJets = 0; cfg.allyJets = 0; cfg.groundTargets = false; cfg.endlessWaves = false;
        b.start(cfg);
        b.setAiEnabled(false);
        GroundTarget g; g.kind = kind; g.pos = tpos; g.label = "T"; g.group = 1; g.armed = false;
        if (moving) { g.pathCenter = tpos + Vec3{0, 0, 300}; g.pathRadius = 300; g.pathAngle = -kPi / 2; g.pos = tpos; }
        target = b.addGround(g);
    }
    bool atsShot() {
        Jet& p = b.player();
        p.weaponAG = true;
        // hold the nose on the target until the lock completes, as a player would
        for (int i = 0; i < 240 && p.lockProgress < 1.0; ++i) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
        PlayerCommands fire; fire.fireMissile = true;
        b.update(FlightModel::kDt, ControlInputs{}, fire);
        for (int i = 0; i < 120 * 20 && b.ground()[target].alive; ++i) {
            b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
            if (b.missiles().empty()) break;
        }
        return !b.ground()[target].alive;
    }
};
}  // namespace

TEST(ats_missiles_reliably_hit_static_and_moving_vehicles) {
    int shots = 0, hits = 0;
    for (bool moving : {false, true})
        for (double dist : {2500.0, 4000.0, 6000.0})
            for (double agl : {300.0, 900.0, 1600.0})
                for (double bearing : {0.0, 1.9, 3.3, 4.6}) {
                    GroundAttackRange r(GroundKind::Truck, dist, agl, bearing, moving);
                    if (!r.valid) continue;
                    ++shots;
                    if (r.atsShot()) ++hits;
                    else std::printf("    miss: moving=%d dist=%.0f agl=%.0f bearing=%.1f\n", moving, dist, agl, bearing);
                }
    std::printf("    ATS vs trucks: %d / %d hits\n", hits, shots);
    CHECK(shots >= 20);
    CHECK(hits >= shots * 0.9);
}

TEST(ats_fired_without_full_lock_still_guides_to_the_target_near_the_nose) {
    GroundAttackRange r(GroundKind::Tank, 3500, 700, 0.0, false);
    CHECK(r.valid);
    Jet& p = r.b.player();
    p.weaponAG = true;
    PlayerCommands fire; fire.fireMissile = true;
    r.b.update(FlightModel::kDt, ControlInputs{}, fire);   // no lock yet
    CHECK(!r.b.missiles().empty());
    if (!r.b.missiles().empty()) CHECK(r.b.missiles()[0].targetGround == r.target);
    for (int i = 0; i < 120 * 20 && r.b.ground()[r.target].alive; ++i) r.b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(!r.b.ground()[r.target].alive);
}

TEST(cannon_strafing_run_destroys_a_truck_with_aim_assist) {
    int kills = 0, runs = 0;
    for (bool moving : {false, true})
        for (double dist : {1400.0, 2000.0}) {
            GroundAttackRange r(GroundKind::Truck, dist, dist * 0.18, 0.0, moving);
            CHECK(r.valid);
            PlayerCommands gun; gun.fireGun = true;
            ++runs;
            // nose slightly off the target (within the assist cone): 2 degrees right
            Jet& p = r.b.player();
            Euler e = toEuler(p.fm->state().q);
            p.fm->mutableState().q = Quat::fromEuler(e.heading + 2.0 * kDeg, e.pitch, 0.0);
            for (int i = 0; i < 120 * 3 && r.b.ground()[r.target].alive; ++i) r.b.update(FlightModel::kDt, ControlInputs{}, gun);
            if (!r.b.ground()[r.target].alive) ++kills;
            else std::printf("    strafe failed: moving=%d dist=%.0f hp=%.0f hits=%d/%d\n", moving, dist, r.b.ground()[r.target].health,
                             r.b.playerStats().roundHits, r.b.playerStats().roundsFired);
        }
    CHECK(kills == runs);
}

TEST(refuelling_takes_fuel_only_in_contact_with_the_basket) {
    const CampaignMissionDef* d = campaignMission(41);
    CHECK(d != nullptr);
    if (!d) return;
    DesertPlaythrough pt(*d);
    FlightModel& fm = *pt.battle.player().fm;
    double maxFuel = fm.profile().fuelMassKg;
    CHECK(fm.state().fuelKg < 0.2 * maxFuel);          // the mission starts low on fuel
    // 1) flying formation 60 m off to the side: no fuel
    double before = fm.state().fuelKg;
    for (int i = 0; i < 240; ++i) {
        const Tanker& tk = pt.battle.tanker();
        Quat q = Quat::fromEuler(tk.heading, 0.0, 0.0);
        FlightState& st = fm.mutableState();
        st.q = q; st.vel = tk.vel;
        st.pos = tk.basket() - q.forward() * 8.0 + q.right() * 60.0;
        pt.step(ControlInputs{});
    }
    CHECK(fm.state().fuelKg <= before);
    CHECK(!pt.battle.tanker().contact);
    // 2) probe in the basket: fuel flows to 90 % within ~20 s
    int steps = 0;
    while (fm.state().fuelKg < 0.9 * maxFuel && steps < 120 * 40) {
        const Tanker& tk = pt.battle.tanker();
        Quat q = Quat::fromEuler(tk.heading, 0.0, 0.0);
        FlightState& st = fm.mutableState();
        st.q = q; st.vel = tk.vel;
        st.pos = tk.basket() - q.forward() * 8.0 - q.up() * 1.0 + tk.vel * FlightModel::kDt;
        pt.step(ControlInputs{});
        ++steps;
    }
    std::printf("    refuelled to 90%% in %.1f s\n", steps / 120.0);
    CHECK(fm.state().fuelKg >= 0.9 * maxFuel);
    CHECK(steps < 120 * 30);
    // 3) flying into the tanker is fatal
    {
        const Tanker& tk = pt.battle.tanker();
        FlightState& st = fm.mutableState();
        st.pos = tk.pos + Vec3{0, 1, 0};
        st.vel = tk.vel;
        pt.step(ControlInputs{});
        CHECK(!pt.battle.player().alive);
    }
}

TEST(trains_run_along_the_railway_and_stop_at_the_terminus) {
    Battle b(terrain());
    b.setLayout(&layout());
    b.createPlayer(AircraftId::F22).fm->resetAirborne({0, 3000, 0}, 0, 220, 0.8);
    BattleConfig cfg; cfg.enemyJets = 0; cfg.allyJets = 0; cfg.groundTargets = false; cfg.endlessWaves = false;
    b.start(cfg);
    GroundTarget loco; loco.kind = GroundKind::Locomotive; loco.railS = layout().railway.length() - 200; loco.railSpeed = 20; loco.armed = false;
    int i = b.addGround(loco);
    Vec3 p0 = b.ground()[i].pos;
    { Vec3 r = layout().railway.at(loco.railS); CHECK(std::hypot(b.ground()[i].pos.x - r.x, b.ground()[i].pos.z - r.z) < 1e-6); }
    CHECK(b.ground()[i].pos.y >= terrain().surfaceHeight(b.ground()[i].pos.x, b.ground()[i].pos.z) - 1e-6);
    for (int k = 0; k < 120 * 5; ++k) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(length(b.ground()[i].pos - p0) > 90.0);                        // moved ~100 m along the line
    for (int k = 0; k < 120 * 20; ++k) b.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    CHECK(b.ground()[i].railS == layout().railway.length());             // stopped at the end of the line
    CHECK(length(b.groundVelocity(i)) < 1e-9);
}

TEST(world_layout_has_towns_roads_railway_and_bridges) {
    const WorldLayout& L = layout();
    CHECK(L.towns.size() >= 5);
    CHECK(L.roads.size() >= 4);
    CHECK(L.railway.length() > 10000);
    CHECK(!L.bridges.empty());
    CHECK(L.largestBridge(true) >= 0);
    CHECK(L.factories.size() >= 4);
    CHECK(L.turbines.size() >= 6);
    // the railway profile respects its maximum grade (2.5 %)
    double maxGrade = 0;
    for (size_t k = 1; k < L.railway.pts.size(); ++k) {
        double ds = L.railway.cum[k] - L.railway.cum[k - 1];
        if (ds > 1) maxGrade = std::max(maxGrade, std::fabs(L.railway.pts[k].y - L.railway.pts[k - 1].y) / ds);
    }
    CHECK(maxGrade <= 0.0251);
    // towns are on dry, reasonably flat ground and away from the runway
    for (const Town& t : L.towns) {
        CHECK(terrain().height(t.center.x, t.center.z) > 2.0);
        CHECK(!terrain().onRunway(t.center.x, t.center.z, 800.0));
    }
}

TEST(real_terrain_file_round_trips_and_builds_a_flat_airfield) {
    // a synthetic "real" map: a 2.5 km high massif with a valley floor at 1400 m
    Heightmap hm;
    hm.n = 257;
    hm.extent = 32000;
    hm.h.resize(static_cast<size_t>(hm.n) * hm.n);
    for (int j = 0; j < hm.n; ++j)
        for (int i = 0; i < hm.n; ++i) {
            double x = -16000 + i * 125.0, z = -16000 + j * 125.0;
            hm.h[static_cast<size_t>(j) * hm.n + i] = static_cast<float>(1400 + 2500 * std::exp(-(x * x + (z + 5000) * (z + 5000)) / (2 * 4000.0 * 4000.0)) +
                                                                          120 * std::sin(x / 2100.0) * std::cos(z / 1700.0));
        }
    std::string path = (std::filesystem::temp_directory_path() / "skyfall_test.skt").string();
    CHECK(saveHeightmap(path, hm, 46.0, 7.7));
    Heightmap back;
    std::string err;
    CHECK(loadHeightmap(path, back, err));
    CHECK(back.n == hm.n);
    CHECK_NEAR(back.extent, 32000.0, 1e-3);
    CHECK_NEAR(back.at(100, 120), std::round(hm.at(100, 120)), 0.51);
    Heightmap missing;
    CHECK(!loadHeightmap(path + ".nope", missing, err));
    const LocationDef* loc = findLocation("matterhorn");
    CHECK(loc != nullptr);
    if (!loc) return;
    Terrain t(back, terrainInfoFor(*loc));
    CHECK(t.info().real);
    CHECK(t.info().biome == Biome::Alpine);
    // the source shape survives (peak ~3.9 km, valley ~1.4 km)
    CHECK_NEAR(t.height(0, -5000), 3900, 160);
    // runway on dry ground and flat along its length
    const RunwayInfo& rw = t.runway();
    double lo = 1e9, hi = -1e9;
    for (double a = -1500; a <= 1500; a += 100) { double h = t.height(rw.center.x + a, rw.center.z); lo = std::min(lo, h); hi = std::max(hi, h); }
    CHECK(hi - lo < 1.5);
    CHECK(rw.center.y > 1000);
    CHECK(t.info().cloudBase > rw.center.y);
    // scenery can be generated on it
    WorldLayout L = WorldLayout::build(t);
    CHECK(!L.towns.empty());
    std::filesystem::remove(path);
}

TEST(real_terrain_sea_level_samples_become_water) {
    // elevation data stores the sea and fjords as exactly 0 m: they must be water, not flat land
    Heightmap hm;
    hm.n = 129;
    hm.extent = 32000;
    hm.h.assign(static_cast<size_t>(hm.n) * hm.n, 400.0f);
    for (int j = 0; j < hm.n; ++j)
        for (int i = 50; i < 70; ++i) hm.h[static_cast<size_t>(j) * hm.n + i] = 0.0f;   // a north-south fjord
    TerrainInfo info;
    info.id = "fjord_test";
    Terrain t(hm, info);
    double fx = -16000 + 60.0 / 128.0 * 32000;
    CHECK(t.isWater(fx, 0));
    CHECK(t.height(fx, 0) < -2.0);
    CHECK(!t.isWater(-12000, 0));
    CHECK(!t.isWater(fx, t.runway().center.z) || !t.onRunway(fx, t.runway().center.z));
}

TEST(every_spoken_line_has_a_recording_in_the_voice_pack) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(__FILE__).parent_path().parent_path() / "assets" / "voice";
    if (!fs::exists(dir)) { std::printf("    (no voice pack in this checkout - skipped)\n"); return; }
    int missing = 0;
    auto lines = allVoiceLines();
    CHECK(lines.size() > 150);
    for (const VoiceLine& l : lines)
        if (!fs::exists(dir / (voiceKey(l.who, l.text) + ".mp3"))) {
            if (missing++ < 5) std::printf("    missing recording: %s\n", l.text.substr(0, 70).c_str());
        }
    CHECK(missing == 0);
    // every briefing ends with the sign-off and names the mission
    for (int n = 0; n <= kCampaignMissions; ++n) {
        Briefing b = makeBriefing(n);
        CHECK(b.speech.size() > 60);
        CHECK(b.speech.find("Good luck, Captain.") != std::string::npos);
    }
}

TEST(world_has_at_least_twelve_real_locations) {
    int real = 0;
    for (const LocationDef& l : locations()) {
        if (l.id != "desert") ++real;
        CHECK(!l.name.empty());
        CHECK(std::fabs(l.lat) <= 90 && std::fabs(l.lon) <= 180);
    }
    CHECK(real >= 12);
    CHECK(findLocation("desert") != nullptr);
    CHECK(findLocation("everest") != nullptr);
}

TEST(generated_towns_do_not_cover_campaign_targets) {
    int bad = 0;
    for (const CampaignMissionDef& m : campaignMissions())
        for (const TargetDef& g : m.targets) {
            if (g.anchor != TargetDef::Anchor::None || g.pathRadius > 0) continue;
            for (size_t k = 1; k < layout().towns.size(); ++k) {   // town 0 is the original city
                const Town& tw = layout().towns[k];
                if (std::hypot(tw.center.x - g.x, tw.center.z - g.z) < tw.radius + 40) {
                    if (bad++ < 10) std::printf("    mission %02d: %s inside %s\n", m.number, g.label.c_str(), tw.name.c_str());
                }
            }
        }
    CHECK(bad == 0);
}

namespace {
Terrain& carrierTerrain() {
    static Terrain t;
    static bool init = false;
    if (!init) { t.setDeck(campaignCarrierDeck()); init = true; }
    return t;
}
/// The carrier launch every carrier mission starts with: afterburner, release the brake,
/// hands off. Returns the flight model after `seconds` of flight.
struct CatShot {
    bool released = false;
    ControlInputs step(FlightModel& fm) {
        ControlInputs in;
        const FlightState& s = fm.state();
        in.throttleRate = s.throttle < 1.0 ? 1.0 : 0.0;
        if (!released && s.throttle >= FlightModel::kAbDetent) { fm.setParkingBrake(false); released = true; }
        if (!s.onGround && s.agl > 60 && s.gearDown) fm.toggleGear();
        return in;
    }
};
}  // namespace

TEST(carrier_deck_is_a_raised_runway_on_the_sea) {
    const Terrain& t = carrierTerrain();
    const DeckInfo& d = t.deck();
    CHECK(d.active);
    CHECK(t.isWater(d.center.x, d.center.z));
    CHECK(t.onDeck(d.center.x, d.center.z));
    CHECK_NEAR(t.surfaceHeight(d.center.x, d.center.z), d.center.y, 1e-6);
    CHECK(t.surface(d.catapultStart().x, d.catapultStart().z) == SurfaceKind::Runway);
    Vec3 off = d.center + Vec3{0, 0, 0} + Vec3{std::cos(d.heading + kPi / 2), 0, std::sin(d.heading + kPi / 2)} * 80.0;
    CHECK(t.surfaceHeight(off.x, off.z) < 1.0);   // the sea beside the ship
}

TEST(catapult_holds_until_afterburner_and_brake_release) {
    for (int ac = 0; ac < 8; ++ac) {
        FlightModel fm(profile(static_cast<AircraftId>(ac)), carrierTerrain());
        fm.resetOnCatapult();
        Vec3 p0 = fm.state().pos;
        ControlInputs in;
        fm.setParkingBrake(false);   // brake off but idle: the holdback keeps it there
        for (int i = 0; i < 240; ++i) fm.step(in);
        CHECK(length(fm.state().pos - p0) < 0.01);
        CHECK(fm.state().catArmed);
        in.throttleRate = 1.0;
        for (int i = 0; i < 600 && fm.state().catArmed; ++i) fm.step(in);
        CHECK(!fm.state().catArmed);
        CHECK(fm.state().catFiring || fm.state().launched);
    }
}

TEST(hands_off_catapult_launch_climbs_away_safely_in_every_aircraft_and_mode) {
    for (int mode = 0; mode < 3; ++mode)
        for (int ac = 0; ac < 8; ++ac) {
            FlightModel fm(profile(static_cast<AircraftId>(ac)), carrierTerrain());
            fm.setHandling(static_cast<HandlingMode>(mode));
            fm.resetOnCatapult();
            CatShot pilot;
            double deckY = carrierTerrain().deck().center.y;
            double minAgl = 1e9, maxDeckSpeed = 0;
            bool leftDeck = false;
            for (int i = 0; i < 120 * 25 && !fm.state().crashed; ++i) {
                fm.step(pilot.step(fm));
                const FlightState& s = fm.state();
                if (s.onGround) maxDeckSpeed = std::max(maxDeckSpeed, s.airspeed);
                if (!s.onGround && s.launched) { leftDeck = true; minAgl = std::min(minAgl, s.pos.y); }
            }
            const FlightState& s = fm.state();
            if (s.crashed || !leftDeck || s.pos.y < deckY + 250 || minAgl < 8.0)
                std::printf("    aircraft %d mode %d: crashed=%d(%s) alt=%.0f minAlt=%.1f deckSpeed=%.0f\n", ac, mode, s.crashed,
                            crashReasonText(s.crashReason), s.pos.y, minAgl, maxDeckSpeed);
            CHECK(!s.crashed);
            CHECK(leftDeck);
            CHECK(minAgl > 8.0);                // never settles towards the waves
            CHECK(s.pos.y > deckY + 250);       // climbing away after 25 s with no pitch input
            CHECK(maxDeckSpeed > fm.profile().rotateSpeedMs);
        }
}

namespace {
struct DesertCombatBot : sky::CombatBot {
    DesertCombatBot(const CampaignMissionDef& d, HandlingMode mode, int tacticsOverride = -1, unsigned seed = 0)
        : sky::CombatBot(d, mode, d.carrier ? static_cast<const Terrain&>(carrierTerrain()) : terrain(), &layout(), tacticsOverride, seed) {}
};
}  // namespace

TEST(hard_campaign_missions_are_winnable_by_a_real_pilot_in_every_handling_mode) {
    // Missions 47-61 are flown by CombatBot against the full enemy AI in all three handling
    // modes, five random seeds each (different enemy decisions). Every mission must be won in
    // every mode, and in most attempts overall: hard, but never a wall.
    const char* names[] = {"arcade", "assisted", "simulation"};
    const unsigned kSeeds = std::getenv("SKYFALL_SEEDS") ? static_cast<unsigned>(std::atoi(std::getenv("SKYFALL_SEEDS"))) : 5u;
    const char* tacEnv = std::getenv("SKYFALL_TAC");
    const bool verbose = std::getenv("SKYFALL_VERBOSE") != nullptr;
    for (int n = 47; n <= kCampaignMissions; ++n) {
        const CampaignMissionDef* d = campaignMission(n);
        if (!d) { CHECK(false); continue; }
        std::printf("    mission %02d %-22s", n, d->title.c_str());
        unsigned total = 0;
        for (int mode = 0; mode < 3; ++mode) {
            unsigned wins = 0, tries = 0;
            double hull = 0, secs = 0;
            // a borderline result (under 2 in 5) gets a second batch of seeds: the outcome of a
            // dogfight is chaotic, and compilers / maths libraries differ in the last bit
            for (unsigned seed = 0; seed < 2 * kSeeds; ++seed) {
                if (seed == kSeeds && wins * 5 >= kSeeds * 2) break;
                ++tries;
                DesertCombatBot bot(*d, static_cast<HandlingMode>(mode), tacEnv ? std::atoi(tacEnv) : -1, seed);
                bool ok = bot.run(900.0);
                const Jet& p = bot.battle.player();
                if (ok) { ++wins; hull += p.health; secs += bot.time; }
                if (verbose || !ok)
                    std::printf("\n      %-10s seed %u %s t=%4.0f s hull %3.0f (air -%.0f, ground -%.0f) at %s%s", names[mode], seed, ok ? "WON " : "LOST",
                                bot.time, p.health, bot.dmgAir, bot.dmgGround,
                                ok ? "end" : (bot.mission.current() ? bot.mission.current()->title.c_str() : "-"),
                                !ok && p.fm->state().crashReason != CrashReason::Destroyed && p.fm->state().crashed ? " (crash)" : "");
            }
            std::printf("  %s %u/%u", names[mode], wins, tries);
            if (wins) std::printf(" (%3.0f s, hull %3.0f)", secs / wins, hull / wins);
            CHECK(wins * 5 >= tries * 2);   // winnable in every mode (at least 2 attempts in 5)
            total += wins * kSeeds / tries;   // counted per kSeeds attempts
        }
        std::printf("\n");
        CHECK(total * 5 >= kSeeds * 3 * 3);   // and won in at least 60 % of all attempts
    }
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = g_failed;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failed == before ? " OK " : "FAIL", t.name);
        ++ran;
    }
    std::printf("\n%d tests, %d checks, %d failures\n", ran, g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
