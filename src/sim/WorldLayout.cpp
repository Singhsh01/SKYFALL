#include "WorldLayout.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

namespace sky {

// ------------------------------------------------------------------------------------------
// Polyline
// ------------------------------------------------------------------------------------------
void Polyline::finalize() {
    cum.assign(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); ++i) {
        double dx = pts[i].x - pts[i - 1].x, dz = pts[i].z - pts[i - 1].z;
        cum[i] = cum[i - 1] + std::sqrt(dx * dx + dz * dz);
    }
}

Vec3 Polyline::at(double s, Vec3* tangent) const {
    if (pts.empty()) return {};
    if (pts.size() == 1) { if (tangent) *tangent = {1, 0, 0}; return pts[0]; }
    s = clampd(s, 0.0, length());
    size_t i = static_cast<size_t>(std::upper_bound(cum.begin(), cum.end(), s) - cum.begin());
    i = std::clamp<size_t>(i, 1, pts.size() - 1);
    double seg = std::max(1e-6, cum[i] - cum[i - 1]);
    double f = (s - cum[i - 1]) / seg;
    const Vec3 &a = pts[i - 1], &b = pts[i];
    if (tangent) *tangent = normalize(Vec3{b.x - a.x, 0, b.z - a.z});
    return a + (b - a) * f;
}

