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

struct WorldLayout {
    std::vector<Town> towns;
    std::vector<Polyline> roads;
    Polyline railway;
    std::vector<Bridge> bridges;
    std::vector<WindTurbine> turbines;
    std::vector<Vec3> factories;   // factory halls in the industrial zone (heading in y unused)

    static WorldLayout build(const Terrain& t);
    /// Index of the longest bridge (rail first), or -1.
    int largestBridge(bool railOnly) const;
    const Town* industrialTown() const;
};

}  // namespace sky
