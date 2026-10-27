// Infrastructure laid over a world layout: the high-voltage grid (a trunk line from a hydro dam,
// the industrial zone or the map edge to the city substation, branches to every town), a hydro
// dam across the narrowest mountain gorge on river maps, water works beside rivers and the sea,
// and on the denser maps an elevated metro and city expressways (flyovers). Everything is a
// deterministic function of the terrain, so every renderer, replay and mission sees the same world.
#include "WorldLayout.hpp"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {

constexpr double kPylonSpacing = 330.0;

struct Ctx {
    const Terrain& t;
    WorldLayout& L;
    const RunwayInfo& rw;
    // runway frame: along / across the runway
    void local(double x, double z, double& u, double& v) const {
        double dx = x - rw.center.x, dz = z - rw.center.z;
        double c = std::cos(rw.heading), s = std::sin(rw.heading);
        u = dx * c + dz * s;
        v = -dx * s + dz * c;
    }
    /// Keep tall structures out of the runway and its approach / climb-out funnels.
    bool nearRunway(double x, double z, double margin = 0.0) const {
        double u, v;
        local(x, z, u, v);
        return std::fabs(u) < rw.length * 0.5 + 3200 + margin && std::fabs(v) < 700 + margin;
    }
    bool inTown(double x, double z, double extra = 0.0) const {
        for (const Town& tw : L.towns) if (std::hypot(x - tw.center.x, z - tw.center.z) < tw.radius + extra) return true;
        return false;
    }
    bool inside(double x, double z, double m = 1200.0) const {
        return std::fabs(x) < Terrain::kHalfSize - m && std::fabs(z) < Terrain::kHalfSize - m;
    }
    bool dry(double x, double z) const { return t.height(x, z) > Terrain::kSeaLevel + 2.0 && !t.isWater(x, z); }
    bool flat(double x, double z, double r, double tol) const {
        double h = t.height(x, z);
        for (int k = 0; k < 8; ++k) {
            double a = k * kPi / 4;
            if (std::fabs(t.height(x + std::cos(a) * r, z + std::sin(a) * r) - h) > tol || t.isWater(x + std::cos(a) * r, z + std::sin(a) * r)) return false;
        }
        return true;
    }
    /// A dry, flat spot near `want` (spiral search), or false.
    bool site(Vec3 want, double r, Vec3& out, double townClear = 60.0) const {
        for (double rr = 0; rr <= 1800; rr += 60) {
            int n = rr == 0 ? 1 : static_cast<int>(rr / 40);
            for (int k = 0; k < n; ++k) {
                double a = k * 2 * kPi / n + rr * 0.01;
                double x = want.x + std::cos(a) * rr, z = want.z + std::sin(a) * rr;
                if (!inside(x, z) || !dry(x, z) || nearRunway(x, z, 200) || inTown(x, z, townClear)) continue;
                if (!flat(x, z, r, 4.0)) continue;
                bool clear = true;
                for (const Facility& f : L.facilities) if (std::hypot(f.pos.x - x, f.pos.z - z) < 160) clear = false;
                if (!clear) continue;
                out = {x, t.height(x, z), z};
                return true;
            }
        }
        return false;
    }
    /// Straight span a..b avoids the runway funnels.
    bool spanOK(Vec3 a, Vec3 b) const {
        double len = std::hypot(b.x - a.x, b.z - a.z);
        int n = std::max(2, static_cast<int>(len / 100));
        for (int i = 0; i <= n; ++i) {
            double f = static_cast<double>(i) / n;
            if (nearRunway(a.x + (b.x - a.x) * f, a.z + (b.z - a.z) * f, 150)) return false;
        }
        return true;
    }
};

