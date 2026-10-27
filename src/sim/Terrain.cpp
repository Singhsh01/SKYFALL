#include "Terrain.hpp"

#include <algorithm>
#include <queue>
#include <cmath>
#include "sim/Parallel.hpp"

namespace sky {

namespace {
inline unsigned hash2(int x, int z, unsigned seed) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
inline double hashUnit(int x, int z, unsigned seed) { return (hash2(x, z, seed) & 0xFFFFFF) / double(0xFFFFFF); }
inline double fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
}  // namespace

double valueNoise2(double x, double z, unsigned seed) {
    double fx = std::floor(x), fz = std::floor(z);
    int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    double tx = fade(x - fx), tz = fade(z - fz);
    double a = hashUnit(ix, iz, seed), b = hashUnit(ix + 1, iz, seed);
    double c = hashUnit(ix, iz + 1, seed), d = hashUnit(ix + 1, iz + 1, seed);
    return lerpd(lerpd(a, b, tx), lerpd(c, d, tx), tz);
}

double fbm2(double x, double z, int octaves, unsigned seed) {
    double sum = 0, amp = 0.5, norm = 0;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise2(x, z, seed + i * 17u);
        norm += amp;
        // rotate + scale between octaves to hide grid alignment
        double nx = x * 1.6 - z * 1.2, nz = x * 1.2 + z * 1.6;
        x = nx; z = nz;
        amp *= 0.5;
    }
    return sum / norm;
}

double ridged2(double x, double z, int octaves, unsigned seed) {
    double sum = 0, amp = 0.5, norm = 0, weight = 1.0;
    for (int i = 0; i < octaves; ++i) {
        double n = 1.0 - std::fabs(2.0 * valueNoise2(x, z, seed + i * 31u) - 1.0);
        n = n * n * weight;
        weight = clampd(n * 1.8, 0.0, 1.0);
        sum += amp * n;
        norm += amp;
        double nx = x * 1.7 - z * 1.1, nz = x * 1.1 + z * 1.7;
        x = nx; z = nz;
        amp *= 0.5;
    }
    return sum / norm;
}

double valueNoiseD(double x, double z, unsigned seed, double& ddx, double& ddz) {
    double fx = std::floor(x), fz = std::floor(z);
    int ix = static_cast<int>(fx), iz = static_cast<int>(fz);
    double tx = x - fx, tz = z - fz;
    double u = fade(tx), w = fade(tz);
    double du = 30.0 * tx * tx * (tx * (tx - 2.0) + 1.0), dw = 30.0 * tz * tz * (tz * (tz - 2.0) + 1.0);
    double a = hashUnit(ix, iz, seed), b = hashUnit(ix + 1, iz, seed);
    double c = hashUnit(ix, iz + 1, seed), d = hashUnit(ix + 1, iz + 1, seed);
    double k = a - b - c + d;
    ddx = du * ((b - a) + k * w);
    ddz = dw * ((c - a) + k * u);
    return a + (b - a) * u + (c - a) * w + k * u * w;
}

double erodedFbm(double x, double z, int octaves, unsigned seed) {
    double sum = 0, amp = 0.5, norm = 0, gx = 0, gz = 0;
    for (int i = 0; i < octaves; ++i) {
        double dx, dz;
        double n = valueNoiseD(x, z, seed + i * 13u, dx, dz);
        gx += dx * amp * 2.0; gz += dz * amp * 2.0;
        sum += amp * n / (1.0 + 0.9 * (gx * gx + gz * gz));
        norm += amp;
        double nx = x * 1.6 - z * 1.2, nz = x * 1.2 + z * 1.6;
        x = nx; z = nz;
        amp *= 0.5;
    }
    return sum / norm;
}

Vec3 RunwayInfo::threshold() const {
    Vec3 dir{std::cos(heading), 0, std::sin(heading)};
    return center - dir * (length * 0.5 - 120.0);
}

