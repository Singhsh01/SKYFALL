// Selectable camouflage schemes. Colours are linear-space albedos; the jet shader builds the
// pattern procedurally from the aircraft's own object-space coordinates (no textures needed).
#pragma once

#include "raylib.h"

namespace sky {

enum class CamoPattern { TwoTone = 0, Splinter = 1, Woodland = 2, Digital = 3, Stripes = 4,
                         CounterShade = 5, Shard = 6, Demo = 7, Metal = 8, Carbon = 9,
                         Armor = 10, Circuit = 11, HexVector = 12 };

// Markings are generic, air-force-INSPIRED designs drawn procedurally in the jet shader. They are
// not reproductions of any service's official artwork.
enum class Insignia { None = 0, Roundel = 1, StarRoundel = 2, StarBar = 3, Cross = 4 };
enum class FinFlash { None = 0, Vertical = 1, Horizontal = 2, Roundel = 3, Code = 4, VerticalOuter = 5 };

struct Skin {
    const char* name;
    Vector3 a, b, c;      // base, second and third colour
    CamoPattern pattern;
    float roughness;
    float metal = 0.12f;            // metalness of the paint
    Vector3 glow{0, 0, 0};          // emissive accent colour (linear, HDR) for circuit / seam patterns
    Insignia insignia = Insignia::None;  // national-style marking on the wings
    FinFlash fin = FinFlash::None;       // fin marking
    Vector3 m0{0, 0, 0}, m1{0, 0, 0}, m2{0, 0, 0};   // marking colours (outer, middle, inner / front, mid, rear)
};

constexpr int kSkinCount = 40;
static_assert(kSkinCount <= 62, "Settings stores one skin per aircraft as 0-9, a-z, A-Z");

inline const Skin& skin(int i) {
    static const Skin k[kSkinCount] = {
        {"FACTORY", {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, CamoPattern::TwoTone, 0.42f},   // per-aircraft default, see paintFor
        {"DESERT", {0.40f, 0.31f, 0.19f}, {0.23f, 0.16f, 0.09f}, {0.48f, 0.40f, 0.28f}, CamoPattern::Splinter, 0.55f},
        {"WOODLAND", {0.11f, 0.14f, 0.07f}, {0.20f, 0.16f, 0.09f}, {0.04f, 0.045f, 0.04f}, CamoPattern::Woodland, 0.6f},
        {"ARCTIC", {0.62f, 0.64f, 0.66f}, {0.33f, 0.35f, 0.38f}, {0.80f, 0.81f, 0.82f}, CamoPattern::Splinter, 0.5f},
        {"NAVAL", {0.16f, 0.21f, 0.29f}, {0.09f, 0.12f, 0.18f}, {0.22f, 0.27f, 0.34f}, CamoPattern::TwoTone, 0.38f},
        {"DIGITAL URBAN", {0.30f, 0.31f, 0.32f}, {0.15f, 0.16f, 0.17f}, {0.45f, 0.46f, 0.47f}, CamoPattern::Digital, 0.5f},
        {"NIGHT", {0.035f, 0.037f, 0.042f}, {0.06f, 0.065f, 0.07f}, {0.02f, 0.02f, 0.022f}, CamoPattern::TwoTone, 0.3f},
        {"TIGER", {0.55f, 0.30f, 0.06f}, {0.03f, 0.03f, 0.03f}, {0.62f, 0.38f, 0.10f}, CamoPattern::Stripes, 0.35f},
        // modern schemes
        {"GHOST GREY", {0.36f, 0.37f, 0.39f}, {0.16f, 0.17f, 0.185f}, {0.11f, 0.115f, 0.125f}, CamoPattern::CounterShade, 0.4f},
        {"STEALTH SHARD", {0.24f, 0.30f, 0.37f}, {0.07f, 0.09f, 0.13f}, {0.46f, 0.52f, 0.58f}, CamoPattern::Shard, 0.36f},
        {"MIDNIGHT NEON", {0.022f, 0.024f, 0.03f}, {0.0f, 0.42f, 0.48f}, {0.7f, 0.72f, 0.74f}, CamoPattern::Demo, 0.24f},
        {"POLAR SHARD", {0.72f, 0.74f, 0.77f}, {0.05f, 0.08f, 0.15f}, {0.36f, 0.40f, 0.45f}, CamoPattern::Shard, 0.34f},
        {"DEMO RED", {0.72f, 0.72f, 0.73f}, {0.50f, 0.02f, 0.025f}, {0.015f, 0.03f, 0.12f}, CamoPattern::Demo, 0.22f},
        {"BARE METAL", {0.78f, 0.79f, 0.80f}, {0.60f, 0.61f, 0.63f}, {0.0f, 0.0f, 0.0f}, CamoPattern::Metal, 0.32f, 0.55f},
        {"SAND GHOST", {0.40f, 0.34f, 0.25f}, {0.21f, 0.18f, 0.135f}, {0.15f, 0.13f, 0.10f}, CamoPattern::CounterShade, 0.45f},
        {"CARBON", {0.018f, 0.019f, 0.021f}, {0.05f, 0.052f, 0.056f}, {0.55f, 0.12f, 0.02f}, CamoPattern::Carbon, 0.28f},
        // cinematic armour finishes (original designs: high-tech armour and aerospace engineering)
        {"CRIMSON GOLD ARMOR", {0.36f, 0.025f, 0.03f}, {0.72f, 0.48f, 0.12f}, {0.05f, 0.02f, 0.02f}, CamoPattern::Armor, 0.24f, 0.72f, {1.6f, 0.9f, 0.35f}},
        {"VOLT GRAPHITE", {0.055f, 0.06f, 0.07f}, {0.10f, 0.11f, 0.125f}, {0.15f, 0.65f, 1.0f}, CamoPattern::Circuit, 0.30f, 0.45f, {0.35f, 1.6f, 4.0f}},
        {"STEALTH SILVER", {0.02f, 0.02f, 0.024f}, {0.12f, 0.12f, 0.13f}, {0.62f, 0.63f, 0.66f}, CamoPattern::Shard, 0.22f, 0.6f},
        {"TITANIUM", {0.56f, 0.57f, 0.61f}, {0.43f, 0.45f, 0.50f}, {0.0f, 0.0f, 0.0f}, CamoPattern::Metal, 0.26f, 0.75f},
        {"HEX VECTOR", {0.05f, 0.09f, 0.10f}, {0.11f, 0.16f, 0.17f}, {0.20f, 0.26f, 0.28f}, CamoPattern::HexVector, 0.32f, 0.35f, {0.3f, 2.2f, 2.4f}},
        {"NANO CRIMSON", {0.025f, 0.02f, 0.022f}, {0.06f, 0.035f, 0.04f}, {0.9f, 0.08f, 0.06f}, CamoPattern::Circuit, 0.26f, 0.5f, {4.0f, 0.35f, 0.25f}},
        {"GOLD TITAN", {0.70f, 0.47f, 0.13f}, {0.10f, 0.10f, 0.11f}, {0.03f, 0.03f, 0.03f}, CamoPattern::Armor, 0.22f, 0.8f, {1.2f, 1.0f, 0.6f}},
        {"CARBON VOLT", {0.018f, 0.019f, 0.021f}, {0.05f, 0.052f, 0.056f}, {0.1f, 0.6f, 1.0f}, CamoPattern::Carbon, 0.26f, 0.12f, {0.3f, 1.4f, 3.2f}},
        // air-force-inspired service schemes with markings and stencils (generic designs)
        {"USAF AIR DOMINANCE", {0.30f, 0.315f, 0.33f}, {0.17f, 0.18f, 0.19f}, {0.13f, 0.14f, 0.15f}, CamoPattern::CounterShade, 0.4f, 0.12f, {0, 0, 0},
         Insignia::StarBar, FinFlash::Code, {0.10f, 0.105f, 0.11f}, {0.36f, 0.37f, 0.38f}, {0.10f, 0.105f, 0.11f}},
        {"USN TACTICAL GREY", {0.26f, 0.27f, 0.285f}, {0.16f, 0.17f, 0.18f}, {0.0f, 0.0f, 0.0f}, CamoPattern::TwoTone, 0.42f, 0.12f, {0, 0, 0},
         Insignia::StarBar, FinFlash::Code, {0.09f, 0.095f, 0.10f}, {0.40f, 0.41f, 0.42f}, {0.09f, 0.095f, 0.10f}},
        {"USN HERITAGE", {0.50f, 0.51f, 0.52f}, {0.34f, 0.35f, 0.36f}, {0.0f, 0.0f, 0.0f}, CamoPattern::CounterShade, 0.3f, 0.12f, {0, 0, 0},
         Insignia::StarBar, FinFlash::Code, {0.02f, 0.04f, 0.16f}, {0.80f, 0.80f, 0.80f}, {0.55f, 0.03f, 0.03f}},
        {"RAF AIR DEFENCE", {0.24f, 0.25f, 0.265f}, {0.31f, 0.32f, 0.33f}, {0.0f, 0.0f, 0.0f}, CamoPattern::TwoTone, 0.42f, 0.12f, {0, 0, 0},
         Insignia::Roundel, FinFlash::Vertical, {0.10f, 0.14f, 0.30f}, {0.45f, 0.45f, 0.46f}, {0.38f, 0.07f, 0.07f}},
        {"LUFTWAFFE GREY", {0.21f, 0.22f, 0.235f}, {0.12f, 0.13f, 0.14f}, {0.30f, 0.31f, 0.32f}, CamoPattern::Splinter, 0.45f, 0.12f, {0, 0, 0},
         Insignia::Cross, FinFlash::Horizontal, {0.015f, 0.015f, 0.015f}, {0.70f, 0.70f, 0.70f}, {0.0f, 0.0f, 0.0f}},
        {"FRENCH AIR BLUE", {0.17f, 0.20f, 0.25f}, {0.10f, 0.12f, 0.16f}, {0.27f, 0.30f, 0.34f}, CamoPattern::CounterShade, 0.38f, 0.12f, {0, 0, 0},
         Insignia::Roundel, FinFlash::Vertical, {0.55f, 0.03f, 0.04f}, {0.78f, 0.78f, 0.78f}, {0.02f, 0.06f, 0.30f}},
        {"INDIAN AIR SPLINTER", {0.26f, 0.29f, 0.33f}, {0.13f, 0.15f, 0.19f}, {0.38f, 0.40f, 0.43f}, CamoPattern::Splinter, 0.45f, 0.12f, {0, 0, 0},
         Insignia::Roundel, FinFlash::VerticalOuter, {0.85f, 0.32f, 0.03f}, {0.78f, 0.78f, 0.78f}, {0.03f, 0.25f, 0.05f}},
        {"BRAZILIAN JUNGLE", {0.10f, 0.13f, 0.07f}, {0.27f, 0.21f, 0.12f}, {0.05f, 0.06f, 0.04f}, CamoPattern::Woodland, 0.55f, 0.12f, {0, 0, 0},
         Insignia::StarRoundel, FinFlash::Horizontal, {0.02f, 0.30f, 0.06f}, {0.80f, 0.62f, 0.02f}, {0.02f, 0.06f, 0.30f}},
        // environment schemes
        {"DIGITAL DESERT", {0.42f, 0.34f, 0.22f}, {0.27f, 0.20f, 0.12f}, {0.55f, 0.47f, 0.34f}, CamoPattern::Digital, 0.55f, 0.12f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.08f, 0.07f, 0.05f}, {0, 0, 0}, {0, 0, 0}},
        {"ARCTIC DIGITAL", {0.70f, 0.72f, 0.74f}, {0.38f, 0.40f, 0.43f}, {0.18f, 0.19f, 0.21f}, CamoPattern::Digital, 0.5f, 0.12f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.08f, 0.08f, 0.09f}, {0, 0, 0}, {0, 0, 0}},
        {"FOREST SPLINTER", {0.12f, 0.15f, 0.08f}, {0.05f, 0.07f, 0.04f}, {0.22f, 0.20f, 0.12f}, CamoPattern::Splinter, 0.6f, 0.12f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.02f, 0.02f, 0.02f}, {0, 0, 0}, {0, 0, 0}},
        {"NIGHT STEALTH", {0.03f, 0.032f, 0.038f}, {0.012f, 0.013f, 0.016f}, {0.055f, 0.06f, 0.07f}, CamoPattern::Shard, 0.3f, 0.2f, {0, 0, 0},
         Insignia::StarBar, FinFlash::Code, {0.05f, 0.05f, 0.055f}, {0.10f, 0.10f, 0.11f}, {0.05f, 0.05f, 0.055f}},
        {"TIGER MEET", {0.62f, 0.36f, 0.05f}, {0.02f, 0.02f, 0.02f}, {0.80f, 0.78f, 0.70f}, CamoPattern::Stripes, 0.32f, 0.12f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.02f, 0.02f, 0.02f}, {0, 0, 0}, {0, 0, 0}},
        {"OCEAN SPLINTER", {0.10f, 0.15f, 0.22f}, {0.05f, 0.08f, 0.12f}, {0.20f, 0.26f, 0.32f}, CamoPattern::Splinter, 0.4f, 0.12f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.70f, 0.70f, 0.72f}, {0, 0, 0}, {0, 0, 0}},
        // demonstration schemes (original designs)
        {"DEMO BLUE GOLD", {0.02f, 0.05f, 0.22f}, {0.75f, 0.50f, 0.05f}, {0.80f, 0.80f, 0.82f}, CamoPattern::Demo, 0.22f, 0.15f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.80f, 0.80f, 0.82f}, {0, 0, 0}, {0, 0, 0}},
        {"DEMO SUNBURST", {0.80f, 0.80f, 0.82f}, {0.85f, 0.22f, 0.02f}, {0.02f, 0.02f, 0.03f}, CamoPattern::Demo, 0.22f, 0.15f, {0, 0, 0},
         Insignia::None, FinFlash::Code, {0.02f, 0.02f, 0.03f}, {0, 0, 0}, {0, 0, 0}},
    };
    return k[(i >= 0 && i < kSkinCount) ? i : 0];
}

}  // namespace sky
