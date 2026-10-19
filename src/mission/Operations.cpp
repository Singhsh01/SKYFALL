#include "Operations.hpp"

#include "../sim/TerrainAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace sky {

const std::vector<OperationTemplate>& operationTemplates() {
    static const std::vector<OperationTemplate> k = {
        {"Valley Patrol", "Patrol the main valley and intercept the intruders."},
        {"Ridge Radar", "Come in low and destroy the radar on the ridge."},
        {"The Pass", "Through the valley to the mountain pass: kill the SAM site, then the fighter cover."},
        {"Convoy Interdiction", "Stop the armoured convoy in the basin before its escort arrives."},
        {"Supply Line", "Cut the enemy supply line and the depot that feeds it."},
        {"Strike Force", "Destroy the enemy strike force and the interceptors it scrambles."},
        {"Long Reach", "Refuel from the tanker, then strike the command post deep in the mountains."},
        {"Peak Hunter", "Two radio relays on the highest peaks, then the patrol that guards them."},
        {"Mountain Dogfight", "Two waves of fighters low in the valleys. Use the terrain."},
        {"Final Operation", "Air defences, the headquarters, the last wave - then bring it home."},
    };
    return k;
}

bool locationIsCoastal(const std::string& id) { return id == "desert" || id == "musandam" || id == "rio"; }

