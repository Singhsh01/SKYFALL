// World detail: the props, night lights, military sites and traffic that make a map feel
// inhabited. Everything is placed deterministically from the terrain and the WorldLayout, so
// every map (real or procedural) gets the same treatment and the result never changes between
// runs, replays or machines.
//
// Placement rules ("collision aware"):
//  * nothing on water, steep ground, the runway, its approach corridors, taxiways or the apron;
//  * props keep clear of each other (footprint radius) and of road / railway centre lines,
//    except street furniture which stands on the verge on purpose;
//  * tall props (masts, cranes, towers) stay out of the runway approach and below 45 m.
#include "WorldLayout.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sky {

namespace {

unsigned mix(unsigned a, unsigned b) {
    unsigned h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
    return h;
}
double rnd(unsigned& s) { s = mix(s, 0x51ED27u); return (s & 0xFFFFFF) / double(0x1000000); }

/// Spatial hash of points (road / rail centre lines, placed footprints) for clearance queries.
struct PointHash {
    double cell = 100.0;
    std::unordered_map<long long, std::vector<Vec3>> m;   // y holds the radius for footprints
    long long key(int i, int j) const { return (static_cast<long long>(i) << 32) ^ static_cast<unsigned>(j); }
    void add(const Vec3& p) {
        int i = static_cast<int>(std::floor(p.x / cell)), j = static_cast<int>(std::floor(p.z / cell));
        m[key(i, j)].push_back(p);
    }
    /// Smallest (distance - stored radius) to anything near p, searching `range` metres.
    double clearance(double x, double z, double range) const {
        int r = static_cast<int>(std::ceil(range / cell));
        int ci = static_cast<int>(std::floor(x / cell)), cj = static_cast<int>(std::floor(z / cell));
        double best = 1e18;
        for (int j = cj - r; j <= cj + r; ++j)
            for (int i = ci - r; i <= ci + r; ++i) {
                auto it = m.find(key(i, j));
                if (it == m.end()) continue;
                for (const Vec3& q : it->second) best = std::min(best, std::hypot(q.x - x, q.z - z) - q.y);
            }
        return best;
    }
};

struct Placer {
    const Terrain& t;
    WorldLayout& L;
    PointHash routes;      // road and railway centre lines (radius = half width)
    PointHash used;        // placed footprints
    RunwayInfo rw;

    Placer(const Terrain& tt, WorldLayout& l) : t(tt), L(l), rw(tt.runway()) {
        routes.cell = 80.0;
        for (const Polyline& r : L.roads) for (const Vec3& p : r.pts) routes.add({p.x, 4.6, p.z});
        for (const Vec3& p : L.railway.pts) routes.add({p.x, 3.0, p.z});
        used.cell = 60.0;
        for (const Vec3& f : L.factories) used.add({f.x, 70.0, f.z});
        for (const WindTurbine& w : L.turbines) used.add({w.pos.x, 45.0, w.pos.z});
    }

