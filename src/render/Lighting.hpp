// Time-of-day lighting presets and CPU mirrors of the shader atmosphere functions
// (used to light/fog particles and cloud sprites consistently with the GPU shading).
#pragma once

#include "raylib.h"
#include "raymath.h"

#include <cmath>

namespace sky {

enum class TimeOfDay { Morning = 0, GoldenHour = 1, Noon = 2, Count = 3 };

struct Lighting {
    Vector3 sunDir{0.5f, 0.5f, 0.5f};
    Vector3 sunColor{3.0f, 2.9f, 2.7f};
    Vector3 zenith{0.12f, 0.3f, 0.7f};
    Vector3 horizon{0.7f, 0.8f, 0.92f};
    Vector3 groundHaze{0.5f, 0.52f, 0.52f};
    float fogDensity = 8.5e-5f;
    float fogFalloff = 1.0f / 1800.0f;
    float cloudCover = 0.42f;
    float cirrus = 0.8f;
    float exposure = 1.0f;
    const char* name = "MORNING";

    static Lighting preset(TimeOfDay t) {
        Lighting l;
        switch (t) {
            case TimeOfDay::Morning:
            default:
                l.sunDir = Vector3Normalize({0.62f, 0.50f, 0.42f});
                l.sunColor = {3.25f, 3.0f, 2.65f};
                l.zenith = {0.09f, 0.26f, 0.52f};
                l.horizon = {0.70f, 0.79f, 0.86f};
                l.groundHaze = {0.62f, 0.58f, 0.52f};
                l.fogDensity = 9.5e-5f;
                l.cloudCover = 0.36f;
                l.exposure = 0.95f;
                l.name = "MORNING";
                break;
            case TimeOfDay::GoldenHour:
                l.sunDir = Vector3Normalize({0.9f, 0.15f, 0.32f});
                l.sunColor = {3.4f, 2.0f, 1.0f};
                l.zenith = {0.13f, 0.22f, 0.45f};
                l.horizon = {1.0f, 0.68f, 0.45f};
                l.groundHaze = {0.58f, 0.46f, 0.37f};
                l.fogDensity = 8.0e-5f;
                l.cloudCover = 0.48f;
                l.exposure = 1.0f;
                l.name = "GOLDEN HOUR";
                break;
            case TimeOfDay::Noon:
                l.sunDir = Vector3Normalize({0.3f, 0.88f, 0.25f});
                l.sunColor = {3.3f, 3.2f, 3.0f};
                l.zenith = {0.08f, 0.25f, 0.55f};
                l.horizon = {0.68f, 0.78f, 0.87f};
                l.groundHaze = {0.64f, 0.60f, 0.55f};
                l.fogDensity = 8.0e-5f;
                l.cloudCover = 0.38f;
                l.exposure = 0.88f;
                l.name = "NOON";
                break;
        }
        return l;
    }

    Vector3 skyColor(Vector3 d) const {
        float y = d.y;
        float h = powf(1.0f - std::fmin(std::fmax(y, 0.0f), 1.0f), 6.0f);
        Vector3 col = Vector3Lerp(zenith, horizon, h);
        if (y < 0) col = Vector3Lerp(horizon, groundHaze, std::fmin(-y * 3.0f, 1.0f));
        float mu = std::fmax(Vector3DotProduct(d, sunDir), 0.0f);
        float m = (powf(mu, 10.0f) * 0.22f + powf(mu, 160.0f) * 0.6f) * (0.35f + 0.65f * h);
        return Vector3Add(col, Vector3Scale(sunColor, m));
    }
    float fogAmount(Vector3 cam, Vector3 p) const {
        Vector3 v = Vector3Subtract(p, cam);
        float dist = Vector3Length(v);
        if (dist < 1e-3f) return 0;
        Vector3 d = Vector3Scale(v, 1.0f / dist);
        float k = d.y * fogFalloff;
        float integral = std::fabs(k) > 1e-5f ? (1.0f - expf(-dist * k)) / k : dist;
        float density = fogDensity * expf(-std::fmax(cam.y, 0.0f) * fogFalloff);
        return 1.0f - expf(-density * integral);
    }
    Vector3 fogColor(Vector3 cam, Vector3 p) const {
        Vector3 d = Vector3Normalize(Vector3Subtract(p, cam));
        d.y = std::fmax(d.y, 0.0f) * 0.5f;
        return skyColor(Vector3Normalize(d));
    }
    Vector3 ambient(float ny) const {
        Vector3 skyAmb = Vector3Scale(Vector3Lerp(horizon, zenith, 0.5f), 0.9f);
        Vector3 grd = Vector3Scale(groundHaze, 0.35f);
        return Vector3Lerp(grd, skyAmb, ny * 0.5f + 0.5f);
    }
};

}  // namespace sky