namespace {

using K = GroundKind;

std::string locName(const std::string& id) {
    const LocationDef* l = findLocation(id);
    return l ? l->name : id;
}

// Fixed objective titles per mission (spoken as "Next: ..."): one list per template; the coastal
// variant of mission 6 has its own list.
const std::vector<std::string>& objectiveTitles(int k, bool coastal) {
    static const std::vector<std::vector<std::string>> t = {
        {"PATROL POINT ALPHA", "PATROL POINT BRAVO", "INTERCEPT THE INTRUDERS"},
        {"LOW APPROACH TO THE RIDGE", "DESTROY THE RIDGE RADAR", "EGRESS LOW"},
        {"INGRESS THROUGH THE VALLEY", "DESTROY THE SAM SITE", "ENGAGE THE FIGHTER COVER"},
        {"STOP THE CONVOY", "ENGAGE THE ESCORT"},
        {"CUT THE SUPPLY LINE", "DESTROY THE FUEL DEPOT"},
        {"DESTROY THE HANGARS", "SCRAMBLED INTERCEPTORS"},
        {"JOIN THE TANKER", "REFUEL", "STRIKE THE COMMAND POST"},
        {"DESTROY THE NORTH RELAY", "DESTROY THE SOUTH RELAY", "INTERCEPT THE PATROL"},
        {"FIRST WAVE", "SECOND WAVE"},
        {"SUPPRESS THE AIR DEFENCES", "DESTROY THE HEADQUARTERS", "THE FINAL WAVE", "LAND AT BASE", "STOP ON THE RUNWAY"},
    };
    static const std::vector<std::string> coastal6 = {"SINK THE PATROL BOATS", "SCRAMBLED INTERCEPTORS"};
    if (k == 6 && coastal) return coastal6;
    return t[std::clamp(k, 1, kOperationMissions) - 1];
}

std::string storyFor(int k, const std::string& name, bool coastal) {
    switch (k) {
        case 1: return "Enemy aircraft have been seen in the valleys of " + name + ". Patrol the main valley, two points, and intercept anything you find.";
        case 2: return "A radar on a ridge above " + name + " is watching our airfield. Stay low in the valleys to the ridge, destroy it, and get out low.";
        case 3: return "The pass is guarded by a missile site. Follow the valley in, below the radar, kill the site, then deal with the fighters covering it.";
        case 4: return "An armoured convoy is moving through a basin near " + name + ". Stop it before its fighter escort arrives.";
        case 5: return "Their supply line runs through " + name + ". Cut it, then destroy the fuel depot that feeds it.";
        case 6:
            return coastal ? "Patrol boats are operating off the coast of " + name + ". Sink them; expect interceptors to scramble."
                           : "An enemy strike force is sheltering at a forward base near " + name + ". Destroy the hangars; expect interceptors to scramble.";
        case 7: return "The command post is deep in the mountains, beyond our normal range. Join the tanker and refuel first, then strike.";
        case 8: return "Two radio relays on the highest peaks of " + name + " coordinate their air defence. Destroy both, then the patrol that guards them.";
        case 9: return "Two waves of fighters are coming in low through the valleys of " + name + ". Use the terrain, and watch your altitude in the turns.";
        default: return "This is the final operation over " + name + ". Suppress the air defences, destroy the headquarters, survive the final wave and bring the jet home.";
    }
}

std::string tipFor(int k) {
    switch (k) {
        case 2: case 3: return "Keep below the height limit on the HUD on the way in; the terrain hides you from their radar.";
        case 7: return "On the tanker, match its speed, line up behind the basket and push in gently.";
        case 9: return "Low and fast in the valleys, but never lower than the ridges ahead of you.";
        case 10: return "When the last wave is down, fly the approach to the runway, touch down and stop.";
        default: return "Select ATS with key two for ground targets and ATA with key one for aircraft. Flares on X.";
    }
}

struct Ctx {
    const Terrain& t;
    const WorldLayout& L;
    HeightGrid g;
    Rng rng;
    Vec3 base;                  // runway centre
    std::vector<Vec3> peaks, basins, used;
    Ctx(const Terrain& t_, const WorldLayout& L_, unsigned seed) : t(t_), L(L_), rng(seed) {
        g = sampleHeightGrid(t, 200.0, 15000.0);
        peaks = findPeaks(g, 16, 2500.0);
        basins = findBasins(g, 30, 2200.0, 2500.0);
        base = t.runway().center;
    }
    static bool inside(const Vec3& p, double m = 2500.0) { return std::fabs(p.x) < Terrain::kHalfSize - m && std::fabs(p.z) < Terrain::kHalfSize - m; }
    bool farFromUsed(const Vec3& p, double d) const {
        for (const Vec3& u : used) if (std::hypot(u.x - p.x, u.z - p.z) < d) return false;
        return true;
    }
    /// A point from `cands` between dmin and dmax from the runway, away from earlier objectives.
    Vec3 pick(const std::vector<Vec3>& cands, double dmin, double dmax) {
        std::vector<Vec3> ok;
        for (const Vec3& p : cands) {
            double d = std::hypot(p.x - base.x, p.z - base.z);
            // not far above the airfield: strikes on the highest summits of a 6-8 km range are
            // beyond what a jet can manoeuvre for in that air
            if (p.y > base.y + 1800.0) continue;
            if (d >= dmin && d <= dmax && inside(p) && farFromUsed(p, 3000.0) && !t.onRunway(p.x, p.z, 1500.0)) ok.push_back(p);
        }
        if (ok.empty()) {
            for (int k = 0; k < 200; ++k) {
                double a = rng.range(0, 2 * kPi), d = rng.range(dmin, dmax);
                Vec3 p{base.x + std::cos(a) * d, 0, base.z + std::sin(a) * d};
                if (!inside(p) || t.isWater(p.x, p.z)) continue;
                p.y = t.surfaceHeight(p.x, p.z);
                if (p.y > base.y + 1800.0 && k < 150) continue;
                if (farFromUsed(p, 2000.0)) { ok.push_back(p); break; }
            }
        }
        Vec3 r = ok.empty() ? Vec3{clampd(base.x + 6000, -12000, 12000), 0, clampd(base.z, -12000, 12000)} : ok[static_cast<size_t>(rng.next() % ok.size())];
        r.y = t.surfaceHeight(r.x, r.z);
        used.push_back(r);
        return r;
    }
    /// A gentle dry spot near p for a compound.
    Vec3 flatNear(Vec3 p, double clearR = 220.0) {
        Vec3 out;
        std::vector<Vec3> avoid = used;
        if (findFlatSpot(t, p, 3500.0, 0.10, clearR, out, {}, 0.0)) { out.y = t.surfaceHeight(out.x, out.z); return out; }
        return p;
    }
    double safeAlt(Vec3 c, double radius, double margin) const {
        double top = c.y;
        for (int r = 0; r <= 3; ++r)
            for (int k = 0; k < 12; ++k) {
                double a = k * kPi / 6.0, d = radius * r / 3.0;
                top = std::max(top, t.surfaceHeight(c.x + std::cos(a) * d, c.z + std::sin(a) * d));
            }
        return top + margin;
    }
    /// A point on the low-ground path from the runway to `target`, `frac` of the way along.
    Vec3 alongValley(Vec3 target, double frac) {
        PathCost pc;
        pc.low = 3.0;
        pc.slope = 4.0;
        std::vector<Vec3> path = valleyPath(g, base, target, pc);
        if (path.size() < 2) return base + (target - base) * frac;
        double L = polylineLength(path), want = L * frac, acc = 0;
        for (size_t i = 1; i < path.size(); ++i) {
            double seg = std::hypot(path[i].x - path[i - 1].x, path[i].z - path[i - 1].z);
            if (acc + seg >= want) {
                Vec3 p = path[i - 1] + (path[i] - path[i - 1]) * ((want - acc) / std::max(seg, 1.0));
                p.y = t.surfaceHeight(p.x, p.z);
                return p;
            }
            acc += seg;
        }
        return path.back();
    }
};

TargetDef tgt(K k, Vec3 p, const std::string& label, int group, bool armed = true, double heading = 0) {
    TargetDef d;
    d.kind = k;
    d.x = p.x;
    d.z = p.z;
    d.label = label;
    d.group = group;
    d.armed = armed;
    d.heading = heading;
    return d;
}

ObjectiveDef wp(const std::string& title, Vec3 p, double radius, double maxAgl, const std::string& instr) {
    ObjectiveDef o;
    o.kind = ObjectiveKind::Waypoint;
    o.title = title;
    o.instruction = instr;
    o.x = p.x;
    o.z = p.z;
    o.radius = radius;
    o.maxAgl = maxAgl;
    return o;
}
ObjectiveDef destroy(const std::string& title, int group, const std::string& instr) {
    ObjectiveDef o;
    o.kind = ObjectiveKind::DestroyGroup;
    o.title = title;
    o.group = group;
    o.instruction = instr;
    o.hint = "2 = ATS   SPACE = FIRE";
    return o;
}
ObjectiveDef air(const std::string& title, std::vector<int> waves, const std::string& instr) {
    ObjectiveDef o;
    o.kind = ObjectiveKind::DestroyAir;
    o.title = title;
    o.instruction = instr;
    o.wavesOnStart = std::move(waves);
    o.hint = "1 = ATA   SPACE = FIRE   X = FLARES";
    return o;
}
WaveDef wave(int n, AiSkill sk, Vec3 c, double alt, double radius = 2000.0) {
    WaveDef w;
    w.count = n;
    w.skill = sk;
    const double lim = Terrain::kHalfSize - 2500.0;   // waves spawn inside the map
    w.x = clampd(c.x, -lim, lim);
    w.z = clampd(c.z, -lim, lim);
    w.altitude = alt;
    w.radius = radius;
    return w;
}

void ring(std::vector<TargetDef>& v, K k, const std::string& prefix, int n, Vec3 c, double radius, int group, bool armed = true, double phase = 0.3) {
    for (int i = 0; i < n; ++i) {
        double a = phase + 2 * kPi * i / n;
        v.push_back(tgt(k, {c.x + std::cos(a) * radius, 0, c.z + std::sin(a) * radius}, prefix + " " + std::to_string(i + 1), group, armed, a));
    }
}

unsigned hashId(const std::string& s) {
    unsigned h = 2166136261u;
    for (char c : s) { h ^= static_cast<unsigned char>(c); h *= 16777619u; }
    return h;
}

}  // namespace

