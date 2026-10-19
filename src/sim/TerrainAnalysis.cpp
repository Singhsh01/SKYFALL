#include "TerrainAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace sky {

bool HeightGrid::index(double x, double z, int& i, int& j) const {
    i = static_cast<int>(std::lround((x - origin) / cell));
    j = static_cast<int>(std::lround((z - origin) / cell));
    bool inside = i >= 0 && j >= 0 && i < n && j < n;
    i = std::clamp(i, 0, n - 1);
    j = std::clamp(j, 0, n - 1);
    return inside;
}

HeightGrid sampleHeightGrid(const Terrain& t, double cell, double half) {
    HeightGrid g;
    g.cell = cell;
    g.origin = -half;
    g.n = static_cast<int>(std::floor(2.0 * half / cell)) + 1;
    g.h.resize(static_cast<size_t>(g.n) * g.n);
    g.water.resize(g.h.size());
    g.hmin = 1e9f;
    g.hmax = -1e9f;
    for (int j = 0; j < g.n; ++j)
        for (int i = 0; i < g.n; ++i) {
            double x = g.origin + i * cell, z = g.origin + j * cell;
            size_t k = static_cast<size_t>(j) * g.n + i;
            g.h[k] = static_cast<float>(t.surfaceHeight(x, z));
            g.water[k] = t.isWater(x, z) ? 1 : 0;
            g.hmin = std::min(g.hmin, g.h[k]);
            g.hmax = std::max(g.hmax, g.h[k]);
        }
    if (g.hmax - g.hmin < 1.0f) g.hmax = g.hmin + 1.0f;
    return g;
}

