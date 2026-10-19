// Terrain analysis shared by the Training Academy course builder and the location campaigns:
// coarse height grids, valley-following A* paths, path smoothing / resampling, clearance checks,
// flat spots for installations, peaks and basins, open water and aircraft-carrier placement.
// Everything here is deterministic and raylib-free, so tests can validate generated content on
// every world location.
#pragma once

#include "Terrain.hpp"

#include <vector>

namespace sky {

/// Surface heights (max(ground, sea)) sampled on a coarse square grid over the map.
struct HeightGrid {
    int n = 0;
    double cell = 160.0;
    double origin = -15000.0;     // world x/z of sample (0, 0)
    std::vector<float> h;         // surface height
    std::vector<unsigned char> water;
    float hmin = 0, hmax = 1;
    float at(int i, int j) const { return h[static_cast<size_t>(j) * n + i]; }
    bool wet(int i, int j) const { return water[static_cast<size_t>(j) * n + i] != 0; }
    Vec3 pos(int i, int j) const { return {origin + i * cell, at(i, j), origin + j * cell}; }
    bool index(double x, double z, int& i, int& j) const;
};
HeightGrid sampleHeightGrid(const Terrain& t, double cell = 160.0, double half = 15000.0);

struct PathCost {
    double low = 3.0;        // preference for low ground: step cost *= 1 + low * normalised height
    double slope = 4.0;      // penalty for climbing / descending between cells
    double water = 0.0;      // extra cost factor over open water (0 = water is just low ground)
    double margin = 1200.0;  // keep the path this far inside the map edge
};
/// Least-cost path between two points that prefers low ground: follows valleys, fjords and
/// canyons and crosses ridges at their lowest saddle. Points are on the surface.
std::vector<Vec3> valleyPath(const HeightGrid& g, Vec3 from, Vec3 to, const PathCost& c);

/// Chaikin corner cutting (end points kept).
std::vector<Vec3> smoothPolyline(std::vector<Vec3> pts, int iterations);
/// Points every `spacing` metres along the polyline (horizontal distance; y interpolated).
std::vector<Vec3> resamplePolyline(const std::vector<Vec3>& pts, double spacing);
double polylineLength(const std::vector<Vec3>& pts);
/// Locally relaxes vertices whose turn would need a radius below `minRadius` (horizontal).
void enforceTurnRadius(std::vector<Vec3>& pts, double minRadius, int maxIterations = 60);
/// Smallest turn radius the polyline needs at any vertex (horizontal plane), metres.
double minTurnRadius(const std::vector<Vec3>& pts);

/// Lowest height of the straight segment a-b above the surface (negative = it hits terrain).
double segmentClearance(const Terrain& t, Vec3 a, Vec3 b, double step = 50.0);
/// Highest surface within `radius` of (x, z).
double maxSurfaceAround(const Terrain& t, double x, double z, double radius);
/// Average surface slope (1 - normal.y) around a point.
double slopeAround(const Terrain& t, double x, double z, double radius);

/// A dry, gentle spot for an installation near `near` (searching outwards up to `searchRadius`),
/// at least `avoidRadius` from every point in `avoid` and off the runway.
bool findFlatSpot(const Terrain& t, Vec3 near, double searchRadius, double maxSlope, double clearRadius, Vec3& out,
                  const std::vector<Vec3>& avoid = {}, double avoidRadius = 0.0);
/// Highest local maxima, at least `minSeparation` apart (dry land only).
std::vector<Vec3> findPeaks(const HeightGrid& g, int count, double minSeparation);
/// Lowest dry local minima (valley floors, basins), at least `minSeparation` apart.
std::vector<Vec3> findBasins(const HeightGrid& g, int count, double minSeparation, double margin = 2500.0);
/// Fraction of the map covered by open sea (surface at sea level, ground below it).
double seaFraction(const HeightGrid& g);
/// Open water at least `minDepth` deep with no land within `clearRadius`, as close to `near` as possible.
bool findOpenWater(const Terrain& t, const HeightGrid& g, Vec3 near, double clearRadius, double minDepth, Vec3& out);

/// Aircraft-carrier placement on open water: the deck heading is chosen so that the final
/// approach (from astern along the angled deck) and the bolter climb-out are over water.
bool placeCarrierDeck(const Terrain& t, const HeightGrid& g, Vec3 preferNear, DeckInfo& out);

/// The angled landing deck of a carrier, in world space.
struct LandingAxis {
    Vec3 touchdown;      // aim point (between the 2nd and 3rd wire), deck height
    Vec3 dir;            // landing direction (horizontal unit vector)
    double heading = 0;  // radians (0 = +X)
};
LandingAxis carrierLandingAxis(const DeckInfo& d);

}  // namespace sky
