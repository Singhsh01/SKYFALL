// The 61-mission campaign on any world location.
//
// The missions are written for the Skyfall Desert (32 x 32 km, sea to the east, airfield in the
// west). Every location map has the same size, so a mission keeps its layout: the whole mission
// is mirrored / rotated about the map centre (one of the 8 symmetries of the square, chosen so
// that sea targets land on water and ground targets on reachable ground), then every target group
// is moved to the nearest suitable ground (or water), altitudes are re-referenced to the local
// terrain, the tanker orbit is kept inside the map and clear of the peaks, the carrier is placed
// on open water (or the mission starts in the air over the airfield when there is no sea), and
// the landing uses the location's runway. Without sea, boats become armoured vehicles and oil
// rigs fuel tanks; without a railway, trains become truck convoys. The objective titles change
// with them, and every possible title is part of the voice pack.
#include "Campaign.hpp"

#include "../sim/TerrainAnalysis.hpp"
#include "../sim/WorldLayout.hpp"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {

using K = GroundKind;

bool isSeaKind(K k) { return k == K::Ship || k == K::OilRig; }

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
}

std::string landText(std::string s, const CampaignVariant& v) {
    if (!v.sea) {
        replaceAll(s, "SINK ", "DESTROY ");
        replaceAll(s, "OIL RIGS", "FUEL TANKS");
        replaceAll(s, "OIL RIG", "FUEL TANK");
        replaceAll(s, "RIG PATROL", "DEPOT PATROL");
        replaceAll(s, "FRIENDLY RIG", "FRIENDLY DEPOT");
        replaceAll(s, "BOATS", "VEHICLES");
        replaceAll(s, "BOAT", "VEHICLE");
        replaceAll(s, "Sink ", "Destroy ");
        replaceAll(s, "oil rigs", "fuel tanks");
        replaceAll(s, "oil rig", "fuel tank");
        replaceAll(s, "boats", "vehicles");
        replaceAll(s, "boat", "vehicle");
    }
    if (!v.rail) {
        replaceAll(s, "SUPPLY TRAIN", "SUPPLY CONVOY");
        replaceAll(s, "ARMOURED TRAIN", "ARMOURED CONVOY");
        replaceAll(s, "LOCOMOTIVE", "CONVOY LEAD");
        replaceAll(s, "WAGON", "TRUCK");
        replaceAll(s, "train", "convoy");
    }
    if (!v.carrier) replaceAll(s, "RETURN TO THE CARRIER", "RETURN TO BASE");
    return s;
}

// ---- the 8 symmetries of the square map (about its centre) ----
struct Sym {
    int k = 0;   // bit 0: swap x / z, bit 1: negate x, bit 2: negate z
    void apply(double& x, double& z) const {
        if (k & 1) std::swap(x, z);
        if (k & 2) x = -x;
        if (k & 4) z = -z;
    }
    double heading(double h) const {
        double x = std::cos(h), z = std::sin(h);
        apply(x, z);
        return std::atan2(z, x);
    }
    bool mirrors() const { int n = (k & 1) + ((k >> 1) & 1) + ((k >> 2) & 1); return n % 2 == 1; }
};

struct World {
    const Terrain& t;
    const WorldLayout& L;
    Vec3 base;
    double groundCap;   // targets above this are beyond what a jet can manoeuvre for in that air
    World(const Terrain& t_, const WorldLayout& L_) : t(t_), L(L_), base(t_.runway().center) { groundCap = base.y + 1800.0; }

