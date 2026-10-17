// Static world geometry: terrain chunks with LOD, far terrain ring, sea, runway/airbase,
// trees, town buildings, plus the procedural textures shared by the shaders.
#pragma once

#include "../sim/Terrain.hpp"
#include "raylib.h"

#include <vector>

namespace sky {

struct TerrainChunk {
    Mesh l0{};            // 129^2 (15.6 m) — built lazily near the camera, LRU cached
    bool hasL0 = false;
    double l0Used = 0;
    double x0 = 0, z0 = 0;
    Mesh hi{}, lo{};      // 65^2 (31 m) and 17^2 (125 m)
    Mesh trees{};
    bool hasTrees = false;
    Vector3 center{};
    float radius = 0;
    float maxH = 1e9f;    // highest ground height in the chunk (m)
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

    Texture2D terrainData{};  // 2048^2: RG normal.xz, B ambient occlusion, A baked sun shadow
    Vector3 bakedSun{0, 0, 0};
    /// Re-bake terrain self-shadowing for a new sun direction (multi-threaded, ~0.1-0.5 s).
    void bakeSunShadows(Vector3 sunDir);
    /// Make sure the 15.6 m mesh exists for this chunk (returns false if the per-frame budget is spent).
    bool ensureL0(TerrainChunk& c, double now, int& budget);
    void evictL0(Vector3 camPos, double now);
    Texture2D noise{};     // tiling RGBA noise
    Texture2D detailGrad{}; // gradient of the noise green channel (terrain detail normals)
    Texture2D landCover{};  // 1024^2 over the map: r forest, g grass/meadow, b farmland, a rock
    std::vector<unsigned char> coverPixels;
    Texture2D depthMap{};  // sea floor height
    Texture2D minimap{};   // coloured hill-shaded map for the HUD (covers +/- kMinimapHalf metres)
    static constexpr double kMinimapHalf = 20000.0;   // map is +/-16 km; the margin shows what lies beyond
    Texture2D puff{};      // soft cloud/smoke sprite
    Texture2D cloud[4]{};  // cumulus sprites (baked shading, upright)
    Texture2D glow{};      // radial glow sprite
    Texture2D spark{};     // hard spark sprite

    void build(const Terrain& t);
    const Terrain* terrain = nullptr;
    std::vector<unsigned char> dataPixels;   // CPU copy of terrainData (RGBA)
    void unload();
};

}  // namespace sky