/// Pylons from a to b every ~330 m, detouring around the runway funnel; a pylon that would stand
/// in water, a town or on a cliff moves sideways (or is left out, giving a longer span).
void lineBetween(Ctx& C, Vec3 a, Vec3 b, bool trunk) {
    std::vector<Vec3> way{a, b};
    // round any town the straight line would cross (lines skirt towns, they do not hang over roofs)
    for (int iter = 0; iter < 8; ++iter) {
        bool changed = false;
        for (size_t w = 0; w + 1 < way.size() && !changed; ++w) {
            Vec3 p0 = way[w], p1 = way[w + 1];
            Vec3 d{p1.x - p0.x, 0, p1.z - p0.z};
            double len2 = d.x * d.x + d.z * d.z;
            if (len2 < 1) continue;
            for (const Town& tw : C.L.towns) {
                double f = clampd(((tw.center.x - p0.x) * d.x + (tw.center.z - p0.z) * d.z) / len2, 0.0, 1.0);
                Vec3 q{p0.x + d.x * f, 0, p0.z + d.z * f};
                double dist = std::hypot(q.x - tw.center.x, q.z - tw.center.z);
                if (dist > tw.radius + 40 || f <= 0.02 || f >= 0.98) continue;
                Vec3 side = dist > 1 ? Vec3{(q.x - tw.center.x) / dist, 0, (q.z - tw.center.z) / dist} : Vec3{-d.z, 0, d.x} * (1.0 / std::sqrt(len2));
                way.insert(way.begin() + static_cast<long>(w) + 1, tw.center + side * (tw.radius + 260.0));
                changed = true;
                break;
            }
        }
        if (!changed) break;
    }
    // detour round one corner of the runway exclusion box where a leg crosses the funnel
    for (size_t w = 0; w + 1 < way.size(); ++w) {
        if (C.spanOK(way[w], way[w + 1])) continue;
        double hl = C.rw.length * 0.5 + 3600, hw = 1100;
        double c = std::cos(C.rw.heading), s = std::sin(C.rw.heading);
        double best = 1e18;
        Vec3 bc{};
        bool found = false;
        for (int k = 0; k < 4; ++k) {
            double u = (k & 1) ? hl : -hl, v = (k & 2) ? hw : -hw;
            Vec3 p{C.rw.center.x + u * c - v * s, 0, C.rw.center.z + u * s + v * c};
            if (!C.spanOK(way[w], p) || !C.spanOK(p, way[w + 1])) continue;
            double d = std::hypot(p.x - way[w].x, p.z - way[w].z) + std::hypot(way[w + 1].x - p.x, way[w + 1].z - p.z);
            if (d < best) { best = d; bc = p; found = true; }
        }
        if (!found) return;   // no clean route: no line rather than one across the approach
        way.insert(way.begin() + static_cast<long>(w) + 1, bc);
        ++w;
    }
    PowerLine pl;
    pl.trunk = trunk;
    for (size_t w = 0; w + 1 < way.size(); ++w) {
        Vec3 p0 = way[w], p1 = way[w + 1];
        double len = std::hypot(p1.x - p0.x, p1.z - p0.z);
        if (len < 1) continue;
        Vec3 dir{(p1.x - p0.x) / len, 0, (p1.z - p0.z) / len}, nrm{-dir.z, 0, dir.x};
        int n = std::max(1, static_cast<int>(std::round(len / kPylonSpacing)));
        for (int i = (w == 0 ? 0 : 1); i <= n; ++i) {
            double f = static_cast<double>(i) / n;
            Vec3 q{p0.x + (p1.x - p0.x) * f, 0, p0.z + (p1.z - p0.z) * f};
            bool ok = false;
            for (double off : {0.0, 40.0, -40.0, 90.0, -90.0, 150.0, -150.0}) {
                double x = q.x + nrm.x * off, z = q.z + nrm.z * off;
                if (!C.dry(x, z) || C.inTown(x, z, 20) || C.nearRunway(x, z, 100)) continue;
                if (C.t.normal(x, z, 8.0).y < 0.82) continue;   // no pylons on cliffs
                q = {x, C.t.height(x, z), z};
                ok = true;
                break;
            }
            if (ok) pl.pylons.push_back(q);
        }
    }
    // spans up to ~1.1 km (a river, a gorge) are fine; a longer gap (a bay, a lake) ends the
    // line there and it starts again on the far side (real crossings use towers we do not model)
    PowerLine part;
    part.trunk = trunk;
    for (size_t i = 0; i < pl.pylons.size(); ++i) {
        if (!part.pylons.empty()) {
            const Vec3& q = part.pylons.back();
            if (std::hypot(pl.pylons[i].x - q.x, pl.pylons[i].z - q.z) > 1100.0) {
                if (part.pylons.size() >= 2) C.L.powerLines.push_back(part);
                part.pylons.clear();
            }
        }
        part.pylons.push_back(pl.pylons[i]);
    }
    if (part.pylons.size() >= 2) C.L.powerLines.push_back(std::move(part));
}

