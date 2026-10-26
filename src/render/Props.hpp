// Meshes for the world-detail props (see sim/WorldDetail.cpp) and the traffic vehicles.
#pragma once

#include "Dressing.hpp"
#include "MeshBuilder.hpp"

#include <vector>

namespace sky {

/// Merges every prop of the layout into culled static pieces. Red obstruction-beacon positions
/// (masts, towers) are appended to `beacons`. Windows use vertex alpha 253 (lit at night) / 251.
void buildSceneProps(const Terrain& t, const WorldLayout& L, bool desertArmy, std::vector<Dressing::Piece>& out, std::vector<Vector3>& beacons);
/// A helicopter in local space (+X forward, ground at y = 0).
void buildHelicopter(MeshBuilder& m, Matrix xf, Color body, bool attack, bool blades);
/// A road vehicle (Car::kind) in local space (+X forward).
void buildCarBox(MeshBuilder& m, Matrix xf, Color body, int kind);
Color sceneCarColor(int v);
/// An airbase vehicle (tug, forklift, fire truck, radar truck, fuel truck) for the animated airbase.
void buildGroundVehicle(MeshBuilder& m, Matrix xf, PropKind k, Color body);

}  // namespace sky