    /// Runway strip, approach corridors, taxiways and apron (the airbase keeps its own props).
    bool airfield(double x, double z, double r, bool tall) const {
        double dx = x - rw.center.x, dz = z - rw.center.z;
        double hl = rw.length * 0.5;
        if (std::fabs(dx) < hl + 120 + r && std::fabs(dz) < rw.width * 0.5 + 90 + r) return true;   // runway + strip
        if (dx > -hl - 50 && dx < hl && dz > 120 - r && dz < 700 + r) return true;                  // taxiways, apron, hangars
        double beyond = std::fabs(dx) - hl;
        if (beyond > 0 && beyond < (tall ? 6000 : 900) && std::fabs(dz) < (tall ? 700 : 160) + r) return true;   // approaches
        return false;
    }
    bool groundOk(double x, double z, double r, double maxSlope) const {
        if (std::fabs(x) > Terrain::kHalfSize - 400 || std::fabs(z) > Terrain::kHalfSize - 400) return false;
        double h = t.height(x, z);
        if (h < Terrain::kSeaLevel + 1.5) return false;
        double lo = h, hi = h;
        for (int k = 0; k < 4; ++k) {
            double a = k * kPi / 2 + 0.4;
            double hk = t.height(x + std::cos(a) * r, z + std::sin(a) * r);
            if (hk < Terrain::kSeaLevel + 1.0) return false;
            lo = std::min(lo, hk); hi = std::max(hi, hk);
        }
        return hi - lo <= maxSlope * 2.0 * std::max(r, 1.0);
    }
    /// Try to place a prop: footprint radius r, maximum slope, clear of routes unless onVerge.
    bool place(PropKind k, double x, double z, double yaw, double r, unsigned char variant = 0, double size = 0,
               double maxSlope = 0.12, bool onVerge = false, bool tall = false, bool airbase = false) {
        if (!airbase && airfield(x, z, r, tall)) return false;
        if (!groundOk(x, z, r, maxSlope)) return false;
        if (!onVerge && routes.clearance(x, z, r + 20) < r + 3.0) return false;
        if (used.clearance(x, z, r + 80) < r) return false;
        SceneProp p;
        p.pos = {x, t.height(x, z), z};
        p.yaw = yaw;
        p.kind = k;
        p.variant = variant;
        p.size = size;
        L.props.push_back(p);
        used.add({x, r, z});
        return true;
    }
    void light(double x, double y, double z, LightKind k, float phase = 0) { L.lights.push_back({{x, y, z}, k, phase}); }
};

Vec3 rot(double x, double z, double yaw) { return {x * std::cos(yaw) - z * std::sin(yaw), 0, x * std::sin(yaw) + z * std::cos(yaw)}; }

// ---------------------------------------------------------------------------------------------
// Military: the army compound beside the runway and forward operating bases
// ---------------------------------------------------------------------------------------------

/// Army compound on the north side of the runway (the hangars and apron are on the south side).
void airbaseCompound(Placer& P, unsigned seed) {
    const RunwayInfo& rw = P.rw;
    const double cx = rw.center.x, cz = rw.center.z;
    const double zA = cz - 300, zB = cz - 455;   // compound rows (north of the runway strip)
    auto put = [&](PropKind k, double x, double z, double yaw, double r, unsigned char v = 0, double size = 0) {
        return P.place(k, x, z, yaw, r, v, size, 0.2, false, false, true);
    };
    // tank park: revetted rows of tanks and APCs (west)
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < 7; ++i) {
            double x = cx - 1240 + i * 22, z = zA - 10 - row * 34;
            put(i % 3 == 2 ? PropKind::Apc : PropKind::Tank, x, z, -kPi / 2, 6.0, static_cast<unsigned char>(row));
        }
    put(PropKind::Revetment, cx - 1175, zA + 10, 0, 4, 0, 160);
    // barracks blocks with a parade square
    for (int row = 0; row < 2; ++row)
        for (int i = 0; i < 5; ++i) put(PropKind::Barracks, cx - 960 + i * 52, zB + 10 + row * 44, 0, 20, static_cast<unsigned char>(i & 1));
    put(PropKind::CommandPost, cx - 690, zA + 4, 0, 16);
    put(PropKind::WaterTower, cx - 1010, zA + 14, 0, 6);
    // helipads with transport and attack helicopters, lit for night operations
    for (int i = 0; i < 4; ++i) {
        double x = cx - 520 + i * 62, z = zA - 40;
        if (!put(PropKind::Helipad, x, z, 0, 15)) continue;
        bool attack = i % 2 == 1;
        SceneProp h;
        h.pos = {x, P.t.height(x, z), z};
        h.yaw = -kPi / 2 + (i - 1.5) * 0.08;
        h.kind = attack ? PropKind::AttackHelicopter : PropKind::Helicopter;
        h.variant = static_cast<unsigned char>(i);
        P.L.props.push_back(h);
        for (int k = 0; k < 6; ++k) {
            double a = k * kPi / 3;
            P.light(x + std::cos(a) * 12, h.pos.y + 0.3, z + std::sin(a) * 12, LightKind::Helipad);
        }
    }
    // motor pool: trucks, fuel bowsers, bladders and containers
    for (int i = 0; i < 8; ++i)
        put(i % 4 == 3 ? PropKind::FuelTruck : (i % 4 == 2 ? PropKind::Jeep : PropKind::Truck), cx - 230 + i * 15, zB + 30, -kPi / 2, 5.0, static_cast<unsigned char>(i));
    for (int i = 0; i < 3; ++i) put(PropKind::FuelBladder, cx - 210 + i * 26, zA - 6, 0, 10);
    for (int i = 0; i < 6; ++i) put(PropKind::Container, cx + 10 + (i % 3) * 14, zB + 50 + (i / 3) * 6.5, 0, 6.5, static_cast<unsigned char>(i));
    // radar and comms (tall: kept east of the threshold approach)
    put(PropKind::RadarMast, cx + 360, zB + 20, 0, 8);
    put(PropKind::Antenna, cx + 420, zB + 10, 0, 4);
    put(PropKind::Antenna, cx + 450, zB + 40, 0, 4);
    // perimeter fence and floodlight masts
    double fx0 = cx - 1300, fx1 = cx + 520;
    P.L.props.push_back({Vec3{(fx0 + fx1) / 2, P.t.height((fx0 + fx1) / 2, zB - 40), zB - 40}, 0, PropKind::Fence, 0, fx1 - fx0});
    for (double x = fx0 + 60; x < fx1; x += 240) {
        if (put(PropKind::FloodMast, x, zB - 30, 0, 2.5)) P.light(x, P.t.height(x, zB - 30) + 17, zB - 30, LightKind::Flood);
        if (put(PropKind::FloodMast, x + 120, zA + 16, 0, 2.5)) P.light(x + 120, P.t.height(x + 120, zA + 16) + 17, zA + 16, LightKind::Flood);
    }
    // gate with a checkpoint on the perimeter road (west end)
    double gx = fx0 - 20, gz = (zA + zB) / 2;
    if (put(PropKind::Checkpoint, gx, gz, 0, 7)) P.light(gx, P.t.height(gx, gz) + 5, gz, LightKind::Checkpoint);
    put(PropKind::Watchtower, fx0 + 10, zB - 30, 0, 3);
    put(PropKind::Watchtower, fx1 - 10, zB - 30, 0, 3);
    // perimeter road: compound -> round the west end of the runway -> the apron road
    Polyline lane;
    double xw = cx - rw.length * 0.5 - 380;
    for (Vec3 p : {Vec3{fx1, 0, gz}, Vec3{cx - 600, 0, gz}, Vec3{gx - 60, 0, gz}, Vec3{xw, 0, gz}, Vec3{xw, 0, cz + 60},
                   Vec3{xw, 0, cz + 760}, Vec3{cx + 400, 0, cz + 760}}) {
        p.y = P.t.height(p.x, p.z) + 0.1;
        lane.pts.push_back(p);
    }
    // resample every 25 m so the vehicles follow the ground
    Polyline fine;
    for (size_t i = 0; i + 1 < lane.pts.size(); ++i) {
        Vec3 a = lane.pts[i], b = lane.pts[i + 1];
        int n = std::max(1, static_cast<int>(std::hypot(b.x - a.x, b.z - a.z) / 25.0));
        for (int k = 0; k < n; ++k) {
            Vec3 p = a + (b - a) * (static_cast<double>(k) / n);
            p.y = P.t.height(p.x, p.z) + 0.1;
            fine.pts.push_back(p);
        }
    }
    fine.pts.push_back(lane.pts.back());
    fine.finalize();
    P.L.lanes.push_back(fine);
    P.L.sites.push_back({Vec3{(fx0 + fx1) / 2, rw.center.y, gz}, (fx1 - fx0) / 2, true, "ARMY COMPOUND"});
    (void)seed;
}

