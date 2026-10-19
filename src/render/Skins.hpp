// Selectable camouflage schemes. Colours are linear-space albedos; the jet shader builds the
// pattern procedurally from the aircraft's own object-space coordinates (no textures needed).
#pragma once

#include "raylib.h"

namespace sky {

enum class CamoPattern { TwoTone = 0, Splinter = 1, Woodland = 2, Digital = 3, Stripes = 4,
                         CounterShade = 5, Shard = 6, Demo = 7, Metal = 8, Carbon = 9 };

struct Skin {
    const char* name;
    Vector3 a, b, c;      // base, second and third colour
    CamoPattern pattern;
    float roughness;
};

constexpr int kSkinCount = 16;

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
        {"BARE METAL", {0.78f, 0.79f, 0.80f}, {0.60f, 0.61f, 0.63f}, {0.0f, 0.0f, 0.0f}, CamoPattern::Metal, 0.32f},
        {"SAND GHOST", {0.40f, 0.34f, 0.25f}, {0.21f, 0.18f, 0.135f}, {0.15f, 0.13f, 0.10f}, CamoPattern::CounterShade, 0.45f},
        {"CARBON", {0.018f, 0.019f, 0.021f}, {0.05f, 0.052f, 0.056f}, {0.55f, 0.12f, 0.02f}, CamoPattern::Carbon, 0.28f},
    };
    return k[(i >= 0 && i < kSkinCount) ? i : 0];
}

}  // namespace sky