namespace {
// Particle hydraulic erosion on a square height grid (heights in "units", 1 unit = 2000 m).
// Deterministic: single-threaded, seeded RNG. Based on the classic droplet model
// (H. T. Beyer 2015): inertia, sediment capacity proportional to slope*speed*water.
void erode(std::vector<float>& h, int n, int droplets, unsigned seed, const std::vector<unsigned char>& allow) {
    const double inertia = 0.08, capFactor = 7.0, minCap = 0.0002, erodeSpeed = 0.4, depositSpeed = 0.08;
    const double evaporate = 0.015, gravity = 6.0;
    const int lifetime = 64, radius = 2;
    // precomputed brush
    std::vector<std::pair<int, int>> bo;
    std::vector<double> bw;
    double wsum = 0;
    for (int dz = -radius; dz <= radius; ++dz)
        for (int dx = -radius; dx <= radius; ++dx) {
            double d = std::sqrt(double(dx * dx + dz * dz));
            if (d > radius) continue;
            bo.push_back({dx, dz});
            bw.push_back(radius - d + 0.25);
            wsum += radius - d + 0.25;
        }
    for (double& w : bw) w /= wsum;
    auto at = [&](int i, int j) -> float& { return h[static_cast<size_t>(j) * n + i]; };
    auto sample = [&](double x, double z, double& gx, double& gz) {
        int i = static_cast<int>(x), j = static_cast<int>(z);
        double u = x - i, v = z - j;
        double a = at(i, j), b = at(i + 1, j), c = at(i, j + 1), d = at(i + 1, j + 1);
        gx = (b - a) * (1 - v) + (d - c) * v;
        gz = (c - a) * (1 - u) + (d - b) * u;
        return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
    };
    Rng rng(seed);
    for (int k = 0; k < droplets; ++k) {
        double x = rng.range(radius + 1, n - radius - 2), z = rng.range(radius + 1, n - radius - 2);
        if (!allow[static_cast<size_t>(static_cast<int>(z)) * n + static_cast<int>(x)]) continue;
        double dirX = 0, dirZ = 0, speed = 1.0, water = 1.0, sediment = 0.0;
        for (int life = 0; life < lifetime; ++life) {
            int i = static_cast<int>(x), j = static_cast<int>(z);
            double u = x - i, v = z - j;
            double gx, gz;
            double hOld = sample(x, z, gx, gz);
            dirX = dirX * inertia - gx * (1 - inertia);
            dirZ = dirZ * inertia - gz * (1 - inertia);
            double len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len < 1e-12) break;
            dirX /= len; dirZ /= len;
            x += dirX; z += dirZ;
            if (x < radius + 1 || z < radius + 1 || x >= n - radius - 2 || z >= n - radius - 2) break;
            double tx, tz;
            double dh = sample(x, z, tx, tz) - hOld;
            double cap = std::max(-dh * speed * water * capFactor, minCap);
            if (sediment > cap || dh > 0) {
                double amt = dh > 0 ? std::min(dh, sediment) : (sediment - cap) * depositSpeed;
                sediment -= amt;
                at(i, j) += static_cast<float>(amt * (1 - u) * (1 - v));
                at(i + 1, j) += static_cast<float>(amt * u * (1 - v));
                at(i, j + 1) += static_cast<float>(amt * (1 - u) * v);
                at(i + 1, j + 1) += static_cast<float>(amt * u * v);
            } else {
                double amt = std::min((cap - sediment) * erodeSpeed, -dh);
                for (size_t b = 0; b < bo.size(); ++b) {
                    float& cell = at(i + bo[b].first, j + bo[b].second);
                    double take = std::min<double>(amt * bw[b], std::max(0.0, double(cell)));
                    cell -= static_cast<float>(take);
                    sediment += take;
                }
            }
            speed = std::sqrt(std::max(0.0, speed * speed - dh * gravity));
            water *= (1 - evaporate);
        }
    }
}
}  // namespace

Terrain::Terrain() {
    runway_.center = {-9000.0, 30.0, 3000.0};
    runway_.heading = 0.0;
    runway_.length = 3000.0;
    runway_.width = 45.0;
    town_ = {2500.0, 0.0, 2200.0};
    // 1. coarse procedural grid (31.25 m)
    const int nc = kGridCells / 2 + 1;
    const double cc = kCell * 2.0;
    const double unit = 2000.0;
    std::vector<float> coarse(static_cast<size_t>(nc) * nc);
    parallelRows(nc, [&](int j) {
        for (int i = 0; i < nc; ++i)
            coarse[static_cast<size_t>(j) * nc + i] = static_cast<float>(rawHeight(-kHalfSize + i * cc, -kHalfSize + j * cc) / unit);
    });
    // 2. hydraulic erosion on land above the shoreline
    std::vector<unsigned char> allow(coarse.size());
    for (size_t k = 0; k < coarse.size(); ++k) allow[k] = coarse[k] * unit > 25.0f;
    std::vector<float> before = coarse;
    erode(coarse, nc, 700000, 1977u, allow);
    // keep the sea floor and shoreline exactly as designed
    for (size_t k = 0; k < coarse.size(); ++k) if (before[k] * unit < 10.0f) coarse[k] = before[k];
    // 3. upsample (bilinear) + fine slope detail, then airfield/town/approach modifiers
    const int n = kGridCells + 1;
    grid_.assign(static_cast<size_t>(n) * n, 0.0f);
    parallelRows(n, [&](int j) {
        for (int i = 0; i < n; ++i) {
            double x = -kHalfSize + i * kCell, z = -kHalfSize + j * kCell;
            int ci = std::min(i / 2, nc - 2), cj = std::min(j / 2, nc - 2);
            double u = (i - ci * 2) * 0.5, v = (j - cj * 2) * 0.5;
            auto C = [&](int a, int b) { return double(coarse[static_cast<size_t>(b) * nc + a]); };
            double h = ((C(ci, cj) * (1 - u) + C(ci + 1, cj) * u) * (1 - v) + (C(ci, cj + 1) * (1 - u) + C(ci + 1, cj + 1) * u) * v) * unit;
            if (h > 20.0) {
                double rough = smoothstep(150.0, 700.0, h);
                h += (ridged2(x / 140.0, z / 140.0, 2, 91u) - 0.4) * 9.0 * rough + (fbm2(x / 60.0, z / 60.0, 2, 93u) - 0.5) * 2.5;
            }
            grid_[static_cast<size_t>(j) * n + i] = static_cast<float>(applyModifiers(x, z, h));
        }
    });
    town_.y = height(town_.x, town_.z);
}