std::vector<Vec3> valleyPath(const HeightGrid& g, Vec3 from, Vec3 to, const PathCost& c) {
    const int n = g.n;
    int si, sj, ti, tj;
    g.index(from.x, from.z, si, sj);
    g.index(to.x, to.z, ti, tj);
    const int lim = std::max(1, static_cast<int>(c.margin / g.cell));
    auto inside = [&](int i, int j) { return i >= lim && j >= lim && i < n - lim && j < n - lim; };
    si = std::clamp(si, lim, n - 1 - lim); sj = std::clamp(sj, lim, n - 1 - lim);
    ti = std::clamp(ti, lim, n - 1 - lim); tj = std::clamp(tj, lim, n - 1 - lim);
    const double range = g.hmax - g.hmin;
    std::vector<float> dist(static_cast<size_t>(n) * n, 1e30f);
    std::vector<int> prev(dist.size(), -1);
    using Node = std::pair<float, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    auto key = [n](int i, int j) { return j * n + i; };
    auto heur = [&](int i, int j) { return static_cast<float>(std::hypot(i - ti, j - tj) * g.cell); };
    dist[key(si, sj)] = 0;
    open.push({heur(si, sj), key(si, sj)});
    const int di[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dj[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (!open.empty()) {
        auto [f, k] = open.top();
        open.pop();
        int i = k % n, j = k / n;
        if (i == ti && j == tj) break;
        float dk = dist[k];
        if (f - heur(i, j) > dk + 1e-3f) continue;   // stale entry
        for (int d = 0; d < 8; ++d) {
            int a = i + di[d], b = j + dj[d];
            if (!inside(a, b)) continue;
            double len = (d < 4 ? 1.0 : 1.41421356) * g.cell;
            double hn = (g.at(a, b) - g.hmin) / range;
            double dh = std::fabs(g.at(a, b) - g.at(i, j)) / len;
            double w = len * (1.0 + c.low * hn + c.slope * dh * 4.0 + (g.wet(a, b) ? c.water : 0.0));
            int kb = key(a, b);
            float nd = dk + static_cast<float>(w);
            if (nd < dist[kb]) {
                dist[kb] = nd;
                prev[kb] = k;
                open.push({nd + heur(a, b), kb});
            }
        }
    }
    std::vector<Vec3> path;
    int k = key(ti, tj);
    if (prev[k] < 0 && k != key(si, sj)) {   // unreachable: straight line
        path = {from, to};
        return path;
    }
    for (; k >= 0; k = prev[k]) path.push_back(g.pos(k % n, k / n));
    std::reverse(path.begin(), path.end());
    // simplify collinear runs (keeps the turns, drops the staircase of grid steps)
    std::vector<Vec3> simple;
    for (size_t q = 0; q < path.size(); ++q) {
        if (q == 0 || q + 1 == path.size() || q % 3 == 0) simple.push_back(path[q]);
    }
    return simple;
}

std::vector<Vec3> smoothPolyline(std::vector<Vec3> pts, int iterations) {
    for (int it = 0; it < iterations && pts.size() > 2; ++it) {
        std::vector<Vec3> out;
        out.push_back(pts.front());
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            Vec3 a = pts[i], b = pts[i + 1];
            out.push_back(a * 0.75 + b * 0.25);
            out.push_back(a * 0.25 + b * 0.75);
        }
        out.push_back(pts.back());
        pts.swap(out);
    }
    return pts;
}

double polylineLength(const std::vector<Vec3>& pts) {
    double L = 0;
    for (size_t i = 1; i < pts.size(); ++i) L += std::hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
    return L;
}

std::vector<Vec3> resamplePolyline(const std::vector<Vec3>& pts, double spacing) {
    std::vector<Vec3> out;
    if (pts.empty()) return out;
    double total = polylineLength(pts);
    int count = std::max(1, static_cast<int>(std::round(total / spacing)));
    double step = total / count;
    out.push_back(pts.front());
    double acc = 0, next = step;
    for (size_t i = 1; i < pts.size() && static_cast<int>(out.size()) <= count; ++i) {
        double seg = std::hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
        while (seg > 1e-9 && acc + seg >= next - 1e-9 && static_cast<int>(out.size()) <= count) {
            double u = (next - acc) / seg;
            out.push_back(lerp(pts[i - 1], pts[i], clampd(u, 0.0, 1.0)));
            next += step;
        }
        acc += seg;
    }
    if (std::hypot(out.back().x - pts.back().x, out.back().z - pts.back().z) > spacing * 0.3) out.push_back(pts.back());
    else out.back() = pts.back();
    return out;
}

namespace {
double vertexRadius(const Vec3& a, const Vec3& b, const Vec3& c) {
    // radius of the circle that turns from segment ab into bc within half of each segment
    double ux = b.x - a.x, uz = b.z - a.z, vx = c.x - b.x, vz = c.z - b.z;
    double lu = std::hypot(ux, uz), lv = std::hypot(vx, vz);
    if (lu < 1e-6 || lv < 1e-6) return 1e9;
    double cosT = clampd((ux * vx + uz * vz) / (lu * lv), -1.0, 1.0);
    double theta = std::acos(cosT);
    if (theta < 1e-4) return 1e9;
    double half = 0.5 * std::min(lu, lv);
    return half / std::tan(theta * 0.5);
}
}  // namespace

double minTurnRadius(const std::vector<Vec3>& pts) {
    double r = 1e9;
    for (size_t i = 1; i + 1 < pts.size(); ++i) r = std::min(r, vertexRadius(pts[i - 1], pts[i], pts[i + 1]));
    return r;
}

void enforceTurnRadius(std::vector<Vec3>& pts, double minRadius, int maxIterations) {
    for (int it = 0; it < maxIterations; ++it) {
        bool changed = false;
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            if (vertexRadius(pts[i - 1], pts[i], pts[i + 1]) >= minRadius) continue;
            Vec3 m = (pts[i - 1] + pts[i + 1]) * 0.5;
            pts[i].x = pts[i].x * 0.5 + m.x * 0.5;
            pts[i].z = pts[i].z * 0.5 + m.z * 0.5;
            changed = true;
        }
        if (!changed) return;
    }
}

double segmentClearance(const Terrain& t, Vec3 a, Vec3 b, double step) {
    double len = length(b - a);
    int n = std::max(2, static_cast<int>(len / step));
    double best = 1e9;
    for (int k = 0; k <= n; ++k) {
        Vec3 p = lerp(a, b, static_cast<double>(k) / n);
        best = std::min(best, p.y - t.surfaceHeight(p.x, p.z));
    }
    return best;
}

double maxSurfaceAround(const Terrain& t, double x, double z, double radius) {
    double best = t.surfaceHeight(x, z);
    for (int r = 1; r <= 2; ++r)
        for (int k = 0; k < 8; ++k) {
            double a = k * kPi / 4.0, d = radius * r / 2.0;
            best = std::max(best, t.surfaceHeight(x + std::cos(a) * d, z + std::sin(a) * d));
        }
    return best;
}