/// A forward operating base: HESCO ring, tents, helipad, armour, watchtowers, lights.
bool forwardBase(Placer& P, double x, double z, double yaw, unsigned seed, const char* name) {
    const double R = 70.0;
    if (!P.groundOk(x, z, R, 0.07) || P.airfield(x, z, R, true)) return false;
    if (P.routes.clearance(x, z, R + 40) < R + 10 || P.used.clearance(x, z, R + 120) < R + 20) return false;
    double g = P.t.height(x, z);
    auto at = [&](double lx, double lz) { Vec3 o = rot(lx, lz, yaw); return Vec3{x + o.x, 0, z + o.z}; };
    auto put = [&](PropKind k, double lx, double lz, double lyaw, double r, unsigned char v = 0, double size = 0) {
        Vec3 p = at(lx, lz);
        return P.place(k, p.x, p.z, yaw + lyaw, r, v, size, 0.25, true, false, false);
    };
    // the wall ring is one prop (size = radius); entrance gap faces +X locally
    P.L.props.push_back({Vec3{x, g, z}, yaw, PropKind::Hesco, 0, R});
    P.used.add({x, 6.0, z});   // the centre stays free for the helipad
    for (int i = 0; i < 4; ++i) put(PropKind::Tent, -32 + i * 15, -36, 0, 6.5, static_cast<unsigned char>(i));
    put(PropKind::CommandPost, -40, 22, 0, 9);
    Vec3 hp = at(10, 0);
    P.L.props.push_back({Vec3{hp.x, P.t.height(hp.x, hp.z), hp.z}, yaw, PropKind::Helipad, 0, 0});
    P.L.props.push_back({Vec3{hp.x, P.t.height(hp.x, hp.z), hp.z}, yaw + 0.3, (seed & 1) ? PropKind::AttackHelicopter : PropKind::Helicopter,
                         static_cast<unsigned char>(seed & 3), 0});
    for (int k = 0; k < 6; ++k) {
        Vec3 l = at(10 + std::cos(k * kPi / 3) * 12, std::sin(k * kPi / 3) * 12);
        P.light(l.x, P.t.height(l.x, l.z) + 0.3, l.z, LightKind::Helipad);
    }
    put(PropKind::Tank, 40, 32, kPi / 2, 5.5);
    put(PropKind::Tank, 48, -30, kPi / 2, 5.5, 1);
    put(PropKind::Truck, -10, 40, 0, 5, 2);
    put(PropKind::Jeep, 52, 6, kPi, 3);
    put(PropKind::FuelBladder, -52, -8, 0, 8);
    put(PropKind::Antenna, -55, 30, 0, 3);
    for (int k = 0; k < 4; ++k) {
        double a = kPi / 4 + k * kPi / 2;
        Vec3 w = at(std::cos(a) * (R - 6), std::sin(a) * (R - 6));
        SceneProp t;
        t.pos = {w.x, P.t.height(w.x, w.z), w.z};
        t.kind = PropKind::Watchtower;
        P.L.props.push_back(t);
        P.light(w.x, t.pos.y + 9.5, w.z, LightKind::Flood);
    }
    Vec3 gate = at(R + 6, 0);
    P.light(gate.x, P.t.height(gate.x, gate.z) + 4, gate.z, LightKind::Checkpoint);
    P.used.add({x, R, z});
    P.L.sites.push_back({Vec3{x, g, z}, R, false, name});
    return true;
}

