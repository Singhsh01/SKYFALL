// Procedural, deterministic terrain shared by physics (collision) and rendering (meshes).
// It is fictional: primitive procedural mountains are NOT a real-world map.
#pragma once

#include "Math.hpp"

#include <vector>

namespace sky {

struct RunwayInfo {
    Vec3 center;          // world position of runway centre (y = runway surface)
    double heading = 0;   // radians, 0 = +X
    double length = 3000; // m
    double width = 45;    // m
    Vec3 threshold() const;  // start of the take-off roll (west end)
};

enum class SurfaceKind { Water, Sand, Grass, Forest, Rock, Snow, Runway, Town };

class Terrain {
public:
    static constexpr double kHalfSize = 16000.0;  // map spans [-16 km, +16 km] on X and Z
    static constexpr double kSeaLevel = 0.0;

    Terrain();
    /// Ground (or sea-floor) height at (x, z). Deterministic and cheap enough for physics.
    double height(double x, double z) const;
    /// Height of whatever the aircraft would collide with: max(ground, sea surface).
    double surfaceHeight(double x, double z) const;
    Vec3 normal(double x, double z, double eps = 4.0) const;
    bool isWater(double x, double z) const { return height(x, z) < kSeaLevel; }
    SurfaceKind surface(double x, double z) const;

    const RunwayInfo& runway() const { return runway_; }
    /// Centre of the fictional battlefield town (ground targets live here).
    Vec3 townCenter() const { return town_; }
    bool onRunway(double x, double z, double margin = 0.0) const;

private:
    double rawHeight(double x, double z) const;
    RunwayInfo runway_;
    Vec3 town_;
};

/// Deterministic value-noise helpers (also mirrored in the terrain shader for detail).
double valueNoise2(double x, double z, unsigned seed);
double fbm2(double x, double z, int octaves, unsigned seed);
double ridged2(double x, double z, int octaves, unsigned seed);

}  // namespace sky
