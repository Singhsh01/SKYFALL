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

enum class Biome { Desert = 0, Alpine = 1, Temperate = 2, Tropical = 3,
                   HighDesert = 4 };   // cold high-altitude desert (Ladakh): grey scree, poplar oases
/// Dry climates: sand / scree ground, sparse vegetation (no forests).
inline bool aridBiome(Biome b) { return b == Biome::Desert || b == Biome::HighDesert; }

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
    // inland water (the sea is anything below 0 m): rivers along the valley floors and lakes
    double riverDepth = 0;     // > 0: ground within this many metres of the local valley floor is river
    double lakeLevel = -1e9;   // ground below this height is a lake / reservoir / backwater
    int urban = 1;             // 0 villages, 1 town, 2 city, 3 metropolis (density of the scenery)
};

/// A square elevation grid (row 0 = north edge, z increasing southwards) covering `extent` metres.
struct Heightmap {
    int n = 0;
    double extent = 32000.0;
    std::vector<float> h;
    float at(int i, int j) const { return h[static_cast<size_t>(j) * n + i]; }
};

/// A ship's flight deck: a raised, flat landing surface on the sea (aircraft carrier).
struct DeckInfo {
    bool active = false;
    Vec3 center;              // deck centre at deck height
    double heading = 0;       // radians, 0 = +X (bow direction)
    double length = 333;      // m
    double width = 42;        // m: the usable strip (the hull is wider; see Renderer)
    double catStart = 40;     // catapult start, m forward of the centre
    double catStroke = 100;   // m
    // Angled landing deck: the landing line runs from (along -160, across -6) to (30, -36) in
    // deck coordinates (along = towards the bow, across = towards starboard), 9 degrees to port.
    // Four arresting wires lie across it at these distances from the start of the line (m).
    double wireT[4] = {32.7, 44.7, 56.7, 68.7};   // 12 m apart, like a real carrier
    Vec3 forward() const;
    Vec3 catapultStart() const;
    /// Position on the landing line: t = metres from its stern end, lateral = metres to starboard.
    void landingLine(double x, double z, double& t, double& lateral) const;
};

/// A solid obstacle of the Training Academy courses (rock pillars, natural arches, lattice
/// masts, bridge decks). Collidable: Terrain::obstacleHit() is checked by the flight model.
struct Obstacle {
    enum class Kind { Pillar, Arch, Mast, Deck } kind = Kind::Pillar;
    Vec3 pos;                // base centre (y = ground)
    double radius = 45;      // pillar / mast radius, arch leg radius
    double height = 300;     // top above pos.y (arch: top of the beam; deck: deck underside above pos.y)
    double heading = 0;      // radians: arch span axis / deck length axis (0 = +X)
    double span = 160;       // arch: distance between the legs (centres); deck: length
    double thickness = 35;   // arch beam depth; deck depth
    double width = 14;       // deck width
    unsigned seed = 1;       // shape variation for the renderer
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
    bool isWater(double x, double z) const {
        double h = height(x, z);
        return h < kSeaLevel || h < inlandLevel(x, z);
    }
    /// Surface of a river or lake at (x, z), or -1e9 where there is none (the sea is not included).
    double inlandLevel(double x, double z) const {
        if (inland_.empty()) return -1e9;
        int i = static_cast<int>((x + kHalfSize) / kWaterCell), j = static_cast<int>((z + kHalfSize) / kWaterCell);
        if (i < 0 || j < 0 || i >= kWaterN || j >= kWaterN) return -1e9;
        return inland_[static_cast<size_t>(j) * kWaterN + i];
    }
    /// Inland water grid (kWaterN^2 cells of kWaterCell metres; -1e9 = dry). Empty without rivers / lakes.
    static constexpr int kWaterN = 1024;
    static constexpr double kWaterCell = 2.0 * kHalfSize / kWaterN;
    const std::vector<float>& inlandWater() const { return inland_; }
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
    /// Aircraft carrier deck (inactive unless a mission places one).
    void setDeck(const DeckInfo& d) { deck_ = d; }
    void clearDeck() { deck_ = DeckInfo{}; }
    const DeckInfo& deck() const { return deck_; }
    bool onDeck(double x, double z, double margin = 0.0) const;
    /// Distance along the deck axis from the centre (+ = towards the bow).
    double deckAlong(double x, double z) const;

    /// Course obstacles (Training Academy). Replaces the current set; bumps obstacleVersion().
    void setObstacles(std::vector<Obstacle> obs);
    void clearObstacles() { setObstacles({}); }
    const std::vector<Obstacle>& obstacles() const { return obstacles_; }
    int obstacleVersion() const { return obstacleVersion_; }
    /// True if the point is inside any obstacle (cheap: bucketed by 500 m cells).
    bool obstacleHit(const Vec3& p) const;
    /// Distance from the point to the nearest obstacle surface (horizontal), up to `maxRange`.
    double obstacleDistance(const Vec3& p, double maxRange) const;

private:
    double rawHeight(double x, double z) const;
    double applyModifiers(double x, double z, double h) const;
    void buildInlandWater();
    std::vector<float> grid_;
    std::vector<float> inland_;   // river / lake surface per water cell (-1e9 = dry)
    RunwayInfo runway_;
    DeckInfo deck_;
    Vec3 town_;
    TerrainInfo info_;
    double outerBase_ = 300.0, outerRelief_ = 300.0;   // real maps: statistics for the land beyond the edge
    std::vector<Obstacle> obstacles_;
    std::vector<std::vector<int>> obstacleBuckets_;    // 64 x 64 cells of 500 m
    int obstacleVersion_ = 0;
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
