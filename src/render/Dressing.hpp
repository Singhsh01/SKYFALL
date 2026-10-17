// Man-made scenery built from the WorldLayout: roads (with embankments and bridges), the railway
// (ballast, rails, viaducts), towns (buildings, gardens), the industrial zone, wind farms and an
// ambient freight train. Static geometry is merged into a handful of meshes per area.
#pragma once

#include "../sim/WorldLayout.hpp"
#include "raylib.h"

#include <vector>

namespace sky {

enum class BuildingStyle { Desert, Temperate, Alpine, Tropical };

class Dressing {
public:
    void build(const Terrain& t, const WorldLayout& layout, BuildingStyle style, bool skipFirstTown);
    void unload();

    struct Piece { Mesh mesh{}; Vector3 center{}; float radius = 0; bool valid = false; };
    std::vector<Piece> statics;              // roads, rail, bridges, towns, industry, turbine towers
    Mesh rotor{};                            // three-blade rotor, hub at the origin, spinning about +X
    std::vector<Vector3> rotorHubs;          // world position of each turbine hub
    std::vector<float> rotorYaw;
    std::vector<Vector3> chimneyTops;        // smoke sources
    const WorldLayout* layout = nullptr;
};

}  // namespace sky