/// The narrowest steep-sided river gorge (a mountain map with rivers): a concrete dam across it.
void hydroDam(Ctx& C) {
    const Terrain& t = C.t;
    if (t.inlandWater().empty() || t.info().riverDepth <= 0) return;
    const auto& w = t.inlandWater();
    const int N = Terrain::kWaterN;
    const double cell = Terrain::kWaterCell;
    double bestScore = 1e18;
    Dam best{};
    for (int j = 8; j < N - 8; j += 5)
        for (int i = 8; i < N - 8; i += 5) {
            float lvl = w[static_cast<size_t>(j) * N + i];
            if (lvl < -1e8f || lvl < 40.0f) continue;
            double x = -Terrain::kHalfSize + (i + 0.5) * cell, z = -Terrain::kHalfSize + (j + 0.5) * cell;
            if (!C.inside(x, z, 2500) || C.nearRunway(x, z, 2500) || C.inTown(x, z, 1200)) continue;
            const double wall = 55.0;
            for (int k = 0; k < 8; ++k) {
                double a = k * kPi / 8;
                double dx = std::cos(a), dz = std::sin(a);
                double ends[2] = {0, 0};
                double waterSpan = 0;
                bool ok = true;
                for (int side = 0; side < 2 && ok; ++side) {
                    double sg = side ? -1.0 : 1.0;
                    bool bank = false;
                    double r = 0;
                    for (r = 15; r < 650; r += 15) {
                        double px = x + dx * r * sg, pz = z + dz * r * sg;
                        bool wet = t.isWater(px, pz);
                        if (!bank && wet) { waterSpan += 15; continue; }
                        if (wet) { ok = false; break; }   // more water across: a lake or a braided bed
                        bank = true;
                        if (t.height(px, pz) >= lvl + wall) break;
                    }
                    if (r >= 650) ok = false;
                    ends[side] = r;
                }
                if (!ok || waterSpan > 140) continue;
                double width = ends[0] + ends[1];
                double score = width - lvl * 0.04;
                if (score < bestScore) {
                    bestScore = score;
                    best.a = {x + dx * ends[0], lvl + wall, z + dz * ends[0]};
                    best.b = {x - dx * ends[1], lvl + wall, z - dz * ends[1]};
                    best.bed = lvl;
                    // downstream: the lower water level along the river (perpendicular to the crest)
                    Vec3 n{-dz, 0, dx};
                    double up = t.inlandLevel(x + n.x * 220, z + n.z * 220), dn = t.inlandLevel(x - n.x * 220, z - n.z * 220);
                    if (up < -1e8) up = lvl;
                    if (dn < -1e8) dn = lvl;
                    best.downstream = up < dn ? n : n * -1.0;
                }
            }
        }
    if (bestScore < 1e17 && std::hypot(best.b.x - best.a.x, best.b.z - best.a.z) < 900) {
        C.L.dams.push_back(best);
        Vec3 mid = (best.a + best.b) * 0.5;
        Vec3 ph = mid + best.downstream * 70.0;
        double yaw = std::atan2(best.downstream.z, best.downstream.x);
        C.L.facilities.push_back({Vec3{ph.x, best.bed, ph.z}, yaw, FacilityKind::Powerhouse});
    }
}

/// The longest dry, gentle straight run through `c` along direction `a` (both ways), metres.
double runLength(const Ctx& C, Vec3 c, double a, double maxR, double& l0, double& l1) {
    double dx = std::cos(a), dz = std::sin(a);
    double h0 = C.t.height(c.x, c.z);
    for (int side = 0; side < 2; ++side) {
        double sg = side ? -1.0 : 1.0, r = 0;
        double prev = h0;
        for (r = 50; r <= maxR; r += 50) {
            double x = c.x + dx * r * sg, z = c.z + dz * r * sg;
            double h = C.t.height(x, z);
            if (!C.inside(x, z, 800) || C.nearRunway(x, z, 300) || h < Terrain::kSeaLevel + 2.0 || std::fabs(h - prev) > 4.5 || (C.t.isWater(x, z) && r > 400)) break;
            prev = h;
        }
        (side ? l1 : l0) = r - 50;
    }
    return std::min(l0, l1) * 2.0 + (l0 + l1) * 0.25;
}