double Terrain::rawHeight(double x, double z) const {
    // --- desert floor: gentle dunes and alluvial fans rising towards the mountains ---
    double floorN = fbm2(x / 2400.0, z / 2400.0, 4, 11u);
    double base = 45.0 + 70.0 * floorN + std::max(0.0, -x - 4000.0) * 0.006;

    // --- where the mountains are: everywhere except a broad central valley and the coast ---
    double valleyWarp = (fbm2(x / 8000.0 + 4.0, z / 8000.0, 3, 17u) - 0.5) * 3600.0;
    double valleyDist = std::fabs(z - 3200.0 - valleyWarp);
    double valley = 1.0 - smoothstep(1900.0, 4600.0, valleyDist);
    double massif = smoothstep(0.30, 0.62, fbm2(x / 10500.0 - 3.0, z / 10500.0 + 7.0, 3, 61u));
    double mask = std::clamp(0.45 + 0.75 * massif, 0.0, 1.0) * (1.0 - 0.95 * valley);

    // --- eroded mountains: domain-warped, derivative-damped noise + sharp ridges ---
    double wx = x + (fbm2(x / 4300.0 + 1.3, z / 4300.0 - 2.1, 3, 5u) - 0.5) * 1900.0;
    double wz = z + (fbm2(x / 4300.0 - 7.7, z / 4300.0 + 3.4, 3, 6u) - 0.5) * 1900.0;
    double e = erodedFbm(wx / 3900.0, wz / 3900.0, 7, 23u);
    double r = ridged2(wx / 2700.0, wz / 2700.0, 6, 29u);
    // t spans ~0.11 (1 %) .. 0.67 (99 %); map without an upper clamp so peaks stay sharp
    double t = std::max(0.0, (0.58 * r + 0.42 * e - 0.15) / 0.52);
    // broad uplifted massif + eroded relief on top (no isolated needles)
    double uplift = 420.0 * smoothstep(0.0, 1.0, mask);
    double mountains = (uplift + std::pow(t, 1.1) * 1150.0) * mask;
    // fine erosion gullies on the slopes (stronger where the mountain is steep/high)
    double gully = ridged2(wx / 520.0, wz / 520.0, 3, 41u);
    mountains += (gully - 0.35) * 70.0 * smoothstep(80.0, 600.0, mountains);
    double h = base + std::max(0.0, mountains);

    // --- dry river bed (wadi) meandering down the valley to the sea ---
    double wadiZ = 3700.0 + (fbm2(x / 3000.0, 4.2, 3, 71u) - 0.5) * 2200.0;
    double wd = std::fabs(z - wadiZ);
    h -= 9.0 * (1.0 - smoothstep(40.0, 220.0, wd)) * smoothstep(-6000.0, -2000.0, x);

    // --- coastline to the east, warped. Sea floor dips to -80 m ---
    double coastWarp = (fbm2(x / 7000.0, z / 7000.0 + 9.0, 4, 41u) - 0.5) * 5200.0;
    double d = x + coastWarp;
    double sea = smoothstep(7400.0, 9800.0, d);
    h = lerpd(h, -80.0, sea);
    if (h > -6.0 && h < 14.0 && sea > 0.05) h = lerpd(h, 3.0, 0.5);  // beach shelf
    return h;
}

double Terrain::analyticHeight(double x, double z) const {
    if (info_.real) {
        // Beyond a real map the landscape goes on: first the map mirrored across its edge (so
        // ridges, valleys and coastlines continue without a seam), then, further out, mountains
        // generated with the map's own height and relief. Sea at the edge stays sea.
        const double H = kHalfSize - 1e-6;
        auto mirror = [&](double v) { return v > H ? clampd(2.0 * H - v, -H, H) : (v < -H ? clampd(-2.0 * H - v, -H, H) : v); };
        double e = height(mirror(x), mirror(z));
        double out = std::max({std::fabs(x) - kHalfSize, std::fabs(z) - kHalfSize, 0.0});
        double w = smoothstep(3000.0, 14000.0, out);
        if (w <= 0.0) return e;
        double wx = x + (fbm2(x / 5200.0 + 3.1, z / 5200.0, 3, 141u) - 0.5) * 2600.0;
        double wz = z + (fbm2(x / 5200.0, z / 5200.0 - 5.3, 3, 142u) - 0.5) * 2600.0;
        double ridges = ridged2(wx / 3600.0, wz / 3600.0, 5, 131u);
        double massif = fbm2(x / 14000.0, z / 14000.0, 3, 137u);
        double proc = outerBase_ + (ridges - 0.45) * outerRelief_ * 2.4 + (massif - 0.5) * outerRelief_ * 1.6;
        proc = std::max(proc, 4.0 + fbm2(x / 900.0, z / 900.0, 2, 139u) * 8.0);   // low ground stays dry land
        double land = smoothstep(-15.0, 12.0, e);
        double sea = std::min(e, -25.0 - out * 0.004);
        return lerpd(sea, lerpd(e, proc, w), land);
    }
    return applyModifiers(x, z, rawHeight(x, z));
}