namespace {

constexpr int kN = 128;                        // route grid: 250 m cells over the 32 km map
constexpr double kHalf = Terrain::kHalfSize;
constexpr double kCellM = 2.0 * kHalf / kN;

struct Grid {
    std::vector<float> h;
    std::vector<unsigned char> wet;   // a river or lake crosses the cell (passable: a bridge)
    const Terrain* t = nullptr;
    float at(int i, int j) const { return h[static_cast<size_t>(j) * kN + i]; }
    static Vec3 world(int i, int j) { return {-kHalf + (i + 0.5) * kCellM, 0, -kHalf + (j + 0.5) * kCellM}; }
    static void cell(double x, double z, int& i, int& j) {
        i = std::clamp(static_cast<int>((x + kHalf) / kCellM), 0, kN - 1);
        j = std::clamp(static_cast<int>((z + kHalf) / kCellM), 0, kN - 1);
    }
};

struct RouteCost { double gradeW, climbW; bool avoidRunway; };

/// Least-cost path on the route grid (8-connected A*). Water is impassable.
std::vector<Vec3> route(const Grid& g, const Terrain& t, Vec3 from, Vec3 to, const RouteCost& rc) {
    int si, sj, ti, tj;
    Grid::cell(from.x, from.z, si, sj);
    Grid::cell(to.x, to.z, ti, tj);
    const RunwayInfo& rw = t.runway();
    auto blocked = [&](int i, int j) {
        if (i < 1 || j < 1 || i >= kN - 1 || j >= kN - 1) return true;
        if (g.at(i, j) < 2.0f) return true;
        if (rc.avoidRunway) {
            Vec3 w = Grid::world(i, j);
            if (std::fabs(w.x - rw.center.x) < rw.length * 0.5 + 300 && std::fabs(w.z - rw.center.z) < 260) return true;
        }
        return false;
    };
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> dist(kN * kN, inf);
    std::vector<int> prev(kN * kN, -1);
    using Node = std::pair<double, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    auto heur = [&](int i, int j) { return std::hypot(i - ti, j - tj) * kCellM; };
    int s0 = sj * kN + si, goal = tj * kN + ti;
    dist[s0] = 0;
    open.push({heur(si, sj), s0});
    static const int di[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        auto [f, cur] = open.top();
        open.pop();
        if (cur == goal) break;
        int ci = cur % kN, cj = cur / kN;
        if (f - heur(ci, cj) > dist[cur] + 1e-6) continue;
        for (int k = 0; k < 8; ++k) {
            int ni = ci + di[k], nj = cj + dj[k];
            int nid = nj * kN + ni;
            if (nid != goal && blocked(ni, nj)) continue;
            double len = (k < 4 ? 1.0 : std::sqrt(2.0)) * kCellM;
            double dh = std::fabs(static_cast<double>(g.at(ni, nj)) - g.at(ci, cj));
            double grade = dh / len;
            double c = len * (1.0 + rc.gradeW * grade * grade) + rc.climbW * dh;
            if (!g.wet.empty() && g.wet[static_cast<size_t>(nid)]) c += len * 6.0;   // bridges are expensive: cross narrow
            if (dist[cur] + c < dist[nid]) {
                dist[nid] = dist[cur] + c;
                prev[nid] = cur;
                open.push({dist[nid] + heur(ni, nj), nid});
            }
        }
    }
    std::vector<Vec3> out;
    if (prev[goal] < 0 && goal != s0) return out;
    for (int c = goal; c >= 0; c = prev[c]) { out.push_back(Grid::world(c % kN, c / kN)); if (c == s0) break; }
    std::reverse(out.begin(), out.end());
    if (!out.empty()) { out.front() = Vec3{from.x, 0, from.z}; out.back() = Vec3{to.x, 0, to.z}; }
    return out;
}

/// Chaikin smoothing, resampling to `step` metres and an engineered height profile:
/// the ground averaged over `window`, then limited to `maxGrade` in both directions.
Polyline engineer(const Terrain& t, std::vector<Vec3> pts, double step, double window, double maxGrade, double minY) {
    for (int it = 0; it < 3 && pts.size() > 2; ++it) {
        std::vector<Vec3> q{pts.front()};
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            q.push_back(pts[i] * 0.75 + pts[i + 1] * 0.25);
            q.push_back(pts[i] * 0.25 + pts[i + 1] * 0.75);
        }
        q.push_back(pts.back());
        pts.swap(q);
    }
    Polyline raw;
    raw.pts = pts;
    raw.finalize();
    Polyline p;
    int n = std::max(2, static_cast<int>(raw.length() / step) + 1);
    std::vector<double> ground(n);
    for (int i = 0; i < n; ++i) {
        Vec3 v = raw.at(raw.length() * i / (n - 1));
        ground[i] = t.surfaceHeight(v.x, v.z);
        p.pts.push_back(v);
    }
    int w = std::max(0, static_cast<int>(window / step / 2));
    std::vector<double> y(n);
    for (int i = 0; i < n; ++i) {
        double sum = 0;
        int c = 0;
        for (int k = std::max(0, i - w); k <= std::min(n - 1, i + w); ++k) { sum += ground[k]; ++c; }
        y[i] = std::max(minY, sum / c);
    }
    double ds = raw.length() / (n - 1);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 1; i < n; ++i) y[i] = clampd(y[i], y[i - 1] - maxGrade * ds, y[i - 1] + maxGrade * ds);
        for (int i = n - 2; i >= 0; --i) y[i] = clampd(y[i], y[i + 1] - maxGrade * ds, y[i + 1] + maxGrade * ds);
    }
    // rivers and lakes crossed (not followed): the deck clears the water on a bridge, with short
    // ramps either side; a route running along a river bed keeps its profile
    std::vector<double> lift(n, -1e9);
    for (int i = 0; i < n;) {
        if (!t.isWater(p.pts[i].x, p.pts[i].z) || t.height(p.pts[i].x, p.pts[i].z) < Terrain::kSeaLevel - 0.5) { ++i; continue; }
        int a = i;
        while (i < n && t.isWater(p.pts[i].x, p.pts[i].z)) ++i;
        if ((i - a) * ds > 450.0) continue;
        double top = 0;
        for (int k = a; k < i; ++k) top = std::max(top, t.surfaceHeight(p.pts[k].x, p.pts[k].z) + 9.0);
        for (int k = a; k < i; ++k) lift[k] = top;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 1; i < n; ++i) lift[i] = std::max(lift[i], lift[i - 1] - 0.22 * ds);
        for (int i = n - 2; i >= 0; --i) lift[i] = std::max(lift[i], lift[i + 1] - 0.22 * ds);
    }
    for (int i = 0; i < n; ++i) y[i] = std::max(y[i], lift[i]);
    for (int i = 0; i < n; ++i) p.pts[i].y = y[i];
    p.finalize();
    return p;
}

