#include "Terrain.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

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
    unsigned threads = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
    auto parallelRows = [&](int rows, auto&& fn) {
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t)
            pool.emplace_back([&, t] { for (int j = static_cast<int>(t); j < rows; j += static_cast<int>(threads)) fn(j); });
        for (auto& th : pool) th.join();
    };
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

double Terrain::analyticHeight(double x, double z) const { return applyModifiers(x, z, rawHeight(x, z)); }

double Terrain::applyModifiers(double x, double z, double h) const {
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

double Terrain::surfaceHeight(double x, double z) const { return std::max(height(x, z), kSeaLevel); }

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
    if (onRunway(x, z)) return SurfaceKind::Runway;
    double h = height(x, z);
    if (h < kSeaLevel) return SurfaceKind::Water;
    if (h < 8.0) return SurfaceKind::Sand;
    Vec3 n = normal(x, z, 20.0);
    if (h > 1500.0 && n.y > 0.75) return SurfaceKind::Snow;
    if (n.y < 0.78) return SurfaceKind::Rock;
    double dx = x - town_.x, dz = z - town_.z;
    if (dx * dx + dz * dz < 1100.0 * 1100.0) return SurfaceKind::Town;
    return SurfaceKind::Sand;
}

}  // namespace sky
