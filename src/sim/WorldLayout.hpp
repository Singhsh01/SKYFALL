// Man-made world features generated deterministically from any terrain (procedural or real
// elevation data): towns, a road network, a railway with an engineered (grade-limited) profile,
// the bridges/viaducts that profile needs, an industrial zone and wind farms. The renderer turns
// this into meshes; the combat sandbox uses the railway for trains and the bridges as targets.
#pragma once

#include "Terrain.hpp"

#include <string>
#include <vector>

namespace sky {

struct Polyline {
    std::vector<Vec3> pts;      // y = surface of the road / rail head
    std::vector<double> cum;    // cumulative length at each point
    double length() const { return cum.empty() ? 0.0 : cum.back(); }
    Vec3 at(double s, Vec3* tangent = nullptr) const;
    void finalize();
};

enum class TownKind { Village, Town, City, Industrial };

struct Town {
    Vec3 center;
    double radius = 600;
    TownKind kind = TownKind::Town;
    std::string name;
};

struct Bridge {
    Vec3 a, b;          // deck end points (deck height in y)
    bool rail = false;
    double maxClearance = 0;   // deck height above the ground below, metres
};

struct WindTurbine { Vec3 pos; double heading = 0; };

/// A high-voltage line: lattice pylons (y = ground) joined by sagging conductors.
struct PowerLine { std::vector<Vec3> pylons; bool trunk = false; };
/// A concrete gravity dam across a river gorge (a..b along the crest, y = crest height).
struct Dam { Vec3 a, b; double bed = 0; Vec3 downstream; };
enum class FacilityKind : unsigned char { Substation, WaterWorks, Powerhouse };
struct Facility { Vec3 pos; double yaw = 0; FacilityKind kind = FacilityKind::Substation; };

// ---- world detail: scenery props, night lights and road traffic (see WorldDetail.cpp) ----
enum class PropKind : unsigned char {
    Helicopter, AttackHelicopter, Tank, Apc, Truck, FuelTruck, Jeep,
    Barracks, Tent, Helipad, RadarMast, FloodMast, Checkpoint, Watchtower, Revetment, Hesco,
    FuelBladder, Container, Crane, WaterTower, CommandPost, Fence,
    Streetlamp, Billboard, GasStation, ParkedCar, Antenna,
    // airbase life and ground equipment
    Tug, Forklift, FireTruck, RadarTruck, AmmoBunker, HeliShelter, CargoStack, FireStation,
    ParkedTransport,          // a C-17 on the apron (drawn with the aircraft model, ramp down)
    // towns and roads
    TrafficSignal, RoadSign, Hospital, Warehouse, Bush
};
struct SceneProp {
    Vec3 pos;                 // ground position (y = ground height)
    double yaw = 0;           // radians about +Y, 0 = facing +X
    PropKind kind = PropKind::Tank;
    unsigned char variant = 0;
    double size = 0;          // kind-specific length (fences: metres)
};
enum class LightKind : unsigned char { Street, Flood, Beacon, Helipad, Canopy, Billboard, Checkpoint };
struct LightPoint { Vec3 pos; LightKind kind = LightKind::Street; float phase = 0; };
/// A site with soldiers' quarters, vehicles and helicopters: the airbase's army compound and
/// forward operating bases out in the countryside (used by missions and the Academy).
struct MilitarySite { Vec3 center; double radius = 120; bool airbase = false; std::string name; };
/// A vehicle driving up and down a lane. Positions are a pure function of time, so replays and
/// every renderer see the same traffic without any simulation state.
struct Car {
    int lane = 0;             // index into WorldLayout::lanes
    double s0 = 0;            // start offset along the lane, metres
    double speed = 14;        // m/s
    unsigned char kind = 0;   // 0 car, 1 van, 2 truck, 3 bus, 4 military truck, 5 jeep
    unsigned char color = 0;
};

struct WorldLayout {
    std::vector<Town> towns;
    std::vector<Polyline> roads;
    Polyline railway;
    std::vector<Bridge> bridges;
    std::vector<WindTurbine> turbines;
    std::vector<Vec3> factories;   // factory halls in the industrial zone (heading in y unused)
    // infrastructure (cities of the denser maps, the power grid, water)
    std::vector<PowerLine> powerLines;
    std::vector<Dam> dams;
    std::vector<Facility> facilities;
    Polyline metro;                // elevated metro viaduct through the city (urban maps), y = rail head
    std::vector<int> flyovers;     // indices into roads of the elevated city expressways
    // detail (filled by populateWorldDetail at the end of build)
    std::vector<SceneProp> props;
    std::vector<LightPoint> lights;
    std::vector<MilitarySite> sites;
    std::vector<Polyline> lanes;   // traffic lanes: the roads plus the base perimeter roads
    std::vector<Car> cars;

    static WorldLayout build(const Terrain& t);
    /// Index of the longest bridge (rail first), or -1.
    int largestBridge(bool railOnly) const;
    const Town* industrialTown() const;
    /// Where a car is at `time` (ping-pongs along its lane, driving on the right).
    Vec3 carAt(const Car& c, double time, Vec3* forward = nullptr) const;
    /// Metro trains running back and forth along the viaduct (a pure function of time).
    int metroTrains() const;
    /// Position of car `car` (0 = leading) of metro train `train` at `time`.
    Vec3 metroCarAt(int train, int car, double time, Vec3* forward = nullptr) const;
    static constexpr double kMetroCarLength = 21.0;
    static constexpr int kMetroCars = 4;
    /// Distance (horizontal) from p to the nearest prop footprint centre, or a large number.
    double nearestProp(const Vec3& p) const;
};

/// Adds the power grid (pylons, substations, a hydro dam on mountain rivers), water works, and
/// on the denser maps an elevated metro and city expressways (deterministic; see Infrastructure.cpp).
void buildInfrastructure(const Terrain& t, WorldLayout& L);
/// Engineered profile along a polyline (shared by roads, railway, metro): see WorldLayout.cpp.
Polyline engineerRoute(const Terrain& t, std::vector<Vec3> pts, double step, double window, double maxGrade, double minY);

/// Adds scenery props, night lights, military sites and traffic to a layout (deterministic).
void populateWorldDetail(const Terrain& t, WorldLayout& L);

}  // namespace sky