void findBridges(const Terrain& t, const Polyline& p, bool rail, std::vector<Bridge>& out) {
    double thr = rail ? 6.0 : 7.0;
    int n = static_cast<int>(p.pts.size());
    int i = 0;
    while (i < n) {
        double c = p.pts[i].y - t.surfaceHeight(p.pts[i].x, p.pts[i].z);
        if (c <= thr) { ++i; continue; }
        int a = i;
        double maxC = c;
        while (i < n) {
            double ci = p.pts[i].y - t.surfaceHeight(p.pts[i].x, p.pts[i].z);
            if (ci <= thr) {
                // tolerate a single dip below the threshold inside a long span
                if (i + 1 < n && p.pts[i + 1].y - t.surfaceHeight(p.pts[i + 1].x, p.pts[i + 1].z) > thr) { ++i; continue; }
                break;
            }
            maxC = std::max(maxC, ci);
            ++i;
        }
        int b = i - 1;
        a = std::max(0, a - 1);
        b = std::min(n - 1, b + 1);
        if (p.cum[b] - p.cum[a] >= (rail ? 60.0 : 90.0)) out.push_back({p.pts[a], p.pts[b], rail, maxC});
    }
}

}  // namespace

Polyline engineerRoute(const Terrain& t, std::vector<Vec3> pts, double step, double window, double maxGrade, double minY) {
    return engineer(t, std::move(pts), step, window, maxGrade, minY);
}

int WorldLayout::largestBridge(bool railOnly) const {
    int best = -1;
    double bl = 0;
    for (size_t i = 0; i < bridges.size(); ++i) {
        if (railOnly && !bridges[i].rail) continue;
        double l = std::hypot(bridges[i].b.x - bridges[i].a.x, bridges[i].b.z - bridges[i].a.z);
        if (l > bl) { bl = l; best = static_cast<int>(i); }
    }
    return best;
}

const Town* WorldLayout::industrialTown() const {
    for (const Town& t : towns) if (t.kind == TownKind::Industrial) return &t;
    return nullptr;
}

