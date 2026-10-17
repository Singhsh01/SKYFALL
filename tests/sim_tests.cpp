// SKYFALL unit / integration tests — no external framework, runs headless via ctest.
#include "aircraft/AircraftProfile.hpp"
#include "app/CameraMode.hpp"
#include "app/Settings.hpp"
#include "combat/Combat.hpp"
#include "mission/Campaign.hpp"
#include "mission/Mission.hpp"
#include "sim/Atmosphere.hpp"
#include "sim/FlightModel.hpp"
#include "telemetry/FrameStats.hpp"
#include "telemetry/Replay.hpp"

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
        CHECK(dh > 180 * kDeg);
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

TEST(frame_stats_percentiles) {
    FrameStats fs(1000);
    for (int i = 1; i <= 100; ++i) fs.push(i);
    Percentiles p = fs.compute();
    CHECK_NEAR(p.p50, 50.5, 1e-9);
    CHECK(p.p95 > 94 && p.p95 < 96.5);
    CHECK(p.p99 > 98 && p.p99 <= 100);
    CHECK_NEAR(p.mean, 50.5, 1e-9);
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

TEST(all_eight_aircraft_profiles_are_defined) {
    int playable = 0;
    for (const auto& p : allProfiles()) {
        CHECK(p.name != nullptr && std::strlen(p.name) > 0);
        CHECK(p.wingAreaM2 > 10 && p.thrustAbN > p.thrustMilN);
        playable += p.playableNow;
    }
    CHECK(allProfiles().size() == 8);
    CHECK(playable == 8);
}

// ------------------------------------------------------------------------------------------
// Campaign 04-40
// ------------------------------------------------------------------------------------------
TEST(campaign_has_forty_playable_missions) {
    const auto& cat = campaignCatalogue();
    CHECK(cat.size() == 40);
    for (size_t i = 0; i < cat.size(); ++i) {
        CHECK(cat[i].number == static_cast<int>(i) + 1);
        CHECK(cat[i].playable);
        CHECK(!cat[i].title.empty());
    }
    for (int n = 4; n <= 40; ++n) {
        const CampaignMissionDef* d = campaignMission(n);
        CHECK(d != nullptr);
        if (d) CHECK(!d->objectives.empty());
    }
    CHECK(campaignMission(3) == nullptr);
    CHECK(campaignMission(41) == nullptr);
}

TEST(campaign_layouts_are_valid) {
    const Terrain& t = terrain();
    int bad = 0;
    auto fail = [&](const CampaignMissionDef& m, const std::string& what) {
        if (bad++ < 25) std::printf("    mission %02d: %s\n", m.number, what.c_str());
    };
    for (const CampaignMissionDef& m : campaignMissions()) {
        if (std::fabs(m.startX) > 14500 || std::fabs(m.startZ) > 14500) fail(m, "start outside the map");
        if (m.startAlt < t.surfaceHeight(m.startX, m.startZ) + 300) fail(m, "start too close to the ground");
        for (const TargetDef& g : m.targets) {
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
struct CampaignPlaythrough {
    const CampaignMissionDef& def;
    Battle battle{terrain()};
    Mission mission;
    CampaignDirector director;
    explicit CampaignPlaythrough(const CampaignMissionDef& d) : def(d), mission(buildCampaignMission(d)) {
        Jet& p = battle.createPlayer(AircraftId::F22);
        p.fm->resetAirborne({d.startX, d.startAlt, d.startZ}, campaignStartHeading(d), d.startSpeed, 0.8);
        director.begin(d, battle);
        battle.setAiEnabled(false);
        director.update(mission, battle);
    }
    void step(const ControlInputs& in) {
        battle.update(FlightModel::kDt, in, PlayerCommands{});
        MissionContext ctx{&battle.player().fm->state(), &battle.player().fm->profile(), &terrain(), 0, &battle};
        mission.update(ctx, FlightModel::kDt);
        director.update(mission, battle);
    }
    bool run(double maxSeconds) {
        const int maxSteps = static_cast<int>(maxSeconds * 120);
        for (int i = 0; i < maxSteps && mission.status() == MissionStatus::Running; ++i) {
            const ObjectiveDef& o = def.objectives[mission.stepIndex()];
            FlightModel& fm = *battle.player().fm;
            ControlInputs in;
            in.throttleRate = fm.state().throttle < 0.95 ? 1.0 : 0.0;
            switch (o.kind) {
                case ObjectiveKind::Waypoint: {
                    double y = terrain().surfaceHeight(o.x, o.z) + (o.maxAgl > 0 ? o.maxAgl * 0.5 : 800.0);
                    if (std::hypot(fm.state().pos.x - o.x, fm.state().pos.z - o.z) > o.radius * 0.5)
                        fm.resetAirborne({o.x, y, o.z}, 0.0, 220, 0.8);
                    break;
                }
                case ObjectiveKind::DestroyGroup:
                    for (int gi = 0; gi < static_cast<int>(battle.ground().size()); ++gi)
                        if (battle.ground()[gi].group == o.group && battle.ground()[gi].alive) { battle.debugDestroyGround(gi); break; }
                    break;
                case ObjectiveKind::DestroyAir:
                    for (auto& j : battle.jets())
                        if (j->team == Team::Red && j->alive) { battle.debugKillJet(j->id); break; }
                    break;
                case ObjectiveKind::PullG: in.roll = toEuler(fm.state().q).bank < 75 * kDeg ? 1.0 : 0.0; in.pitch = 1.0; break;
                case ObjectiveKind::Loop: in.pitch = 1.0; break;
                case ObjectiveKind::AileronRoll: in.roll = 1.0; in.pitch = 0.1; break;
                case ObjectiveKind::Touchdown:
                    if (!fm.state().onGround) fm.resetOnRunway(false);
                    break;
                case ObjectiveKind::Stop: in.throttleRate = -1.0; in.brake = 1.0; break;
            }
            step(in);
            if (fm.state().crashed) return false;
        }
        return mission.status() == MissionStatus::Complete;
    }
};
}  // namespace

TEST(campaign_missions_do_not_complete_without_action) {
    for (const CampaignMissionDef& d : campaignMissions()) {
        CampaignPlaythrough pt(d);
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
        CampaignPlaythrough pt(d);
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
    CampaignPlaythrough pt(*d);
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