/// An elevated line straight through a city: a gently curving viaduct at `elev` metres, with
/// ramps down to the ground at the ends when `ramps`.
Polyline elevated(const Ctx& C, Vec3 c, double a, double l0, double l1, double elev, bool ramps, double wiggle) {
    std::vector<Vec3> pts;
    double dx = std::cos(a), dz = std::sin(a);
    for (double s = -l1; s <= l0 + 1; s += 25) {
        double off = std::sin(s * 0.0011 + c.x * 0.001) * wiggle;
        pts.push_back({c.x + dx * s - dz * off, 0, c.z + dz * s + dx * off});
    }
    Polyline p;
    p.pts = pts;
    p.finalize();
    int n = static_cast<int>(pts.size());
    std::vector<double> g(n);
    for (int i = 0; i < n; ++i) g[i] = C.t.surfaceHeight(pts[i].x, pts[i].z);
    std::vector<double> y(n);
    for (int i = 0; i < n; ++i) {
        double m = g[i];
        for (int k = std::max(0, i - 3); k <= std::min(n - 1, i + 3); ++k) m = std::max(m, g[k]);
        y[i] = m + elev;
    }
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 1; i + 1 < n; ++i) y[i] = std::max(y[i], (y[i - 1] + y[i + 1]) * 0.5 - 0.5);
    for (int i = 0; i < n; ++i) {
        if (ramps) {
            double d = std::min(p.cum[i], p.length() - p.cum[i]);
            double f = clampd(d / 260.0, 0.0, 1.0);
            f = f * f * (3 - 2 * f);
            y[i] = g[i] + (y[i] - g[i]) * f + 0.1;
        }
        p.pts[i].y = y[i];
    }
    p.finalize();
    return p;
}

void cityTransit(Ctx& C) {
    int urban = std::clamp(C.t.info().urban, 0, 3);
    if (urban < 2 || C.L.towns.empty() || C.L.towns[0].kind != TownKind::City) return;
    const Town& city = C.L.towns[0];
    double maxR = city.radius * (urban == 3 ? 2.6 : 2.1);
    double bestA = 0, bestS = -1, b0 = 0, b1 = 0;
    for (int k = 0; k < 16; ++k) {
        double a = k * kPi / 16, l0 = 0, l1 = 0;
        double sc = runLength(C, city.center, a, maxR, l0, l1);
        if (sc > bestS) { bestS = sc; bestA = a; b0 = l0; b1 = l1; }
    }
    if (b0 + b1 > 2500) C.L.metro = elevated(C, city.center, bestA, b0, b1, 13.0, false, 140.0);
    // the expressway crosses the metro near the centre (offset so the two viaducts do not share piers)
    double fa = bestA + kPi / 2;
    Vec3 fc = city.center + Vec3{std::cos(bestA), 0, std::sin(bestA)} * (city.radius * 0.18);
    double l0 = 0, l1 = 0;
    runLength(C, fc, fa, city.radius * 1.5, l0, l1);
    if (l0 + l1 > 1400) {
        Polyline fly = elevated(C, fc, fa, l0, l1, 9.0, true, 60.0);
        C.L.flyovers.push_back(static_cast<int>(C.L.roads.size()));
        C.L.roads.push_back(std::move(fly));
    }
    if (urban == 3) {
        // a second expressway along the metro on the other side of downtown
        Vec3 fc2 = city.center + Vec3{std::cos(fa), 0, std::sin(fa)} * (city.radius * 0.45);
        double m0 = 0, m1 = 0;
        runLength(C, fc2, bestA, city.radius * 1.4, m0, m1);
        if (m0 + m1 > 1400) {
            C.L.flyovers.push_back(static_cast<int>(C.L.roads.size()));
            C.L.roads.push_back(elevated(C, fc2, bestA, m0, m1, 8.0, true, 0.0));
        }
    }
}

void powerGrid(Ctx& C) {
    if (C.L.towns.empty()) return;
    const Town& city = C.L.towns[0];
    // the source: the hydro dam's powerhouse, else a plant beside the industrial zone, else a
    // trunk line arriving from beyond the map edge
    Vec3 src;
    bool haveSrc = false;
    for (const Facility& f : C.L.facilities)
        if (f.kind == FacilityKind::Powerhouse) {
            Vec3 p;
            if (C.site(f.pos + Vec3{std::cos(f.yaw), 0, std::sin(f.yaw)} * 300.0, 30, p, 200)) { src = p; haveSrc = true; }
            break;
        }
    if (!haveSrc && !C.L.factories.empty()) {
        Vec3 p;
        if (C.site(C.L.factories[0] + Vec3{0, 0, 420}, 30, p, 120)) { src = p; haveSrc = true; }
    }
    if (!haveSrc) {
        // the dry map edge point furthest from the runway
        double best = -1;
        for (int k = 0; k < 32; ++k) {
            double a = k * kPi / 16;
            double x = std::cos(a) * (Terrain::kHalfSize - 1300), z = std::sin(a) * (Terrain::kHalfSize - 1300);
            if (!C.dry(x, z)) continue;
            double d = std::hypot(x - C.rw.center.x, z - C.rw.center.z) - std::hypot(x - city.center.x, z - city.center.z) * 0.5;
            if (d > best) { best = d; src = {x, C.t.height(x, z), z}; haveSrc = true; }
        }
    }
    if (!haveSrc) return;
    // city substation on the edge facing the source
    Vec3 toSrc = normalize(Vec3{src.x - city.center.x, 0, src.z - city.center.z});
    Vec3 main;
    if (!C.site(city.center + toSrc * (city.radius + 260), 35, main)) return;
    C.L.facilities.push_back({main, std::atan2(toSrc.z, toSrc.x), FacilityKind::Substation});
    lineBetween(C, src, main, true);
    // branches: the main substation to a substation at each town
    for (size_t i = 1; i < C.L.towns.size(); ++i) {
        const Town& tw = C.L.towns[i];
        if (tw.kind == TownKind::Village && C.L.powerLines.size() > 5) continue;
        Vec3 dir = normalize(Vec3{main.x - tw.center.x, 0, main.z - tw.center.z});
        Vec3 sub;
        if (!C.site(tw.center + dir * (tw.radius + 180), 25, sub)) continue;
        if (std::hypot(sub.x - main.x, sub.z - main.z) > 16000) continue;
        C.L.facilities.push_back({sub, std::atan2(dir.z, dir.x), FacilityKind::Substation});
        lineBetween(C, main, sub, false);
    }
}