namespace {
// Elevation sources store the sea surface (and fjords) as exactly 0 m. Turn those samples into a
// sea floor that deepens away from the shore so they read and behave as water.
Heightmap withSeaFloor(const Heightmap& in) {
    Heightmap hm = in;
    const int n = hm.n;
    if (n <= 0) return hm;
    const float inf = 1e9f;
    std::vector<float> d(static_cast<size_t>(n) * n);
    bool any = false;
    for (size_t k = 0; k < d.size(); ++k) {
        bool sea = hm.h[k] <= 0.3f;
        d[k] = sea ? inf : 0.0f;
        any |= sea;
    }
    if (!any) return hm;
    const float c1 = 1.0f, c2 = 1.41421f;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            float& v = d[static_cast<size_t>(j) * n + i];
            if (i > 0) v = std::min(v, d[static_cast<size_t>(j) * n + i - 1] + c1);
            if (j > 0) {
                v = std::min(v, d[static_cast<size_t>(j - 1) * n + i] + c1);
                if (i > 0) v = std::min(v, d[static_cast<size_t>(j - 1) * n + i - 1] + c2);
                if (i + 1 < n) v = std::min(v, d[static_cast<size_t>(j - 1) * n + i + 1] + c2);
            }
        }
    for (int j = n - 1; j >= 0; --j)
        for (int i = n - 1; i >= 0; --i) {
            float& v = d[static_cast<size_t>(j) * n + i];
            if (i + 1 < n) v = std::min(v, d[static_cast<size_t>(j) * n + i + 1] + c1);
            if (j + 1 < n) {
                v = std::min(v, d[static_cast<size_t>(j + 1) * n + i] + c1);
                if (i + 1 < n) v = std::min(v, d[static_cast<size_t>(j + 1) * n + i + 1] + c2);
                if (i > 0) v = std::min(v, d[static_cast<size_t>(j + 1) * n + i - 1] + c2);
            }
        }
    const float cell = static_cast<float>(hm.extent / (n - 1));
    for (size_t k = 0; k < d.size(); ++k) {
        if (hm.h[k] > 0.3f) continue;
        float depth = std::min(70.0f, 2.5f + d[k] * cell * 0.035f);
        if (d[k] >= inf * 0.5f) depth = 70.0f;   // no land anywhere
        hm.h[k] = std::min(hm.h[k], -depth);
    }
    return hm;
}
}  // namespace

