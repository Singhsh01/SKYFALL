// Real-world locations playable with real elevation data. The elevation files are produced by
// tools/fetch_real_terrain.py (AWS Terrain Tiles / Mapzen "terrarium": SRTM, GMTED2010, ETOPO1,
// NED and other open sources) as assets/terrain/<id>.skt: a 32 x 32 km square, 1025 x 1025
// samples, int16 metres. The procedural "Skyfall Desert" needs no data file.
//
// File layout (.skt, little endian):
//   char[4] "SKT1" | int32 n | float extent_m | float min_m | float max_m | float lat | float lon | int32 0
//   int16 heights[n * n]   row 0 = north edge, west -> east
#pragma once

#include "Terrain.hpp"

#include <string>
#include <vector>

namespace sky {

struct LocationDef {
    std::string id;
    std::string name;
    std::string region;
    double lat, lon;          // centre of the 32 km square
    Biome biome;
    double snowLine, treeLine, highRock;
    std::string blurb;
};

/// The playable world: index 0 is the procedural desert, then the real locations.
const std::vector<LocationDef>& locations();
const LocationDef* findLocation(const std::string& id);
TerrainInfo terrainInfoFor(const LocationDef& loc);

/// Reads a .skt file. Returns false (with a reason) if missing or malformed.
bool loadHeightmap(const std::string& path, Heightmap& out, std::string& error);
/// Writes a .skt file (used by tests and tools).
bool saveHeightmap(const std::string& path, const Heightmap& hm, double lat, double lon);

}  // namespace sky
