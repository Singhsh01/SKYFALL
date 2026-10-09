#pragma once
#include "../sim/Math.hpp"
#include "raylib.h"
#include "raymath.h"

namespace sky {
inline Vector3 toRl(const Vec3& v) { return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)}; }
inline Vec3 toSky(const Vector3& v) { return {v.x, v.y, v.z}; }
inline Quaternion toRlQ(const Quat& q) {
    return {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
}
/// Model matrix for an object at `pos` with body->world orientation `q` (raylib order: R then T).
inline Matrix bodyMatrix(const Vec3& pos, const Quat& q) {
    return MatrixMultiply(QuaternionToMatrix(toRlQ(q)), MatrixTranslate(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));
}
inline Color hdrColor(Vector3 c, float scale, float alpha) {
    auto ch = [&](float v) { v = v / scale; return static_cast<unsigned char>(v <= 0 ? 0 : (v >= 1 ? 255 : v * 255.0f)); };
    return {ch(c.x), ch(c.y), ch(c.z), static_cast<unsigned char>(alpha <= 0 ? 0 : (alpha >= 1 ? 255 : alpha * 255.0f))};
}
}  // namespace sky
