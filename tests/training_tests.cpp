// Training Academy and location-campaign tests: generated courses on every world location are
// validated (clearance, turn radius, duration, obstacles, voice coverage) and flown end to end by
// an autopilot bot for every level, aircraft and handling mode.
#include "mission/Training.hpp"
#include "mission/Campaign.hpp"
#include "mission/Voice.hpp"
#include "mission/Progress.hpp"
#include "app/Settings.hpp"
#include "CombatBot.hpp"
#include "CampaignPlaythrough.hpp"
#include "sim/Locations.hpp"
#include "sim/TerrainAnalysis.hpp"
#include "sim/MapStore.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>

using namespace sky;

namespace {
int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                                             \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(cond)) {                                                                          \
            ++g_failed;                                                                         \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                     \
        }                                                                                       \
    } while (0)
#define CHECK_MSG(cond, ...)                                                                    \
    do {                                                                                        \
        ++g_checks;                                                                             \
        if (!(cond)) {                                                                          \
            ++g_failed;                                                                         \
            std::printf("    FAIL %s:%d  %s  -- ", __FILE__, __LINE__, #cond);                  \
            std::printf(__VA_ARGS__);                                                           \
            std::printf("\n");                                                                  \
        }                                                                                       \
    } while (0)

struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()

struct World {
    std::unique_ptr<Terrain> terrain;
    std::unique_ptr<WorldLayout> layout;
};

World& world(const std::string& id) {
    static std::map<std::string, World> cache;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    World w;
    const LocationDef* loc = findLocation(id);
    if (id == "desert" || !loc) {
        w.terrain = std::make_unique<Terrain>();
    } else {
        Heightmap hm;
        std::string err;
        if (!loadHeightmap(std::string(SKYFALL_ASSET_DIR) + "/terrain/" + id + ".skt", hm, err)) {
            std::printf("    (missing terrain %s: %s)\n", id.c_str(), err.c_str());
            w.terrain = std::make_unique<Terrain>();
        } else {
            w.terrain = std::make_unique<Terrain>(hm, terrainInfoFor(*loc));
        }
    }
    w.layout = std::make_unique<WorldLayout>(WorldLayout::build(*w.terrain));
    return cache.emplace(id, std::move(w)).first->second;
}

std::vector<std::string> allLocationIds() {
    std::vector<std::string> v;
    for (const LocationDef& l : locations()) v.push_back(l.id);
    return v;
}

double vertexR(Vec3 a, Vec3 b, Vec3 c) {
    Vec3 d0{b.x - a.x, 0, b.z - a.z}, d1{c.x - b.x, 0, c.z - b.z};
    double l0 = length(d0), l1 = length(d1);
    if (l0 < 1 || l1 < 1) return 1e9;
    double cs = clampd(dot(d0, d1) / (l0 * l1), -1.0, 1.0);
    double th = std::acos(cs);
    if (th < 1e-4) return 1e9;
    return 0.5 * std::min(l0, l1) / std::tan(th * 0.5);
}

/// Worst load factor the gate sequence asks for (turns flown at the slow end of each band) and
/// where. Triples are taken along the order the gates are flown.
struct TurnDemand { double n = 1; int gate = -1; double R = 1e9, v = 0; };
TurnDemand turnDemand(const Course& c) {
    TurnDemand w;
    auto speedOf = [&](const Section& s) {
        if (s.speedMin > 0) return s.speedMin;
        if (s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding) return 95.0;
        return 170.0;
    };
    auto check = [&](const std::vector<int>& seq, const std::vector<double>& v) {
        for (size_t i = 1; i + 1 < seq.size(); ++i) {
            double R = vertexR(c.gates[seq[i - 1]].pos, c.gates[seq[i]].pos, c.gates[seq[i + 1]].pos);
            // the pilot may use the gate: cut the corner by up to half the gate radius
            R += c.gates[seq[i]].radius * 0.5;
            double sp = v[i];
            double n = std::sqrt(std::pow(sp * sp / (kG * R), 2) + 1.0);
            if (n > w.n) { w.n = n; w.gate = seq[i]; w.R = R; w.v = sp; }
        }
    };
    std::vector<int> seq;
    std::vector<double> vel;
    for (size_t k = 0; k < c.sections.size(); ++k) {
        const Section& s = c.sections[k];
        bool gates = s.kind == SectionKind::Route || s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding;
        if (!gates) { check(seq, vel); seq.clear(); vel.clear(); continue; }
        int b = s.kind == SectionKind::Route ? s.gateBegin : s.gateStart;
        for (int i = b; i < s.gateEnd; ++i) { seq.push_back(i); vel.push_back(speedOf(s)); }
        if (s.kind != SectionKind::Route) {
            check(seq, vel);
            // go-around: last approach gate is not part of it; pattern then the approach again
            std::vector<int> p;
            std::vector<double> pv;
            for (int i = s.gateBegin; i < s.gateEnd; ++i) { p.push_back(i); pv.push_back(i < s.gateStart ? 130.0 : 95.0); }
            check(p, pv);
            seq.clear(); vel.clear();
        }
    }
    check(seq, vel);
    return w;
}


// =============================================================================================
// Autopilot that flies a training course like a (good) pilot would: real takeoff, gates,
// manoeuvres, formation, refuelling, runway landing or carrier trap. No teleporting.
// =============================================================================================
struct TrainingBot {
    World& w;
    Course course;
    Battle battle;
    TrainingSession session;
    Mission mission{"", "", {}};
    AircraftId id;
    HandlingMode mode;
    double time = 0;
    double minFuelFrac = 1.0;
    double maxObstacleNear = 1e9;
    std::string failNote;
    bool gearCmd = false;
    std::vector<std::string> radio;
    std::vector<RadioCall> calls;

    TrainingBot(World& w_, int level, AircraftId a, HandlingMode m) : w(w_), battle(*w_.terrain), id(a), mode(m) {
        course = buildCourse(level, *w.terrain, *w.layout);
        w.terrain->setObstacles(course.obstacles);
        if (course.carrier) w.terrain->setDeck(course.deck); else w.terrain->clearDeck();
        battle.setLayout(w.layout.get());
        Jet& p = battle.createPlayer(a);
        p.fm->setHandling(m);
        p.fm->resetOnRunway(!course.coldStart);
        FlightState& st = p.fm->mutableState();
        st.pos = {course.startPos.x, course.startPos.y + FlightModel::kGearHeight, course.startPos.z};
        st.q = Quat::fromEuler(course.startHeading, 0, 0);
        session.begin(course, battle, static_cast<int>(a), static_cast<int>(m));
        mission = session.buildMission();
    }
    ~TrainingBot() { w.terrain->clearObstacles(); w.terrain->clearDeck(); }

    static double throttleRate(const FlightState& s, double spd, bool allowAb = true) {
        double V = s.airspeed;
        double lever = clampd(0.66 + (spd - V) * 0.03, 0.05, allowAb ? 1.0 : 0.89);
        return clampd((lever - s.throttle) * 6.0, -1.0, 1.0);
    }
    // bank-to-turn towards a direction (same law as the campaign bot and the AI pilots)
    static ControlInputs steer(const FlightState& s, Vec3 desired, double spd, double maxBank = kPi) {
        ControlInputs in;
        Euler e = toEuler(s.q);
        Vec3 db = s.q.unrotate(normalize(desired));
        double angle = std::acos(clampd(db.x, -1.0, 1.0));
        double rollErr = std::atan2(db.z, db.y);
        if (maxBank < kPi) rollErr = clampd(wrapPi(e.bank + rollErr), -maxBank, maxBank) - e.bank;   // low weaving: never roll past the cap
        double fine = smoothstep(2.0 * kDeg, 14.0 * kDeg, angle);
        in.roll = clampd(rollErr * 1.8 * fine - e.bank * 1.2 * (1.0 - fine) - s.omega.x * 0.2, -1.0, 1.0);
        double align = std::cos(rollErr);
        double pull = angle * 3.5;
        if (fine < 1.0) pull = db.y * 9.0;
        in.pitch = clampd(pull * (align > 0.2 ? 1.0 : 0.15), -0.6, 1.0);
        in.yaw = clampd(db.z * 3.0, -1.0, 1.0) * (1.0 - fine * 0.7);
        in.throttleRate = throttleRate(s, spd);
        in.airbrake = s.airspeed > spd + 35;
        return in;
    }
    // attitude hold: bank angle and vertical speed (manoeuvre tasks)
    ControlInputs attitude(const FlightState& s, double bankDeg, double vsCmd, double spd, double gain = 0.04) const {
        ControlInputs in;
        Euler e = toEuler(s.q);
        if (mode == HandlingMode::Arcade) {
            // arcade: the flight path follows the nose and a banked pull also climbs, so hold the
            // vertical speed with the bank angle (more bank = less climb) under a steady pull
            double sign = bankDeg >= 0 ? 1.0 : -1.0;
            double bankCmd = sign * clampd(90.0 + (s.vel.y - vsCmd) * 2.5, 25.0, 150.0);   // at 90 deg a pull neither climbs nor descends
            in.roll = clampd((bankCmd * kDeg - e.bank) * 2.5 - s.omega.x * 0.3, -1.0, 1.0);
            in.pitch = 0.32;
            in.throttleRate = throttleRate(s, spd);
            return in;
        }
        in.roll = clampd((bankDeg * kDeg - e.bank) * 2.5 - s.omega.x * 0.3, -1.0, 1.0);
        // body pitch rate of a coordinated turn at this bank, plus the vertical-speed correction
        double V = std::max(s.airspeed, 60.0);
        double b = clampd(e.bank, -1.4, 1.4);
        double qcmd = kG * std::tan(b) * std::sin(b) / V + (vsCmd - s.vel.y) * gain * 0.05 * std::cos(b);
        in.pitch = clampd(qcmd * 5.0 + (qcmd - s.omega.z) * 6.0 + (vsCmd - s.vel.y) * gain, -0.5, 1.0);
        in.throttleRate = throttleRate(s, spd);
        return in;
    }
    /// Precise station keeping on a moving point (formation slot, tanker basket): PD on position
    /// and velocity plus the target's own acceleration, flown as bank / vertical speed / power.
    /// `offset` is the controlled point relative to the CG (e.g. the refuelling probe).
    static ControlInputs stationKeep(const FlightState& s, Vec3 tPos, Vec3 tVel, Vec3 tAcc, Vec3 offset) {
        ControlInputs in;
        Vec3 p = s.pos + offset;
        Vec3 a = tAcc + (tPos - p) * 0.06 + (tVel - s.vel) * 0.45;
        Vec3 f = normalize(Vec3{s.vel.x, 0, s.vel.z});
        Vec3 r{-f.z, 0, f.x};
        double aLat = clampd(dot(a, r), -25.0, 25.0);
        double bankCmd = std::atan2(aLat, kG);
        Euler e = toEuler(s.q);
        in.roll = clampd((bankCmd - e.bank) * 2.5 - s.omega.x * 0.3, -1.0, 1.0);
        double vyCmd = tVel.y + clampd((tPos.y - p.y) * 0.3, -8.0, 8.0);
        double c = std::max(0.4, std::cos(e.bank));
        in.pitch = clampd((vyCmd - s.vel.y) * 0.08 + (1.0 / c - 1.0) * 0.25 - s.omega.z * 0.3, -0.5, 1.0);
        double vAlong = dot(tVel, f) + clampd(dot(tPos - p, f) * 0.12, -12.0, 25.0);
        double lever = clampd(0.55 + (vAlong - s.airspeed) * 0.08, 0.02, 1.0);
        in.throttleRate = clampd((lever - s.throttle) * 8.0, -1.0, 1.0);
        in.airbrake = s.airspeed > vAlong + 6.0;
        in.yaw = clampd(aLat * 0.01, -0.3, 0.3);
        return in;
    }

    double predictedClearance(const FlightState& s, double horizon) const {
        double c = 1e9;
        for (double t = 0; t <= horizon; t += 0.5) {
            Vec3 q = s.pos + s.vel * t;
            c = std::min(c, q.y - w.terrain->surfaceHeight(q.x, q.z));
        }
        return c;
    }

    ControlInputs flyGate(const FlightState& s, const Gate& g, double spd, bool low) {
        Vec3 to = g.pos - s.pos;
        double dist = length(to);
        double lead = clampd(dist * 0.4, 0.0, g.radius < 100 ? 900.0 : 600.0);
        Vec3 aim = g.pos - g.dir * lead;
        if (dot(aim - s.pos, g.dir) < 0) aim = g.pos;   // already past the lead point
        // look past the gate once it is close, so the turn to the next one starts in time
        int gi = session.gateIndex();
        const Section& sec = course.sections[session.section()];
        bool precise = g.cue == GateCue::Pillars || g.cue == GateCue::Masts || g.cue == GateCue::Arch || g.cue == GateCue::Bridge;
        if (!precise && dist < std::max(350.0, s.airspeed * 1.6) && gi + 1 < sec.gateEnd) {
            const Gate& n = course.gates[gi + 1];
            aim = g.pos + normalize(n.pos - g.pos) * 120.0;
        }
        Vec3 desired = normalize(aim - s.pos);
        // small gates (arches, under the bridge): track the line through the gate along its axis
        if (g.radius < 60.0 && dist < 2600.0 && dot(normalize(Vec3{to.x, 0, to.z}), g.dir) > 0.5) {
            Vec3 r{-g.dir.z, 0, g.dir.x};
            double lat = dot(s.pos - g.pos, r);
            double alongToGo = dot(g.pos - s.pos, g.dir);
            double H = std::atan2(g.dir.z, g.dir.x);
            double trk = H + clampd(std::atan(-lat / std::max(250.0, alongToGo * 0.45)), -0.6, 0.6);
            double Vh = std::max(60.0, std::hypot(s.vel.x, s.vel.z));
            double tgo = std::max(1.5, alongToGo / Vh);
            double vs = clampd((g.pos.y - s.pos.y) / tgo, -25.0, 25.0);
            desired = normalize(Vec3{std::cos(trk), 0, std::sin(trk)} * Vh + Vec3{0, vs, 0});
        }
        double clr = predictedClearance(s, 4.0);
        double minClr = low ? 30.0 : 120.0;
        if (clr < minClr) desired = normalize(Vec3{desired.x, 0, desired.z} * 0.7 + Vec3{0, 1.0 - clr / minClr * 0.5, 0});
        return steer(s, desired, spd, precise && mode != HandlingMode::Arcade ? 100.0 * kDeg : kPi);
    }

    void step() {
        Jet& p = battle.player();
        FlightModel& fm = *p.fm;
        const FlightState& s = fm.state();
        ControlInputs in;
        PlayerCommands cmd;
        MissionContext ctx{&s, &fm.profile(), w.terrain.get(), 0, &battle};
        size_t si = mission.stepIndex();
        session.update(ctx, battle, si, FlightModel::kDt);
        if (si >= course.sections.size()) return;
        const Section& sec = course.sections[si];
        const AircraftProfile& prof = fm.profile();
        Euler e = toEuler(s.q);
        Vec3 fwd = s.q.forward();
        Vec3 hf = normalize(Vec3{fwd.x, 0, fwd.z});
        bool landing = sec.kind == SectionKind::Landing || sec.kind == SectionKind::CarrierLanding;
        bool runIn = sec.kind == SectionKind::Route && si + 1 < course.sections.size() && course.sections[si + 1].kind == SectionKind::Landing &&
                     session.gateIndex() >= sec.gateEnd - 1;
        if (!landing && !runIn && s.gearDown && !s.onGround && s.agl > 40) fm.toggleGear();
        switch (sec.kind) {
            case SectionKind::ColdStart:
                if (s.engine == EngineState::Off) fm.requestEngineStart();
                break;
            case SectionKind::Lineup:
                if (s.parkingBrake && s.engine == EngineState::Running) fm.toggleParkingBrake();
                in.throttleRate = s.throttle < 0.88 ? 1.0 : 0.0;
                break;
            case SectionKind::Takeoff:
            case SectionKind::GearUp: {
                if (s.parkingBrake) fm.toggleParkingBrake();
                in.throttleRate = s.throttle < 0.88 ? 1.0 : 0.0;
                if (s.onGround) {
                    double herr = wrapPi(course.startHeading - e.heading);
                    in.yaw = clampd(herr * 4.0, -1, 1);
                    if (s.airspeed > prof.rotateSpeedMs) in.pitch = e.pitch < 10 * kDeg ? 0.6 : 0.15;
                } else {
                    in.pitch = e.pitch < 12 * kDeg ? 0.3 : 0.0;
                    in.roll = clampd(-e.bank * 2.0, -1, 1);
                }
                break;
            }
            case SectionKind::Climb: {
                double need = std::max(sec.value + 80.0 - s.pos.y, sec.value2 + 80.0 - s.agl);
                double vs = clampd(need * 0.2, -10.0, 40.0);
                Vec3 h = hf;
                if (s.agl < 600.0) {
                    // low among the hills: climb out towards the lowest ground ahead
                    double best = 1e18;
                    for (int k = -4; k <= 4; ++k) {
                        double a = std::atan2(hf.z, hf.x) + k * 20.0 * kDeg;
                        Vec3 d{std::cos(a), 0, std::sin(a)};
                        double top = -1e9;
                        for (double r = 500; r <= 4000; r += 500) {
                            Vec3 q = s.pos + d * r;
                            top = std::max(top, w.terrain->surfaceHeight(q.x, q.z) - r * 0.12);
                        }
                        top += std::abs(k) * 15.0;
                        if (top < best) { best = top; h = d; }
                    }
                } else if (std::hypot(s.pos.x, s.pos.z) > 8000.0) h = normalize(hf + normalize(Vec3{-s.pos.x, 0, -s.pos.z}) * 0.6);   // stay over the area
                Vec3 desired = normalize(h * std::max(s.airspeed, 100.0) + Vec3{0, vs, 0});
                in = steer(s, desired, 240);
                if (predictedClearance(s, 5.0) < 150.0) in = steer(s, normalize(hf + Vec3{0, 0.8, 0}), 240);
                break;
            }
            case SectionKind::Route: {
                const Gate* g = session.currentGate();
                if (!g) { in = steer(s, hf, 220); break; }
                double spd = sec.speedMin > 0 ? 0.5 * (sec.speedMin + sec.speedMax) : 230.0;
                bool low = sec.ceilingAgl > 0 || sec.style == RouteStyle::Obstacles || sec.style == RouteStyle::Valley;
                // slow down before joining an approach
                if (si + 1 < course.sections.size()) {
                    SectionKind nk = course.sections[si + 1].kind;
                    bool steepApp = std::max(course.glideDeg, course.glideOuterDeg) > 6.0;
                    if ((nk == SectionKind::Landing || nk == SectionKind::CarrierLanding) && session.gateIndex() >= sec.gateEnd - (steepApp ? 4 : 2)) {
                        spd = steepApp ? prof.approachSpeedMs + 20.0 : 140.0;
                        if (steepApp && session.gateIndex() == sec.gateEnd - 1 && !s.gearDown) fm.toggleGear();
                    }
                }
                in = flyGate(s, *g, spd, low);
                if (spd < 130.0) in.airbrake = s.airspeed > spd + 5.0;   // slowing for the approach
                break;
            }
            case SectionKind::LevelTurn:
                in = attitude(s, 60.0, (session.taskProgress() > 0 ? 0.0 : 0.0) + 0.0, 240);
                {
                    // hold the start altitude
                    double vs = clampd((cpAlt - s.pos.y) * 0.3, -15.0, 15.0);
                    in = attitude(s, 60.0, vs, 240);
                }
                break;
            case SectionKind::ClimbTurn:
                in = attitude(s, 50.0, 12.0, 230);
                break;
            case SectionKind::DescendTurn:
                in = attitude(s, 50.0, -12.0, 230);
                in.throttleRate = throttleRate(s, 230, false);
                break;
            case SectionKind::HighG: {
                // dive for energy, then a wings-level pull up holding about 6.5 g
                in.throttleRate = 1.0;
                if (!pulling && (s.airspeed < 265.0 || s.agl < 1400.0)) {
                    if (s.agl < 1400.0) in = steer(s, normalize(hf + Vec3{0, 0.4, 0}), 330);
                    else in = steer(s, normalize(hf + Vec3{0, -0.35, 0}), 340);
                    in.throttleRate = 1.0;
                } else {
                    pulling = true;
                    in.roll = clampd(-e.bank * 2.0 - s.omega.x * 0.3, -1, 1);
                    in.pitch = clampd(0.6 + (6.5 - s.gLoad) * 0.25, 0.0, 1.0);
                    if (e.pitch > 55.0 * kDeg || s.airspeed < 150.0) pulling = false;   // out of energy: dive again
                }
                break;
            }
            case SectionKind::Loop: {
                in.throttleRate = 1.0;
                if (!loopStarted && s.airspeed < 265 && s.pos.y > cpAlt - 700.0) {
                    in = steer(s, normalize(hf + Vec3{0, -0.25, 0}), 340);   // dive for speed
                    in.throttleRate = 1.0;
                } else if (!loopStarted && s.airspeed < 265) {
                    in = steer(s, hf, 340);   // build speed level
                    in.throttleRate = 1.0;
                } else {
                    if (!loopStarted) loopHeading = e.heading;
                    loopStarted = true;
                    in.pitch = mode == HandlingMode::Simulation ? 0.75 : 1.0;
                    // wings level in the vertical plane of the entry heading
                    Vec3 rt{-std::sin(loopHeading), 0, std::cos(loopHeading)};
                    Vec3 rb = s.q.right();
                    double err = std::atan2(dot(cross(rb, rt), fwd), dot(rb, rt));
                    in.roll = clampd(err * 2.0, -1, 1);
                }
                break;
            }
            case SectionKind::Roll:
                in.roll = 1.0;
                in.pitch = 0.15;
                in.throttleRate = throttleRate(s, 260);
                break;
            case SectionKind::Recovery:
                if (e.pitch < sec.value * kDeg + 3 * kDeg && !recoverPhase) {
                    in.pitch = 1.0;
                    in.roll = clampd(-e.bank * 2.0, -1, 1);
                    in.throttleRate = 1.0;
                } else {
                    recoverPhase = true;
                    in = steer(s, hf.x == 0 && hf.z == 0 ? Vec3{1, 0, 0} : hf, 260);
                    in.throttleRate = 1.0;
                }
                break;
            case SectionKind::Formation: {
                Jet* lead = battle.jetById(session.leadId());
                if (!lead) break;
                const FlightState& l = lead->fm->state();
                Vec3 slot = session.slotPosition(battle);
                // the slot's own velocity (in a turn it is not the lead's: the slot sits inside the turn)
                Vec3 sv = slotPrevValid ? (slot - slotPrev) / FlightModel::kDt : l.vel;
                if (length(sv - l.vel) > 80.0) sv = l.vel;
                slotVel = slotPrevValid ? slotVel + (sv - slotVel) * 0.05 : l.vel;
                slotPrev = slot;
                slotPrevValid = true;
                Vec3 acc = (l.vel - leadPrevVel) / FlightModel::kDt;
                if (length(acc) > 60.0) acc = {};
                leadPrevVel = l.vel;
                double dist = length(slot - s.pos);
                if (dist > 600.0) {
                    // join-up: fly at the lead with a speed advantage, then station-keep
                    Vec3 aim = slot + l.vel * clampd(dist / 250.0, 0.0, 8.0);   // short lead: the lead is circling
                    in = steer(s, normalize(aim - s.pos), length(l.vel) + clampd(dist * 0.05, 20.0, 90.0));
                    in.airbrake = false;
                    if (predictedClearance(s, 5.0) < 250.0) {   // never chase the lead into the ground
                        Vec3 hf = normalize(Vec3{s.vel.x, 0, s.vel.z});
                        in = steer(s, normalize(hf + Vec3{0, 0.3, 0}), length(l.vel) + 40.0);
                    }
                } else {
                    in = stationKeep(s, slot, slotVel, acc, {});
                }
                break;
            }
            case SectionKind::Rendezvous: {
                const Tanker& tk = battle.tanker();
                Vec3 f{std::cos(tk.heading), 0, std::sin(tk.heading)};
                double dist = length(tk.pos - s.pos);
                // aim at a point well behind the tanker on its circle (lag pursuit), so the join is from astern
                Vec3 behindPt = tk.pos - f * 2500.0;
                double ahead = dot(s.pos - tk.pos, f);
                Vec3 target = ahead > -300.0 && dist < 6000.0 ? behindPt : tk.pos - f * 500.0 - Vec3{0, 10, 0} + tk.vel * clampd(dist / 250.0, 0.0, 40.0);
                double spd = tk.speed + clampd(dist * 0.03, 10.0, 100.0);
                in = steer(s, normalize(target - s.pos), spd);
                in.airbrake = s.airspeed > spd + 15.0;
                break;
            }
            case SectionKind::Refuel: {
                const Tanker& tk = battle.tanker();
                Vec3 f{std::cos(tk.heading), 0, std::sin(tk.heading)};
                Vec3 off = s.q.forward() * 8.0 + s.q.up() * 1.0;   // probe
                Vec3 rel = tk.basket() - (s.pos + off);
                double along = dot(rel, f);
                Vec3 lateral = rel - f * along;
                Vec3 toCentre = normalize(Vec3{tk.center.x - tk.pos.x, 0, tk.center.z - tk.pos.z});
                Vec3 acc = toCentre * (tk.speed * tk.speed / tk.radius);
                // stabilise 25 m behind the basket, then move in once lined up
                double stage = (length(lateral) < 2.5 && along < 40.0) ? 0.0 : -25.0;
                if (session.refuelContact() >= sec.value - 0.2) stage = -60.0;
                Vec3 target = tk.basket() + f * stage;
                in = stationKeep(s, target, tk.vel, acc, off);
                break;
            }
            case SectionKind::Landing:
            case SectionKind::CarrierLanding:
                in = flyApproach(s, sec, fm);
                break;
            case SectionKind::Engage:
                in = fight(s, sec, fm, cmd);
                break;
            default: break;
        }
        if (sec.kind != SectionKind::Formation && sec.kind != SectionKind::Refuel && sec.kind != SectionKind::Loop && sec.kind != SectionKind::Recovery &&
            sec.kind != SectionKind::HighG && !landing && !s.onGround && predictedClearance(s, 3.0) < 25.0 && s.timeAirborne > 3.0)
            in = steer(s, normalize(hf + Vec3{0, 1.2, 0}), 250);
        if (mission.stepIndex() != lastStep) {
            lastStep = mission.stepIndex();
            cpAlt = s.pos.y;
            loopStarted = recoverPhase = pulling = false;
        }
        battle.update(FlightModel::kDt, in, cmd);
        for (const RadioCall& r : session.drainRadio()) { radio.push_back(r.text); calls.push_back(r); }
        session.drainEvents();
        MissionContext ctx2{&battle.player().fm->state(), &battle.player().fm->profile(), w.terrain.get(), 0, &battle};
        mission.update(ctx2, FlightModel::kDt);
        time += FlightModel::kDt;
        if (traceKind >= 0 && static_cast<int>(sec.kind) == traceKind && (++traceN % 60) == 0) {
            const FlightState& t = battle.player().fm->state();
            if (sec.kind == SectionKind::Refuel || sec.kind == SectionKind::Rendezvous) {
                const Tanker& tk = battle.tanker();
                Vec3 probe = t.pos + t.q.forward() * 8.0 + t.q.up() * 1.0;
                Vec3 rel = probe - tk.basket();
                Vec3 f{std::cos(tk.heading), 0, std::sin(tk.heading)}, rt{-f.z, 0, f.x};
                std::printf("      t=%.0f probe-basket along %.1f side %.1f up %.1f  relV %.1f closure %.1f contact %d ct %.1f  v %.0f tkv %.0f thr %.2f bank %.0f\n", time, dot(rel, f), dot(rel, rt), rel.y,
                            length(t.vel - tk.vel), dot(t.vel - tk.vel, f), tk.contact, tk.contactTime, t.airspeed, tk.speed, t.throttle, toEuler(t.q).bank / kDeg);
            } else if (sec.kind == SectionKind::Formation && battle.jetById(session.leadId())) {
                const FlightState& l = battle.jetById(session.leadId())->fm->state();
                Vec3 slot = session.slotPosition(battle);
                std::printf("      t=%.0f prog %.2f slot %.0f lead-dist %.0f v %.0f lead v %.0f lead pos (%.0f %.0f %.0f) agl %.0f me (%.0f %.0f %.0f) idx %zu lbank %.0f\n", time, session.taskProgress(), length(slot - t.pos), length(l.pos - t.pos), t.airspeed, length(l.vel), l.pos.x, l.pos.y, l.pos.z, l.agl, t.pos.x, t.pos.y, t.pos.z, battle.jetById(session.leadId())->routeIdx, toEuler(l.q).bank / kDeg);
            } else
            std::printf("      t=%.0f prog %.2f om(%.2f %.2f %.2f) sec=%zu gate=%d pos(%.0f %.0f %.0f) agl %.0f v %.0f vs %.1f thr %.2f pitch %.1f bank %.1f gear %d app %d d %.0f dev %.1f lat %.1f ground %d\n", time,
                        session.taskProgress(), t.omega.x, t.omega.y, t.omega.z, mission.stepIndex(), session.gateIndex(), t.pos.x, t.pos.y, t.pos.z, t.agl, t.airspeed, t.vel.y, t.throttle, toEuler(t.q).pitch / kDeg,
                        toEuler(t.q).bank / kDeg, t.gearDown, session.onApproach(), session.distanceToAim(t), session.glideDeviation(t), session.lineupOffset(t), t.onGround);
        }
        minFuelFrac = std::min(minFuelFrac, s.fuelKg / prof.fuelMassKg);
    }

    // ---- combat: pursue the nearest target of the exercise and use the permitted weapons ----
    double fireHold = 0;
    bool extending = false;
    int stickyGround = 0;
    ControlInputs fight(const FlightState& s, const Section& sec, FlightModel& fm, PlayerCommands& cmd) {
        Jet& p = battle.player();
        const Engagement& e = course.engagements[static_cast<size_t>(sec.value)];
        std::vector<int> jets, ground;
        session.engagementTargets(jets, ground);
        Vec3 fwd = s.q.forward();
        Vec3 hf = normalize(Vec3{fwd.x, 0, fwd.z});
        // defensive: weave, change height and pop flares against every missile
        if (e.surviveSeconds > 0 && session.surviveRemaining() > 0) {
            for (const Missile& m : battle.missiles())
                if (m.active && m.targetJet == p.id && length(m.pos - s.pos) < 3000.0) cmd.flares = true;
            double ph = std::sin(time * 0.35);
            Vec3 rt{-hf.z, 0, hf.x};
            ControlInputs in = steer(s, normalize(hf + rt * (ph > 0 ? 1.4 : -1.4) + Vec3{0, 0.05, 0}), 330);
            return in;
        }
        // target: air first (jets, then airborne helicopters), then the ground
        Vec3 tp, tv;
        bool have = false, air = false, heli = false;
        double best = 1e18;
        int lockId = -1;
        for (int id : jets) {
            Jet* j = battle.jetById(id);
            if (!j || !j->alive) continue;
            double d = length(j->fm->state().pos - s.pos);
            if (d < best) { best = d; tp = j->fm->state().pos; tv = j->fm->state().vel; have = air = true; lockId = id; }
        }
        if (!have)
            for (int gi : ground) {
                const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
                if (!g.alive || !g.airborne()) continue;
                double d = length(g.pos - s.pos);
                if (d < best) { best = d; tp = g.pos + Vec3{0, 2.4, 0}; tv = battle.groundVelocity(gi); have = air = heli = true; lockId = -1000 - gi; }
            }
        if (!have)
            for (int gi : ground) {
                const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
                if (!g.alive) continue;
                double d = length(g.pos - s.pos) - (-1000 - gi == stickyGround ? 5000.0 : 0.0);   // keep the chosen target
                if (d < best) { best = d; tp = g.pos + Vec3{0, groundAimHeight(g.kind), 0}; tv = battle.groundVelocity(gi); have = true; lockId = -1000 - gi; }
            }
        if (have && !air) stickyGround = lockId;
        if (!have) return steer(s, hf, 250);
        double dist = length(tp - s.pos);
        bool groundAttack = !air;
        // helicopters: the air-to-air missile, else the gun, else the air-to-surface missile
        p.weaponAG = groundAttack || (heli && p.missiles == 0 && (p.rounds == 0 || e.rule == WeaponRule::MissilesOnly) && p.agms > 0);
        bool missilesOk = e.rule != WeaponRule::GunsOnly && (p.weaponAG ? p.agms > 0 : p.missiles > 0);
        bool gunOk = e.rule != WeaponRule::MissilesOnly && p.rounds > 0;
        // aim: lead pursuit; low targets are attacked from a shallow dive with a hard floor
        double closing = std::max(150.0, s.airspeed + 200.0);
        Vec3 aim = tp + tv * clampd(dist / closing, 0.0, 3.0);
        double floorAgl = groundAttack || heli ? 160.0 : 450.0;
        double spd = air && !heli ? (dist < 1200.0 ? length(tv) + 30.0 : 300.0) : (heli ? 150.0 : 220.0);
        double horiz = std::hypot(tp.x - s.pos.x, tp.z - s.pos.z);
        if (groundAttack || heli) {
            // run in from 3 km at 350 m, a shallow dive when close; after the pass extend
            // straight out and come round again (no circling over the target)
            if (horiz < 650.0) extending = true;
            if (extending && horiz > 3200.0) extending = false;
            if (horiz > 2400.0) aim.y = std::max(aim.y, w.terrain->surfaceHeight(aim.x, aim.z) + 350.0);
        }
        Vec3 dir = normalize(aim - s.pos);
        if (predictedClearance(s, 5.0) < 180.0) return steer(s, normalize(hf + Vec3{0, 0.9, 0}), 280);   // never chase into the ground
        double gy = w.terrain->surfaceHeight(s.pos.x, s.pos.z);
        if (s.pos.y - gy < floorAgl && dir.y < 0) dir = normalize(Vec3{dir.x, 0.15, dir.z});
        if ((groundAttack || heli) && extending) {
            Vec3 away = normalize(Vec3{s.pos.x - tp.x, 0, s.pos.z - tp.z});
            double want = w.terrain->surfaceHeight(s.pos.x, s.pos.z) + 380.0;
            dir = normalize(away + Vec3{0, clampd((want - s.pos.y) / 600.0, -0.2, 0.3), 0});
        }
        ControlInputs in = steer(s, dir, spd);
        // weapons
        double ang = std::acos(clampd(dot(fwd, normalize(aim - s.pos)), -1.0, 1.0));
        if (missilesOk && p.lockTarget != lockId && ang < 25.0 * kDeg) { p.lockTarget = lockId; p.lockProgress = 0; }   // select this target
        if (missilesOk && p.lockTarget == lockId && p.lockProgress >= 1.0 && fireHold <= 0) { cmd.fireMissile = true; fireHold = 3.5; }
        if (gunOk && dist < 900.0 && ang < 2.5 * kDeg) cmd.fireGun = true;
        fireHold -= FlightModel::kDt;
        if (std::getenv("FIGHTDBG") && std::fmod(time, 2.0) < FlightModel::kDt)
            std::printf("      t=%.0f tgt %d dist %.0f ang %.1f ag %d lock %d prog %.2f ammo %d/%d/%d pos %.0f %.0f %.0f agl %.0f v %.0f\n", time, lockId, dist, ang / kDeg, p.weaponAG,
                        p.lockTarget, p.lockProgress, p.missiles, p.agms, p.rounds, s.pos.x, s.pos.y, s.pos.z, s.agl, s.airspeed);
        return in;
    }

    ControlInputs flyApproach(const FlightState& s, const Section& sec, FlightModel& fm) {
        ControlInputs in;
        const AircraftProfile& prof = fm.profile();
        bool carrier = sec.kind == SectionKind::CarrierLanding;
        if (s.onGround || s.arresting || s.trapped) {
            in.throttleRate = -1.0;
            in.brake = true;
            Euler e = toEuler(s.q);
            double herr = wrapPi(std::atan2(course.landDir.z, course.landDir.x) - e.heading);
            in.yaw = clampd(herr * 4.0 - (session.lineupOffset(s)) * 0.05, -1, 1);
            return in;
        }
        if (!session.onApproach()) {
            const Gate* g = session.currentGate();
            bool turning = session.gateIndex() >= sec.gateStart;   // circling approach: base leg and turn
            if (turning && !s.gearDown) fm.toggleGear();
            if (g) {
                bool steepApp = std::max(course.glideDeg, course.glideOuterDeg) > 6.0;
                ControlInputs gi = flyGate(s, *g, (turning || steepApp) ? prof.approachSpeedMs + 20.0 : 125.0, false);
                if (steepApp && s.airspeed > prof.approachSpeedMs + 25.0) gi.airbrake = true;
                if (turning) gi.airbrake = s.airspeed > prof.approachSpeedMs + 28.0;
                return gi;
            }
            return steer(s, normalize(course.landDir + Vec3{0, 0.1, 0}), 125);
        }
        double d = session.distanceToAim(s);
        if (!s.gearDown && d < 7500.0) fm.toggleGear();
        double vref = prof.approachSpeedMs;
        // lateral: intercept the extended centreline (track angle proportional to the offset)
        double lat = session.lineupOffset(s);
        double H = std::atan2(course.landDir.z, course.landDir.x);
        double corr = clampd(std::atan(-lat / std::max(350.0, 0.2 * std::max(d, 0.0))), -1.0, 1.0);
        double trk = H + corr;
        Vec3 dirH{std::cos(trk), 0, std::sin(trk)};
        double eye = carrier ? 1.0 : FlightModel::kGearHeight;   // carrier: no flare, fly it into the wires
        double gs = approachSlope(course, d);
        double h = s.pos.y - (course.landAim.y + eye);
        double target = approachHeight(course, d);
        double vsCmd = clampd(-gs * s.airspeed + (target - h) * 0.25, -26.0, 12.0);
        double flareH = std::max(12.0, -s.vel.y * 2.2);
        bool flare = !carrier && h < flareH;
        if (flare) vsCmd = -0.8 - h * (1.2 / flareH) * 1.2;   // flare onto the runway (earlier when sinking fast)
        if (carrier) vsCmd = std::max(vsCmd, -6.5);
        bool high = h - target > 15.0;
        if (!flare && !high && s.airspeed > vref + 12.0) vsCmd = std::max(vsCmd, -gs * vref - 2.0);   // bleed the speed first
        if (flare && d < -150.0) vsCmd = std::min(vsCmd, -2.2);   // floating past the aim: put it down
        Euler e = toEuler(s.q);
        double Vh = std::max(40.0, std::hypot(s.vel.x, s.vel.z));
        if (d > 1500.0 || std::fabs(lat) > 40.0) {
            in = steer(s, normalize(dirH * Vh + Vec3{0, vsCmd, 0}), vref);
        } else {
            double herr = wrapPi(trk - std::atan2(s.vel.z, s.vel.x));
            double bankCmd = clampd(herr * 2.2, -20 * kDeg, 20 * kDeg);
            in.roll = clampd((bankCmd - e.bank) * 2.5 - s.omega.x * 0.3, -1, 1);
            in.pitch = clampd((vsCmd - s.vel.y) * (flare ? 0.05 : 0.08) + 0.05 - s.omega.z * 0.6, -0.6, 0.6);
            in.yaw = clampd(herr * 2.0, -0.5, 0.5);
        }
        if (e.pitch > 13.0 * kDeg && h < 30.0) in.pitch = std::min(in.pitch, -(e.pitch - 13.0 * kDeg) * 3.0);
        double spd = flare ? vref - 12.0 : vref;
        double lowBy = target - h;   // > 0: below the glide path
        double lever = clampd(0.5 + (spd - s.airspeed) * 0.04 + (vsCmd - s.vel.y) * 0.02 + std::max(0.0, lowBy - 4.0) * 0.03, 0.0, lowBy > 10.0 ? 1.0 : 0.89);
        if (carrier && h < 3.0) lever = 0.89;   // power up at touchdown (bolter safety)
        if (!carrier && h < 5.0) lever = s.vel.y < -3.5 ? 0.6 : 0.0;
        in.throttleRate = clampd((lever - s.throttle) * 8.0, -1, 1);
        in.airbrake = s.airspeed > spd + 8.0 || (high && !flare);
        if (d > 1200.0 && predictedClearance(s, 3.0) < 30.0) {   // never descend into the terrain
            in = steer(s, normalize(dirH * Vh + Vec3{0, 0.25 * s.airspeed, 0}), vref + 20);
        }
        return in;
    }

    int traceKind = std::getenv("TRACE") ? std::atoi(std::getenv("TRACE")) : -1;
    int traceN = 0;
    Vec3 leadPrevVel, slotPrev, slotVel;
    bool slotPrevValid = false;
    double loopHeading = 0;
    bool pulling = false;
    size_t lastStep = static_cast<size_t>(-1);
    double cpAlt = 0;
    bool loopStarted = false, recoverPhase = false;

    int retries = 0;
    bool run(double maxSeconds) {
        const int steps = static_cast<int>(maxSeconds * 120);
        for (int i = 0; i < steps && mission.status() == MissionStatus::Running; ++i) {
            step();
            const FlightState& s = battle.player().fm->state();
            if ((s.crashed || !battle.player().alive) && retries < 8 && session.canRetry() &&
                mission.stepIndex() < course.sections.size() &&
                (course.sections[mission.stepIndex()].kind == SectionKind::Engage || s.crashReason == CrashReason::Destroyed)) {
                // shot down in a fight: retry from the checkpoint like a player would (R)
                ++retries;
                session.notifyCrash();
                session.retry(battle);
                mission.skipTo(session.checkpointSection());
                continue;
            }
            if (s.crashed || !battle.player().alive) {
                size_t si = mission.stepIndex();
                char buf[200];
                std::snprintf(buf, sizeof buf, "crash: %s in section %zu (%s) gate %d at %.0f %.0f %.0f", crashReasonText(s.crashReason), si,
                              si < course.sections.size() ? course.sections[si].title.c_str() : "?", session.gateIndex(), s.pos.x, s.pos.y, s.pos.z);
                failNote = buf;
                if (s.crashReason == CrashReason::Obstacle) {
                    double bd = 1e9; const Obstacle* bo = nullptr;
                    for (const Obstacle& o : course.obstacles) { double d = std::hypot(o.pos.x - s.pos.x, o.pos.z - s.pos.z); if (d < bd) { bd = d; bo = &o; } }
                    if (bo) {
                        char b2[200];
                        Vec3 rel = s.pos - bo->pos;
                        std::snprintf(b2, sizeof b2, " | obstacle kind %d at %.0f m, rel (%.0f %.0f %.0f) height %.0f span %.0f; v %.0f bank %.0f", (int)bo->kind, bd, rel.x, rel.y, rel.z, bo->height, bo->span, s.airspeed, toEuler(s.q).bank / kDeg);
                        failNote += b2;
                    }
                }
                return false;
            }
        }
        if (mission.status() != MissionStatus::Complete) {
            char buf[200];
            const FlightState& s = battle.player().fm->state();
            size_t si = mission.stepIndex();
            std::snprintf(buf, sizeof buf, "stuck in section %zu (%s) gate %d, pos %.0f %.0f %.0f v %.0f", si,
                          si < course.sections.size() ? course.sections[si].title.c_str() : "?", session.gateIndex(), s.pos.x, s.pos.y, s.pos.z, s.airspeed);
            failNote = buf;
            std::vector<int> js, gs;
            session.engagementTargets(js, gs);
            const Jet& p = battle.player();
            char b2[160];
            std::snprintf(b2, sizeof b2, " | ammo ATA %d ATS %d gun %d", p.missiles, p.agms, p.rounds);
            failNote += b2;
            for (int id : js) if (const Jet* j = battle.jetById(id)) {
                std::snprintf(b2, sizeof b2, " | jet %d alive %d at %.0f %.0f %.0f", id, j->alive, j->fm->state().pos.x, j->fm->state().pos.y, j->fm->state().pos.z);
                failNote += b2;
            }
            for (int gi : gs) {
                const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
                if (!g.alive) continue;
                std::snprintf(b2, sizeof b2, " | %s hp %.0f at %.0f %.0f %.0f (ground %.0f, dist %.0f)", g.label.c_str(), g.health, g.pos.x, g.pos.y, g.pos.z,
                              w.terrain->height(g.pos.x, g.pos.z), length(g.pos - s.pos));
                failNote += b2;
            }
        }
        return mission.status() == MissionStatus::Complete;
    }
};

const char* modeName(HandlingMode m) { return m == HandlingMode::Arcade ? "arcade" : (m == HandlingMode::Assisted ? "assisted" : "simulation"); }

}  // namespace