Terrain::Terrain(const Heightmap& hmIn, const TerrainInfo& info) : info_(info) {
    const Heightmap hm = withSeaFloor(hmIn);
    info_.real = true;
    runway_.heading = 0.0;
    runway_.length = 3000.0;
    runway_.width = 45.0;
    const int n = kGridCells + 1;
    grid_.assign(static_cast<size_t>(n) * n, 0.0f);
    // 1. resample the source grid (bilinear) onto the game grid, plus metre-scale relief detail
    auto src = [&](double x, double z) {
        double u = (x / hm.extent + 0.5) * (hm.n - 1), v = (z / hm.extent + 0.5) * (hm.n - 1);
        u = clampd(u, 0.0, hm.n - 1.001);
        v = clampd(v, 0.0, hm.n - 1.001);
        int i = static_cast<int>(u), j = static_cast<int>(v);
        double fu = u - i, fv = v - j;
        double a = hm.at(i, j), b = hm.at(i + 1, j), c = hm.at(i, j + 1), d = hm.at(i + 1, j + 1);
        return (a + (b - a) * fu) * (1.0 - fv) + (c + (d - c) * fu) * fv;
    };
    parallelRows(n, [&](int j) {
        for (int i = 0; i < n; ++i) {
            double x = -kHalfSize + i * kCell, z = -kHalfSize + j * kCell;
            double h = src(x, z);
            if (h > 2.0) h += (ridged2(x / 120.0, z / 120.0, 2, 91u) - 0.4) * 4.0 + (fbm2(x / 50.0, z / 50.0, 2, 93u) - 0.5) * 1.5;
            grid_[static_cast<size_t>(j) * n + i] = static_cast<float>(h);
        }
    });
    // 1b. rivers and lakes (before choosing the runway and the town, which must stay dry)
    buildInlandWater();
    auto raw = [&](double x, double z) {
        double gx = (x + kHalfSize) / kCell, gz = (z + kHalfSize) / kCell;
        int i = std::clamp(static_cast<int>(gx), 0, kGridCells), j = std::clamp(static_cast<int>(gz), 0, kGridCells);
        double h = static_cast<double>(gridAt(i, j));
        return inlandLevel(x, z) > h ? -1.0 : h;   // wet ground counts as water for the site searches
    };
    // 2. runway: the flattest dry east-west strip, preferring the middle of the map
    double best = 1e18;
    Vec3 rc{0, 0, 0};
    for (double cz = -11000; cz <= 11000; cz += 500)
        for (double cx = -10000; cx <= 10000; cx += 500) {
            double lo = 1e9, hi = -1e9;
            bool wet = false;
            for (double ax = -1800; ax <= 1800; ax += 300)
                for (double az : {-350.0, 0.0, 350.0}) {
                    double h = raw(cx + ax, cz + az);
                    if (h < 2.0) wet = true;
                    lo = std::min(lo, h);
                    hi = std::max(hi, h);
                }
            if (wet) continue;
            // a usable approach: the steepest angle the terrain forces on a straight-in from either end
            double base = 0.5 * (lo + hi), app = 90.0;
            for (double sgn : {-1.0, 1.0}) {
                double need = 0;
                // the final and the turn onto it need room inside the map
                if (std::fabs(cx + sgn * (1500.0 + 7000.0)) > kHalfSize - 1800.0) need = 30.0;
                for (double d = 400; d <= 7000 && need < 30.0; d += 300) {
                    double x = cx + sgn * (1500.0 + d);
                    for (double az : {-120.0, 0.0, 120.0})
                        need = std::max(need, std::atan2(raw(x, cz + az) - base + 40.0, d) / kDeg);
                }
                app = std::min(app, need);
            }
            double score = (hi - lo) + std::hypot(cx, cz) * 0.004 + std::max(0.0, app - 3.5) * 60.0;
            if (score < best) { best = score; rc = {cx, base, cz}; }
        }
    if (best > 1e17) rc = {0, std::max(10.0, raw(0, 0)), 0};
    runway_.center = rc;
    // 3. battlefield town: the flattest dry spot at least 5 km from the runway
    best = 1e18;
    town_ = {rc.x + 6000, 0, rc.z};
    for (double cz = -12000; cz <= 12000; cz += 500)
        for (double cx = -12000; cx <= 12000; cx += 500) {
            if (std::hypot(cx - rc.x, cz - rc.z) < 5000) continue;
            double lo = 1e9, hi = -1e9;
            bool wet = false;
            for (int k = 0; k < 9; ++k) {
                double a = k * kPi / 4, r = k == 8 ? 0.0 : 500.0;
                double h = raw(cx + std::cos(a) * r, cz + std::sin(a) * r);
                if (h < 2.0) wet = true;
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
            if (wet) continue;
            double score = (hi - lo) + std::hypot(cx - rc.x, cz - rc.z) * 0.001;
            if (score < best) { best = score; town_ = {cx, 0.5 * (lo + hi), cz}; }
        }
    // cumulus layer ~1.4 km above the median ground (valleys stay clear, high peaks poke through)
    {
        std::vector<float> sample;
        for (int j = 0; j < n; j += 32) for (int i = 0; i < n; i += 32) sample.push_back(grid_[static_cast<size_t>(j) * n + i]);
        std::nth_element(sample.begin(), sample.begin() + sample.size() / 2, sample.end());
        info_.cloudBase = std::max(1800.0, static_cast<double>(sample[sample.size() / 2]) + 1400.0);
    }
    // land statistics for the procedural continuation beyond the map edge
    {
        double sum = 0, sum2 = 0; int cnt = 0;
        for (int j = 0; j < n; j += 8) for (int i = 0; i < n; i += 8) {
            double h = grid_[static_cast<size_t>(j) * n + i];
            if (h < 2.0) continue;
            sum += h; sum2 += h * h; ++cnt;
        }
        if (cnt > 0) {
            outerBase_ = sum / cnt;
            outerRelief_ = std::clamp(std::sqrt(std::max(0.0, sum2 / cnt - outerBase_ * outerBase_)), 60.0, 1400.0);
        }
    }
    // 4. flatten the airbase and the town square
    parallelRows(n, [&](int j) {
        for (int i = 0; i < n; ++i) {
            double x = -kHalfSize + i * kCell, z = -kHalfSize + j * kCell;
            float& g = grid_[static_cast<size_t>(j) * n + i];
            g = static_cast<float>(applyModifiers(x, z, g));
        }
    });
    town_.y = height(town_.x, town_.z);
}

/// Rivers along the valley floors (ground within riverDepth metres of the lowest ground around
/// it, in real valleys or on the lowest plain of the map) and lakes / backwaters (ground below
/// lakeLevel). Speckles are removed and tiny ponds dropped, so what is left reads as rivers and lakes.
void Terrain::buildInlandWater() {
    inland_.clear();
    const bool river = info_.riverDepth > 0, lake = info_.lakeLevel > -1e8;
    if (!river && !lake) return;
    const int N = kWaterN;
    std::vector<float> H(static_cast<size_t>(N) * N);
    parallelRows(N, [&](int j) {
        for (int i = 0; i < N; ++i)
            H[static_cast<size_t>(j) * N + i] = static_cast<float>(height(-kHalfSize + (i + 0.5) * kWaterCell, -kHalfSize + (j + 0.5) * kWaterCell));
    });
    auto idx = [](int i, int j) { return static_cast<size_t>(j) * kWaterN + i; };
    std::vector<float> level(H.size(), -1e9f);
    if (river) {
        // rivers follow the drainage: fill the pits (priority flood), route every cell to its
        // steepest downhill neighbour, accumulate the area draining through each cell; where that
        // area is large there is a river, wider the more it carries. Valleys crossing the map edge
        // bring in water from outside (the big through-flowing rivers).
        std::vector<float> F(H);
        {
            using Q = std::pair<float, size_t>;
            std::priority_queue<Q, std::vector<Q>, std::greater<Q>> pq;
            std::vector<char> seen(H.size(), 0);
            for (int i = 0; i < N; ++i)
                for (int j : {0, N - 1}) { pq.push({F[idx(i, j)], idx(i, j)}); pq.push({F[idx(j, i)], idx(j, i)}); seen[idx(i, j)] = seen[idx(j, i)] = 1; }
            while (!pq.empty()) {
                auto [hc, k] = pq.top(); pq.pop();
                int i = static_cast<int>(k % N), j = static_cast<int>(k / N);
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        int a2 = i + di, b2 = j + dj;
                        if ((!di && !dj) || a2 < 0 || b2 < 0 || a2 >= N || b2 >= N) continue;
                        size_t q = idx(a2, b2);
                        if (seen[q]) continue;
                        seen[q] = 1;
                        F[q] = std::max(F[q], hc + 0.001f);
                        pq.push({F[q], q});
                    }
            }
        }
        std::vector<int> down(H.size(), -1);
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                float best = 0; int bk = -1;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        int a2 = i + di, b2 = j + dj;
                        if ((!di && !dj) || a2 < 0 || b2 < 0 || a2 >= N || b2 >= N) continue;
                        float drop = (F[idx(i, j)] - F[idx(a2, b2)]) / ((di && dj) ? 1.414f : 1.0f);
                        if (drop > best) { best = drop; bk = static_cast<int>(idx(a2, b2)); }
                    }
                down[idx(i, j)] = bk;
            }
        std::vector<double> acc(H.size(), 1.0);
        // inflow where a main valley crosses the map edge: the lowest stretches of the border
        {
            std::vector<float> edge;
            for (int i = 0; i < N; ++i) for (int j : {0, N - 1}) { edge.push_back(H[idx(i, j)]); edge.push_back(H[idx(j, i)]); }
            std::nth_element(edge.begin(), edge.begin() + edge.size() / 8, edge.end());
            float low = edge[edge.size() / 8];
            auto inflow = [&](int i, int j, int ai, int aj) {
                size_t k = idx(i, j);
                if (H[k] <= 0.5f || H[k] > low) return;
                for (int d = -48; d <= 48; ++d) {   // the lowest point of the border within 1.5 km
                    int a2 = i + ai * d, b2 = j + aj * d;
                    if (a2 < 0 || b2 < 0 || a2 >= N || b2 >= N) continue;
                    if (H[idx(a2, b2)] < H[k]) return;
                }
                acc[k] += 400000.0;   // ~ 400 km2 of catchment outside the map
            };
            for (int i = 0; i < N; ++i) { inflow(i, 0, 1, 0); inflow(i, N - 1, 1, 0); inflow(0, i, 0, 1); inflow(N - 1, i, 0, 1); }
        }
        std::vector<size_t> order(H.size());
        for (size_t k = 0; k < order.size(); ++k) order[k] = k;
        std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return F[x] > F[y]; });
        for (size_t k : order) if (down[k] >= 0) acc[static_cast<size_t>(down[k])] += acc[k];
        const double cellKm2 = kWaterCell * kWaterCell * 1e-6;
        const double minKm2 = 10.0 / std::max(0.5, info_.riverDepth);   // bigger riverDepth: smaller streams show too
        for (int j = 0; j < N; ++j)
            for (int i = 0; i < N; ++i) {
                size_t k = idx(i, j);
                double km2 = acc[k] * cellKm2;
                if (km2 < minKm2 || H[k] <= 0.5f) continue;
                // half-width in cells: a stream is one cell, a big river several hundred metres
                int r = static_cast<int>(std::clamp(std::sqrt(km2 / 25.0) + 0.5, 1.0, 9.0));
                float lv = H[k] + 0.8f + 0.25f * static_cast<float>(r);
                for (int dj = -r; dj <= r; ++dj)
                    for (int di = -r; di <= r; ++di) {
                        int a2 = i + di, b2 = j + dj;
                        if (a2 < 0 || b2 < 0 || a2 >= N || b2 >= N || di * di + dj * dj > r * r + r) continue;
                        size_t q = idx(a2, b2);
                        if (H[q] < lv) level[q] = std::max(level[q], lv);   // the banks stay dry
                    }
            }
    }
    if (lake) {
        for (size_t k = 0; k < H.size(); ++k)
            if (H[k] > 0.0f && H[k] < info_.lakeLevel) level[k] = std::max(level[k], static_cast<float>(info_.lakeLevel));
    }
    // clean up: a water cell needs at least 3 wet neighbours (no speckles), then drop ponds
    std::vector<float> clean(level);
    for (int j = 1; j < N - 1; ++j)
        for (int i = 1; i < N - 1; ++i) {
            if (level[idx(i, j)] < -1e8f) continue;
            int wet = 0;
            for (int dj = -1; dj <= 1; ++dj) for (int di = -1; di <= 1; ++di) if ((di || dj) && level[idx(i + di, j + dj)] > -1e8f) ++wet;
            if (wet < 2) clean[idx(i, j)] = -1e9f;
        }
    std::vector<int> comp(H.size(), -1);
    std::vector<size_t> stack;
    for (size_t s0 = 0; s0 < H.size(); ++s0) {
        if (clean[s0] < -1e8f || comp[s0] >= 0) continue;
        std::vector<size_t> members;
        stack.assign(1, s0);
        comp[s0] = 1;
        while (!stack.empty()) {
            size_t k = stack.back(); stack.pop_back();
            members.push_back(k);
            int i = static_cast<int>(k % N), j = static_cast<int>(k / N);
            const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; ++d) {
                int a = i + di[d], b = j + dj[d];
                if (a < 0 || b < 0 || a >= N || b >= N) continue;
                size_t q = idx(a, b);
                if (clean[q] < -1e8f || comp[q] >= 0) continue;
                comp[q] = 1;
                stack.push_back(q);
            }
        }
        if (members.size() < 24) for (size_t k : members) clean[k] = -1e9f;   // ponds and puddles
    }
    size_t wet = 0;
    for (float v : clean) wet += v > -1e8f;
    if (wet) inland_ = std::move(clean);
}

