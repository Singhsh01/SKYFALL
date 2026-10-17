// Selectable camouflage schemes. Colours are linear-space albedos; the jet shader builds the
// pattern procedurally from the aircraft's own object-space coordinates (no textures needed).
#pragma once

#include "raylib.h"

namespace sky {

enum class CamoPattern { TwoTone = 0, Splinter = 1, Woodland = 2, Digital = 3, Stripes = 4 };

struct Skin {
    const char* name;
    Vector3 a, b, c;      // base, second and third colour
    CamoPattern pattern;
    float roughness;
};

constexpr int kSkinCount = 8;

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
    };
    return k[(i >= 0 && i < kSkinCount) ? i : 0];
}

}  // namespace sky