CampaignMissionDef buildOperation(int k, const Terrain& t, const WorldLayout& L) {
    k = std::clamp(k, 1, kOperationMissions);
    const std::string id = t.info().id;
    const bool coastal = locationIsCoastal(id);
    Ctx c(t, L, hashId(id) * 31u + static_cast<unsigned>(k) * 7919u);
    const auto& titles = objectiveTitles(k, coastal);
    CampaignMissionDef d;
    d.number = 200 + k;
    d.title = operationTemplates()[k - 1].title;
    d.summary = operationTemplates()[k - 1].summary;
    d.briefing = storyFor(k, locName(id), coastal);
    d.debrief = k < kOperationMissions ? "Good work, Captain. That operation is complete." : "Operation complete. Outstanding work over " + locName(id) + ", Captain.";
    d.timeOfDay = (k % 3 == 1) ? 0 : (k % 3 == 2 ? 2 : 1);
    d.tactics = k <= 5 ? 1 : 2;
    d.atsReload = 9.0;
    AiSkill sk1 = k <= 3 ? AiSkill::Rookie : (k <= 6 ? AiSkill::Veteran : AiSkill::Ace);
    // start: airborne over the airfield, climbing out
    const RunwayInfo& rw = t.runway();
    Vec3 u{std::cos(rw.heading), 0, std::sin(rw.heading)};
    Vec3 sp = rw.center + u * 1500.0;
    sp.y = rw.center.y;
    d.startX = sp.x;
    d.startZ = sp.z;
    d.startAlt = std::max(rw.center.y + 700.0, c.safeAlt(sp, 4000.0, 450.0));
    d.startSpeed = 220;
    d.allies = k >= 9 ? 1 : 0;

    switch (k) {
        case 1: {
            Vec3 a = c.pick(c.basins, 5000, 9000), b = c.pick(c.basins, 7000, 12500);
            d.objectives.push_back(wp(titles[0], a, 1100, 0, "Fly to the first patrol point in the valley."));
            d.objectives.push_back(wp(titles[1], b, 1100, 0, "Continue to the second patrol point."));
            d.waves.push_back(wave(2, AiSkill::Rookie, b + (b - a) * 0.4, c.safeAlt(b, 4000, 700)));
            d.objectives.push_back(air(titles[2], {0}, "Shoot down the intruders."));
            break;
        }
        case 2: {
            Vec3 ridge = c.pick(c.peaks, 6000, 12500);
            Vec3 site = c.flatNear(ridge, 120.0);
            Vec3 ing = c.alongValley(site, 0.7);
            d.objectives.push_back(wp(titles[0], ing, 1300, 450, "Stay below 450 m above the ground and follow the valley to the ridge."));
            d.targets.push_back(tgt(K::Radar, site, "RIDGE RADAR", 1));
            d.targets.push_back(tgt(K::Command, site + Vec3{60, 0, 40}, "RADAR CABIN", 1, false));
            ring(d.targets, K::AAA, "FLAK", 2, site, 350, 0);
            d.objectives.push_back(destroy(titles[1], 1, "Destroy the radar and its cabin."));
            Vec3 out = c.alongValley(site, 0.35);
            d.objectives.push_back(wp(titles[2], out, 1500, 500, "Leave low, below 500 m, back down the valley."));
            break;
        }
        case 3: {
            Vec3 pass = c.pick(c.peaks, 6500, 12500);
            Vec3 site = c.alongValley(pass, 0.85);
            site = c.flatNear(site, 140.0);
            Vec3 ing = c.alongValley(site, 0.55);
            d.objectives.push_back(wp(titles[0], ing, 1300, 400, "Follow the valley in below 400 m: their radar cannot see you there."));
            d.targets.push_back(tgt(K::SAM, site, "SAM 1", 1));
            d.targets.push_back(tgt(K::SAM, site + Vec3{180, 0, -120}, "SAM 2", 1));
            d.targets.push_back(tgt(K::Radar, site + Vec3{-120, 0, 90}, "SAM RADAR", 1));
            ring(d.targets, K::Gun, "GUN", 2, site, 420, 0);
            d.objectives.push_back(destroy(titles[1], 1, "Destroy both launchers and the radar."));
            d.waves.push_back(wave(2, sk1, site + Vec3{3000, 0, 0}, c.safeAlt(site, 4000, 800)));
            d.objectives.push_back(air(titles[2], {0}, "Shoot down the fighter cover."));
            break;
        }
        case 4: {
            Vec3 bas = c.pick(c.basins, 5000, 12000);
            bas = c.flatNear(bas, 300.0);
            for (int i = 0; i < 5; ++i) {
                TargetDef v = tgt(i % 2 ? K::Truck : K::Tank, bas, std::string(i % 2 ? "TRUCK " : "TANK ") + std::to_string(i / 2 + 1), 1);
                v.pathRadius = 320;
                v.pathAngle = i * 0.32;
                d.targets.push_back(v);
            }
            d.targets.push_back(tgt(K::AAA, bas + Vec3{0, 0, 500}, "ESCORT FLAK", 0));
            d.objectives.push_back(destroy(titles[0], 1, "Destroy every vehicle in the convoy."));
            d.waves.push_back(wave(2, sk1, bas + Vec3{-4000, 0, 2000}, c.safeAlt(bas, 5000, 700)));
            d.objectives.push_back(air(titles[1], {0}, "Shoot down the escort fighters."));
            break;
        }
        case 5: {
            bool rail = !L.railway.pts.empty();
            if (rail) {
                TargetDef loco;
                loco.kind = K::Locomotive;
                loco.anchor = TargetDef::Anchor::Railway;
                loco.railFraction = 0.25;
                loco.railSpeed = 18;
                loco.label = "SUPPLY TRAIN";
                loco.group = 1;
                d.targets.push_back(loco);
                for (int w = 1; w <= 3; ++w) {
                    TargetDef wg = loco;
                    wg.kind = K::Wagon;
                    wg.railFraction = 0.25 - w * 0.004;
                    wg.label = "WAGON " + std::to_string(w);
                    d.targets.push_back(wg);
                }
            } else {
                Vec3 road = c.pick(c.basins, 5000, 11000);
                for (int i = 0; i < 4; ++i) {
                    TargetDef v = tgt(K::Truck, road, "SUPPLY TRUCK " + std::to_string(i + 1), 1);
                    v.pathRadius = 400;
                    v.pathAngle = i * 0.25;
                    d.targets.push_back(v);
                }
            }
            d.objectives.push_back(destroy(titles[0], 1, rail ? "Destroy the supply train." : "Destroy the supply trucks."));
            Vec3 dep = c.flatNear(c.pick(c.basins, 6000, 12500), 200.0);
            ring(d.targets, K::FuelTank, "TANK", 4, dep, 70, 2, false);
            ring(d.targets, K::AAA, "DEPOT FLAK", 2, dep, 450, 0);
            d.objectives.push_back(destroy(titles[1], 2, "Destroy every storage tank at the depot."));
            break;
        }
        case 6: {
            Vec3 at;
            bool ships = false;
            if (coastal) {
                Vec3 sea;
                if (findOpenWater(t, c.g, c.base, 900.0, 15.0, sea) && Ctx::inside(sea, 2000.0)) {
                    ships = true;
                    at = sea;
                    for (int i = 0; i < 3; ++i) {
                        TargetDef b = tgt(K::Ship, sea, "PATROL BOAT " + std::to_string(i + 1), 1);
                        b.pathRadius = 500;
                        b.pathAngle = i * 2.1;
                        d.targets.push_back(b);
                    }
                    d.objectives.push_back(destroy(titles[0], 1, "Sink every patrol boat."));
                }
            }
            if (!ships) {
                at = c.flatNear(c.pick(c.basins, 6000, 12500), 260.0);
                ring(d.targets, K::Hangar, "HANGAR", 3, at, 140, 1, false);
                ring(d.targets, K::ParkedJet, "PARKED JET", 2, at, 60, 0, false, 1.0);
                ring(d.targets, K::SAM, "BASE SAM", 1, at, 600, 0);
                d.objectives.push_back(destroy("DESTROY THE HANGARS", 1, "Destroy every hangar."));
            }
            d.waves.push_back(wave(2, sk1, at + Vec3{2500, 0, -2500}, c.safeAlt(at, 5000, 800)));
            d.objectives.push_back(air(titles[1], {0}, "Shoot down the interceptors."));
            break;
        }
        case 7: {
            d.tanker = true;
            d.startFuel = 0.55;
            // the tanker orbits over the lowest ground near the middle of the map (jets have a ceiling)
            d.tankerRadius = 7000;
            {
                double bestTop = 1e18;
                for (int i = -4; i <= 4; ++i)
                    for (int j = -4; j <= 4; ++j) {
                        Vec3 cc{clampd(c.base.x * 0.4, -5000, 5000) + i * 1000.0, 0, clampd(c.base.z * 0.4, -5000, 5000) + j * 1000.0};
                        double top = -1e9;
                        for (int a = 0; a < 36; ++a)
                            for (double rr : {d.tankerRadius - 1500.0, d.tankerRadius, d.tankerRadius + 1500.0})
                                top = std::max(top, t.surfaceHeight(cc.x + std::cos(a * kPi / 18) * rr, cc.z + std::sin(a * kPi / 18) * rr));
                        top += std::hypot(i, j) * 30.0;
                        if (top < bestTop) { bestTop = top; d.tankerX = cc.x; d.tankerZ = cc.z; }
                    }
                d.tankerAlt = std::max(3000.0, std::ceil((bestTop + 1800.0) / 500.0) * 500.0);
            }
            ObjectiveDef rv;
            rv.kind = ObjectiveKind::Rendezvous;
            rv.title = titles[0];
            rv.instruction = "Fly to the tanker and close to within 1 km.";
            rv.radius = 1000;
            d.objectives.push_back(rv);
            ObjectiveDef rf;
            rf.kind = ObjectiveKind::Refuel;
            rf.title = titles[1];
            rf.instruction = "Plug the basket and take on fuel to 90 percent.";
            rf.value = 0.9;
            d.objectives.push_back(rf);
            Vec3 cp = c.flatNear(c.pick(c.peaks.empty() ? c.basins : c.peaks, 9000, 14000), 160.0);
            d.targets.push_back(tgt(K::Command, cp, "COMMAND POST", 1));
            d.targets.push_back(tgt(K::Bunker, cp + Vec3{90, 0, 60}, "BUNKER", 1));
            ring(d.targets, K::Gun, "GUN", 2, cp, 400, 0);
            d.objectives.push_back(destroy(titles[2], 1, "Destroy the command post and its bunker."));
            break;
        }
        case 8: {
            Vec3 a = c.flatNear(c.pick(c.peaks, 5000, 12500), 60.0), b = c.flatNear(c.pick(c.peaks, 5000, 12500), 60.0);
            if (a.z > b.z) std::swap(a, b);   // north first (north = -z)
            d.targets.push_back(tgt(K::CommTower, a, "NORTH RELAY", 1));
            d.targets.push_back(tgt(K::AAA, a + Vec3{250, 0, 150}, "RELAY FLAK", 1));
            d.targets.push_back(tgt(K::CommTower, b, "SOUTH RELAY", 2));
            d.targets.push_back(tgt(K::SAM, b + Vec3{-300, 0, 200}, "RELAY SAM", 2));
            d.objectives.push_back(destroy(titles[0], 1, "Destroy the relay mast on the northern peak and the flak guarding it."));
            d.objectives.push_back(destroy(titles[1], 2, "Destroy the relay mast on the southern peak and its SAM."));
            d.waves.push_back(wave(2, AiSkill::Veteran, (a + b) * 0.5, c.safeAlt((a + b) * 0.5, 6000, 900)));
            d.objectives.push_back(air(titles[2], {0}, "Shoot down the patrol."));
            break;
        }
        case 9: {
            Vec3 v1 = c.pick(c.basins, 6000, 12000), v2 = c.pick(c.basins, 6000, 12500);
            d.waves.push_back(wave(2, AiSkill::Veteran, v1, c.safeAlt(v1, 2500, 450), 1500));
            d.waves.push_back(wave(2, AiSkill::Ace, v2, c.safeAlt(v2, 2500, 500), 1800));
            d.objectives.push_back(air(titles[0], {0}, "Shoot down the first wave, low in the valleys."));
            d.objectives.push_back(air(titles[1], {1}, "The second wave is better. Shoot them down."));
            break;
        }
        default: {
            Vec3 hq = c.flatNear(c.pick(c.basins, 8000, 13500), 220.0);
            ring(d.targets, K::SAM, "SAM", 2, hq, 900, 1);
            ring(d.targets, K::Gun, "GUN", 2, hq, 600, 1, true, 1.2);
            d.objectives.push_back(destroy(titles[0], 1, "Destroy every SAM launcher and gun around the headquarters."));
            d.targets.push_back(tgt(K::Command, hq, "HEADQUARTERS", 2));
            d.targets.push_back(tgt(K::Bunker, hq + Vec3{100, 0, -70}, "HQ BUNKER", 2));
            d.targets.push_back(tgt(K::Radar, hq + Vec3{-90, 0, 80}, "HQ RADAR", 2));
            d.objectives.push_back(destroy(titles[1], 2, "Destroy the headquarters, its bunker and radar."));
            d.waves.push_back(wave(3, AiSkill::Veteran, hq + Vec3{-3000, 0, 3000}, c.safeAlt(hq, 6000, 900)));
            d.objectives.push_back(air(titles[2], {0}, "Shoot down the final wave."));
            ObjectiveDef td;
            td.kind = ObjectiveKind::Touchdown;
            td.title = titles[3];
            td.instruction = "Fly the approach to the runway: gear down, approach speed, touch down.";
            td.hint = "G = GEAR";
            d.objectives.push_back(td);
            ObjectiveDef st;
            st.kind = ObjectiveKind::Stop;
            st.title = titles[4];
            st.instruction = "Brakes on and bring it to a stop on the runway.";
            st.hint = "CTRL = BRAKES";
            d.objectives.push_back(st);
            break;
        }
    }
    return d;
}