WorldLayout WorldLayout::build(const Terrain& t) {
    WorldLayout L;
    Grid g;
    g.t = &t;
    g.h.resize(kN * kN);
    for (int j = 0; j < kN; ++j)
        for (int i = 0; i < kN; ++i) { Vec3 w = Grid::world(i, j); g.h[static_cast<size_t>(j) * kN + i] = static_cast<float>(t.height(w.x, w.z)); }
    if (!t.inlandWater().empty()) {
        g.wet.assign(kN * kN, 0);
        for (int j = 0; j < kN; ++j)
            for (int i = 0; i < kN; ++i) {
                Vec3 w = Grid::world(i, j);
                bool wt = false;
                for (int k = 0; k < 9 && !wt; ++k) wt = t.isWater(w.x + (k % 3 - 1) * kCellM * 0.4, w.z + (k / 3 - 1) * kCellM * 0.4);
                g.wet[static_cast<size_t>(j) * kN + i] = wt && g.at(i, j) >= 2.0f;
            }
    }
    const int urban = std::clamp(t.info().urban, 0, 3);
    const RunwayInfo& rw = t.runway();

    // ---- towns: the flattest dry spots, spread out, away from the runway ----
    static const char* names[] = {"SANDHAVEN", "REDROCK", "OASIS POINT", "KESTREL", "DUNE RIDGE", "HIGHMESA", "SALTWATER", "IRONWOOD", "CEDAR FALLS", "MARROW"};
    Vec3 tc = t.townCenter();
    // the hub city grows with the map's density: a metropolis on the urban maps (Mumbai, Chennai, Bengaluru)
    static const double kCityR[4] = {650, 750, 1150, 1650};
    if (t.height(tc.x, tc.z) > 2.0) L.towns.push_back({Vec3{tc.x, t.height(tc.x, tc.z), tc.z}, kCityR[urban], TownKind::City, "AL-MADINA"});
    struct Cand { Vec3 p; double score; };
    std::vector<Cand> cands;
    for (double z = -13500; z <= 13500; z += 750)
        for (double x = -13500; x <= 13500; x += 750) {
            double h = t.height(x, z);
            if (h < 4.0 || h > 6000.0) continue;
            if (std::fabs(x - rw.center.x) < 3500 && std::fabs(z - rw.center.z) < 1800) continue;
            double rough = 0;
            bool wet = false;
            for (int k = 0; k < 12; ++k) {
                double a = k * kPi / 6;
                for (double r : {250.0, 500.0}) {
                    double hk = t.height(x + std::cos(a) * r, z + std::sin(a) * r);
                    if (hk < 2.0 || t.isWater(x + std::cos(a) * r, z + std::sin(a) * r)) wet = true;
                    rough = std::max(rough, std::fabs(hk - h));
                }
            }
            if (wet || rough > 35.0 || t.isWater(x, z)) continue;
            cands.push_back({Vec3{x, h, z}, rough + h * 0.004});
        }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    for (const Cand& c : cands) {
        static const size_t kMaxTowns[4] = {6, 8, 9, 10};
        if (L.towns.size() >= kMaxTowns[urban]) break;
        bool ok = true;
        for (const Town& tw : L.towns) if (std::hypot(tw.center.x - c.p.x, tw.center.z - c.p.z) < (urban >= 2 ? 4200 : 5000) + tw.radius - 600) ok = false;
        if (!ok) continue;
        Town tw;
        tw.center = c.p;
        size_t k = L.towns.size();
        if (urban >= 2 && k < 3) { tw.radius = urban == 3 ? 950 : 800; tw.kind = TownKind::City; }   // satellite cities
        else if (urban == 0) { tw.radius = k < 2 ? 520 : 380; tw.kind = k < 2 ? TownKind::Town : TownKind::Village; }
        else { tw.radius = k < 3 ? 600 : 420; tw.kind = k < 3 ? TownKind::Town : TownKind::Village; }
        tw.name = names[L.towns.size() % 10];
        L.towns.push_back(tw);
    }

    // ---- railway: west edge -> the city (or a central town) -> towards the east ----
    Polyline railPlan;   // a line too steep to build still decides where industry grew
    RouteCost railCost{900.0, 12.0, true};
    if (!L.towns.empty()) {
        Vec3 mid = L.towns[0].center;
        // western terminus: the lowest-cost dry point along the west edge
        Vec3 west{-15000, 0, mid.z};
        double best = 1e18;
        for (double z = -12000; z <= 12000; z += 1000) {
            double h = t.height(-15000, z);
            if (h < 3) continue;
            double sc = std::fabs(h - mid.y) + std::fabs(z - mid.z) * 0.05;
            if (sc < best) { best = sc; west = {-15000, 0, z}; }
        }
        // eastern terminus: the town furthest east (port if the coast is there)
        const Town* eastTown = nullptr;
        for (const Town& tw : L.towns) if (&tw != &L.towns[0] && (!eastTown || tw.center.x > eastTown->center.x)) eastTown = &tw;
        Vec3 east;
        if (eastTown && eastTown->center.x > mid.x + 3000) east = eastTown->center;
        else {
            // last dry ground east of the hub, short of the coast or the map edge
            double x = mid.x;
            for (double xx = mid.x; xx < 14500; xx += 250) {
                if (t.height(xx, mid.z) < 6.0) break;
                x = xx;
            }
            east = {std::max(mid.x + 1500, x - 600), 0, mid.z};
        }
        std::vector<Vec3> a = route(g, t, west, mid, railCost);
        std::vector<Vec3> b = route(g, t, mid, east, railCost);
        if (!a.empty() && !b.empty()) {
            a.insert(a.end(), b.begin() + 1, b.end());
            L.railway = engineer(t, a, 40.0, 1400.0, 0.025, Terrain::kSeaLevel + 6.0);
            // a grade-limited line through high mountains would need viaducts hundreds of metres
            // tall (real lines tunnel; tunnels are not modelled): no railway there
            double worst = 0;
            for (const Vec3& q : L.railway.pts) worst = std::max(worst, q.y - t.surfaceHeight(q.x, q.z));
            if (worst > 95.0) { railPlan = L.railway; L.railway = Polyline{}; }
        }
    }

    // ---- roads: minimum spanning tree over the airbase and the towns ----
    std::vector<Vec3> nodes{Vec3{rw.center.x + 400, 0, rw.center.z + 700}};
    for (const Town& tw : L.towns) nodes.push_back(tw.center);
    std::vector<bool> in(nodes.size(), false);
    in[0] = true;
    RouteCost roadCost{160.0, 3.0, true};
    for (size_t added = 1; added < nodes.size(); ++added) {
        double bd = 1e18;
        int ba = -1, bb = -1;
        for (size_t i = 0; i < nodes.size(); ++i) if (in[i])
            for (size_t j = 0; j < nodes.size(); ++j) if (!in[j]) {
                double d = std::hypot(nodes[i].x - nodes[j].x, nodes[i].z - nodes[j].z);
                if (d < bd) { bd = d; ba = static_cast<int>(i); bb = static_cast<int>(j); }
            }
        if (bb < 0) break;
        in[bb] = true;
        std::vector<Vec3> r = route(g, t, nodes[ba], nodes[bb], roadCost);
        if (r.size() >= 2) {
            Polyline road = engineer(t, r, 25.0, 220.0, 0.09, Terrain::kSeaLevel + 3.0);
            auto worst = [&](const Polyline& p) {
                double w = 0;
                for (const Vec3& q : p.pts) w = std::max(w, q.y - t.surfaceHeight(q.x, q.z));
                return w;
            };
            // mountain roads: hairpin grades rather than towering viaducts
            if (worst(road) > 60.0) road = engineer(t, r, 25.0, 100.0, 0.16, Terrain::kSeaLevel + 3.0);
            L.roads.push_back(std::move(road));
        }
    }

    // ---- bridges / viaducts wherever an engineered profile leaves the ground ----
    if (!L.railway.pts.empty()) findBridges(t, L.railway, true, L.bridges);
    for (const Polyline& r : L.roads) findBridges(t, r, false, L.bridges);

    // ---- industrial zone: the town closest to the railway (not the city) ----
    const Polyline& industryRail = L.railway.pts.empty() ? railPlan : L.railway;
    if (!industryRail.pts.empty() && L.towns.size() > 1) {
        int bestT = -1;
        double bd = 1e18;
        for (size_t i = 1; i < L.towns.size(); ++i)
            for (size_t k = 0; k < industryRail.pts.size(); k += 10) {
                double d = std::hypot(L.towns[i].center.x - industryRail.pts[k].x, L.towns[i].center.z - industryRail.pts[k].z);
                if (d < bd) { bd = d; bestT = static_cast<int>(i); }
            }
        if (bestT > 0) {
            Town& it = L.towns[bestT];
            it.kind = TownKind::Industrial;
            for (int k = 0; k < 4; ++k) {
                double x = it.center.x + (k % 2 ? 140 : -140), z = it.center.z + (k / 2 ? 120 : -120);
                L.factories.push_back({x, t.height(x, z), z});
            }
        }
    }

    // ---- wind farms ("mills") on gentle hilltops near towns ----
    struct Hill { Vec3 p; double prom; };
    std::vector<Hill> hills;
    for (double z = -14000; z <= 14000; z += 400)
        for (double x = -14000; x <= 14000; x += 400) {
            double h = t.height(x, z);
            if (h < 20) continue;
            if (std::fabs(x - rw.center.x) < 3000 && std::fabs(z - rw.center.z) < 1500) continue;
            if (t.normal(x, z, 20.0).y < 0.95) continue;
            double lo = h;
            for (int k = 0; k < 8; ++k) lo = std::min(lo, t.height(x + std::cos(k * kPi / 4) * 700, z + std::sin(k * kPi / 4) * 700));
            double prom = h - lo;
            if (prom < 50) continue;
            bool nearTown = false;
            for (const Town& tw : L.towns) if (std::hypot(tw.center.x - x, tw.center.z - z) < 7000) nearTown = true;
            if (nearTown) hills.push_back({Vec3{x, h, z}, prom});
        }
    std::sort(hills.begin(), hills.end(), [](const Hill& a, const Hill& b) { return a.prom > b.prom; });
    for (const Hill& hl : hills) {
        if (L.turbines.size() >= 14) break;
        bool ok = true;
        for (const WindTurbine& w : L.turbines) if (std::hypot(w.pos.x - hl.p.x, w.pos.z - hl.p.z) < 350) ok = false;
        for (const Town& tw : L.towns) if (std::hypot(tw.center.x - hl.p.x, tw.center.z - hl.p.z) < tw.radius + 300) ok = false;
        if (ok) L.turbines.push_back({hl.p, 0.6});
    }
    buildInfrastructure(t, L);
    populateWorldDetail(t, L);
    return L;
}

}  // namespace sky
