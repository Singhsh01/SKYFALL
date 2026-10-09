// SKYFALL unit / integration tests — no external framework, runs headless via ctest.
#include "aircraft/AircraftProfile.hpp"
#include "app/CameraMode.hpp"
#include "app/Settings.hpp"
#include "combat/Combat.hpp"
#include "mission/Mission.hpp"
#include "sim/Atmosphere.hpp"
#include "sim/FlightModel.hpp"
#include "telemetry/FrameStats.hpp"

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

TEST(all_eight_aircraft_profiles_are_defined) {
    int playable = 0;
    for (const auto& p : allProfiles()) {
        CHECK(p.name != nullptr && std::strlen(p.name) > 0);
        CHECK(p.wingAreaM2 > 10 && p.thrustAbN > p.thrustMilN);
        playable += p.playableNow;
    }
    CHECK(allProfiles().size() == 8);
    CHECK(playable >= 1);
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