double Terrain::applyModifiers(double x, double z, double h) const {
    if (info_.real) {
        // airbase plateau (narrower transition than the desert map: real valleys are tighter)
        double dx = std::fabs(x - runway_.center.x) - (runway_.length * 0.5 + 400.0);
        double dz = std::fabs(z - runway_.center.z) - 500.0;
        double outside = std::sqrt(std::max(dx, 0.0) * std::max(dx, 0.0) + std::max(dz, 0.0) * std::max(dz, 0.0));
        double w = 1.0 - smoothstep(0.0, 900.0, outside);
        h = lerpd(h, runway_.center.y - 0.6, w);
        double tdx = x - town_.x, tdz = z - town_.z;
        double tw = 1.0 - smoothstep(450.0, 900.0, std::sqrt(tdx * tdx + tdz * tdz));
        h = lerpd(h, town_.y, tw);
        return h;
    }
    // Airbase plateau: flatten a soft rectangle around the runway.
    {
        double dx = std::fabs(x - runway_.center.x) - (runway_.length * 0.5 + 600.0);
        double dz = std::fabs(z - runway_.center.z) - 650.0;
        double outside = std::sqrt(std::max(dx, 0.0) * std::max(dx, 0.0) + std::max(dz, 0.0) * std::max(dz, 0.0));
        double w = 1.0 - smoothstep(0.0, 1400.0, outside);
        h = lerpd(h, runway_.center.y - 0.6, w);
    }
    // Approach corridor west of runway 09: keep terrain below a 1.5-degree slope so a normal
    // 3-degree approach never meets a hill (Mission 03).
    {
        double thX = runway_.center.x - runway_.length * 0.5;
        double along = thX - x;  // metres before the threshold
        if (along > 0.0) {
            double wz = 1.0 - smoothstep(500.0, 1500.0, std::fabs(z - runway_.center.z));
            double wx = 1.0 - smoothstep(9000.0, 11000.0, along);
            double cap = runway_.center.y - 0.6 + along * std::tan(1.5 * kDeg);
            if (h > cap) h = lerpd(h, cap, wz * wx);
        }
    }
    // Town valley: flatten a disc so buildings/vehicles sit on level ground.
    {
        double dx = x - 2500.0, dz = z - 2200.0;
        double r = std::sqrt(dx * dx + dz * dz);
        double w = 1.0 - smoothstep(1300.0, 2600.0, r);
        h = lerpd(h, 55.0 + 10.0 * fbm2(x / 400.0, z / 400.0, 2, 77u), w);
    }
    return h;
}

