// Shared by the test programs: a combat pilot that flies campaign missions for real.
#pragma once

#include "aircraft/AircraftProfile.hpp"
#include "combat/Combat.hpp"
#include "mission/Campaign.hpp"
#include "mission/Mission.hpp"
#include "sim/FlightModel.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sky {
/// A competent but human-like combat pilot for the hard campaign missions: it really flies (stick
/// and throttle through the flight model, in any handling mode), locks and fires its own weapons,
/// beams incoming missiles with flares and dogfights the enemy AI, which is fully enabled. It is
/// how the tests prove that every mission can be won without cheating.
struct CombatBot {
    const CampaignMissionDef& def;
    const Terrain& T;
    Battle battle;
    Mission mission;
    CampaignDirector director;
    bool released = false;
    double reattack = 0;      // > 0: breaking away after a ground attack pass
    int prevMissileWarn = 0;
    double time = 0;
    double dmgAir = 0, dmgGround = 0;
    CombatBot(const CampaignMissionDef& d, HandlingMode mode, const Terrain& terrain_, const WorldLayout* layout_, int tacticsOverride = -1, unsigned seed = 0,
              AircraftId aircraft = AircraftId::F22)
        : def(d), T(terrain_), battle(T), mission(buildCampaignMission(d)) {
        if (layout_) battle.setLayout(layout_);
        Jet& p = battle.createPlayer(aircraft);
        p.fm->setHandling(mode);
        if (d.carrier) p.fm->resetOnCatapult();
        else p.fm->resetAirborne({d.startX, d.startAlt, d.startZ}, campaignStartHeading(d), d.startSpeed, 0.8);
        if (d.startFuel >= 0) p.fm->mutableState().fuelKg = d.startFuel * p.fm->profile().fuelMassKg;
        CampaignMissionDef* mut = const_cast<CampaignMissionDef*>(&d);
        int saved = mut->tactics;
        if (tacticsOverride >= 0) mut->tactics = tacticsOverride;
        director.begin(d, battle, seed);
        mut->tactics = saved;
        director.update(mission, battle);
    }
    // bank-to-turn: the same control law the AI pilots use
    static ControlInputs steer(const FlightState& s, Vec3 desired, double desiredSpeed) {
        ControlInputs in;
        Euler e = toEuler(s.q);
        double V = std::max(s.airspeed, 1.0);
        Vec3 db = s.q.unrotate(normalize(desired));
        double angle = std::acos(clampd(db.x, -1.0, 1.0));
        double rollErr = std::atan2(db.z, db.y);
        double fine = smoothstep(2.0 * kDeg, 14.0 * kDeg, angle);
        in.roll = clampd(rollErr * 1.8 * fine - e.bank * 1.2 * (1.0 - fine), -1.0, 1.0);
        double align = std::cos(rollErr);
        double pull = angle * 3.5;
        if (fine < 1.0) pull = db.y * 9.0;
        in.pitch = clampd(pull * (align > 0.2 ? 1.0 : 0.15), -0.6, 1.0);
        in.yaw = clampd(db.z * 3.0, -1.0, 1.0) * (1.0 - fine * 0.7);
        double lever = V < desiredSpeed - 25 ? 1.0 : (V > desiredSpeed + 30 ? 0.55 : 0.85);
        in.throttleRate = clampd((lever - s.throttle) * 6.0, -1.0, 1.0);
        in.airbrake = V > desiredSpeed + 120;
        return in;
    }
    /// Station keeping on a moving point (the tanker basket): PD on position and velocity.
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
    double clearance(const FlightState& s) const {
        double c = 1e9;
        for (double t : {0.0, 1.5, 3.0, 5.0}) {
            Vec3 q = s.pos + s.vel * t;
            c = std::min(c, q.y - T.surfaceHeight(q.x, q.z));
        }
        return c;
    }
    const Jet* nearestEnemy(double maxD) const {
        const Jet* best = nullptr;
        double bd = maxD;
        const Vec3 pp = battle.player().fm->state().pos;
        for (const auto& j : battle.jets())
            if (j->team == Team::Red && j->alive) {
                double d = length(j->fm->state().pos - pp);
                if (d < bd) { bd = d; best = j.get(); }
            }
        return best;
    }
    void step() {
        Jet& p = battle.player();
        FlightModel& fm = *p.fm;
        const FlightState& s = fm.state();
        const ObjectiveDef& o = def.objectives[std::min(mission.stepIndex(), def.objectives.size() - 1)];
        ControlInputs in;
        PlayerCommands cmd;
        Vec3 fwd = s.q.forward();
        Vec3 desired = fwd;
        double speed = 250;
        bool manual = false;
        // ---- carrier launch, flown for real
        if (o.kind == ObjectiveKind::FullPower || o.kind == ObjectiveKind::Launch || (s.onGround && s.catArmed)) {
            in.throttleRate = 1.0;
            if (!released && s.throttle >= FlightModel::kAbDetent && fm.state().abLevel > 0.6) { fm.setParkingBrake(false); released = true; }
            manual = true;
        } else if (s.gearDown && !s.onGround && s.agl > 40) {
            fm.toggleGear();
        }
        if (o.kind == ObjectiveKind::Rendezvous || o.kind == ObjectiveKind::Refuel) {
            // tanker join from astern (lag pursuit), then fly the probe into the basket
            const Tanker& tk = battle.tanker();
            Vec3 f{std::cos(tk.heading), 0, std::sin(tk.heading)};
            if (o.kind == ObjectiveKind::Rendezvous) {
                double dist = length(tk.pos - s.pos);
                double ahead = dot(s.pos - tk.pos, f);
                Vec3 target = ahead > -300.0 && dist < 6000.0 ? tk.pos - f * 2500.0 : tk.pos - f * 500.0 - Vec3{0, 10, 0} + tk.vel * clampd(dist / 250.0, 0.0, 40.0);
                double spd = tk.speed + clampd(dist * 0.03, 10.0, 100.0);
                Vec3 dir = normalize(target - s.pos);
                if (clearance(s) < 400.0 || s.agl < 400.0) dir = normalize(Vec3{dir.x, 0, dir.z} * 0.6 + Vec3{0, 1.0, 0});   // terrain first
                in = steer(s, dir, spd);
                in.airbrake = s.airspeed > spd + 15.0;
            } else {
                Vec3 off = s.q.forward() * 8.0 + s.q.up() * 1.0;
                Vec3 rel = tk.basket() - (s.pos + off);
                double along = dot(rel, f);
                Vec3 lateral = rel - f * along;
                Vec3 toCentre = normalize(Vec3{tk.center.x - tk.pos.x, 0, tk.center.z - tk.pos.z});
                Vec3 acc = toCentre * (tk.speed * tk.speed / tk.radius);
                double stage = (length(lateral) < 2.5 && along < 40.0) ? 0.0 : -25.0;
                in = stationKeep(s, tk.basket() + f * stage, tk.vel, acc, off);
            }
            manual = true;
        }
        if (o.kind == ObjectiveKind::Touchdown) { if (!s.onGround) fm.resetOnRunway(false); manual = true; }
        if (o.kind == ObjectiveKind::Stop) { in.throttleRate = -1.0; in.brake = 1.0; manual = true; }
        if (!manual) {
            const bool groundPhase = o.kind == ObjectiveKind::DestroyGroup;
            const Jet* bandit = nearestEnemy(o.kind == ObjectiveKind::DestroyAir ? 1e9 : (groundPhase ? 4500.0 : 7000.0));
            double agl = s.agl;
            if (b_missileThreat()) {
                // beam the missile and dispense flares
                Vec3 los = battle.nearestThreatDir();
                Vec3 side = normalize(cross(los, Vec3{0, 1, 0}));
                if (dot(side, s.q.right()) < 0) side = -side;
                desired = normalize(side + Vec3{0, agl > 1500 ? -0.2 : 0.15, 0});
                speed = 400;
                if (battle.nearestThreatDistance() < 2000.0 && p.flareCharges > 0 && p.flareBurstTimer <= 0) cmd.flares = true;
            } else if (const Jet* six = onMySix()) {
                // guns defence: break hard towards the attacker and keep the turn tight
                Vec3 rel = six->fm->state().pos - s.pos;
                Vec3 r = s.q.right();
                Vec3 hr = normalize(Vec3{r.x, 0, r.z});
                double side = dot(rel, r) >= 0 ? 1.0 : -1.0;
                desired = normalize(hr * (1.5 * side) + fwd * 0.3 + Vec3{0, agl > 1200 ? -0.1 : 0.15, 0});
                speed = 300;
                if (six->lockTarget == p.id && six->lockProgress > 0.7 && p.flareCharges > 0 && p.flareBurstTimer <= 0) cmd.flares = true;
            } else if (bandit) {
                p.weaponAG = false;
                const FlightState& ts = bandit->fm->state();
                Vec3 d = ts.pos - s.pos;
                double dist = length(d);
                double tLead = clampd(dist / 1300.0, 0.0, 1.6);
                Vec3 aim = ts.pos + ts.vel * tLead;
                desired = normalize(aim - s.pos);
                speed = dist > 3000 ? 320 : 260;
                // no head-on gun duels: if his nose is on us inside 1.8 km, step out of the line of fire
                Vec3 u = normalize(s.pos - ts.pos);
                if (dist < 1800.0 && dist > 300.0 && dot(ts.q.forward(), u) > std::cos(20.0 * kDeg) && dot(fwd, -u) > std::cos(30.0 * kDeg)) {
                    Vec3 r = s.q.right();
                    desired = normalize(desired + Vec3{r.x, 0, r.z} * 0.8 + Vec3{0, 0.25, 0});
                }
                double off = std::acos(clampd(dot(normalize(aim - s.pos), fwd), -1.0, 1.0));
                cmd.fireGun = dist < 900.0 && off < 3.0 * kDeg;
                if (p.lockProgress >= 1.0 && p.lockTarget == bandit->id && p.missiles > 0 && p.missileCooldown <= 0 && dist < 5000 && dist > 400)
                    cmd.fireMissile = true;
            } else if (groundPhase) {
                p.weaponAG = true;
                // like a careful player: suppress the guns and SAMs that cover the objective
                // first, launch from range and turn away before the flak can track
                std::vector<int> objective, defences;
                for (int i = 0; i < static_cast<int>(battle.ground().size()); ++i) {
                    const GroundTarget& g = battle.ground()[i];
                    if (g.alive && g.group == o.group) objective.push_back(i);
                }
                for (int i = 0; i < static_cast<int>(battle.ground().size()); ++i) {
                    const GroundTarget& g = battle.ground()[i];
                    if (!g.alive || g.team != Team::Red || !g.armed || !groundShootsAir(g.kind) || g.group == o.group) continue;
                    for (int k : objective)
                        if (length(battle.ground()[k].pos - g.pos) < 3500.0) { defences.push_back(i); break; }
                }
                auto nearest = [&](const std::vector<int>& v) {
                    int best = -1;
                    double bd = 1e18;
                    for (int i : v) {
                        double dd = length(battle.ground()[i].pos - s.pos);
                        if (dd < bd) { bd = dd; best = i; }
                    }
                    return best;
                };
                int gi = defences.empty() ? nearest(objective) : nearest(defences);
                if (gi >= 0) {
                    Vec3 tp = battle.groundAimPoint(gi);
                    double hd = std::hypot(tp.x - s.pos.x, tp.z - s.pos.z);
                    bool threat = groundShootsAir(battle.ground()[gi].kind) && battle.ground()[gi].armed;
                    reattack = std::max(0.0, reattack - FlightModel::kDt);
                    bool gunRun = p.agms == 0 && !threat && defences.empty();
                    if (reattack > 0) {
                        // pull off, climbing, then come round again
                        Vec3 away = normalize(Vec3{s.pos.x - tp.x, 0, s.pos.z - tp.z});
                        desired = normalize(away + Vec3{0, 0.3, 0});
                        speed = 330;
                    } else if (p.agms == 0 && !gunRun) {
                        // missiles re-arming: orbit outside the defences
                        Vec3 r = normalize(Vec3{s.pos.x - tp.x, 0, s.pos.z - tp.z});
                        Vec3 tangent{-r.z, 0, r.x};
                        desired = normalize(tangent + r * ((6000.0 - hd) / 3000.0) + Vec3{0, (tp.y + 1500.0 - s.pos.y) / 3000.0, 0});
                        speed = 280;
                    } else {
                        Vec3 cruise{tp.x, std::max(tp.y + 1400.0, s.pos.y), tp.z};
                        // run in high (lock cone is wide), only the cannon pass goes low
                        Vec3 runIn = gunRun ? tp : Vec3{tp.x, tp.y + std::max(500.0, hd * 0.22), tp.z};
                        desired = hd > 7000 ? normalize(cruise - s.pos) : normalize(runIn - s.pos);
                        speed = 280;
                        if (p.lockProgress >= 1.0 && p.lockTarget <= -1000 && p.agms > 0 && p.missileCooldown <= 0) {
                            cmd.fireMissile = true;
                            if (hd < 5000.0) reattack = 6.0;
                        }
                        double off = std::acos(clampd(dot(normalize(tp - s.pos), fwd), -1.0, 1.0));
                        if (gunRun && hd < 1600 && off < 3.0 * kDeg) cmd.fireGun = true;
                        if (hd < (gunRun ? 900.0 : 2500.0)) reattack = 8.0;
                    }
                }
            } else if (o.kind == ObjectiveKind::Waypoint) {
                // transit high, descend only for the last few kilometres of a low pass
                double toGo = std::hypot(o.x - s.pos.x, o.z - s.pos.z);
                double y = T.surfaceHeight(o.x, o.z) + (o.maxAgl > 0 && toGo < 4000.0 ? o.maxAgl * 0.5 : 1500.0);
                desired = normalize(Vec3{o.x, y, o.z} - s.pos);
                speed = 300;
            } else if (o.kind == ObjectiveKind::Climb) {
                desired = normalize(Vec3{fwd.x, 0, fwd.z} + Vec3{0, 0.35, 0});
                speed = 260;
            } else {
                desired = normalize(Vec3{fwd.x, 0, fwd.z});
            }
            // keep out of the ground, the sea and the low cloud of flak
            double minAgl = (o.kind == ObjectiveKind::Waypoint && o.maxAgl > 0 && std::hypot(o.x - s.pos.x, o.z - s.pos.z) < 4000.0) ? 200.0 : 450.0;
            if (clearance(s) < minAgl || agl < minAgl) desired = normalize(Vec3{desired.x, 0, desired.z} * 0.6 + Vec3{0, 1.0, 0});
            in = steer(s, desired, speed);
            if (s.throttle > 0.995 && speed < 380) in.throttleRate = std::min(in.throttleRate, 0.0);
        }
        battle.update(FlightModel::kDt, in, cmd);
        for (const CombatEvent& e : battle.drainEvents())
            if (e.type == EventType::PlayerHit) (e.a >= 0 ? dmgAir : dmgGround) += e.size;
        MissionContext ctx{&battle.player().fm->state(), &battle.player().fm->profile(), &T, 0, &battle};
        mission.update(ctx, FlightModel::kDt);
        director.update(mission, battle);
        time += FlightModel::kDt;
    }
    const Jet* onMySix() const {
        const FlightState& s = battle.player().fm->state();
        for (const auto& j : battle.jets()) {
            if (j->team != Team::Red || !j->alive) continue;
            Vec3 rel = s.pos - j->fm->state().pos;
            double d = length(rel);
            if (d > 1300.0 || d < 1.0) continue;
            Vec3 u = rel * (1.0 / d);
            if (dot(j->fm->state().q.forward(), u) > std::cos(15.0 * kDeg) && dot(s.q.forward(), u) > std::cos(60.0 * kDeg)) return j.get();
        }
        return nullptr;
    }
    bool b_missileThreat() const { return battle.playerMissileWarning() && battle.nearestThreatDistance() < 3200.0; }
    bool run(double maxSeconds) {
        const int steps = static_cast<int>(maxSeconds * 120);
        for (int i = 0; i < steps && mission.status() == MissionStatus::Running; ++i) {
            step();
            if (!battle.player().alive || battle.player().fm->state().crashed) return false;
        }
        return mission.status() == MissionStatus::Complete;
    }
};

}  // namespace sky