// ---------------------------------------------------------------------------------------------
// Civilian: street lights, billboards, filling stations, parked cars, ports and yards
// ---------------------------------------------------------------------------------------------

bool inTown(const WorldLayout& L, double x, double z, double margin, const Town** which = nullptr) {
    for (const Town& tw : L.towns)
        if (std::hypot(x - tw.center.x, z - tw.center.z) < tw.radius + margin) { if (which) *which = &tw; return true; }
    return false;
}

void streetFurniture(Placer& P, unsigned seed) {
    const RunwayInfo& rw = P.rw;
    for (size_t ri = 0; ri < P.L.roads.size(); ++ri) {
        const Polyline& r = P.L.roads[ri];
        double len = r.length();
        double lastLamp = -1e9;
        int side = 1;
        for (double s = 10; s < len - 10; s += 5) {
            Vec3 tan;
            Vec3 p = r.at(s, &tan);
            const Town* tw = nullptr;
            bool town = inTown(P.L, p.x, p.z, 120, &tw);
            bool base = std::fabs(p.x - rw.center.x) < rw.length * 0.5 + 800 && std::fabs(p.z - rw.center.z) < 1400;
            double spacing = town ? 42.0 : (base ? 60.0 : 0.0);
            if (spacing <= 0 || s - lastLamp < spacing) continue;
            if (p.y - P.t.height(p.x, p.z) > 4.0) continue;   // on a bridge: the parapet has no lamps
            lastLamp = s;
            Vec3 n{-tan.z, 0, tan.x};
            double off = 7.0 * side;
            double x = p.x + n.x * off, z = p.z + n.z * off;
            if (P.airfield(x, z, 1, false) || P.t.height(x, z) < 1.0) continue;
            SceneProp lp;
            lp.pos = {x, std::min(P.t.height(x, z), p.y), z};
            lp.yaw = std::atan2(-n.z * side, -n.x * side);   // arm reaches over the road
            lp.kind = PropKind::Streetlamp;
            P.L.props.push_back(lp);
            Vec3 head{x - n.x * side * 2.2, lp.pos.y + 9.0, z - n.z * side * 2.2};
            P.light(head.x, head.y, head.z, LightKind::Street);
            if (town && tw && tw->kind == TownKind::City) side = -side;   // avenues: both sides alternately
        }
        // billboards and a filling station on the approach to each town
        for (const Town& tw : P.L.towns) {
            double bestD = 1e18, bestS = -1;
            for (size_t i = 0; i < r.pts.size(); ++i) {
                double d = std::fabs(std::hypot(r.pts[i].x - tw.center.x, r.pts[i].z - tw.center.z) - (tw.radius + 160));
                if (d < bestD) { bestD = d; bestS = r.cum[i]; }
            }
            if (bestS < 0 || bestD > 40) continue;
            unsigned h = mix(seed, static_cast<unsigned>(ri * 131 + static_cast<size_t>(tw.center.x)));
            Vec3 tan;
            Vec3 p = r.at(bestS, &tan);
            Vec3 n{-tan.z, 0, tan.x};
            double yaw = std::atan2(tan.z, tan.x);
            for (int k = 0; k < 2; ++k) {
                double sk = bestS + (k ? 70 : -70);
                if (sk < 0 || sk > len) continue;
                Vec3 q = r.at(sk, &tan);
                double sd = k ? -1 : 1;
                double x = q.x + n.x * 18 * sd, z = q.z + n.z * 18 * sd;
                if (P.place(PropKind::Billboard, x, z, yaw + kPi / 2, 6, static_cast<unsigned char>((h >> (k * 3)) & 7), 0, 0.35, true))
                    P.light(x, P.t.height(x, z) + 9, z, LightKind::Billboard);
            }
            if ((h & 3) != 0) {
                double x = p.x + n.x * 26, z = p.z + n.z * 26;
                if (P.place(PropKind::GasStation, x, z, yaw, 16, static_cast<unsigned char>(h >> 8), 0, 0.12, true))
                    P.light(x, P.t.height(x, z) + 5.2, z, LightKind::Canopy);
            }
        }
    }
    // parked cars along town streets (kerbside, both sides)
    for (const Town& tw : P.L.towns) {
        if (tw.kind == TownKind::Industrial) continue;
        unsigned s = mix(seed, static_cast<unsigned>(tw.center.x * 7 + tw.center.z));
        int want = tw.kind == TownKind::City ? 70 : (tw.kind == TownKind::Town ? 34 : 14);
        int placed = 0;
        for (int tries = 0; tries < want * 6 && placed < want; ++tries) {
            const Polyline* best = nullptr;
            for (const Polyline& r : P.L.roads) if (!r.pts.empty()) { best = &r; if (rnd(s) < 0.5) break; }
            if (!best) break;
            double sAlong = rnd(s) * best->length();
            Vec3 tan;
            Vec3 p = best->at(sAlong, &tan);
            if (std::hypot(p.x - tw.center.x, p.z - tw.center.z) > tw.radius) continue;
            Vec3 n{-tan.z, 0, tan.x};
            double sd = rnd(s) < 0.5 ? -1 : 1;
            double x = p.x + n.x * 5.6 * sd, z = p.z + n.z * 5.6 * sd;
            if (P.used.clearance(x, z, 20) < 2.6) continue;
            SceneProp c;
            c.pos = {x, P.t.surfaceHeight(x, z), z};
            c.yaw = std::atan2(tan.z, tan.x);
            c.kind = PropKind::ParkedCar;
            c.variant = static_cast<unsigned char>(s & 15);
            P.L.props.push_back(c);
            P.used.add({x, 2.4, z});
            ++placed;
        }
    }
}

