// Procedural, ORIGINAL aircraft meshes ("inspired by" silhouettes only — no copied or licensed
// models). Body frame: +X nose, +Y up, +Z right wing. Units: metres.
//
// Every airframe is built at three levels of detail. LOD 0 (close up) has separate, animated
// control surfaces (ailerons, flaps / elevons, rudders, canards, all-moving stabilisers),
// thrust-vectoring nozzles where the real family has them, spinning wheels on compressing
// gear, and positions for navigation, strobe and landing lights. LOD 1 and LOD 2 merge the
// moving parts into one static mesh with progressively coarser sections.
#pragma once

#include "../aircraft/AircraftProfile.hpp"
#include "raylib.h"

#include <vector>

namespace sky {

enum class JetPartKind { Aileron = 0, Flap = 1, Rudder = 2, Canard = 3, Nozzle = 4, Wheel = 5, Stab = 6, Ramp = 7 };

/// A moving part: mesh vertices are relative to `pivot`; it rotates about `axis` (body frame).
struct JetPart {
    Mesh mesh{};
    Vector3 pivot{};
    Vector3 axis{0, 0, 1};
    JetPartKind kind = JetPartKind::Aileron;
    float side = 1.0f;        // +1 right / -1 left (0 = centreline)
    int engine = -1;          // nozzle index for Nozzle parts
    float radius = 0.4f;      // wheel radius
};

struct JetLod {
    Mesh body{};      // fuselage, wings, tails, intakes, pylons (material id in colour alpha)
    Mesh canopy{};    // glass + frame (hidden from inside the cockpit)
    std::vector<JetPart> parts;
};

struct JetModel {
    JetLod lod[3];
    Mesh gear{};      // struts and doors (extended position), wheels are parts of LOD 0
    Mesh gearLo{};    // struts + wheels in one mesh for LOD 1
    Vector3 eye{4.9f, 0.98f, 0.0f};  // pilot eye point
    Vector3 nozzles[4]{};    // nozzle exit centres
    Vector3 nozzlePivot[4]{};
    int nozzleCount = 2;
    float nozzleRadius = 0.5f;
    bool vectoring = false;  // thrust-vectoring nozzles (pitch)
    bool tailless = false;   // elevons do the pitch work (delta wing)
    Vector3 wingtips[2]{};   // for vapour trails / nav lights
    Vector3 tailLight{}, strobe{}, landingLight{};
    float length = 18.9f;
    bool turbofan = false;   // high-bypass engines without afterburner: no flame plume, heat haze only
    float plumeScale = 1.0f; // afterburner plume length / diamond character per engine family
    float plumeHue = 0.0f;   // -1 bluer .. +1 more orange
    bool hasRamp = false;    // cargo ramp (transport)
    bool loaded = false;
    int triangles(int l) const;
};

JetModel buildJetModel(AirframeStyle style);
void unloadJetModel(JetModel& m);
/// Unit afterburner plume: from x = 0 (radius 1) to x = -1 (radius 0.15).
Mesh buildFlameMesh();
/// Air-to-air missile (approx. 3.7 m), body frame.
Mesh buildMissileMesh();

}  // namespace sky