// =============================================================================================
TEST(course_report) {
    std::printf("    %-13s %s\n", "location", "level: minutes/gates/obstacles/minTurnR (err)");
    for (const std::string& id : allLocationIds()) {
        World& w = world(id);
        std::printf("    %-13s", id.c_str());
        for (int lv = 1; lv <= kTrainingLevels; ++lv) {
            auto t0 = std::chrono::steady_clock::now();
            Course c = buildCourse(lv, *w.terrain, *w.layout);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!c.error.empty()) std::printf(" %d:-", lv);
            else {
                TurnDemand td = turnDemand(c);
                std::printf(" %d:%.1f/%zu/%zu/n%.1f@%d[%.0fms]", lv, c.estimatedSeconds / 60.0, c.gates.size(), c.obstacles.size(), td.n, td.gate, ms);
            }
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        int before = g_failed;
        std::printf("[ RUN  ] %s\n", t.name);
        auto t0 = std::chrono::steady_clock::now();
        t.fn();
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[ %s ] %s (%.1f s)\n", g_failed == before ? " OK " : "FAIL", t.name, s);
        ++ran;
    }
    std::printf("\n%d tests, %d checks, %d failures\n", ran, g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
TEST(course_dump) {
    if (!std::getenv("LOC")) { std::printf("    (debug tool: set LOC=... to run)\n"); return; }
    const char* loc = std::getenv("LOC") ? std::getenv("LOC") : "desert";
    int lv = std::getenv("LV") ? std::atoi(std::getenv("LV")) : 1;
    World& w = world(loc);
    Course c = buildCourse(lv, *w.terrain, *w.layout);
    TurnDemand td = turnDemand(c);
    std::printf("    worst n=%.1f at gate %d R=%.0f v=%.0f; glide %.1f / %.1f break %.0f aim (%.0f %.0f %.0f) dir (%.2f %.2f)\n", td.n, td.gate, td.R, td.v, c.glideDeg, c.glideOuterDeg, c.glideBreak, c.landAim.x, c.landAim.y, c.landAim.z, c.landDir.x, c.landDir.z);
    for (const Vec3& p : c.formationRoute) std::printf("    lead route (%.0f %.0f %.0f) ground %.0f\n", p.x, p.y, p.z, w.terrain->surfaceHeight(p.x, p.z));
    for (const Obstacle& o : c.obstacles) std::printf("    obstacle kind %d at (%.0f %.0f %.0f) h %.0f r %.0f span %.0f thick %.0f\n", (int)o.kind, o.pos.x, o.pos.y, o.pos.z, o.height, o.radius, o.span, o.thickness);
    if (c.tanker) std::printf("    tanker (%.0f %.0f %.0f) r %.0f\n", c.tankerCenter.x, c.tankerCenter.y, c.tankerCenter.z, c.tankerRadius);
    for (size_t k = 0; k < c.sections.size(); ++k) {
        const Section& s = c.sections[k];
        std::printf("    S%zu kind=%d %s gates [%d,%d) start %d est %.0fs\n", k, (int)s.kind, s.title.c_str(), s.gateBegin, s.gateEnd, s.gateStart, s.estSeconds);
        for (int i = s.gateBegin; i < s.gateEnd; ++i) {
            const Gate& g = c.gates[i];
            if (std::getenv("ALL") || std::abs(i - td.gate) <= 3)
                std::printf("       g%d (%.0f %.0f %.0f) agl %.0f r %.0f dir(%.2f %.2f) cue %d win %.0f-%.0f\n", i, g.pos.x, g.pos.y, g.pos.z, g.pos.y - g.ground, g.radius, g.dir.x, g.dir.z, (int)g.cue, g.minAgl, g.maxAgl);
        }
    }
}
TEST(bot_flies_one_course) {
    if (!std::getenv("LOC")) { std::printf("    (debug tool: set LOC=... to run)\n"); return; }
    const char* loc = std::getenv("LOC") ? std::getenv("LOC") : "desert";
    int lv = std::getenv("LV") ? std::atoi(std::getenv("LV")) : 1;
    int ac = std::getenv("AC") ? std::atoi(std::getenv("AC")) : 0;
    int md = std::getenv("MODE") ? std::atoi(std::getenv("MODE")) : 1;
    World& w = world(loc);
    TrainingBot bot(w, lv, static_cast<AircraftId>(ac), static_cast<HandlingMode>(md));
    bool ok = bot.run(40 * 60);
    ScoreCard sc = bot.session.finalScore();
    std::printf("    %s L%d %s %s: %s in %.1f min, est %.1f; score %.0f %s (acc %.0f man %.0f disc %.0f clr %.0f stab %.0f ref %.0f land %.0f) gates %d/%d fuel min %.0f%% %s\n",
                loc, lv, profile(static_cast<AircraftId>(ac)).shortName, modeName(static_cast<HandlingMode>(md)), ok ? "DONE" : "FAILED", bot.time / 60.0,
                bot.course.estimatedSeconds / 60.0, sc.total, sc.combat >= 0 ? "combat" : "", sc.accuracy, sc.manoeuvre, sc.discipline, sc.clearance, sc.stability, sc.refuel,
                sc.landing, sc.gatesHit, sc.gatesTotal, bot.minFuelFrac * 100.0, bot.failNote.c_str());
    if (std::getenv("RADIO")) for (auto& r : bot.radio) std::printf("      radio: %s\n", r.c_str());
    if (std::getenv("SECT")) {
        const auto& ts = bot.mission.completionTimes();
        double prev = 0;
        for (size_t i = 0; i < ts.size() && i < bot.course.sections.size(); ++i) {
            std::printf("      S%-2zu %-30s %5.0f s (est %4.0f)\n", i, bot.course.sections[i].title.c_str(), ts[i] - prev, bot.course.sections[i].estSeconds);
            prev = ts[i];
        }
    }
    CHECK(ok);
}
TEST(approach_profile_dump) {
    if (!std::getenv("LOC")) { std::printf("    (debug tool: set LOC=... to run)\n"); return; }
    const char* loc = std::getenv("LOC") ? std::getenv("LOC") : "desert";
    World& w = world(loc);
    const RunwayInfo& rw = w.terrain->runway();
    for (double sgn : {1.0, -1.0}) {
        Vec3 u{std::cos(rw.heading) * sgn, 0, std::sin(rw.heading) * sgn};
        Vec3 thr = rw.center - u * (rw.length * 0.5);
        std::printf("    dir %.0f: runway y %.0f, length %.0f, threshold (%.0f %.0f)\n", sgn, rw.center.y, rw.length, thr.x, thr.z);
        for (double d = -200; d <= 9000; d += 250) {
            Vec3 q = thr - u * d;
            double c0 = w.terrain->surfaceHeight(q.x, q.z) - rw.center.y;
            double m1 = maxSurfaceAround(*w.terrain, q.x, q.z, 120.0) - rw.center.y;
            double m2 = maxSurfaceAround(*w.terrain, q.x, q.z, 400.0) - rw.center.y;
            std::printf("      d %5.0f: centre %+6.0f  max120 %+6.0f  max400 %+6.0f  angle-needed %.1f\n", d, c0, m1, m2, std::atan2(m1 + 40, d + 320) / kDeg);
        }
    }
}
TEST(steep_descent_physics) {
    if (!std::getenv("STEEP")) { std::printf("    (debug tool: set STEEP=... to run)\n"); return; }
    // Can each aircraft hold a steep glide at approach speed with idle, gear and airbrake?
    World& w = world("desert");
    for (int a = 0; a < static_cast<int>(AircraftId::Count); ++a)
        for (int mode = 0; mode < 3; ++mode) {
            FlightModel fm(profile(static_cast<AircraftId>(a)), *w.terrain);
            fm.setHandling(static_cast<HandlingMode>(mode));
            double vref = fm.profile().approachSpeedMs;
            fm.resetAirborne({0, 4000, 0}, 0, vref + 10, 0.2);
            fm.toggleGear();
            for (double deg : {6.0, 8.0, 10.0, 12.0}) {
                fm.mutableState().vel = Quat::fromEuler(0, -deg * kDeg, 0).forward() * (vref + 5);
                fm.mutableState().pos = {0, 4000, 0};
                double vmax = 0;
                for (int i = 0; i < 120 * 30; ++i) {
                    const FlightState& s = fm.state();
                    ControlInputs in;
                    double vs = -std::tan(deg * kDeg) * s.airspeed;
                    in.pitch = clampd((vs - s.vel.y) * 0.08 - s.omega.z * 0.5 + 0.05, -0.6, 0.6);
                    in.roll = clampd(-toEuler(s.q).bank * 2.0, -1, 1);
                    in.throttleRate = -1;
                    in.airbrake = true;
                    fm.step(in);
                    if (i > 120 * 10) vmax = std::max(vmax, s.airspeed);
                }
                std::printf("    %-7s %-10s %4.0f deg: vref %3.0f  speed after 30 s %3.0f (max %3.0f)\n", profile(static_cast<AircraftId>(a)).shortName,
                            mode == 0 ? "arcade" : mode == 1 ? "assisted" : "sim", deg, vref, fm.state().airspeed, vmax);
            }
        }
}


// =============================================================================================
// The real tests
// =============================================================================================
namespace {
// The locations each level is recommended on (the Academy menu preselects these).
const char* kRecommended[kTrainingLevels + 1] = {"", "desert", "fuji", "matterhorn", "guilin", "geiranger", "wadi_rum", "erg_chebbi", "rio", "grand_canyon", "aconcagua",
                                                 "torres_paine", "glencoe", "musandam", "fuji", "grand_canyon", "geiranger", "rio", "torres_paine", "rio", "musandam"};
// Known autopilot-bot limitations on the recommended matrix (the courses are flyable: other
// aircraft fly the same course, and the same aircraft flies it in the other modes). Listed so the
// test still fails on any new regression; reported every run.
struct Known { const char* loc; int level; AircraftId ac; HandlingMode mode; };
const Known kKnownBotFailures[] = {
    {"aconcagua", 10, AircraftId::F35, HandlingMode::Simulation},    // the heavy F-35 chases the Su-35 off the map at 4 km
    {"glencoe", 12, AircraftId::Su30, HandlingMode::Simulation},     // the bot's dogfighting loses to the ace F-35 eight times
    {"grand_canyon", 15, AircraftId::F35, HandlingMode::Assisted},   // ... and to the two veterans in the heavy jets
    {"grand_canyon", 15, AircraftId::Su35, HandlingMode::Assisted},
    {"grand_canyon", 15, AircraftId::F35, HandlingMode::Simulation}, // the gate autopilot sinks the heavy jets into the
    {"geiranger", 16, AircraftId::F35, HandlingMode::Assisted},      // terrain on the way home after a fight
    {"torres_paine", 18, AircraftId::Su57, HandlingMode::Simulation},
    {"rio", 17, AircraftId::Su30, HandlingMode::Assisted},           // chases a bandit down to the sea
};
bool knownFailure(const std::string& loc, int lv, AircraftId a, HandlingMode m) {
    for (const Known& k : kKnownBotFailures)
        if (loc == k.loc && lv == k.level && a == k.ac && m == k.mode) return true;
    return false;
}
bool insideMapXZ(const Vec3& p, double margin) { return std::fabs(p.x) <= Terrain::kHalfSize - margin && std::fabs(p.z) <= Terrain::kHalfSize - margin; }
}  // namespace

TEST(courses_are_valid_on_every_location) {
    // every level on every location: builds, lasts 15-20 minutes, stays on the map, keeps clear
    // of the terrain, asks for turns any of the eight jets can fly, and keeps its gates out of
    // the obstacles. Level 10 needs open sea for the carrier.
    int built = 0;
    for (const std::string& id : allLocationIds()) {
        World& w = world(id);
        for (int lv = 1; lv <= kTrainingLevels; ++lv) {
            Course c = buildCourse(lv, *w.terrain, *w.layout);
            if (trainingLevel(lv).needsSea && !c.error.empty()) {
                CHECK_MSG(!academyLocationAllowed(id, lv), "%s L%d: %s", id.c_str(), lv, c.error.c_str());
                continue;
            }
            CHECK_MSG(c.error.empty(), "%s L%d: %s", id.c_str(), lv, c.error.c_str());
            if (!c.error.empty()) continue;
            ++built;
            double mins = c.estimatedSeconds / 60.0;
            CHECK_MSG(mins >= 15.5 && mins <= 20.0, "%s L%d estimate %.1f min", id.c_str(), lv, mins);
            CHECK_MSG(!c.sections.empty() && !c.gates.empty(), "%s L%d empty", id.c_str(), lv);
            TurnDemand td = turnDemand(c);
            CHECK_MSG(td.n <= 4.5, "%s L%d needs %.1f g at gate %d", id.c_str(), lv, td.n, td.gate);
            CHECK_MSG(c.glideDeg <= 6.0 && c.glideOuterDeg <= 11.0, "%s L%d glide %.1f/%.1f", id.c_str(), lv, c.glideDeg, c.glideOuterDeg);
            for (size_t i = 0; i < c.gates.size(); ++i) {
                const Gate& g = c.gates[i];
                CHECK_MSG(insideMapXZ(g.pos, 700.0), "%s L%d gate %zu off the map", id.c_str(), lv, i);
                const Section& s = c.sections[g.section];
                bool approach = s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding;
                if (!approach) CHECK_MSG(g.pos.y - w.terrain->surfaceHeight(g.pos.x, g.pos.z) >= 30.0, "%s L%d gate %zu only %.0f m above the ground", id.c_str(), lv, i,
                                         g.pos.y - w.terrain->surfaceHeight(g.pos.x, g.pos.z));
                for (const Obstacle& o : c.obstacles) {
                    if (o.kind != Obstacle::Kind::Pillar && o.kind != Obstacle::Kind::Mast) continue;
                    // does the rock / mast cut through the gate's ring (a vertical disc across g.dir)?
                    Vec3 rel{o.pos.x - g.pos.x, 0, o.pos.z - g.pos.z};
                    double along = std::fabs(dot(rel, g.dir));
                    double lateral = std::fabs(dot(rel, Vec3{-g.dir.z, 0, g.dir.x}));
                    bool cuts = false;
                    if (along < o.radius + 15.0 && lateral < g.radius + o.radius) {
                        double off = std::max(0.0, lateral - o.radius);
                        double half = std::sqrt(std::max(0.0, g.radius * g.radius - off * off));
                        cuts = o.pos.y + o.height > g.pos.y - half;
                    }
                    CHECK_MSG(!cuts, "%s L%d gate %zu is cut by an obstacle (%.0f m along, %.0f m lateral)", id.c_str(), lv, i, along, lateral);
                    // and the gate centre (the line a pilot flies) is clear of it
                    CHECK_MSG(length(rel) > o.radius + 40.0 || g.pos.y > o.pos.y + o.height + 40.0, "%s L%d gate %zu centre too close to an obstacle", id.c_str(), lv, i);
                }
            }
            // sections chain: gate ranges in order, every section has a title
            int prevEnd = 0;
            for (const Section& s : c.sections) {
                CHECK(!s.title.empty());
                CHECK(s.gateBegin >= prevEnd || s.gateBegin == s.gateEnd);
                if (s.gateEnd > s.gateBegin) prevEnd = s.gateEnd;
            }
            if (trainingLevel(lv).needsSea) CHECK(c.carrier);
            if (lv == 18 || lv == 20) CHECK(c.tanker);
            if (lv == 16) CHECK(!c.formationRoute.empty());
            // combat levels: every engagement has something to fight and a section to fight it in
            int engage = 0;
            for (const Section& s : c.sections) if (s.kind == SectionKind::Engage) ++engage;
            CHECK_MSG(static_cast<size_t>(engage) == c.engagements.size(), "%s L%d engagements", id.c_str(), lv);
            if (lv >= 5 && lv != 16 && !(lv == 19 && !c.carrier)) CHECK_MSG(engage > 0, "%s L%d has no combat", id.c_str(), lv);
            for (const Engagement& e : c.engagements) {
                size_t n = e.jets.size() + e.ground.size() + static_cast<size_t>(e.transportHelis + e.attackHelis);
                CHECK_MSG(n > 0 || e.continueFrom >= 0, "%s L%d empty engagement", id.c_str(), lv);
                if (e.transportHelis + e.attackHelis + static_cast<int>(e.ground.size()) > 0)
                    CHECK_MSG(insideMapXZ(e.area, 1500.0), "%s L%d engagement area off the map", id.c_str(), lv);
            }
        }
    }
    std::printf("    %d courses checked\n", built);
    CHECK(built >= 14 * 18 + 3 * 2);
}

TEST(courses_are_deterministic) {
    for (const char* id : {"desert", "geiranger", "musandam"}) {
        World& w = world(id);
        for (int lv : {1, 4, 9, 14, 17, 20}) {
            Course a = buildCourse(lv, *w.terrain, *w.layout), b = buildCourse(lv, *w.terrain, *w.layout);
            CHECK(a.gates.size() == b.gates.size());
            CHECK(a.sections.size() == b.sections.size());
            CHECK(std::fabs(a.estimatedSeconds - b.estimatedSeconds) < 1e-9);
            for (size_t i = 0; i < std::min(a.gates.size(), b.gates.size()); ++i) CHECK(length(a.gates[i].pos - b.gates[i].pos) < 1e-9);
        }
    }
}

TEST(bot_flies_every_level_with_every_aircraft_in_every_mode) {
    // 20 levels x 8 aircraft x 3 handling modes on each level's recommended location, flown by the
    // autopilot bot through the real flight model: takeoff, gates, manoeuvres, formation,
    // combat (the bot hunts the targets with the permitted weapons and retries from the
    // checkpoint if it is shot down), refuelling, landing or carrier trap. SKY_QUICK=1: assisted
    // mode only. SKY_LEVELS=a-b: a range of levels.
    bool quick = std::getenv("SKY_QUICK") != nullptr;
    int runs = 0, done = 0, known = 0;
    int lvA = 1, lvB = kTrainingLevels;
    if (const char* r = std::getenv("SKY_LEVELS")) std::sscanf(r, "%d-%d", &lvA, &lvB);
    double scoreSum = 0;
    for (int lv = lvA; lv <= lvB; ++lv) {
        std::string loc = kRecommended[lv];
        World& w = world(loc);
        for (int a = 0; a < static_cast<int>(AircraftId::Count); ++a)
            for (int m = 0; m < 3; ++m) {
                if (quick && m != 1) continue;
                TrainingBot bot(w, lv, static_cast<AircraftId>(a), static_cast<HandlingMode>(m));
                bool ok = bot.run(40 * 60);
                ++runs;
                ScoreCard sc = bot.session.finalScore();
                bool isKnown = knownFailure(loc, lv, static_cast<AircraftId>(a), static_cast<HandlingMode>(m));
                if (ok) {
                    ++done;
                    scoreSum += sc.total;
                    if (isKnown) std::printf("    note: listed bot limitation now passes: %s L%d %s %s\n", loc.c_str(), lv, profile(static_cast<AircraftId>(a)).shortName, modeName(static_cast<HandlingMode>(m)));
                    if (bot.course.carrier) CHECK_MSG(bot.session.trapped(), "L%d %s %s finished without a trap", lv, profile(static_cast<AircraftId>(a)).shortName, modeName(static_cast<HandlingMode>(m)));
                    if (!bot.course.engagements.empty()) CHECK_MSG(sc.kills == sc.targets && sc.targets > 0, "L%d %s %s: %d of %d targets", lv, profile(static_cast<AircraftId>(a)).shortName, modeName(static_cast<HandlingMode>(m)), sc.kills, sc.targets);
                    else CHECK(bot.session.landed());
                    CHECK(bot.minFuelFrac > 0.05);   // never flown to empty tanks
                } else if (isKnown) {
                    ++known;
                    std::printf("    known bot limitation: %s L%d %s %s: %s\n", loc.c_str(), lv, profile(static_cast<AircraftId>(a)).shortName, modeName(static_cast<HandlingMode>(m)), bot.failNote.c_str());
                } else {
                    CHECK_MSG(ok, "%s L%d %s %s: %s", loc.c_str(), lv, profile(static_cast<AircraftId>(a)).shortName, modeName(static_cast<HandlingMode>(m)), bot.failNote.c_str());
                }
            }
    }
    std::printf("    %d/%d courses completed (%d listed bot limitations); mean score %.0f\n", done, runs, known, done ? scoreSum / done : 0.0);
}

TEST(every_training_radio_call_is_in_the_voice_vocabulary) {
    // whatever the instructor, tower, lead and tanker say while real courses are flown must be a
    // line the voice pack can contain (fixed text, no free-form numbers)
    std::set<std::string> vocab;
    for (const VoiceLine& l : trainingVoiceLines()) vocab.insert(voiceKey(l.who, l.text));
    CHECK(vocab.size() > 200);
    int missing = 0, heard = 0;
    std::set<std::string> reported;
    for (int lv = 1; lv <= kTrainingLevels; ++lv) {
        World& w = world(kRecommended[lv]);
        for (AircraftId a : {AircraftId::F16, AircraftId::Su30})
            for (HandlingMode m : {HandlingMode::Arcade, HandlingMode::Simulation}) {
                TrainingBot bot(w, lv, a, m);
                bot.run(40 * 60);
                for (const RadioCall& r : bot.calls) {
                    ++heard;
                    if (!vocab.count(voiceKey(r.who, r.text)) && reported.insert(r.text).second) {
                        ++missing;
                        if (missing <= 10) std::printf("    not in the vocabulary: \"%s\"\n", r.text.c_str());
                    }
                }
            }
    }
    std::printf("    %d calls heard, %d distinct lines missing\n", heard, missing);
    CHECK(heard > 500);
    CHECK(missing == 0);
}

TEST(academy_progress_history_without_medals_or_locks) {
    Settings s;
    const int f16 = static_cast<int>(AircraftId::F16), f22 = static_cast<int>(AircraftId::F22);
    for (int lv = 1; lv <= kTrainingLevels; ++lv) CHECK(academyUnlocked(s, lv));   // all twenty open
    CHECK(!academyUnlocked(s, kTrainingLevels + 1));
    recordAcademyAttempt(s, 1, f16);
    AcademyRecord r = academyRecord(s, 1, f16);
    CHECK(r.attempts == 1 && !r.done);
    ScoreCard sc;
    sc.total = 82;
    sc.flightSeconds = 900;
    CHECK(recordAcademyResult(s, 1, f16, sc));
    CHECK(academyCompleted(s, 1) && !academyCompleted(s, 2));
    sc.total = 40;   // a weak run still counts as completed, but is not a new best
    CHECK(!recordAcademyResult(s, 1, f16, sc));
    r = academyRecord(s, 1, f16);
    CHECK(r.done && std::fabs(r.best - 82) < 0.6 && std::fabs(r.seconds - 900) < 1);
    CHECK(academyRecord(s, 1, f22).attempts == 0);   // history is per aircraft
    Settings s2;
    s2.extra = s.extra;   // the settings file keeps these keys
    CHECK(academyRecord(s2, 1, f16).done);
    // records of the ten-level academy carry over to the level that grew out of each
    Settings old;
    old.extra["academy_l05_a7"] = "2,81.0,3,950";    // old level 5 (aircraft control) -> new level 2
    old.extra["academy_l03_a7"] = "1,66.0,2,1000";   // old 3 + 4 -> new level 4
    old.extra["academy_l04_a7"] = "0,0.0,4,0";
    AcademyRecord m2 = academyRecord(old, 2, 7), m4 = academyRecord(old, 4, 7);
    CHECK(m2.done && std::fabs(m2.best - 81) < 0.6 && m2.attempts == 3);
    CHECK(m4.done && std::fabs(m4.best - 66) < 0.6 && m4.attempts == 6);
    CHECK(!academyRecord(old, 5, 7).done);   // new combat levels start fresh
    recordAcademyAttempt(old, 2, 7);
    CHECK(academyRecord(old, 2, 7).attempts == 4 && old.extra.count("academy_l05_a7"));   // the old key stays for older builds
    // the debrief remark never mentions medals
    for (double t : {10.0, 65.0, 80.0, 95.0}) CHECK(std::string(trainingRemarkLine(t)).find("medal") == std::string::npos);
    // campaign progress is per location (the desert keeps the original record)
    CHECK(!campaignCompletedAt(s, "fuji", 7));
    markCampaignCompletedAt(s, "fuji", 7);
    CHECK(campaignCompletedAt(s, "fuji", 7));
    CHECK(!campaignCompletedAt(s, "desert", 7) && !campaignCompletedAt(s, "rio", 7));
    markCampaignCompletedAt(s, "desert", 3);
    CHECK(s.missionCompleted(3) && campaignCompletedAt(s, "desert", 3));
    CHECK(campaignCountAt(s, "fuji") == 1);
}

/// Mutable terrain of a location with (or without) the carrier deck of a mission.
Terrain& deckTerrain(World& w, const CampaignMissionDef& d) {
    if (d.carrier) w.terrain->setDeck(d.deck);
    else w.terrain->clearDeck();
    return *w.terrain;
}

TEST(campaign_is_laid_out_on_every_location) {
    // All 61 missions on every location: every target on suitable ground (ships and rigs on
    // water), every wave, start and tanker orbit clear of the terrain and on the map, the carrier
    // on open water (or an airborne start where there is none), the landing on the runway, every
    // objective group populated, every title and briefing in the voice pack, and the objective
    // logic completes.
    std::set<std::string> vocab;
    for (const VoiceLine& l : allVoiceLines()) vocab.insert(voiceKey(l.who, l.text));
    int checked = 0;
    for (const std::string& id : allLocationIds()) {
        World& w = world(id);
        const Terrain& t = *w.terrain;
        CampaignVariant v = campaignVariantFor(t, *w.layout);
        std::printf("    %-13s sea %d carrier %d rail %d climb+%.0f\n", id.c_str(), v.sea, v.carrier, v.rail, v.climbOffset);
        const RunwayInfo& rw = t.runway();
        // missions 1-3: the solo flight starts clear of the hills, the landing approach is open
        double off = mission02Offset(t);
        CHECK_MSG(1250.0 + off > maxSurfaceAround(t, rw.center.x + 2000.0, rw.center.z, 3000.0) + 300.0, "%s mission 2 starts too low", id.c_str());
        {
            // mission 3 starts on a clear glide path (runway 27 where the terrain blocks 09)
            Mission03Approach ap = mission03Approach(t);
            Vec3 th = landingThreshold(rw, ap.dir);
            double c = segmentClearance(t, {th.x - ap.dir * ap.dist, rw.center.y + glidePathHeight(ap.dist), th.z},
                                        {th.x - ap.dir * 1000.0, rw.center.y + glidePathHeight(1000.0), th.z});
            CHECK_MSG(c > 25.0, "%s mission 3: glide path only %.0f m above the terrain", id.c_str(), c);
            CHECK(v.runway27 == (ap.dir < 0));
            if (id == "desert") CHECK(ap.dir == 1 && ap.dist == 4800.0);
        }
        for (int n = 1; n <= 3; ++n)
            for (const auto& p : makeBriefing(n, &v).parts) CHECK_MSG(vocab.count(voiceKey(Speaker::Captain, p)) == 1, "%s M%d briefing not voiced", id.c_str(), n);
        for (int n = 4; n <= kCampaignMissions; ++n) {
            const CampaignMissionDef& src = *campaignMission(n);
            CampaignMissionDef d = adaptCampaignMission(src, t, *w.layout);
            const bool orig = id == "desert";   // the original layout: its geometry is the reference
            if (orig) CHECK(d.targets.size() == src.targets.size() && d.startX == src.startX && d.objectives.size() == src.objectives.size());
            CHECK(!d.objectives.empty());
            CHECK_MSG(insideMapXZ({d.startX, 0, d.startZ}, 1000.0), "%s M%d start off the map", id.c_str(), n);
            if (d.carrier) {
                CHECK_MSG(d.deck.active && t.height(d.deck.center.x, d.deck.center.z) < -6.0, "%s M%d carrier not on open water", id.c_str(), n);
            } else if (!orig) {
                CHECK_MSG(d.startAlt > maxSurfaceAround(t, d.startX, d.startZ, 1500.0) + 250.0, "%s M%d starts %.0f m above the ground", id.c_str(), n,
                          d.startAlt - t.surfaceHeight(d.startX, d.startZ));
            }
            CHECK_MSG(src.carrier == d.carrier || !v.carrier, "%s M%d lost its carrier", id.c_str(), n);
            std::set<int> groups;
            for (const TargetDef& td : d.targets) {
                if (td.anchor == TargetDef::Anchor::Railway) { CHECK(!w.layout->railway.pts.empty()); groups.insert(td.group); continue; }
                if (td.anchor == TargetDef::Anchor::Bridge) { CHECK(!w.layout->bridges.empty()); groups.insert(td.group); continue; }
                if (td.anchor == TargetDef::Anchor::Industrial) { CHECK(td.index < static_cast<int>(w.layout->factories.size())); groups.insert(td.group); continue; }
                groups.insert(td.group);
                CHECK_MSG(insideMapXZ({td.x, 0, td.z}, 800.0), "%s M%d target %s off the map", id.c_str(), n, td.label.c_str());
                double h = t.height(td.x, td.z);
                if (td.kind == GroundKind::Ship || td.kind == GroundKind::OilRig) {
                    CHECK_MSG(h < -3.0, "%s M%d %s on land", id.c_str(), n, td.label.c_str());
                    for (int k = 0; k < 8 && td.pathRadius > 0; ++k)
                        CHECK_MSG(t.isWater(td.x + std::cos(k * kPi / 4) * td.pathRadius, td.z + std::sin(k * kPi / 4) * td.pathRadius),
                                  "%s M%d %s sails onto land", id.c_str(), n, td.label.c_str());
                } else {
                    CHECK_MSG(h > 0.0 && !t.onRunway(td.x, td.z, 100.0), "%s M%d %s in the water or on the runway", id.c_str(), n, td.label.c_str());
                    CHECK_MSG(orig || h < rw.center.y + 1900.0, "%s M%d %s %.0f m above the airfield", id.c_str(), n, td.label.c_str(), h - rw.center.y);
                }
            }
            for (const ObjectiveDef& o : d.objectives) {
                if (o.kind == ObjectiveKind::DestroyGroup) CHECK_MSG(groups.count(o.group), "%s M%d group %d empty", id.c_str(), n, o.group);
                if (o.kind == ObjectiveKind::Waypoint) CHECK_MSG(insideMapXZ({o.x, 0, o.z}, 900.0), "%s M%d waypoint %s off the map", id.c_str(), n, o.title.c_str());
            }
            for (const WaveDef& wv : d.waves) {
                CHECK(insideMapXZ({wv.x, 0, wv.z}, 2000.0));
                CHECK_MSG(orig || (wv.altitude > maxSurfaceAround(t, wv.x, wv.z, wv.radius) + 300.0 && wv.altitude < 12500.0), "%s M%d wave at %.0f m", id.c_str(), n, wv.altitude);
            }
            if (d.tanker) {
                double top = -1e9;
                for (int a = 0; a < 72; ++a)
                    top = std::max(top, t.surfaceHeight(d.tankerX + std::cos(a * kPi / 36) * d.tankerRadius, d.tankerZ + std::sin(a * kPi / 36) * d.tankerRadius));
                CHECK_MSG(d.tankerAlt > top + 600.0 && d.tankerAlt < 11500.0, "%s M%d tanker orbit %.0f m above the ground", id.c_str(), n, d.tankerAlt - top);
                CHECK(insideMapXZ({d.tankerX, 0, d.tankerZ}, d.tankerRadius + 1000.0));
            }
            for (const ObjectiveDef& o : d.objectives)
                if (o.kind == ObjectiveKind::Touchdown) CHECK_MSG(t.onRunway(d.homeX, d.homeZ, 10.0), "%s M%d landing not on the runway", id.c_str(), n);
            // titles and briefing in the voice pack
            Mission m = buildCampaignMission(d);
            for (size_t i = 1; i < m.steps().size(); ++i)
                if (!(d.carrier && m.steps()[i].title == "GEAR UP"))
                    CHECK_MSG(vocab.count(voiceKey(Speaker::Awacs, nextObjectiveLine(m.steps()[i].title))) == 1, "%s M%d '%s' not voiced", id.c_str(), n, m.steps()[i].title.c_str());
            for (const auto& p : makeBriefing(n, &v).parts) CHECK_MSG(vocab.count(voiceKey(Speaker::Captain, p)) == 1, "%s M%d briefing not voiced", id.c_str(), n);
            // the objective logic completes
            CampaignPlaythrough pt(d, deckTerrain(w, d), w.layout.get());
            bool ok = pt.run(500.0);
            CHECK_MSG(ok, "%s M%d stuck at objective %zu (%s)", id.c_str(), n, pt.mission.stepIndex() + 1, pt.mission.current() ? pt.mission.current()->title.c_str() : "-");
            w.terrain->clearDeck();
            ++checked;
        }
    }
    std::printf("    %d missions checked\n", checked);
}

TEST(campaign_is_winnable_over_other_locations) {
    // The combat pilot that proves the desert campaign flies missions 4-61 over other locations
    // (enemy AI fully enabled), assisted mode, F-22, up to 3 attempts each, and is compared with
    // the same pilot in the Skyfall Desert: the bot is not a perfect pilot (it cannot fly the
    // aerobatics of mission 5 and loses some of the big dogfights even in the desert), so the
    // test counts the missions it wins in the desert but loses on the location. By default the
    // desert and a sample (a high Alpine map without sea, a coastal map without a railway, the
    // highest Academy map); SKY_CAMP_ALL=1 flies every location, SKY_CAMP_LOC=<id> one more.
    const char* one = std::getenv("SKY_CAMP_LOC");
    std::vector<std::string> ids = std::getenv("SKY_CAMP_ALL") ? allLocationIds()
                                 : std::vector<std::string>{"desert", "matterhorn", "musandam", "aconcagua"};
    if (one && std::string(one) != "desert") ids = {"desert", one};
    std::set<int> desertWins;
    int won = 0, flown = 0;
    for (const std::string& id : ids) {
        World& w = world(id);
        std::printf("    %-13s", id.c_str());
        std::vector<int> extra;
        int locWon = 0;
        for (int n = 4; n <= kCampaignMissions; ++n) {
            CampaignMissionDef d = adaptCampaignMission(*campaignMission(n), *w.terrain, *w.layout);
            Terrain& t = deckTerrain(w, d);
            int wins = 0;
            for (unsigned seed = 0; seed < 3 && wins == 0; ++seed) {
                CombatBot bot(d, HandlingMode::Assisted, t, w.layout.get(), -1, seed);
                if (bot.run(900.0)) ++wins;
                else if (std::getenv("SKY_CAMP_VERBOSE")) {
                    const FlightState& fs = bot.battle.player().fm->state();
                    std::printf("\n      M%d seed %u lost at %s t=%.0f hull %.0f crash %d (%s) pos %.0f %.0f %.0f agl %.0f", n, seed,
                                bot.mission.current() ? bot.mission.current()->title.c_str() : "-", bot.time, bot.battle.player().health, fs.crashed ? 1 : 0,
                                crashReasonText(fs.crashReason), fs.pos.x, fs.pos.y, fs.pos.z, fs.agl);
                }
            }
            w.terrain->clearDeck();
            ++flown;
            won += wins > 0;
            locWon += wins > 0;
            if (id == "desert" && wins) desertWins.insert(n);
            if (id != "desert" && !wins && desertWins.count(n)) extra.push_back(n);
            std::printf("%s", wins ? "." : "X");
            std::fflush(stdout);
        }
        std::printf("  %d/58", locWon);
        if (!extra.empty()) {
            std::printf("  lost beyond the desert:");
            for (int n : extra) std::printf(" %d", n);
        }
        std::printf("\n");
        if (id == "desert") continue;
        // Everest (5-8.6 km terrain, not offered in the Academy) has its ground attacks between
        // the highest peaks on Earth; listed in the README
        const size_t allowed = id == "everest" ? 8 : 4;
        CHECK_MSG(extra.size() <= allowed, "%s: %zu missions lost that the bot wins in the desert", id.c_str(), extra.size());
    }
    std::printf("    %d/%d missions won\n", won, flown);
}

TEST(world_detail_is_populated_on_every_location) {
    // Every map gets the army compound, forward bases, street lights, traffic and night lights;
    // nothing stands on water, the runway or in front of an Academy gate.
    auto tall = [](PropKind k) {
        return k == PropKind::Crane || k == PropKind::RadarMast || k == PropKind::Antenna || k == PropKind::FloodMast ||
               k == PropKind::WaterTower || k == PropKind::Watchtower || k == PropKind::Billboard;
    };
    for (const std::string& id : allLocationIds()) {
        World& w = world(id);
        const Terrain& t = *w.terrain;
        const WorldLayout& L = *w.layout;
        const RunwayInfo& rw = t.runway();
        int fobs = 0, helis = 0, lamps = 0, armour = 0;
        for (const MilitarySite& m : L.sites) if (!m.airbase) ++fobs;
        for (const SceneProp& p : L.props) {
            if (p.kind == PropKind::Helicopter || p.kind == PropKind::AttackHelicopter) ++helis;
            if (p.kind == PropKind::Streetlamp) ++lamps;
            if (p.kind == PropKind::Tank || p.kind == PropKind::Apc) ++armour;
            bool onRunway = std::fabs(p.pos.x - rw.center.x) < rw.length * 0.5 + 60 && std::fabs(p.pos.z - rw.center.z) < rw.width * 0.5 + 60;
            CHECK_MSG(!onRunway, "%s prop %d on the runway at (%.0f %.0f)", id.c_str(), (int)p.kind, p.pos.x, p.pos.z);
            if (p.kind != PropKind::Fence && p.kind != PropKind::Hesco)
                CHECK_MSG(t.height(p.pos.x, p.pos.z) > -0.5, "%s prop %d in the sea at (%.0f %.0f)", id.c_str(), (int)p.kind, p.pos.x, p.pos.z);
        }
        std::printf("    %-13s props %5zu lights %5zu cars %3zu lanes %2zu sites %zu (FOB %d) helis %d armour %d lamps %d\n", id.c_str(),
                    L.props.size(), L.lights.size(), L.cars.size(), L.lanes.size(), L.sites.size(), fobs, helis, armour, lamps);
        CHECK_MSG(L.sites.size() >= 1 && L.sites[0].airbase, "%s has no army compound", id.c_str());
        CHECK_MSG(helis >= 3 && armour >= 6, "%s compound is empty", id.c_str());
        CHECK_MSG(fobs >= 1, "%s has no forward base", id.c_str());
        CHECK_MSG(!L.cars.empty() && L.lights.size() > 40, "%s has no traffic or lights", id.c_str());
        for (const Car& c : L.cars) {
            Vec3 f;
            Vec3 p = L.carAt(c, 123.4, &f);
            CHECK(std::fabs(length(f) - 1.0) < 1e-6);
            CHECK(std::fabs(p.x) < Terrain::kHalfSize && std::fabs(p.z) < Terrain::kHalfSize);
        }
        // tall props never stand in the flight path between consecutive Academy gates
        auto topOf = [](PropKind k) {
            switch (k) {
                case PropKind::Crane: return 44.0; case PropKind::Antenna: return 31.0; case PropKind::RadarMast: return 26.0;
                case PropKind::WaterTower: return 21.0; case PropKind::FloodMast: return 18.0; default: return 11.0;
            }
        };
        for (int lv = 1; lv <= kTrainingLevels; ++lv) {
            Course c = buildCourse(lv, t, L);
            for (size_t gi = 0; gi + 1 < c.gates.size(); ++gi) {
                Vec3 a = c.gates[gi].pos, b = c.gates[gi + 1].pos;
                double len = std::hypot(b.x - a.x, b.z - a.z);
                for (const SceneProp& p : L.props) {
                    if (!tall(p.kind)) continue;
                    for (double u = 0; u <= len; u += 20) {
                        Vec3 q = a + (b - a) * (len > 0 ? u / len : 0.0);
                        double d = std::hypot(q.x - p.pos.x, q.z - p.pos.z);
                        if (d < 45.0 && q.y < p.pos.y + topOf(p.kind) + 30.0) {
                            CHECK_MSG(false, "%s level %d: prop %d in the path of gate %zu (%.0f m, %.0f m above)", id.c_str(), lv, (int)p.kind, gi, d, q.y - p.pos.y);
                            break;
                        }
                    }
                }
            }
        }
    }
}

TEST(map_store_installs_verifies_and_repairs) {
    // SHA-256 against the FIPS test vectors
    const std::string abc = "abc";
    CHECK(sha256Hex(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(nullptr, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha256Hex(reinterpret_cast<const std::uint8_t*>(two.data()), two.size()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // every map shipped with the game matches its recorded checksum
    for (const std::string& id : allLocationIds()) {
        if (id == "desert") continue;
        std::string p = std::string(SKYFALL_ASSET_DIR) + "/terrain/" + id + ".skt";
        CHECK_MSG(sha256File(p) == bundledMapChecksum(id), "%s: bundled map checksum", id.c_str());
        std::string err;
        CHECK_MSG(validMapFile(p, err), "%s: %s", id.c_str(), err.c_str());
    }
    // a store in a scratch folder: install, survive a restart, detect damage, repair
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("skyfall_maps_test_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    {
        MapStore st(dir.string());
        CHECK(st.check("fuji") == MapStore::State::Missing);
        std::string err;
        bool verified = false;
        CHECK(st.install("fuji", std::string(SKYFALL_ASSET_DIR) + "/terrain/fuji.skt", "bundled", err, &verified));
        CHECK(verified);
        CHECK(st.check("fuji", true) == MapStore::State::Valid);
        // a file that is not a map is refused
        std::ofstream(dir / "junk.skt") << "not a map";
        CHECK(!st.install("rio", (dir / "junk.skt").string(), "download", err));
        CHECK(st.check("rio") == MapStore::State::Missing);
    }
    {
        MapStore st(dir.string());   // a new session: the manifest is read back
        CHECK(st.check("fuji", true) == MapStore::State::Valid);
        // flip one byte: the deep check sees it, the size check does not need to
        {
            std::fstream f(st.pathFor("fuji"), std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(5000);
            char c = 0x55;
            f.write(&c, 1);
        }
        CHECK(st.check("fuji", false) == MapStore::State::Valid);
        CHECK(st.check("fuji", true) == MapStore::State::Corrupt);
        // truncation is caught without hashing
        fs::resize_file(st.pathFor("fuji"), 1000);
        CHECK(st.check("fuji", false) == MapStore::State::Corrupt);
        // repair = reinstall
        st.remove("fuji");
        std::string err;
        CHECK(st.install("fuji", std::string(SKYFALL_ASSET_DIR) + "/terrain/fuji.skt", "bundled", err));
        CHECK(st.check("fuji", true) == MapStore::State::Valid);
        // another map is added without touching the first
        CHECK(st.install("rio", std::string(SKYFALL_ASSET_DIR) + "/terrain/rio.skt", "bundled", err));
        CHECK(st.entries().size() == 2 && st.check("fuji", true) == MapStore::State::Valid);
    }
    fs::remove_all(dir);
}

TEST(bot_sweep_over_every_location) {
    // report only (SKY_SWEEP=<aircraft index>,<mode>): every level on every location
    const char* e = std::getenv("SKY_SWEEP");
    if (!e) { std::printf("    (set SKY_SWEEP=ac,mode to run)\n"); return; }
    int ac = std::atoi(e), md = std::strchr(e, ',') ? std::atoi(std::strchr(e, ',') + 1) : 1;
    int ok = 0, n = 0;
    for (const std::string& id : allLocationIds())
        for (int lv = 1; lv <= kTrainingLevels; ++lv) {
            if (!academyLocationAllowed(id, lv)) continue;
            TrainingBot bot(world(id), lv, static_cast<AircraftId>(ac), static_cast<HandlingMode>(md));
            bool r = bot.run(40 * 60);
            ++n; ok += r;
            if (!r) std::printf("    %s L%d: %s\n", id.c_str(), lv, bot.failNote.c_str());
        }
    std::printf("    %d/%d\n", ok, n);
}