Briefing operationBriefing(const std::string& locationId, int k) {
    k = std::clamp(k, 1, kOperationMissions);
    bool coastal = locationIsCoastal(locationId);
    Briefing b;
    char h[64];
    std::snprintf(h, sizeof h, "OPERATIONS %02d", k);
    b.header = h;
    b.title = operationTemplates()[k - 1].title;
    std::string name = locName(locationId);
    std::string story = storyFor(k, name, coastal);
    std::string tip = tipFor(k);
    b.objectives = objectiveTitles(k, coastal);
    b.display = story + " " + tip + " Good luck, Captain.";
    b.parts.push_back("Captain, " + name + " operations, mission " + std::to_string(k) + ", " + b.title + ". " + story);
    std::string obj = "Your objectives: ";
    for (size_t i = 0; i < b.objectives.size(); ++i) obj += spokenCaps(b.objectives[i]) + (i + 1 < b.objectives.size() ? "; " : ".");
    b.parts.push_back(obj);
    b.parts.push_back(tip + " Good luck, Captain.");
    for (const auto& p : b.parts) b.speech += (b.speech.empty() ? "" : " ") + p;
    return b;
}

std::vector<VoiceLine> operationVoiceLines() {
    std::vector<VoiceLine> v;
    std::set<std::string> seen;
    auto add = [&](Speaker s, const std::string& t) {
        if (!t.empty() && seen.insert(voiceKey(s, t)).second) v.push_back({s, t});
    };
    for (const LocationDef& l : locations()) {
        for (int k = 1; k <= kOperationMissions; ++k) {
            for (const auto& p : operationBriefing(l.id, k).parts) add(Speaker::Captain, p);
            for (const auto& t : objectiveTitles(k, locationIsCoastal(l.id))) add(Speaker::Awacs, nextObjectiveLine(t));
            add(Speaker::Awacs, k < kOperationMissions ? std::string("Good work, Captain. That operation is complete.")
                                                       : "Operation complete. Outstanding work over " + l.name + ", Captain.");
        }
        add(Speaker::Awacs, nextObjectiveLine("SINK THE PATROL BOATS"));
    }
    return v;
}

}  // namespace sky
