// Particles, ribbon trails, explosions, flares, tracers and lights. Pure visuals: nothing here
// affects the simulation.
#pragma once

#include "../combat/Combat.hpp"
#include "Lighting.hpp"
#include "raylib.h"

#include <deque>
#include <unordered_map>
#include <vector>

namespace sky {

struct JetModel;

enum class PKind : unsigned char { Smoke, Fire, Spark, Glow, Dust };

struct Particle {
    Vector3 pos, vel;
    float life = 0, maxLife = 1;
    float size0 = 1, size1 = 2;
    Vector3 color{1, 1, 1};   // HDR colour (smoke: albedo, lit at draw time)
    float alpha = 1;
    float drag = 0.5f;
    float lift = 0;           // upward acceleration (buoyancy) or negative gravity
    float rot = 0, spin = 0;
    PKind kind = PKind::Smoke;
    bool stretch = false;     // draw as a streak along velocity
};

struct TrailPoint {
    Vector3 pos;
    float age = 0;
    float width = 1;
};

struct Trail {
    std::deque<TrailPoint> pts;
    float maxAge = 6.0f;
    float width0 = 1.5f, width1 = 9.0f;
    Vector3 color{1, 1, 1};
    float alpha = 0.85f;
    bool alive = true;     // owner still emitting
    bool dark = false;
    Vector3 lastEmit{};
    bool hasLast = false;
};

struct FlashLight {
    Vector3 pos{0, -10000, 0};
    Vector3 color{0, 0, 0};
    float intensity = 0;
};

struct Wreck { Vector3 pos; float timer; float size; };
struct Debris { Vector3 pos, vel; float life; long long trailKey; bool burning; };

class Effects {
public:
    void init(Texture2D puff, Texture2D glow, Texture2D spark);
    void clear();
    void onEvent(const CombatEvent& e, const Battle& b);
    void update(double dt, const Battle& b, const Lighting& L, Vector3 camPos,
                const std::vector<const JetModel*>& jetModels);
    void draw(const Camera3D& cam, const Battle& b, const Lighting& L, Shader particleShader, int intensityLoc,
              const std::vector<Vector3>& runwayLights, bool showRunwayLights);
    const FlashLight& flash() const { return flash_; }
    size_t particleCount() const { return particles_.size(); }
    /// Camera shake impulse accumulated from nearby explosions (consumed by the camera).
    float takeShake() { float s = shake_; shake_ = 0; return s; }

    void spawnExplosion(Vector3 p, float size, bool ground, bool water);
    void spawnSmokePuff(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col, float alpha);
    void spawnFire(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col);

private:
    void emitTrailPoint(long long key, Vector3 p, float width0, float width1, float maxAge, Vector3 col, float alpha,
                        float spacing, bool dark = false);
    void drawTrails(const Camera3D& cam, const Lighting& L);
    void billboard(Vector3 p, float size, float rot, Vector3 right, Vector3 up, Color c);

    Texture2D puff_{}, glow_{}, spark_{};
    std::vector<Particle> particles_;
    std::unordered_map<long long, Trail> trails_;
    std::vector<Wreck> wrecks_;
    std::vector<Debris> debris_;
    FlashLight flash_;
    Rng rng_{777};
    float shake_ = 0;
    double time_ = 0;
    long long debrisCounter_ = 0;
    std::unordered_map<int, int> vaporGen_;
    std::unordered_map<int, bool> vaporOn_;
    Vector3 camPos_{};
    std::vector<std::pair<float, const Particle*>> smokeSort_;   // back-to-front order of smoke puffs
};

}  // namespace sky