    static bool inside(double x, double z, double m) { return std::fabs(x) < Terrain::kHalfSize - m && std::fabs(z) < Terrain::kHalfSize - m; }
    bool landOk(double x, double z) const {
        if (!inside(x, z, 1200.0) || t.isWater(x, z) || t.onRunway(x, z, 400.0)) return false;
        double h = t.height(x, z);
        if (h > groundCap || h < 1.0) return false;
        if (t.normal(x, z, 18.0).y <= 0.9) return false;
        // room for an attack run: no wall towering over the target close by
        return maxSurfaceAround(t, x, z, 1200.0) - h < 650.0;
    }
    bool waterOk(double x, double z) const { return inside(x, z, 1200.0) && t.height(x, z) < -4.0; }
    /// A target (and the circle it drives / sails) on suitable ground or water.
    bool targetOk(const TargetDef& d, double x, double z) const {
        bool sea = isSeaKind(d.kind);
        auto ok = [&](double px, double pz) { return sea ? waterOk(px, pz) : landOk(px, pz); };
        if (!ok(x, z)) return false;
        if (d.pathRadius > 0)
            for (int k = 0; k < 8; ++k) {
                double a = k * kPi / 4.0;
                if (!ok(x + std::cos(a) * d.pathRadius, z + std::sin(a) * d.pathRadius)) return false;
            }
        return true;
    }
};

/// Moves a group of targets by the smallest offset that puts as many of them as possible on
/// suitable ground (the group keeps its shape), then fixes stragglers one by one.
void placeGroup(const World& w, const HeightGrid& grid, std::vector<TargetDef*>& g) {
    if (g.empty()) return;
    auto count = [&](double dx, double dz) {
        int n = 0;
        for (const TargetDef* d : g) n += w.targetOk(*d, d->x + dx, d->z + dz) ? 1 : 0;
        return n;
    };
    double bx = 0, bz = 0;
    int best = count(0, 0);
    const int all = static_cast<int>(g.size());
    for (double r = 250.0; r <= 9000.0 && best < all; r += 250.0) {
        int steps = std::max(8, static_cast<int>(2.0 * kPi * r / 250.0));
        for (int k = 0; k < steps; ++k) {
            double a = 2.0 * kPi * k / steps, dx = std::cos(a) * r, dz = std::sin(a) * r;
            if (!World::inside(g[0]->x + dx, g[0]->z + dz, 1500.0)) continue;
            int n = count(dx, dz);
            if (n > best) { best = n; bx = dx; bz = dz; }
            if (best == all) break;
        }
    }
    for (TargetDef* d : g) { d->x += bx; d->z += bz; }
    for (TargetDef* d : g) {
        if (w.targetOk(*d, d->x, d->z)) continue;
        // a moving target that cannot fit its circle drives / sails a smaller one, or holds
        for (double shrink : {1.0, 0.6, 0.35, 0.0}) {
            double r0 = d->pathRadius;
            d->pathRadius = r0 * shrink;
            bool done = false;
            for (double r = 0.0; r <= 2500.0 && !done; r += 100.0) {
                int steps = r == 0 ? 1 : std::max(8, static_cast<int>(2.0 * kPi * r / 100.0));
                for (int k = 0; k < steps; ++k) {
                    double a = 2.0 * kPi * k / steps;
                    double x = d->x + std::cos(a) * r, z = d->z + std::sin(a) * r;
                    if (w.targetOk(*d, x, z)) { d->x = x; d->z = z; done = true; break; }
                }
            }
            if (!done && isSeaKind(d->kind)) {
                // the nearest open water anywhere on the map (a narrow fjord, a bay)
                Vec3 o;
                if (findOpenWater(w.t, grid, {d->x, 0, d->z}, std::max(120.0, d->pathRadius + 80.0), 4.0, o) && w.targetOk(*d, o.x, o.z)) {
                    d->x = o.x; d->z = o.z; done = true;
                }
            }
            if (done) break;
            d->pathRadius = r0;
            if (shrink == 0.0) d->pathRadius = 0.0;
        }
    }
}

double clampIn(double v, double m) { return clampd(v, -(Terrain::kHalfSize - m), Terrain::kHalfSize - m); }

}  // namespace

