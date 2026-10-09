// SKYFALL — minimal, dependency-free 3D math for the simulation layer.
//
// Conventions (used everywhere in sim/, combat/, mission/):
//   * SI units: metres, seconds, kilograms, newtons, radians.
//   * World frame: right-handed, +Y up, ground plane is X/Z (same as raylib).
//   * Body frame of an aircraft: +X forward (nose), +Y up (canopy), +Z right wing.
//     forward x up = right, so the body frame is right-handed as well.
//   * Quaternions rotate body vectors into the world frame: v_world = q * v_body.
#pragma once

#include <algorithm>
#include <cmath>

namespace sky {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kG = 9.80665;

struct Vec3 {
    double x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(double X, double Y, double Z) : x(X), y(Y), z(Z) {}
    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    constexpr bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
};
inline constexpr Vec3 operator*(double s, const Vec3& v) { return v * s; }
inline constexpr double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline double length2(const Vec3& v) { return dot(v, v); }
inline Vec3 normalize(const Vec3& v) {
    double l = length(v);
    return l > 1e-12 ? v / l : Vec3{0, 0, 0};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }
inline double clampd(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
inline double lerpd(double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstep(double e0, double e1, double x) {
    double t = clampd((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
/// Exponential approach factor for a first-order lag with time constant tau.
inline double approachFactor(double dt, double tau) { return tau <= 0 ? 1.0 : 1.0 - std::exp(-dt / tau); }
/// Wrap an angle to (-pi, pi].
inline double wrapPi(double a) {
    while (a > kPi) a -= 2 * kPi;
    while (a <= -kPi) a += 2 * kPi;
    return a;
}

struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
    constexpr Quat() = default;
    constexpr Quat(double W, double X, double Y, double Z) : w(W), x(X), y(Y), z(Z) {}

    static Quat fromAxisAngle(const Vec3& axis, double angle) {
        Vec3 a = normalize(axis);
        double s = std::sin(angle * 0.5);
        return {std::cos(angle * 0.5), a.x * s, a.y * s, a.z * s};
    }
    /// Build an orientation from heading (yaw about world +Y, 0 = looking down world +X,
    /// positive = turning towards +Z i.e. to the right), pitch (nose up positive) and bank
    /// (right wing down positive).
    static Quat fromEuler(double heading, double pitch, double bank) {
        // Body x forward, y up, z right. Heading rotates about world -Y (so that +heading
        // moves the nose from +X towards +Z), pitch about body right (+Z, nose up), bank
        // about body forward (+X: canopy tilts towards the right wing = right bank).
        Quat qh = fromAxisAngle({0, -1, 0}, heading);
        Quat qp = fromAxisAngle({0, 0, 1}, pitch);
        Quat qb = fromAxisAngle({1, 0, 0}, bank);
        return qh * qp * qb;
    }
    constexpr Quat operator*(const Quat& o) const {
        return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
    }
    constexpr Quat conjugate() const { return {w, -x, -y, -z}; }
    Quat normalized() const {
        double l = std::sqrt(w * w + x * x + y * y + z * z);
        return l > 1e-12 ? Quat{w / l, x / l, y / l, z / l} : Quat{};
    }
    /// Rotate a vector (body -> world).
    Vec3 rotate(const Vec3& v) const {
        Vec3 u{x, y, z};
        Vec3 t = cross(u, v) * 2.0;
        return v + t * w + cross(u, t);
    }
    /// Inverse rotate (world -> body).
    Vec3 unrotate(const Vec3& v) const { return conjugate().rotate(v); }

    Vec3 forward() const { return rotate({1, 0, 0}); }
    Vec3 up() const { return rotate({0, 1, 0}); }
    Vec3 right() const { return rotate({0, 0, 1}); }

    /// Integrate body angular rate omega (rad/s, body frame) over dt.
    Quat integrated(const Vec3& omegaBody, double dt) const {
        double ang = length(omegaBody) * dt;
        if (ang < 1e-12) return *this;
        return ((*this) * fromAxisAngle(omegaBody, ang)).normalized();
    }
};

/// Pitch (nose above horizon), heading (0 = +X, clockwise seen from above towards +Z) and
/// bank (right wing down positive) extracted from an orientation.
struct Euler {
    double heading, pitch, bank;
};
inline Euler toEuler(const Quat& q) {
    Vec3 f = q.forward();
    Vec3 u = q.up();
    Euler e{};
    e.pitch = std::asin(clampd(f.y, -1.0, 1.0));
    e.heading = std::atan2(f.z, f.x);
    // Bank: angle between body-up and the vertical plane containing forward.
    Vec3 worldUp{0, 1, 0};
    Vec3 rightLevel = normalize(cross(f, worldUp));  // horizontal right vector
    if (length2(rightLevel) < 1e-9) {
        e.bank = 0;
    } else {
        Vec3 upLevel = cross(rightLevel, f);
        e.bank = std::atan2(dot(u, rightLevel), dot(u, upLevel));
    }
    return e;
}

/// Small deterministic RNG (xorshift64*) — identical sequences on every platform.
struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
    unsigned long long next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 2685821657736338717ull;
    }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }
    double range(double a, double b) { return a + (b - a) * uniform(); }
};

}  // namespace sky
