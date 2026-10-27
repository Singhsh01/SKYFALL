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

    struct Piece { Mesh mesh{}; Vector3 center{}; float radius = 0; bool valid = false; float distScale = 1.0f; /* x the scenery distance */ };
    std::vector<Piece> statics;              // roads, rail, bridges, towns, industry, turbine towers
    Mesh rotor{};                            // three-blade rotor, hub at the origin, spinning about +X
    std::vector<Vector3> rotorHubs;          // world position of each turbine hub
    std::vector<float> rotorYaw;
    std::vector<Vector3> chimneyTops;        // smoke sources
    std::vector<Vector3> beacons;            // red obstruction lights (masts, cranes, towers)
    // power-line conductors, drawn as lines (always about a pixel wide, faded with distance):
    // each span holds kWireStrands strands of kWirePoints points
    static constexpr int kWireStrands = 7, kWirePoints = 13;
    struct WireSpan { Vector3 center; float radius; int first; };
    std::vector<Vector3> wirePts;
    std::vector<WireSpan> wireSpans;
    const WorldLayout* layout = nullptr;
};

/// Power lines (pylons and conductors), substations, the hydro dam and its powerhouse, water
/// works and the metro viaduct with its stations (InfraDressing.cpp).
void buildInfrastructureMeshes(const Terrain& t, const WorldLayout& L, Dressing& d);

}  // namespace sky
