// Procedural, deterministic terrain shared by physics (collision) and rendering (meshes).
// By default it is fictional: an arid, eroded mountain range around a desert valley, a coast to
// the east. It can also be built from real elevation data (see sim/Locations.hpp): the runway and
// the battlefield town are then placed on the flattest suitable ground.
//
// The analytic height function (domain-warped noise + ridges) is evaluated on a 1025^2 grid,
// shaped by a deterministic particle-based hydraulic erosion pass (dendritic gullies, alluvial
// fans), then upsampled to a 2049 x 2049 grid (15.6 m spacing). Physics and every renderer mesh sample that same grid
// bilinearly, so what you see is exactly what you collide with.
#pragma once

#include "Math.hpp"

#include <string>
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

enum class Biome { Desert = 0, Alpine = 1, Temperate = 2, Tropical = 3 };

/// What the terrain represents and how it should be dressed (materials, vegetation, clouds).
struct TerrainInfo {
    std::string id = "desert";
    std::string name = "Skyfall Desert";
    std::string region = "Procedural";
    Biome biome = Biome::Desert;
    double snowLine = 1650;    // m: snow on gentle slopes above this
    double treeLine = 520;     // m: vegetation below this
    double highRock = 1050;    // m: bare grey rock above this
    double cloudBase = 2100;   // m: cumulus layer height
    bool real = false;         // built from real elevation data
    std::string attribution;
};

/// A square elevation grid (row 0 = north edge, z increasing southwards) covering `extent` metres.
struct Heightmap {
    int n = 0;
    double extent = 32000.0;
    std::vector<float> h;
    float at(int i, int j) const { return h[static_cast<size_t>(j) * n + i]; }
};

class Terrain {
public:
    static constexpr double kHalfSize = 16000.0;  // map spans [-16 km, +16 km] on X and Z
    static constexpr double kSeaLevel = 0.0;

    Terrain();                                                   // procedural desert (default)
    Terrain(const Heightmap& hm, const TerrainInfo& info);       // real elevation data
    const TerrainInfo& info() const { return info_; }
    /// Ground (or sea-floor) height at (x, z). Deterministic and cheap enough for physics.
    double height(double x, double z) const;
    /// Height of whatever the aircraft would collide with: max(ground, sea surface).
    double surfaceHeight(double x, double z) const;
    Vec3 normal(double x, double z, double eps = 4.0) const;
    bool isWater(double x, double z) const { return height(x, z) < kSeaLevel; }
    SurfaceKind surface(double x, double z) const;

    // Height grid shared with the renderer (row-major, (N+1) x (N+1) samples).
    static constexpr int kGridCells = 2048;
    static constexpr double kCell = 2.0 * kHalfSize / kGridCells;
    const std::vector<float>& grid() const { return grid_; }
    float gridAt(int i, int j) const { return grid_[static_cast<size_t>(j) * (kGridCells + 1) + i]; }
    /// Unmodified procedural height (no grid) — used outside the map and to build the grid.
    double analyticHeight(double x, double z) const;

    const RunwayInfo& runway() const { return runway_; }
    /// Centre of the fictional battlefield town (ground targets live here).
    Vec3 townCenter() const { return town_; }
    bool onRunway(double x, double z, double margin = 0.0) const;

private:
    double rawHeight(double x, double z) const;
    double applyModifiers(double x, double z, double h) const;
    std::vector<float> grid_;
    RunwayInfo runway_;
    Vec3 town_;
    TerrainInfo info_;
};

/// Deterministic value-noise helpers (also mirrored in the terrain shader for detail).
double valueNoise2(double x, double z, unsigned seed);
double fbm2(double x, double z, int octaves, unsigned seed);
double ridged2(double x, double z, int octaves, unsigned seed);
/// Value noise with analytic derivatives: returns value, writes d/dx and d/dz.
double valueNoiseD(double x, double z, unsigned seed, double& dx, double& dz);
/// Derivative-damped fbm: flattens where slopes accumulate, giving eroded-looking ridges.
double erodedFbm(double x, double z, int octaves, unsigned seed);

}  // namespace sky