/// Water works beside a river, lake or the sea near the city and the largest town.
void waterWorks(Ctx& C) {
    int placed = 0;
    for (size_t i = 0; i < C.L.towns.size() && placed < 2; ++i) {
        const Town& tw = C.L.towns[i];
        if (tw.kind == TownKind::Village) continue;
        bool done = false;
        for (double r = tw.radius + 250; r < tw.radius + 2600 && !done; r += 150)
            for (int k = 0; k < 24 && !done; ++k) {
                double a = k * kPi / 12;
                double x = tw.center.x + std::cos(a) * r, z = tw.center.z + std::sin(a) * r;
                if (!C.dry(x, z)) continue;
                // water within 160 m on one side
                bool nearWater = false;
                for (int q = 0; q < 8 && !nearWater; ++q) {
                    double b = q * kPi / 4;
                    double wx = x + std::cos(b) * 160, wz = z + std::sin(b) * 160;
                    nearWater = C.t.isWater(wx, wz) || C.t.height(wx, wz) < Terrain::kSeaLevel;
                }
                if (!nearWater) continue;
                Vec3 p;
                if (!C.inside(x, z) || C.nearRunway(x, z, 300) || C.inTown(x, z, 60) || !C.flat(x, z, 55, 3.5)) continue;
                p = {x, C.t.height(x, z), z};
                C.L.facilities.push_back({p, a, FacilityKind::WaterWorks});
                ++placed;
                done = true;
            }
    }
}

}  // namespace

void buildInfrastructure(const Terrain& t, WorldLayout& L) {
    L.powerLines.clear();
    L.dams.clear();
    L.facilities.clear();
    L.metro = Polyline{};
    L.flyovers.clear();
    Ctx C{t, L, t.runway()};
    hydroDam(C);
    cityTransit(C);
    powerGrid(C);
    waterWorks(C);
}

int WorldLayout::metroTrains() const {
    double len = metro.length();
    if (len < 1500) return 0;
    return std::clamp(static_cast<int>(len / 2200.0), 2, 5);
}

Vec3 WorldLayout::metroCarAt(int train, int car, double time, Vec3* forward) const {
    double len = metro.length();
    if (len < 1) return metro.pts.empty() ? Vec3{} : metro.pts[0];
    const double v = 16.0;   // m/s line speed
    // each train shuttles end to end with a 25 s dwell at the termini; trains are evenly spaced
    double run = len - kMetroCarLength * kMetroCars;
    double cycle = 2.0 * (run / v + 25.0);
    double u = std::fmod(time + cycle * train / std::max(1, metroTrains()), cycle);
    if (u < 0) u += cycle;
    double half = cycle * 0.5;
    bool back = u > half;
    double w = back ? u - half : u;
    double s = std::min(run, std::max(0.0, (w - 12.5) * v));
    // ease into and out of the stations
    double lead = back ? len - s : s + kMetroCarLength * kMetroCars;
    double c = back ? lead + kMetroCarLength * (car + 0.5) : lead - kMetroCarLength * (car + 0.5);
    Vec3 tan;
    Vec3 p = metro.at(c, &tan);
    if (back) tan = tan * -1.0;
    Vec3 right{-tan.z, 0, tan.x};
    p = p + right * 2.4;   // each direction on its own track
    if (forward) *forward = tan;
    return p;
}

}  // namespace sky
