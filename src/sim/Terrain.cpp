#include "Terrain.hpp"

#include <algorithm>
#include <cmath>

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

Vec3 RunwayInfo::threshold() const {
    Vec3 dir{std::cos(heading), 0, std::sin(heading)};
    return center - dir * (length * 0.5 - 120.0);
}

Terrain::Terrain() {
    runway_.center = {-9000.0, 30.0, 3000.0};
    runway_.heading = 0.0;
    runway_.length = 3000.0;
    runway_.width = 45.0;
    town_ = {2500.0, 0.0, 2200.0};
    town_.y = height(town_.x, town_.z);
}

double Terrain::rawHeight(double x, double z) const {
    // Rolling hills everywhere.
    double hills = (fbm2(x / 2600.0, z / 2600.0, 5, 11u) - 0.45) * 2.0 * 190.0 + 70.0;
    // Mountain ranges in the north and south-east, ridged for sharp crests.
    double north = smoothstep(-2500.0, -8500.0, z);
    double south = smoothstep(8000.0, 13000.0, z) * 0.8;
    double west = smoothstep(-12500.0, -15500.0, x) * 0.7;
    double mask = std::min(1.0, north + south + west);
    double warp = fbm2(x / 9000.0 + 3.1, z / 9000.0 - 1.7, 3, 5u);
    double ridges = ridged2(x / 4200.0 + warp, z / 4200.0 - warp, 6, 23u);
    double mountains = ridges * 1900.0 * mask;
    double h = hills + mountains;

    // Coastline to the east, warped by low-frequency noise. Sea floor dips to -80 m.
    double coastWarp = (fbm2(x / 7000.0, z / 7000.0 + 9.0, 4, 41u) - 0.5) * 5200.0;
    double d = x + coastWarp;
    double sea = smoothstep(7200.0, 9800.0, d);
    h = lerpd(h, -80.0, sea);
    // Gentle beach shelf right at the coastline.
    if (h > -6.0 && h < 14.0 && sea > 0.05) h = lerpd(h, 3.0, 0.5);
    return h;
}

double Terrain::height(double x, double z) const {
    double h = rawHeight(x, z);
    // Airbase plateau: flatten a soft rectangle around the runway.
    {
        double dx = std::fabs(x - runway_.center.x) - (runway_.length * 0.5 + 600.0);
        double dz = std::fabs(z - runway_.center.z) - 650.0;
        double outside = std::sqrt(std::max(dx, 0.0) * std::max(dx, 0.0) + std::max(dz, 0.0) * std::max(dz, 0.0));
        double w = 1.0 - smoothstep(0.0, 1400.0, outside);
        h = lerpd(h, runway_.center.y - 0.6, w);
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
    if (h > 1300.0 && n.y > 0.75) return SurfaceKind::Snow;
    if (n.y < 0.78) return SurfaceKind::Rock;
    double dx = x - town_.x, dz = z - town_.z;
    if (dx * dx + dz * dz < 1100.0 * 1100.0) return SurfaceKind::Town;
    if (fbm2(x / 900.0, z / 900.0, 3, 99u) > 0.52 && h < 900.0) return SurfaceKind::Forest;
    return SurfaceKind::Grass;
}

}  // namespace sky
