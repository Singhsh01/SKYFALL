// Static world geometry: terrain chunks with LOD, far terrain ring, sea, runway/airbase,
// trees, town buildings, plus the procedural textures shared by the shaders.
#pragma once

#include "../sim/Terrain.hpp"
#include "raylib.h"

#include <vector>

namespace sky {

struct TerrainChunk {
    Mesh hi{}, lo{};
    Mesh trees{};
    bool hasTrees = false;
    Vector3 center{};
    float radius = 0;
};

struct WorldMeshes {
    static constexpr int kChunks = 16;            // 16 x 16 chunks
    static constexpr double kChunkSize = 2000.0;  // metres

    std::vector<TerrainChunk> chunks;
    Mesh farRing{};
    Mesh water{};
    Mesh cloudPlane{};
    Mesh runway{};       // uv = metres along / across
    Mesh taxiways{};
    Mesh airbase{};      // hangars, tower, tanks (vertex coloured, lit shader)
    Mesh town{};
    Mesh runwayLights{}; // unused placeholder (lights drawn as sprites)
    std::vector<Vector3> edgeLights;
    std::vector<Vector3> parkedJets;   // parking spots (x,z,heading in y)

    Texture2D noise{};     // tiling RGBA noise
    Texture2D depthMap{};  // sea floor height
    Texture2D minimap{};   // coloured hill-shaded map for the HUD
    Texture2D puff{};      // soft cloud/smoke sprite
    Texture2D glow{};      // radial glow sprite
    Texture2D spark{};     // hard spark sprite

    void build(const Terrain& t);
    void unload();
};

}  // namespace sky