double Terrain::height(double x, double z) const {
    double gx = (x + kHalfSize) / kCell, gz = (z + kHalfSize) / kCell;
    if (gx < 0.0 || gz < 0.0 || gx >= kGridCells || gz >= kGridCells) return analyticHeight(x, z);
    int i = static_cast<int>(gx), j = static_cast<int>(gz);
    double tx = gx - i, tz = gz - j;
    double a = gridAt(i, j), b = gridAt(i + 1, j), c = gridAt(i, j + 1), d = gridAt(i + 1, j + 1);
    return (a + (b - a) * tx) * (1.0 - tz) + (c + (d - c) * tx) * tz;
}

double Terrain::surfaceHeight(double x, double z) const {
    double h = std::max(height(x, z), kSeaLevel);
    if (!inland_.empty()) h = std::max(h, inlandLevel(x, z));
    if (deck_.active && onDeck(x, z, 14.0)) h = std::max(h, deck_.center.y);   // hull is wider than the strip
    return h;
}

void DeckInfo::landingLine(double x, double z, double& t, double& lateral) const {
    double c = std::cos(heading), sn = std::sin(heading);
    double dx = x - center.x, dz = z - center.z;
    double along = dx * c + dz * sn, across = -dx * sn + dz * c;
    const double a0 = -160.0, c0 = -6.0, ua0 = 190.0, uc0 = -30.0;
    double ul = std::hypot(ua0, uc0), ua = ua0 / ul, uc = uc0 / ul;
    double ra = along - a0, rc = across - c0;
    t = ra * ua + rc * uc;
    lateral = -ra * uc + rc * ua;   // + = starboard of the landing line
}

namespace {
constexpr int kObsCells = 64;
constexpr double kObsCell = 500.0;
int obsCell(double v) { return std::clamp(static_cast<int>(std::floor((v + kObsCells * kObsCell * 0.5) / kObsCell)), 0, kObsCells - 1); }
double obstacleReach(const Obstacle& o) {
    switch (o.kind) {
        case Obstacle::Kind::Arch: return o.span * 0.5 + o.radius;
        case Obstacle::Kind::Deck: return o.span * 0.5 + o.width;
        default: return o.radius;
    }
}
}  // namespace

void Terrain::setObstacles(std::vector<Obstacle> obs) {
    obstacles_ = std::move(obs);
    obstacleBuckets_.assign(kObsCells * kObsCells, {});
    for (int k = 0; k < static_cast<int>(obstacles_.size()); ++k) {
        const Obstacle& o = obstacles_[k];
        double r = obstacleReach(o) + 20.0;
        for (int j = obsCell(o.pos.z - r); j <= obsCell(o.pos.z + r); ++j)
            for (int i = obsCell(o.pos.x - r); i <= obsCell(o.pos.x + r); ++i) obstacleBuckets_[j * kObsCells + i].push_back(k);
    }
    ++obstacleVersion_;
}