CampaignVariant campaignVariantFor(const Terrain& t, const WorldLayout& L) {
    CampaignVariant v;
    if (!t.info().real && t.info().id == "desert") return v;   // the original campaign
    HeightGrid g = sampleHeightGrid(t, 250.0, 15000.0);
    // boats need open water; the carrier a real stretch of sea (not a fjord between 1500 m walls)
    Vec3 water;
    double sea = seaFraction(g);
    v.sea = sea > 0.02 && findOpenWater(t, g, {0, 0, 0}, 300.0, 5.0, water);
    DeckInfo d;
    v.carrier = v.sea && sea > 0.10 && placeCarrierDeck(t, g, {13600.0, 0.0, 1500.0}, d);
    v.rail = !L.railway.pts.empty();
    v.climbOffset = mission02Offset(t);
    v.runway27 = mission03Approach(t).dir < 0;
    return v;
}

double mission02Offset(const Terrain& t) {
    if (!t.info().real && t.info().id == "desert") return 0.0;
    const RunwayInfo& rw = t.runway();
    // mission 2 starts 2 km east of the runway at 1250 m and climbs to 2000 m: on a high
    // airfield every height is raised by the same amount (whole hundreds of metres)
    double need = maxSurfaceAround(t, rw.center.x + 2000.0, rw.center.z, 5000.0) + 450.0 - 1250.0;
    return need <= 0 ? 0.0 : std::ceil(need / 100.0) * 100.0;
}

CampaignMissionDef retitleCampaignMission(const CampaignMissionDef& src, const CampaignVariant& v) {
    CampaignMissionDef d = src;
    for (TargetDef& t : d.targets) {
        if (!v.sea && t.kind == K::Ship) t.kind = K::Tank;
        if (!v.sea && t.kind == K::OilRig) t.kind = K::FuelTank;
        if (!v.rail && t.anchor == TargetDef::Anchor::Railway) {
            t.kind = K::Truck;
            t.anchor = TargetDef::Anchor::None;
        }
        t.label = landText(t.label, v);
    }
    if (d.carrier && !v.carrier) {
        d.carrier = false;
        // no catapult: the mission starts in the air over the airfield
        std::vector<ObjectiveDef> keep;
        for (const ObjectiveDef& o : d.objectives)
            if (o.kind != ObjectiveKind::FullPower && o.kind != ObjectiveKind::Launch && o.kind != ObjectiveKind::GearUp &&
                o.kind != ObjectiveKind::Climb)
                keep.push_back(o);
        d.objectives = keep;
    }
    for (ObjectiveDef& o : d.objectives) {
        o.title = landText(o.title, v);
        o.instruction = landText(o.instruction, v);
        if (o.title == "RETURN TO BASE") o.instruction = "Head back to the airfield and make a low pass over the runway, below 700 m.";
    }
    return d;
}