double slopeAround(const Terrain& t, double x, double z, double radius) {
    double s = 0;
    int n = 0;
    for (int k = 0; k < 5; ++k) {
        double a = k * 2.0 * kPi / 5.0;
        double px = k == 0 ? x : x + std::cos(a) * radius, pz = k == 0 ? z : z + std::sin(a) * radius;
        s += 1.0 - t.normal(px, pz, 12.0).y;
        ++n;
    }
    return s / n;
}

bool findFlatSpot(const Terrain& t, Vec3 near, double searchRadius, double maxSlope, double clearRadius, Vec3& out,
                  const std::vector<Vec3>& avoid, double avoidRadius) {
    const double step = 90.0;
    for (double r = 0; r <= searchRadius; r += step) {
        int count = r < 1 ? 1 : std::max(6, static_cast<int>(2.0 * kPi * r / step));
        for (int k = 0; k < count; ++k) {
            double a = k * 2.0 * kPi / count + r * 0.0007;
            double x = near.x + std::cos(a) * r, z = near.z + std::sin(a) * r;
            if (std::fabs(x) > Terrain::kHalfSize - 1500 || std::fabs(z) > Terrain::kHalfSize - 1500) continue;
            if (t.isWater(x, z) || t.height(x, z) < 2.0) continue;
            if (t.onRunway(x, z, 400.0)) continue;
            bool ok = true;
            for (const Vec3& v : avoid)
                if (std::hypot(v.x - x, v.z - z) < avoidRadius) { ok = false; break; }
            if (!ok) continue;
            if (slopeAround(t, x, z, clearRadius) > maxSlope) continue;
            bool wet = false;
            for (int q = 0; q < 6 && !wet; ++q) {
                double b = q * kPi / 3.0;
                if (t.isWater(x + std::cos(b) * clearRadius, z + std::sin(b) * clearRadius)) wet = true;
            }
            if (wet) continue;
            out = {x, t.height(x, z), z};
            return true;
        }
    }
    return false;
}

namespace {
std::vector<Vec3> extrema(const HeightGrid& g, int count, double minSeparation, bool highest, double margin) {
    struct C { float h; int i, j; };
    std::vector<C> cand;
    const int rad = 3, lim = std::max(rad, static_cast<int>(margin / g.cell));
    for (int j = lim; j < g.n - lim; ++j)
        for (int i = lim; i < g.n - lim; ++i) {
            if (g.wet(i, j)) continue;
            float h = g.at(i, j);
            if (!highest && h < 2.0f) continue;
            bool ext = true;
            for (int b = -rad; b <= rad && ext; ++b)
                for (int a = -rad; a <= rad; ++a) {
                    if (!a && !b) continue;
                    float o = g.at(i + a, j + b);
                    if (highest ? o > h : o < h) { ext = false; break; }
                }
            if (ext) cand.push_back({h, i, j});
        }
    std::sort(cand.begin(), cand.end(), [&](const C& a, const C& b) { return highest ? a.h > b.h : a.h < b.h; });
    std::vector<Vec3> out;
    for (const C& c : cand) {
        Vec3 p = g.pos(c.i, c.j);
        bool far = true;
        for (const Vec3& o : out)
            if (std::hypot(o.x - p.x, o.z - p.z) < minSeparation) { far = false; break; }
        if (far) out.push_back(p);
        if (static_cast<int>(out.size()) >= count) break;
    }
    return out;
}
}  // namespace

std::vector<Vec3> findPeaks(const HeightGrid& g, int count, double minSeparation) { return extrema(g, count, minSeparation, true, 1500.0); }
std::vector<Vec3> findBasins(const HeightGrid& g, int count, double minSeparation, double margin) {
    return extrema(g, count, minSeparation, false, margin);
}

double seaFraction(const HeightGrid& g) {
    size_t w = 0;
    for (unsigned char c : g.water) w += c;
    return g.water.empty() ? 0.0 : static_cast<double>(w) / g.water.size();
}