/// Container yards and cranes beside the factories, and a small port at a coastal town.
void industry(Placer& P, unsigned seed) {
    if (!P.L.factories.empty()) {
        const Vec3& f = P.L.factories[0];
        for (int k = 0; k < 2; ++k) {
            double x = f.x - 330 + k * 60, z = f.z + 40;
            if (P.place(PropKind::Crane, x, z, 0.3, 9, 0, 0, 0.15, false, true))
                P.light(x, P.t.height(x, z) + 42, z, LightKind::Beacon, static_cast<float>(k) * 0.4f);
        }
        unsigned s = seed;
        for (int i = 0; i < 40; ++i)
            P.place(PropKind::Container, f.x - 300 + (i % 10) * 7.5, f.z - 30 - (i / 10) * 14, 0, 6.5, static_cast<unsigned char>(rnd(s) * 8), 0, 0.15);
    }
    // port: the coastal town with the most sea nearby gets quayside cranes and boxes
    const Town* port = nullptr;
    double bestWet = 0;
    for (const Town& tw : P.L.towns) {
        int wet = 0;
        for (int k = 0; k < 16; ++k) {
            double a = k * kPi / 8;
            if (P.t.height(tw.center.x + std::cos(a) * (tw.radius + 500), tw.center.z + std::sin(a) * (tw.radius + 500)) < 0) ++wet;
        }
        if (wet > bestWet) { bestWet = wet; port = &tw; }
    }
    if (port && bestWet >= 3) {
        // walk out from the centre to the shore in the wettest direction
        double bestA = 0;
        int bw = -1;
        for (int k = 0; k < 16; ++k) {
            double a = k * kPi / 8;
            int wet = 0;
            for (double r = 300; r < port->radius + 1200; r += 100)
                if (P.t.height(port->center.x + std::cos(a) * r, port->center.z + std::sin(a) * r) < 0) ++wet;
            if (wet > bw) { bw = wet; bestA = a; }
        }
        for (double r = 100; r < port->radius + 1500; r += 20) {
            double x = port->center.x + std::cos(bestA) * r, z = port->center.z + std::sin(bestA) * r;
            if (P.t.height(x, z) < 1.5) {
                double back = 45;
                double qx = x - std::cos(bestA) * back, qz = z - std::sin(bestA) * back;
                double side = bestA + kPi / 2;
                for (int k = -1; k <= 1; k += 2) {
                    double cxr = qx + std::cos(side) * 45 * k, czr = qz + std::sin(side) * 45 * k;
                    if (P.place(PropKind::Crane, cxr, czr, bestA, 9, 1, 0, 0.3, true, true))
                        P.light(cxr, P.t.height(cxr, czr) + 42, czr, LightKind::Beacon, k > 0 ? 0.5f : 0.f);
                }
                unsigned s = seed ^ 0xBEEFu;
                for (int i = 0; i < 30; ++i) {
                    double along = -60 + (i % 10) * 13, out = -25 - (i / 10) * 8;
                    double px = qx + std::cos(side) * along + std::cos(bestA) * out, pz = qz + std::sin(side) * along + std::sin(bestA) * out;
                    P.place(PropKind::Container, px, pz, side, 6.5, static_cast<unsigned char>(rnd(s) * 8), 0, 0.3, true);
                }
                break;
            }
        }
    }
}

