// Procedural, ORIGINAL low-poly aircraft meshes ("inspired by" silhouettes only — no copied
// or licensed models). Body frame: +X nose, +Y up, +Z right wing. Units: metres.
#pragma once

#include "../aircraft/AircraftProfile.hpp"
#include "raylib.h"

namespace sky {

struct JetModel {
    Mesh body{};      // fuselage, wings, tails, intakes, nozzles (material id in colour alpha)
    Mesh canopy{};    // glass + frame (hidden from inside the cockpit)
    Vector3 eye{4.9f, 0.98f, 0.0f};  // pilot eye point
    Mesh gear{};      // landing gear (drawn when extended)
    Mesh stabL{}, stabR{};   // all-moving horizontal stabilisers (animated)
    Vector3 stabPivotL{}, stabPivotR{};
    Vector3 nozzles[2]{};    // nozzle exit centres
    int nozzleCount = 2;
    float nozzleRadius = 0.5f;
    Vector3 wingtips[2]{};   // for vapour trails
    float length = 18.9f;
    bool loaded = false;
};

JetModel buildJetModel(AirframeStyle style);
void unloadJetModel(JetModel& m);
/// Unit afterburner plume: from x = 0 (radius 1) to x = -1 (radius 0.15).
Mesh buildFlameMesh();
/// Air-to-air missile (approx. 3.7 m), body frame.
Mesh buildMissileMesh();

}  // namespace sky