CampaignMissionDef adaptCampaignMission(const CampaignMissionDef& src, const Terrain& t, const WorldLayout& L) {
    if (!t.info().real && t.info().id == "desert") return resolveCampaignAnchors(src, t, L);
    const CampaignVariant v = campaignVariantFor(t, L);
    CampaignMissionDef d = retitleCampaignMission(src, v);
    d.location = t.info().id;
    World w(t, L);
    const RunwayInfo& rw = t.runway();
    HeightGrid g = sampleHeightGrid(t, 250.0, 15000.0);
    const DeckInfo desertDeck = campaignCarrierDeck();

    // ---- 1. the symmetry that suits the terrain best ----
    auto score = [&](const Sym& s) {
        double sc = 0;
        for (const TargetDef& td : d.targets) {
            if (td.anchor != TargetDef::Anchor::None) continue;
            double x = td.x, z = td.z;
            s.apply(x, z);
            if (isSeaKind(td.kind)) sc += w.waterOk(x, z) ? 3.0 : 0.0;
            else sc += w.landOk(x, z) ? 1.0 : 0.0;
        }
        if (src.carrier && v.carrier) {
            double x = desertDeck.center.x, z = desertDeck.center.z;
            s.apply(x, z);
            sc += t.height(x, z) < -18.0 ? 6.0 : 0.0;
        }
        for (const ObjectiveDef& o : d.objectives) {
            if (o.kind != ObjectiveKind::Waypoint || o.cityAnchor || o.title == "APPROACH GATE" || o.title == "RETURN TO THE CARRIER" || o.title == "RETURN TO BASE") continue;
            double x = o.x, z = o.z;
            s.apply(x, z);
            sc += t.surfaceHeight(x, z) < w.groundCap ? 0.5 : 0.0;
        }
        return sc;
    };
    Sym sym;
    double bestScore = -1;
    for (int k = 0; k < 8; ++k) {
        Sym s{k};
        double sc = score(s);
        if (sc > bestScore + 1e-9) { bestScore = sc; sym = s; }
    }

    // ---- 2. transform everything ----
    sym.apply(d.startX, d.startZ);
    if (d.startHeading < 1e8) d.startHeading = sym.heading(d.startHeading);
    for (TargetDef& td : d.targets) {
        if (td.anchor != TargetDef::Anchor::None) continue;
        sym.apply(td.x, td.z);
        td.heading = sym.heading(td.heading);
        if (td.pathRadius > 0) {
            td.pathAngle = sym.heading(td.pathAngle);
        }
    }
    for (WaveDef& wv : d.waves) {
        sym.apply(wv.x, wv.z);
        wv.x = clampIn(wv.x, 2500.0);
        wv.z = clampIn(wv.z, 2500.0);
    }
    for (ObjectiveDef& o : d.objectives) {
        if (o.kind != ObjectiveKind::Waypoint || o.cityAnchor) continue;   // city waypoints follow the city
        sym.apply(o.x, o.z);
        o.x = clampIn(o.x, 2000.0);
        o.z = clampIn(o.z, 2000.0);
    }
    if (d.tanker) sym.apply(d.tankerX, d.tankerZ);

    // ---- 3. targets on suitable ground / water, infrastructure the layout lacks ----
    {
        // trains on a map without a railway: a truck convoy on gentle ground near a town
        std::vector<TargetDef*> convoy;
        for (size_t i = 0; i < d.targets.size(); ++i)
            if (src.targets[i].anchor == TargetDef::Anchor::Railway && d.targets[i].anchor == TargetDef::Anchor::None) convoy.push_back(&d.targets[i]);
        if (!convoy.empty()) {
            Vec3 near = L.towns.empty() ? Vec3{0, 0, 0} : L.towns[0].center;
            Vec3 c;
            if (!findFlatSpot(t, near, 9000.0, 0.08, 500.0, c)) c = near;
            for (size_t i = 0; i < convoy.size(); ++i) {
                TargetDef* td = convoy[i];
                td->x = c.x; td->z = c.z;
                td->pathRadius = 420.0;
                td->pathAngle = -static_cast<double>(i) * (45.0 / 420.0) * (td->railSpeed >= 0 ? 1.0 : -1.0);
            }
        }
        // factories on a map without an industrial zone
        std::vector<Vec3> placed;
        for (TargetDef& td : d.targets) {
            if (td.anchor != TargetDef::Anchor::Industrial || td.index < static_cast<int>(L.factories.size())) continue;
            const Town* it = L.industrialTown();
            Vec3 near = it ? it->center : (L.towns.empty() ? Vec3{0, 0, 0} : L.towns[0].center);
            Vec3 c;
            if (!findFlatSpot(t, near, 9000.0, 0.08, 120.0, c, placed, 260.0)) c = {near.x + 300.0 * placed.size(), 0, near.z};
            placed.push_back(c);
            td.anchor = TargetDef::Anchor::None;
            td.x = c.x; td.z = c.z;
        }
    }
    {
        // groups: same objective group and the same medium (land / sea)
        std::vector<std::vector<TargetDef*>> groups;
        std::vector<std::pair<int, bool>> keys;
        for (size_t i = 0; i < d.targets.size(); ++i) {
            TargetDef& td = d.targets[i];
            if (td.anchor != TargetDef::Anchor::None) continue;
            if (src.targets[i].anchor == TargetDef::Anchor::Railway || src.targets[i].anchor == TargetDef::Anchor::Industrial) continue;   // placed above
            std::pair<int, bool> key{td.group * 2 + (td.team == Team::Blue ? 1 : 0), isSeaKind(td.kind)};
            size_t gi = 0;
            while (gi < keys.size() && keys[gi] != key) ++gi;
            if (gi == keys.size()) { keys.push_back(key); groups.emplace_back(); }
            groups[gi].push_back(&td);
        }
        for (auto& grp : groups) placeGroup(w, g, grp);
    }

    // the bridge a mission strikes: the longest one (rail first) that is reachable, not one at
    // the bottom of a deep canyon or high in the mountains
    {
        int best = -1;
        double bestScore = -1e18;
        for (int i = 0; i < static_cast<int>(L.bridges.size()); ++i) {
            const Bridge& b = L.bridges[i];
            Vec3 mid = (b.a + b.b) * 0.5;
            double len = std::hypot(b.b.x - b.a.x, b.b.z - b.a.z);
            double wall = maxSurfaceAround(t, mid.x, mid.z, 1500.0) - mid.y;
            double sc = len + (b.rail ? 400.0 : 0.0) - (wall > 700.0 ? 5000.0 : 0.0) - (mid.y > w.groundCap ? 5000.0 : 0.0) -
                        (World::inside(mid.x, mid.z, 2000.0) ? 0.0 : 3000.0);
            if (sc > bestScore) { bestScore = sc; best = i; }
        }
        for (TargetDef& td : d.targets)
            if (td.anchor == TargetDef::Anchor::Bridge) td.bridge = best;
    }

    // ---- 4. heights: the same clearance above the local terrain as in the desert ----
    const double kDesertGround = 250.0;
    for (WaveDef& wv : d.waves) {
        double clear = std::max(wv.altitude - kDesertGround, 700.0);
        double top = std::max(maxSurfaceAround(t, wv.x, wv.z, wv.radius), maxSurfaceAround(t, wv.x, wv.z, wv.radius + 1500.0));
        wv.altitude = std::max(top + 450.0, std::max(rw.center.y, 0.0) + clear);
    }
    if (d.tanker) {
        const double r = d.tankerRadius;   // the desert radius: a tighter orbit is a harder join
        auto ringTop = [&](double cx, double cz) {
            double top = 0;
            for (int k = 0; k < 24; ++k) {
                double a = k * kPi / 12.0;
                top = std::max(top, maxSurfaceAround(t, cx + std::cos(a) * r, cz + std::sin(a) * r, 1500.0));
            }
            return top;
        };
        // the orbit over the lowest ground in reach of where the desert mission had it (a
        // tanker high above the peaks is a hard join in thin air)
        double want = clampIn(d.tankerX, r + 1800.0), wantZ = clampIn(d.tankerZ, r + 1800.0);
        double bestCost = 1e18, bx = want, bz = wantZ;
        for (double cx = -12000.0; cx <= 12000.0; cx += 1500.0)
            for (double cz = -12000.0; cz <= 12000.0; cz += 1500.0) {
                if (!World::inside(cx, cz, r + 1800.0)) continue;
                double dist = std::hypot(cx - want, cz - wantZ);
                double cost = ringTop(cx, cz) + 0.08 * dist;
                if (cost < bestCost) { bestCost = cost; bx = cx; bz = cz; }
            }
        d.tankerX = bx;
        d.tankerZ = bz;
        d.tankerAlt = std::max(ringTop(bx, bz) + 900.0, std::max(rw.center.y, 0.0) + std::max(src.tankerAlt - kDesertGround, 1200.0));
    }

    // ---- 5. the carrier, or an airborne start over the airfield ----
    if (src.carrier) {
        if (d.carrier) {
            double x = desertDeck.center.x, z = desertDeck.center.z;
            sym.apply(x, z);
            DeckInfo deck;
            if (!placeCarrierDeck(t, g, {x, 0, z}, deck)) placeCarrierDeck(t, g, {13600.0, 0.0, 1500.0}, deck);
            d.deck = deck;
            Vec3 c = deck.catapultStart();
            d.startX = c.x; d.startZ = c.z; d.startAlt = deck.center.y; d.startSpeed = 0; d.startHeading = deck.heading;
            for (ObjectiveDef& o : d.objectives)
                if (o.title == "RETURN TO THE CARRIER") { o.x = deck.center.x; o.z = deck.center.z; }
        } else {
            d.deck = DeckInfo{};
            // climb out along the runway, towards the lower end
            double best = -1e9;
            int dir = 1;
            for (int s : {1, -1}) {
                double x = rw.center.x + s * (rw.length * 0.5 + 3500.0);
                double clear = -maxSurfaceAround(t, clampIn(x, 2500.0), rw.center.z, 3000.0);
                if (clear > best) { best = clear; dir = s; }
            }
            d.startX = clampIn(rw.center.x + dir * (rw.length * 0.5 + 1500.0), 2500.0);
            d.startZ = rw.center.z;
            d.startAlt = std::max(std::max(maxSurfaceAround(t, d.startX, d.startZ, 1500.0), maxSurfaceAround(t, d.startX, d.startZ, 3500.0)) + 450.0, rw.center.y + 500.0);
            d.startSpeed = 210.0;
            d.startHeading = dir > 0 ? 0.0 : kPi;
            for (ObjectiveDef& o : d.objectives)
                if (o.title == "RETURN TO BASE") { o.x = rw.center.x; o.z = rw.center.z; }
        }
    } else {
        d.startX = clampIn(d.startX, 2500.0);
        d.startZ = clampIn(d.startZ, 2500.0);
        double clear = std::max(src.startAlt - kDesertGround, 600.0);
        double top = std::max(maxSurfaceAround(t, d.startX, d.startZ, 1500.0), maxSurfaceAround(t, d.startX, d.startZ, 3000.0));
        d.startAlt = std::max(top + 450.0, std::max(rw.center.y, 0.0) + clear);
    }

    // ---- 6. home: the approach gate of the runway, from the side with the clearer glide path ----
    {
        int dir = 1;
        double best = -1e18;
        for (int s : {1, -1}) {
            Vec3 thr{rw.center.x - s * (rw.length * 0.5 - 120.0), rw.center.y, rw.center.z};
            Vec3 far{thr.x - s * 6000.0, rw.center.y + 6000.0 * std::tan(3.0 * kDeg), thr.z};
            if (!World::inside(far.x, far.z, 300.0)) { far.x = clampIn(far.x, 300.0); }
            double c = segmentClearance(t, far, thr + Vec3{0, 15, 0}) - (s == 1 ? 0.0 : 5.0);   // prefer runway 09 when equal
            if (c > best) { best = c; dir = s; }
        }
        Vec3 thr{rw.center.x - dir * (rw.length * 0.5 - 120.0), rw.center.y, rw.center.z};
        d.homeX = thr.x;
        d.homeZ = thr.z;
        for (ObjectiveDef& o : d.objectives)
            if (o.title == "APPROACH GATE") {
                o.x = clampIn(thr.x - dir * 3000.0, 1000.0);
                o.z = thr.z;
                o.instruction = std::string("Fly to the approach gate 3 km from the runway, then line up on heading ") + (dir > 0 ? "090." : "270.");
            }
        for (ObjectiveDef& o : d.objectives)
            if (o.kind == ObjectiveKind::Touchdown)
                o.instruction = std::string("Gear down (G), fly the glide path and touch down on runway ") + (dir > 0 ? "09." : "27.");
    }
    return resolveCampaignAnchors(d, t, L);
}

}  // namespace sky