/// The hand-built grid town of the procedural desert map (its streets are not layout roads):
/// lamps along the avenues and a few lanes of traffic.
void gridTown(Placer& P) {
    if (P.t.info().real) return;
    Vec3 c = P.t.townCenter();
    std::vector<Polyline> avenues;
    for (double k = -1050; k <= 1050; k += 150) {
        double ext = std::sqrt(std::max(0.0, 1250.0 * 1250.0 - k * k));
        for (int axis = 0; axis < 2; ++axis) {
            Polyline l;
            for (double u = -ext; u <= ext; u += 25.0) {
                double x = axis ? c.x + k : c.x + u, z = axis ? c.z + u : c.z + k;
                l.pts.push_back({x, P.t.height(x, z) + 0.35, z});
            }
            if (l.pts.size() < 4) continue;
            l.finalize();
            // lamps every 45 m, inside the built-up area
            for (double s = 20; s < l.length() - 20; s += 45) {
                Vec3 tan;
                Vec3 p = l.at(s, &tan);
                if (std::hypot(p.x - c.x, p.z - c.z) > 1100) continue;
                Vec3 n{-tan.z, 0, tan.x};
                int side = (static_cast<int>(s / 45) & 1) ? 1 : -1;
                double x = p.x + n.x * 6.5 * side, z = p.z + n.z * 6.5 * side;
                SceneProp lp;
                lp.pos = {x, P.t.height(x, z), z};
                lp.yaw = std::atan2(-n.z * side, -n.x * side);
                lp.kind = PropKind::Streetlamp;
                P.L.props.push_back(lp);
                P.light(x - n.x * side * 2.2, lp.pos.y + 9.0, z - n.z * side * 2.2, LightKind::Street);
            }
            if (std::fabs(k) <= 450 && static_cast<int>(std::fabs(k)) % 300 == 0) avenues.push_back(l);
        }
    }
    for (Polyline& a : avenues) P.L.lanes.push_back(a);
}