namespace {
// Signed distance-like test in the obstacle's frame; returns horizontal clearance (<= 0: inside).
double obstacleClearance(const Obstacle& o, const Vec3& p) {
    double y = p.y - o.pos.y;
    switch (o.kind) {
        case Obstacle::Kind::Pillar:
        case Obstacle::Kind::Mast: {
            if (y < -60.0 || y > o.height) return 1e9;
            double taper = o.kind == Obstacle::Kind::Pillar ? 1.0 - 0.22 * clampd(y / o.height, 0.0, 1.0) : 1.0;
            return std::hypot(p.x - o.pos.x, p.z - o.pos.z) - o.radius * taper;
        }
        case Obstacle::Kind::Arch: {
            double c = std::cos(o.heading), s = std::sin(o.heading);
            double dx = p.x - o.pos.x, dz = p.z - o.pos.z;
            double u = dx * c + dz * s, v = -dx * s + dz * c;   // u along the span, v along the route
            double best = 1e9;
            if (y >= -60.0 && y <= o.height) {
                for (double side : {-1.0, 1.0}) best = std::min(best, std::hypot(u - side * o.span * 0.5, v) - o.radius);
            }
            if (y >= o.height - o.thickness && y <= o.height) {
                double du = std::fabs(u) - (o.span * 0.5 + o.radius), dv = std::fabs(v) - o.radius * 0.9;
                best = std::min(best, std::max(du, dv));
            }
            return best;
        }
        case Obstacle::Kind::Deck: {
            if (y < o.height || y > o.height + o.thickness) return 1e9;
            double c = std::cos(o.heading), s = std::sin(o.heading);
            double dx = p.x - o.pos.x, dz = p.z - o.pos.z;
            double u = dx * c + dz * s, v = -dx * s + dz * c;
            return std::max(std::fabs(u) - o.span * 0.5, std::fabs(v) - o.width * 0.5);
        }
    }
    return 1e9;
}
}  // namespace

bool Terrain::obstacleHit(const Vec3& p) const {
    if (obstacles_.empty() || obstacleBuckets_.empty()) return false;
    for (int k : obstacleBuckets_[obsCell(p.z) * kObsCells + obsCell(p.x)])
        if (obstacleClearance(obstacles_[k], p) <= 0.0) return true;
    return false;
}

double Terrain::obstacleDistance(const Vec3& p, double maxRange) const {
    double best = maxRange;
    if (obstacles_.empty() || obstacleBuckets_.empty()) return best;
    for (int j = obsCell(p.z - maxRange); j <= obsCell(p.z + maxRange); ++j)
        for (int i = obsCell(p.x - maxRange); i <= obsCell(p.x + maxRange); ++i)
            for (int k : obstacleBuckets_[j * kObsCells + i]) best = std::min(best, std::max(0.0, obstacleClearance(obstacles_[k], p)));
    return best;
}

Vec3 DeckInfo::forward() const { return {std::cos(heading), 0.0, std::sin(heading)}; }
Vec3 DeckInfo::catapultStart() const { return center + forward() * catStart; }

double Terrain::deckAlong(double x, double z) const {
    return (x - deck_.center.x) * std::cos(deck_.heading) + (z - deck_.center.z) * std::sin(deck_.heading);
}

bool Terrain::onDeck(double x, double z, double margin) const {
    if (!deck_.active) return false;
    double c = std::cos(deck_.heading), s = std::sin(deck_.heading);
    double dx = x - deck_.center.x, dz = z - deck_.center.z;
    double along = dx * c + dz * s;
    double across = -dx * s + dz * c;
    return std::fabs(along) <= deck_.length * 0.5 + margin * 0.2 && std::fabs(across) <= deck_.width * 0.5 + margin;
}

Vec3 Terrain::normal(double x, double z, double eps) const {
    double hl = height(x - eps, z), hr = height(x + eps, z);
    double hd = height(x, z - eps), hu = height(x, z + eps);
    return normalize(Vec3{hl - hr, 2.0 * eps, hd - hu});
}

bool Terrain::onRunway(double x, double z, double margin) const {
    double c = std::cos(runway_.heading), s = std::sin(runway_.heading);
    double dx = x - runway_.center.x, dz = z - runway_.center.z;
    double along = dx * c + dz * s;
    double across = -dx * s + dz * c;
    return std::fabs(along) <= runway_.length * 0.5 + margin && std::fabs(across) <= runway_.width * 0.5 + margin;
}

SurfaceKind Terrain::surface(double x, double z) const {
    if (onRunway(x, z) || onDeck(x, z)) return SurfaceKind::Runway;
    double h = height(x, z);
    if (h < kSeaLevel || h < inlandLevel(x, z)) return SurfaceKind::Water;
    if (h < 8.0) return SurfaceKind::Sand;
    Vec3 n = normal(x, z, 20.0);
    if (h > info_.snowLine && n.y > 0.75) return SurfaceKind::Snow;
    if (n.y < 0.78) return SurfaceKind::Rock;
    double dx = x - town_.x, dz = z - town_.z;
    if (dx * dx + dz * dz < 1100.0 * 1100.0) return SurfaceKind::Town;
    return SurfaceKind::Sand;
}

}  // namespace sky
