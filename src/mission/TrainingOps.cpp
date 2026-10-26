// TRAINING ACADEMY OPERATIONS — protected assets, allied wingmen and the operation radio.
//
// An operation protects something: a C-17 flying a scripted path (takeoff roll, climb, cruise,
// airdrop, approach, landing roll, unloading), a convoy driving a road, rescue helicopters flying
// to a pick-up and back, or installations at a base, ships of the fleet or troops in the field.
// The attackers of a wave go for the asset (escort target, strike aircraft, hunting attack
// helicopters); losing more than the asset's loss limit fails the operation. Checkpoints keep
// the asset's state so a retry resumes with it as it was.
#include "Training.hpp"

#include <algorithm>
#include <cmath>

namespace sky {

namespace ops_ {
// wingmen
const char* kWingJoin = "Two's joined, fighting wing on your right.";
const char* kWingLost = "Two is going down! I'm out!";
const char* kWingHit = "Two is hit, still with you.";
// the picture (AWACS): where the next wave comes from
const char* kPicture[8] = {"Picture: bandits from the north.", "Picture: bandits from the north-east.", "Picture: bandits from the east.",
                           "Picture: bandits from the south-east.", "Picture: bandits from the south.", "Picture: bandits from the south-west.",
                           "Picture: bandits from the west.", "Picture: bandits from the north-west."};
const char* kHelos[8] = {"Helicopters inbound from the north.", "Helicopters inbound from the north-east.", "Helicopters inbound from the east.",
                         "Helicopters inbound from the south-east.", "Helicopters inbound from the south.", "Helicopters inbound from the south-west.",
                         "Helicopters inbound from the west.", "Helicopters inbound from the north-west."};
const char* kSplit = "Two groups, splitting to bracket you. Watch both sides.";
const char* kScramble = "They're scrambling fighters from their base. Expect company.";
const char* kStrikers = "Strike aircraft in the group, they're going for our ground forces.";
const char* kGoingForAsset = "Bandits are going for the transport! Get between them.";
// transport
const char* kTransportRolling = "REACH two one rolling. We'll need cover on the climb out.";
const char* kTransportInbound = "REACH two one, Globemaster inbound with critical supplies. Glad to have you with us.";
const char* kTransportHit = "REACH is hit! Still flying, keep them off us!";
const char* kTransportBadlyHit = "REACH, we've lost an engine! We can't take another one!";
const char* kTransportFinal = "REACH on final, gear down. Keep the airfield clear.";
const char* kTransportDown = "REACH on the ground. Ramp coming down, unloading now.";
const char* kTransportDelivered = "Supplies are off the ramp. Thanks for the escort.";
const char* kTransportDropSoon = "One minute to the drop zone. Ramp coming open.";
const char* kTransportDrop = "Green light! Pallets away.";
const char* kTransportDropDone = "Drop complete. Turning for home.";
const char* kTransportHome = "REACH leaving the area. Thanks for the cover.";
const char* kTransportLost = "REACH is going down! We've lost the transport!";
// convoy
const char* kConvoyRolling = "Convoy rolling. Keep the sky clear over the road.";
const char* kConvoyHit = "Convoy taking fire!";
const char* kConvoyLost = "We lost a vehicle!";
const char* kConvoyArrived = "Convoy has arrived. Good work up there.";
// rescue
const char* kRescueOut = "Rescue helicopters outbound to the downed pilot. Low and fast.";
const char* kRescuePickup = "Rescue on the pick-up. Give us forty-five seconds.";
const char* kRescueAboard = "Survivor aboard! Heading home.";
const char* kRescueHit = "Rescue helicopter taking fire!";
const char* kRescueLost = "We lost a rescue helicopter!";
const char* kRescueHome = "Rescue helicopters are home. Survivor safe.";
// base, fleet, troops
const char* kBaseAttack = "The base is under attack! All fighters, defend the airfield!";
const char* kBaseHit = "We're taking hits on the airfield!";
const char* kFleetAttack = "Attackers inbound on the fleet!";
const char* kFleetHit = "A ship is hit!";
const char* kTroopsAttack = "Our troops are under attack from the air!";
const char* kTroopsHit = "We lost a vehicle down here!";

int sector8(Vec3 from, Vec3 to) {
    // compass sector of `to` as seen from `from` (0 = north = -Z, clockwise; +X = east)
    double bearing = std::atan2(to.x - from.x, -(to.z - from.z));
    int s = static_cast<int>(std::lround(bearing / (kPi / 4.0)));
    return ((s % 8) + 8) % 8;
}
}  // namespace ops_

std::vector<VoiceLine> trainingOpsLines() {
    using namespace ops_;
    std::vector<VoiceLine> v;
    for (const char* t : {kSplit, kScramble, kStrikers, kGoingForAsset}) v.push_back({Speaker::Awacs, t});
    for (int i = 0; i < 8; ++i) { v.push_back({Speaker::Awacs, kPicture[i]}); v.push_back({Speaker::Awacs, kHelos[i]}); }
    for (const char* t : {kWingJoin, kWingLost, kWingHit,
                          kTransportRolling, kTransportInbound, kTransportHit, kTransportBadlyHit, kTransportFinal, kTransportDown,
                          kTransportDelivered, kTransportDropSoon, kTransportDrop, kTransportDropDone, kTransportHome, kTransportLost,
                          kConvoyRolling, kConvoyHit, kConvoyLost, kConvoyArrived,
                          kRescueOut, kRescuePickup, kRescueAboard, kRescueHit, kRescueLost, kRescueHome,
                          kBaseAttack, kBaseHit, kFleetAttack, kFleetHit, kTroopsAttack, kTroopsHit})
        v.push_back({Speaker::Wingman, t});
    return v;
}

std::string pictureCall(Vec3 from, Vec3 to, bool helicopters) {
    int s = ops_::sector8(from, to);
    return helicopters ? ops_::kHelos[s] : ops_::kPicture[s];
}

namespace {
/// Point at path distance s (3D distances) and the unit tangent there.
Vec3 pathAt(const std::vector<Vec3>& P, double s, Vec3* tangent = nullptr) {
    if (P.empty()) return {};
    if (P.size() == 1) { if (tangent) *tangent = {1, 0, 0}; return P[0]; }
    double rest = std::max(0.0, s);
    for (size_t i = 1; i < P.size(); ++i) {
        double L = length(P[i] - P[i - 1]);
        if (rest <= L || i + 1 == P.size()) {
            double t = L > 1e-6 ? clampd(rest / L, 0.0, 1.0) : 1.0;
            if (tangent) *tangent = L > 1e-6 ? (P[i] - P[i - 1]) / L : Vec3{1, 0, 0};
            return P[i - 1] + (P[i] - P[i - 1]) * t;
        }
        rest -= L;
    }
    return P.back();
}
double pathLength(const std::vector<Vec3>& P) {
    double L = 0;
    for (size_t i = 1; i < P.size(); ++i) L += length(P[i] - P[i - 1]);
    return L;
}
/// Ground placements around `c` within `radius` on flat dry ground (or water for ships), clear of
/// the runway and of each other.
Vec3 placeUnit(const Terrain& t, Vec3 c, double radius, int k, int n, bool naval, const std::vector<Vec3>& taken) {
    const RunwayInfo& rw = t.runway();
    for (int tries = 0; tries < 60; ++tries) {
        double a = 2.0 * kPi * (static_cast<double>(k) / std::max(1, n)) + tries * 0.61;
        double r = radius * (0.35 + 0.65 * ((k * 37 + tries * 13) % 17) / 16.0) + tries * 8.0;
        Vec3 q{c.x + std::cos(a) * r, 0, c.z + std::sin(a) * r};
        if (std::fabs(q.x) > Terrain::kHalfSize - 600 || std::fabs(q.z) > Terrain::kHalfSize - 600) continue;
        double h = t.height(q.x, q.z);
        bool ok = naval ? h < -5.0 : (h > 1.0 && t.normal(q.x, q.z, 10.0).y > 0.93 && !t.onRunway(q.x, q.z, 90.0));
        for (const Vec3& o : taken) if (std::hypot(o.x - q.x, o.z - q.z) < (naval ? 400.0 : 70.0)) ok = false;
        (void)rw;
        if (ok) { q.y = naval ? 0.0 : h; return q; }
    }
    return c;
}
}  // namespace

double TrainingSession::transportSpeedAt(const Asset& a, double s) const {
    // the takeoff roll accelerates at ~2.4 m/s2 to liftoff, then ~1 m/s2 to the cruise; before a
    // landing the speed comes back to 70 m/s at the threshold and the roll stops at the path end
    const double L = pathLength(a.path);
    double v = a.speed;
    if (a.takesOff) {
        if (s < a.liftoffS) v = std::min(v, std::sqrt(2.0 * 2.4 * s + 16.0));
        else v = std::min(v, std::sqrt(72.0 * 72.0 + 2.0 * 1.0 * (s - a.liftoffS)));
    }
    if (a.landsAtEnd) {
        if (s < a.touchdownS) v = std::min(v, std::sqrt(70.0 * 70.0 + 2.0 * 0.8 * (a.touchdownS - s)));
        else v = std::min(70.0, std::sqrt(std::max(0.0, 2.0 * 1.8 * (L - s))));
    }
    return v;
}

void TrainingSession::spawnAsset(int i, Battle& battle, double startS) {
    const Asset& a = course_.assets[static_cast<size_t>(i)];
    AssetRun& r = assetRun_[static_cast<size_t>(i)];
    r = AssetRun{};
    r.spawned = true;
    r.s = startS;
    const Terrain& t = battle.terrain();
    switch (a.kind) {
        case AssetKind::Transport: {
            Vec3 tan;
            Vec3 p = pathAt(a.path, startS, &tan);
            r.v = a.takesOff && startS < a.liftoffS ? std::sqrt(2.0 * 2.4 * startS + 1.0) : transportSpeedAt(a, startS);
            Jet& j = battle.debugAddJet(Team::Blue, AircraftId::C17, p, std::atan2(tan.z, tan.x), std::max(r.v, 60.0));
            j.kinematic = true;
            j.damageScale = 0.3;          // three missile hits
            j.missiles = 0; j.agms = 0; j.rounds = 0;
            j.flareCharges = 6;
            j.callsign = a.name;
            r.jet = j.id;
            r.prevHeading = std::atan2(tan.z, tan.x);
            r.lastHealth = 100.0;
            driveTransport(i, battle, 0.0);
            break;
        }
        case AssetKind::Convoy: {
            // vehicles in column, 45 m apart, the lead first
            for (int k = 0; k < a.count; ++k) {
                GroundTarget g;
                g.kind = a.unitKinds.empty() ? GroundKind::Truck : static_cast<GroundKind>(a.unitKinds[static_cast<size_t>(k) % a.unitKinds.size()]);
                g.team = Team::Blue;
                g.armed = false;
                g.route = a.path;
                g.routeSpeed = a.speed;
                g.routeS = std::max(0.0, startS - k * 45.0);
                Vec3 p = pathAt(a.path, g.routeS);
                g.pos = p;
                g.label = "CONVOY " + std::to_string(k + 1);
                int gi = battle.addGround(g);
                { GroundTarget& gm = battle.groundMut(gi); gm.maxHealth = gm.health = std::max(gm.maxHealth, 120.0); }   // armoured supply trucks
                r.ground.push_back(gi);
            }
            break;
        }
        case AssetKind::Rescue: {
            // out along the path, a hold over the pick-up, then back along the same path
            std::vector<Vec3> route = a.path;
            for (int k = static_cast<int>(a.path.size()) - 2; k >= 0; --k) route.push_back(a.path[static_cast<size_t>(k)]);
            for (int k = 0; k < a.count; ++k) {
                GroundTarget g;
                g.kind = GroundKind::Helicopter;
                g.team = Team::Blue;
                g.armed = false;
                g.flyAgl = 70.0 + 25.0 * k;
                g.flySpeed = a.speed;
                g.route = route;
                g.routeSpeed = a.speed;
                g.routeS = std::max(0.0, startS - k * 160.0);
                g.pos = pathAt(route, g.routeS);
                g.label = "RESCUE " + std::to_string(k + 1);
                int gi = battle.addGround(g);
                { GroundTarget& gm = battle.groundMut(gi); gm.maxHealth = gm.health = 170.0; }   // armoured rescue helicopters
                r.ground.push_back(gi);
            }
            break;
        }
        case AssetKind::Base: case AssetKind::Fleet: case AssetKind::Troops: {
            std::vector<Vec3> taken;
            const bool naval = a.kind == AssetKind::Fleet;
            for (int k = 0; k < a.count; ++k) {
                GroundTarget g;
                g.kind = a.unitKinds.empty() ? GroundKind::Tank : static_cast<GroundKind>(a.unitKinds[static_cast<size_t>(k) % a.unitKinds.size()]);
                g.team = Team::Blue;
                g.armed = false;
                if (k < static_cast<int>(a.unitPos.size())) {
                    g.pos = {a.unitPos[static_cast<size_t>(k)].x, 0, a.unitPos[static_cast<size_t>(k)].z};
                    g.heading = a.unitPos[static_cast<size_t>(k)].y;
                    if (k < static_cast<int>(a.unitAircraft.size())) g.aircraft = a.unitAircraft[static_cast<size_t>(k)];
                } else {
                    g.pos = placeUnit(t, a.area, a.radius, k, a.count, naval, taken);
                    g.heading = std::atan2(a.area.z - g.pos.z, a.area.x - g.pos.x) + kPi / 2;
                }
                taken.push_back(g.pos);
                if (g.kind == GroundKind::ParkedJet && g.aircraft < 0) {
                    static const int parked[] = {static_cast<int>(AircraftId::F16), static_cast<int>(AircraftId::F22), static_cast<int>(AircraftId::FA18),
                                                 static_cast<int>(AircraftId::Typhoon)};
                    g.aircraft = parked[k % 4];
                    g.heading = t.runway().heading + kPi / 2;
                }
                if (naval) { g.pathCenter = a.area; g.pathRadius = std::hypot(g.pos.x - a.area.x, g.pos.z - a.area.z) + 1.0; g.pathAngle = std::atan2(g.pos.z - a.area.z, g.pos.x - a.area.x); }
                g.label = std::string(a.kind == AssetKind::Base ? "BASE " : a.kind == AssetKind::Fleet ? "SHIP " : "UNIT ") + std::to_string(k + 1);
                // the fleet's ships and the company's anti-aircraft gun defend themselves (close in)
                g.armed = naval || g.kind == GroundKind::AAA;
                int gi = battle.addGround(g);
                if (naval) { GroundTarget& gm = battle.groundMut(gi); gm.maxHealth = gm.health = 620.0; }   // a frigate takes three missiles
                r.ground.push_back(gi);
            }
            break;
        }
        default: break;
    }
    // first call
    using namespace ops_;
    switch (a.kind) {
        case AssetKind::Transport: say(Speaker::Wingman, a.takesOff ? kTransportRolling : kTransportInbound, 1, "asset" + std::to_string(i), 1e9); break;
        case AssetKind::Convoy: say(Speaker::Wingman, kConvoyRolling, 1, "asset" + std::to_string(i), 1e9); break;
        case AssetKind::Rescue: say(Speaker::Wingman, kRescueOut, 1, "asset" + std::to_string(i), 1e9); break;
        default: break;
    }
}

void TrainingSession::driveTransport(int i, Battle& battle, double dt) {
    const Asset& a = course_.assets[static_cast<size_t>(i)];
    AssetRun& r = assetRun_[static_cast<size_t>(i)];
    Jet* j = battle.jetById(r.jet);
    if (!j || !j->alive || !j->kinematic) return;
    using namespace ops_;
    const double L = pathLength(a.path);
    // speed: chase the profile with a gentle acceleration limit
    double want = transportSpeedAt(a, r.s);
    double acc = r.s >= a.touchdownS && a.landsAtEnd ? 2.0 : 1.6;
    r.v += clampd(want - r.v, -acc * dt, acc * dt);
    if (a.takesOff && r.s < a.liftoffS) r.v = std::max(r.v, std::min(want, 4.0));   // the roll starts at once
    double before = r.s;
    r.s = std::min(L, r.s + r.v * dt);
    bool stopped = a.landsAtEnd && r.s >= L - 1.0;
    if (stopped) r.v = 0;
    // attitude from a smoothed tangent; bank from the turn rate
    Vec3 p = pathAt(a.path, r.s);
    Vec3 ahead = pathAt(a.path, r.s + 120.0), behind = pathAt(a.path, r.s - 120.0);
    Vec3 tan = length(ahead - behind) > 1.0 ? normalize(ahead - behind) : Vec3{std::cos(r.prevHeading), 0, std::sin(r.prevHeading)};
    double heading = std::atan2(tan.z, tan.x);
    bool onGround = (a.takesOff && r.s < a.liftoffS) || (a.landsAtEnd && r.s >= a.touchdownS);
    double pitch = onGround ? 0.0 : std::asin(clampd(tan.y, -1.0, 1.0)) + 3.0 * kDeg;
    if (a.takesOff && r.s < a.liftoffS && r.s > a.liftoffS - 250.0) pitch = 8.0 * kDeg * (1.0 - (a.liftoffS - r.s) / 250.0);   // rotation
    double yawRate = dt > 0 ? wrapPi(heading - r.prevHeading) / dt : 0.0;
    r.prevHeading = heading;
    double bankT = onGround ? 0.0 : clampd(std::atan(r.v * yawRate / kG), -30.0 * kDeg, 30.0 * kDeg);
    r.bank += (bankT - r.bank) * std::min(1.0, dt * 1.5);
    FlightState& s = j->fm->mutableState();
    s.pos = p;
    s.vel = tan * r.v;
    s.q = Quat::fromEuler(heading, pitch, r.bank);
    s.airspeed = r.v;
    s.onGround = onGround;
    s.engine = EngineState::Running;
    s.rpm = 0.88;
    s.throttle = 0.85;
    s.agl = p.y - battle.terrain().surfaceHeight(p.x, p.z);
    bool gearWanted = onGround || (a.takesOff && r.s < a.liftoffS + 2500.0) || (a.landsAtEnd && r.s > a.touchdownS - 9000.0);
    s.gearDown = gearWanted;
    s.gearPos = clampd(s.gearPos + (gearWanted ? dt : -dt) / 6.0, 0.0, 1.0);
    if (dt <= 0) s.gearPos = gearWanted ? 1.0 : 0.0;
    // calls along the way
    if (a.landsAtEnd && before < a.touchdownS - 9000.0 && r.s >= a.touchdownS - 9000.0) say(Speaker::Wingman, kTransportFinal, 1, "tfinal" + std::to_string(i), 1e9);
    // airdrop: the ramp opens a minute out, pallets leave at the drop point under parachutes
    if (a.dropS > 0) {
        if (before < a.dropS - 6000.0 && r.s >= a.dropS - 6000.0) say(Speaker::Wingman, kTransportDropSoon, 1, "tdrops" + std::to_string(i), 1e9);
        bool rampOpen = r.s > a.dropS - 2500.0 && r.s < a.dropS + 1500.0;
        j->ramp = clampd(j->ramp + (rampOpen ? dt : -dt) / 8.0, 0.0, 1.0);
        if (!r.dropped && r.s >= a.dropS) {
            r.dropped = true;
            say(Speaker::Wingman, kTransportDrop, 2, "tdrop" + std::to_string(i), 1e9);
            for (int k = 0; k < 4; ++k) {
                GroundTarget g;
                g.kind = GroundKind::Cargo;
                g.team = Team::Blue;
                g.armed = false;
                g.chuteV = 7.0;
                g.pos = p - tan * (32.0 + k * 22.0) - Vec3{0, 4.0, 0};
                g.heading = heading;
                g.label = "SUPPLIES";
                r.cargo.push_back(battle.addGround(g));
            }
        }
        if (r.dropped && before < a.dropS + 3000.0 && r.s >= a.dropS + 3000.0) say(Speaker::Wingman, kTransportDropDone, 1, "tdropd" + std::to_string(i), 1e9);
    }
    // landed: stop, ramp down, the pallets come off one by one
    if (stopped) {
        if (!r.unloading) { r.unloading = true; r.timer = 0; say(Speaker::Wingman, kTransportDown, 1, "tdown" + std::to_string(i), 1e9); }
        r.timer += dt;
        j->ramp = clampd(j->ramp + dt / 8.0, 0.0, 1.0);
        int want = std::min(4, static_cast<int>((r.timer - 8.0) / 4.0));
        while (static_cast<int>(r.cargo.size()) < want) {
            int k = static_cast<int>(r.cargo.size());
            Vec3 back = -tan;
            Vec3 side{-tan.z, 0, tan.x};
            GroundTarget g;
            g.kind = GroundKind::Cargo;
            g.team = Team::Blue;
            g.armed = false;
            g.pos = p + back * (40.0 + 6.0 * (k / 2)) + side * ((k % 2) ? 3.5 : -3.5);
            g.heading = heading;
            g.label = "SUPPLIES";
            r.cargo.push_back(battle.addGround(g));
        }
        if (r.timer > 26.0 && !r.done) {
            r.done = true;
            events_.push_back({TrainingEventKind::AssetDelivered, i, 0});
            say(Speaker::Wingman, kTransportDelivered, 1, "tdeliv" + std::to_string(i), 1e9);
        }
    } else if (!a.landsAtEnd && r.s >= L - 1.0 && !r.done) {
        // flew out of the area: done, it leaves
        r.done = true;
        events_.push_back({TrainingEventKind::AssetDelivered, i, 0});
        say(Speaker::Wingman, kTransportHome, 1, "thome" + std::to_string(i), 1e9);
        battle.removeJet(r.jet);
        r.jet = -1;
    }
}

void TrainingSession::updateAssets(const MissionContext& ctx, Battle& battle, double dt) {
    using namespace ops_;
    (void)ctx;
    for (int i = 0; i < static_cast<int>(course_.assets.size()); ++i) {
        const Asset& a = course_.assets[static_cast<size_t>(i)];
        AssetRun& r = assetRun_[static_cast<size_t>(i)];
        if (!r.spawned) continue;
        int lost = 0;
        switch (a.kind) {
            case AssetKind::Transport: {
                Jet* j = battle.jetById(r.jet);
                if (r.jet >= 0 && (!j || !j->alive)) {
                    lost = 1;
                    if (r.lost == 0) say(Speaker::Wingman, kTransportLost, 2, "tlost" + std::to_string(i), 1e9);
                } else if (j) {
                    if (j->health < r.lastHealth - 5.0) {
                        say(Speaker::Wingman, j->health < 45.0 ? kTransportBadlyHit : kTransportHit, 2, "thit" + std::to_string(i), 12);
                    }
                    r.lastHealth = j->health;
                    if (!r.done) driveTransport(i, battle, dt);
                }
                break;
            }
            default: {
                int alive = 0;
                for (int gi : r.ground) if (battle.ground()[static_cast<size_t>(gi)].alive) ++alive;
                lost = static_cast<int>(r.ground.size()) - alive;
                if (lost > r.lost) {
                    const char* line = a.kind == AssetKind::Convoy ? kConvoyLost : a.kind == AssetKind::Rescue ? kRescueLost
                                     : a.kind == AssetKind::Base ? kBaseHit : a.kind == AssetKind::Fleet ? kFleetHit : kTroopsHit;
                    say(Speaker::Wingman, line, 2, "alost" + std::to_string(i), 6);
                    events_.push_back({TrainingEventKind::AssetLost, i, 0});
                }
                // under fire (a hit that did not kill)
                for (int gi : r.ground) {
                    const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
                    if (g.alive && g.health < g.maxHealth - 1.0) {
                        const char* hit = a.kind == AssetKind::Convoy ? kConvoyHit : a.kind == AssetKind::Rescue ? kRescueHit
                                        : a.kind == AssetKind::Base ? kBaseAttack : a.kind == AssetKind::Fleet ? kFleetAttack : kTroopsAttack;
                        if (!r.hitCalled) { say(Speaker::Wingman, hit, 2, "ahit" + std::to_string(i), 20); r.hitCalled = true; }
                        break;
                    }
                }
                if (a.kind == AssetKind::Convoy && !r.done && alive > 0) {
                    bool all = true;
                    double lead = 0;
                    for (int gi : r.ground) {
                        const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
                        if (!g.alive) continue;
                        all = all && g.arrived();
                        lead = std::max(lead, g.routeS);
                    }
                    r.s = lead;
                    if (all) { r.done = true; events_.push_back({TrainingEventKind::AssetDelivered, i, 0}); say(Speaker::Wingman, kConvoyArrived, 1, "carr" + std::to_string(i), 1e9); }
                }
                if (a.kind == AssetKind::Rescue && !r.done && alive > 0) {
                    const double out = pathLength(a.path);
                    double lead = 0;
                    bool all = true;
                    for (int gi : r.ground) {
                        GroundTarget& g = battle.groundMut(gi);
                        if (!g.alive) continue;
                        // the pick-up: each helicopter holds once when it reaches the end of the outbound leg
                        if (!r.dropped && g.routeS >= out - 1.0 && g.hold <= 0) {
                            g.hold = a.holdSeconds;
                            if (r.timer <= 0) say(Speaker::Wingman, kRescuePickup, 1, "rpick" + std::to_string(i), 1e9);
                            r.timer = std::max(r.timer, a.holdSeconds);
                        }
                        lead = std::max(lead, g.routeS);
                        all = all && g.arrived();
                    }
                    if (r.timer > 0) {
                        r.timer -= dt;
                        if (r.timer <= 0) { r.dropped = true; say(Speaker::Wingman, kRescueAboard, 1, "rabo" + std::to_string(i), 1e9); }
                    }
                    r.s = lead;
                    if (all) { r.done = true; events_.push_back({TrainingEventKind::AssetDelivered, i, 0}); say(Speaker::Wingman, kRescueHome, 1, "rhome" + std::to_string(i), 1e9); }
                }
                break;
            }
        }
        r.lost = std::max(r.lost, lost);
        if (r.lost > a.lossLimit && failure_.empty()) {
            switch (a.kind) {
                case AssetKind::Transport: failure_ = "The C-17 was shot down."; break;
                case AssetKind::Convoy: failure_ = "The convoy was destroyed."; break;
                case AssetKind::Rescue: failure_ = "The rescue helicopters were lost."; break;
                case AssetKind::Base: failure_ = "The airbase was overrun."; break;
                case AssetKind::Fleet: failure_ = "The fleet was lost."; break;
                case AssetKind::Troops: failure_ = "Our ground forces were overrun."; break;
                default: failure_ = "The operation failed."; break;
            }
        }
    }
    // wingmen: hit and loss calls
    for (size_t k = 0; k < wingmenIds_.size(); ++k) {
        Jet* w = battle.jetById(wingmenIds_[k]);
        if (!w) continue;
        if (!w->alive) say(Speaker::Wingman, kWingLost, 2, "winglost" + std::to_string(k), 1e9);
        else if (w->health < 70.0) say(Speaker::Wingman, kWingHit, 1, "winghit" + std::to_string(k), 1e9);
    }
}

void TrainingSession::spawnWingmen(const MissionContext& ctx, Battle& battle) {
    if (wingmenSpawned_ || course_.wingmen.empty()) return;
    const FlightState& f = *ctx.flight;
    wingmenSpawned_ = true;
    Vec3 fw{f.vel.x, 0, f.vel.z};
    fw = length(fw) > 1.0 ? normalize(fw) : Vec3{1, 0, 0};
    Vec3 rt{-fw.z, 0, fw.x};
    for (size_t k = 0; k < course_.wingmen.size(); ++k) {
        Vec3 p = f.pos - fw * (350.0 + 150.0 * k) + rt * (250.0 * (k + 1)) + Vec3{0, 30.0, 0};
        p.y = std::max(p.y, battle.terrain().surfaceHeight(p.x, p.z) + 300.0);
        Jet& w = battle.debugAddJet(Team::Blue, static_cast<AircraftId>(course_.wingmen[k]), p, std::atan2(fw.z, fw.x), std::max(f.airspeed, 180.0));
        w.followId = battle.player().id;
        w.skill = AiSkill::Veteran;
        w.missiles = 4;
        w.flareCharges = 3;
        w.callsign = k == 0 ? "VIPER 2" : "VIPER " + std::to_string(3 + static_cast<int>(k) - 1);
        wingmenIds_.push_back(w.id);
    }
    say(Speaker::Wingman, ops_::kWingJoin, 1, "wingjoin", 1e9);
}

TrainingSession::AssetStatus TrainingSession::assetStatus(int i, const Battle& battle) const {
    AssetStatus st;
    if (i < 0 || i >= static_cast<int>(course_.assets.size()) || i >= static_cast<int>(assetRun_.size())) return st;
    const Asset& a = course_.assets[static_cast<size_t>(i)];
    const AssetRun& r = assetRun_[static_cast<size_t>(i)];
    st.active = r.spawned;
    st.done = r.done;
    st.lost = r.lost;
    st.lossLimit = a.lossLimit;
    st.jet = r.jet;
    st.ground = r.ground;
    if (a.kind == AssetKind::Transport) {
        st.total = 1;
        const Jet* j = battle.jetById(r.jet);
        st.alive = j && j->alive ? 1 : 0;
        if (j) st.pos = j->fm->state().pos;
        double L = pathLength(a.path);
        st.progress = L > 0 ? clampd(r.s / L, 0.0, 1.0) : 0.0;
    } else {
        st.total = static_cast<int>(r.ground.size());
        Vec3 sum{};
        for (int gi : r.ground) {
            const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
            if (g.alive) { ++st.alive; sum += g.pos; }
        }
        st.pos = st.alive > 0 ? sum / st.alive : a.area;
        double L = a.kind == AssetKind::Rescue ? 2.0 * pathLength(a.path) : pathLength(a.path);
        st.progress = L > 0 ? clampd(r.s / L, 0.0, 1.0) : 0.0;
    }
    return st;
}

}  // namespace sky