void traffic(Placer& P, unsigned seed) {
    // every road is a lane; the base perimeter road was added by airbaseCompound
    std::vector<Polyline> lanes = P.L.roads;
    int militaryLane = -1;
    if (!P.L.lanes.empty()) { militaryLane = static_cast<int>(lanes.size()); lanes.push_back(P.L.lanes[0]); }
    for (size_t i = 1; i < P.L.lanes.size(); ++i) lanes.push_back(P.L.lanes[i]);   // town avenues
    P.L.lanes = lanes;
    unsigned s = seed ^ 0xCA75u;
    for (size_t i = 0; i < P.L.lanes.size(); ++i) {
        double len = P.L.lanes[i].length();
        if (len < 300) continue;
        bool mil = static_cast<int>(i) == militaryLane;
        int n = std::clamp(static_cast<int>(len / (mil ? 600.0 : 260.0)), 2, 70);
        for (int k = 0; k < n; ++k) {
            Car c;
            c.lane = static_cast<int>(i);
            c.s0 = rnd(s) * len * 2.0;   // phase over the whole there-and-back cycle
            double r = rnd(s);
            if (mil) {
                c.kind = r < 0.5 ? 4 : 5;
                c.speed = 9.0 + rnd(s) * 5.0;
            } else {
                c.kind = r < 0.70 ? 0 : (r < 0.84 ? 1 : (r < 0.95 ? 2 : 3));
                c.speed = (c.kind >= 2 ? 15.0 : 19.0) + rnd(s) * 8.0;
            }
            c.color = static_cast<unsigned char>(rnd(s) * 16);
            P.L.cars.push_back(c);
            if (P.L.cars.size() >= 900) return;
        }
    }
}

}  // namespace

Vec3 WorldLayout::carAt(const Car& c, double time, Vec3* forward) const {
    if (c.lane < 0 || c.lane >= static_cast<int>(lanes.size())) return {};
    const Polyline& l = lanes[static_cast<size_t>(c.lane)];
    double len = l.length();
    if (len < 1) return l.pts.empty() ? Vec3{} : l.pts[0];
    double u = std::fmod(c.s0 + c.speed * time, 2.0 * len);
    if (u < 0) u += 2.0 * len;
    bool back = u > len;
    double s = back ? 2.0 * len - u : u;
    Vec3 tan;
    Vec3 p = l.at(s, &tan);
    if (back) tan = tan * -1.0;
    Vec3 right{-tan.z, 0, tan.x};
    p = p + right * 2.1;   // drive on the right-hand lane
    if (forward) *forward = tan;
    return p;
}

double WorldLayout::nearestProp(const Vec3& p) const {
    double best = 1e18;
    for (const SceneProp& q : props) best = std::min(best, std::hypot(q.pos.x - p.x, q.pos.z - p.z));
    return best;
}

void populateWorldDetail(const Terrain& t, WorldLayout& L) {
    L.props.clear();
    L.lights.clear();
    L.sites.clear();
    L.lanes.clear();
    L.cars.clear();
    unsigned seed = mix(static_cast<unsigned>(std::hash<std::string>{}(t.info().id)), 0x5EEDu);
    Placer P(t, L);
    airbaseCompound(P, seed);
    // forward operating bases: dry, flat ground 2.5-9 km from a town, away from the airbase
    static const char* names[] = {"FOB VIPER", "FOB TALON", "FOB GRANITE"};
    int fobs = 0;
    unsigned s = seed;
    for (int tries = 0; tries < 400 && fobs < 2; ++tries) {
        double x = (rnd(s) * 2 - 1) * 12500, z = (rnd(s) * 2 - 1) * 12500;
        if (std::hypot(x - P.rw.center.x, z - P.rw.center.z) < 4500) continue;
        bool nearTown = false, inside = false;
        for (const Town& tw : L.towns) {
            double d = std::hypot(x - tw.center.x, z - tw.center.z);
            if (d < tw.radius + 1200) inside = true;
            if (d < 9000) nearTown = true;
        }
        if (inside || (!nearTown && !L.towns.empty())) continue;
        bool farFromOther = true;
        for (const MilitarySite& m : L.sites) if (!m.airbase && std::hypot(m.center.x - x, m.center.z - z) < 6000) farFromOther = false;
        if (!farFromOther) continue;
        if (forwardBase(P, x, z, rnd(s) * 2 * kPi, s, names[fobs])) ++fobs;
    }
    streetFurniture(P, seed);
    gridTown(P);
    industry(P, seed);
    traffic(P, seed);
}

}  // namespace sky