bool findOpenWater(const Terrain& t, const HeightGrid& g, Vec3 near, double clearRadius, double minDepth, Vec3& out) {
    double best = 1e18;
    bool found = false;
    const int lim = static_cast<int>(1500.0 / g.cell);
    for (int j = lim; j < g.n - lim; j += 2)
        for (int i = lim; i < g.n - lim; i += 2) {
            if (!g.wet(i, j)) continue;
            Vec3 p = g.pos(i, j);
            if (t.height(p.x, p.z) > -minDepth) continue;
            double d = std::hypot(p.x - near.x, p.z - near.z);
            if (d >= best) continue;
            bool clear = true;
            for (int r = 1; r <= 3 && clear; ++r)
                for (int k = 0; k < 12; ++k) {
                    double a = k * kPi / 6.0, rr = clearRadius * r / 3.0;
                    if (!t.isWater(p.x + std::cos(a) * rr, p.z + std::sin(a) * rr)) { clear = false; break; }
                }
            if (!clear) continue;
            best = d;
            out = {p.x, 0.0, p.z};
            found = true;
        }
    return found;
}

LandingAxis carrierLandingAxis(const DeckInfo& d) {
    // deck coordinates: along = towards the bow, across = towards starboard
    const double la0a = -160.0, la0c = -6.0, la1a = 30.0, la1c = -36.0;
    double ua = la1a - la0a, uc = la1c - la0c, ul = std::hypot(ua, uc);
    ua /= ul; uc /= ul;
    double tdist = d.wireT[1] * 0.5 + d.wireT[2] * 0.5;   // between the 2nd and 3rd wire
    double ta = la0a + ua * tdist, tc = la0c + uc * tdist;
    Vec3 f = d.forward(), r{-f.z, 0.0, f.x};
    LandingAxis L;
    L.touchdown = d.center + f * ta + r * tc;
    L.touchdown.y = d.center.y;
    L.dir = normalize(f * ua + r * uc);
    L.heading = std::atan2(L.dir.z, L.dir.x);
    return L;
}

bool placeCarrierDeck(const Terrain& t, const HeightGrid& g, Vec3 preferNear, DeckInfo& out) {
    // candidate open-water spots, nearest first; for each try 16 headings and keep the first
    // whose 10 km approach and 4 km climb-out stay over water / well below the glide path
    std::vector<std::pair<double, Vec3>> spots;
    const int lim = static_cast<int>(2500.0 / g.cell);
    for (int j = lim; j < g.n - lim; j += 3)
        for (int i = lim; i < g.n - lim; i += 3) {
            if (!g.wet(i, j)) continue;
            Vec3 p = g.pos(i, j);
            if (t.height(p.x, p.z) > -18.0) continue;
            bool clear = true;
            for (int r = 1; r <= 3 && clear; ++r)
                for (int k = 0; k < 12; ++k) {
                    double a = k * kPi / 6.0, rr = 900.0 * r / 3.0;
                    if (!t.isWater(p.x + std::cos(a) * rr, p.z + std::sin(a) * rr)) { clear = false; break; }
                }
            if (clear) spots.push_back({std::hypot(p.x - preferNear.x, p.z - preferNear.z), p});
        }
    std::sort(spots.begin(), spots.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& sp : spots) {
        for (int k = 0; k < 16; ++k) {
            DeckInfo d;
            d.active = true;
            d.center = {sp.second.x, 19.5, sp.second.z};
            d.heading = wrapPi(k * kPi / 8.0);
            LandingAxis L = carrierLandingAxis(d);
            bool ok = true;
            for (double s = -10000.0; s <= 4000.0 && ok; s += 250.0) {
                Vec3 q = L.touchdown + L.dir * s;
                if (std::fabs(q.x) > Terrain::kHalfSize - 300 || std::fabs(q.z) > Terrain::kHalfSize - 300) { ok = false; break; }
                double path = s < 0 ? -s * std::tan(3.5 * kDeg) : 60.0 + s * 0.12;   // glide path / climb-out
                if (t.surfaceHeight(q.x, q.z) > 19.5 + path * 0.45 - 30.0 && t.height(q.x, q.z) > 0.0) ok = false;
            }
            // the whole ship (hull ~ 340 x 80 m) must float
            for (double a = -170; a <= 170 && ok; a += 85)
                for (double c = -40; c <= 40; c += 40) {
                    Vec3 f = d.forward(), r{-f.z, 0, f.x};
                    Vec3 q = d.center + f * a + r * c;
                    if (t.height(q.x, q.z) > -6.0) { ok = false; break; }
                }
            if (ok) { out = d; return true; }
        }
    }
    return false;
}

}  // namespace sky
