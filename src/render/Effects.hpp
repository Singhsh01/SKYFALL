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
    float heat = 0;           // smoke: glow of the fire inside it (fades fast); fire: 1 = white-hot core
    bool toSmoke = false;     // fire: leaves a puff of dark smoke where it burns out
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

struct Wreck { Vector3 pos; float timer; float size; float fire = 1.0f; };
/// A broken-off piece of an aircraft or vehicle: tumbles, burns or smokes, and falls.
struct Chunk {
    Vector3 pos, vel, axis{0, 1, 0};
    float angle = 0, angVel = 0;
    Vector3 size{1, 0.3f, 1};      // box dimensions (m)
    float life = 0;
    long long trailKey = 0;
    bool burning = false, smoking = false;
    Vector3 color{0.08f, 0.08f, 0.085f};
};
/// The main body of a destroyed aircraft falling to the ground, burning.
struct FallingWreck {
    int id = 0;
    AircraftId aircraft = AircraftId::F22;
    Team team = Team::Red;
    int skin = 0;
    Vector3 pos, vel;
    Quat q;
    Vec3 omega;                    // body rates (rad/s): the tumble
    float life = 0;
    long long trailKey = 0;
    float fire = 1.0f;             // 1 = fully ablaze
};
struct PendingBlast { float t; Vector3 pos; float size; bool ground; };
/// Per-aircraft damage effects (missile hits, engine fire, leaks) while it is still flying.
struct JetFx {
    float leak = 0;                // fuel / hydraulic mist from a wing (seconds left)
    int leakSide = 1;
    float engineFire = 0;          // 0..1 engine fire intensity
    float hitTimer = 0;            // seconds since the last heavy hit (fireball on the airframe)
    Vector3 hitOffset{0, 0, 0};    // body-frame point of the last hit
};

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
    /// Layered blast: white-hot flash, expanding fireball (white -> orange -> red), embers and
    /// sparks, rolling black smoke; on the ground a dust ring and a dirt column.
    void spawnBlast(Vector3 p, Vector3 inherit, float size, bool ground);
    void setParticleBudget(size_t n) { budget_ = std::max<size_t>(2000, std::min<size_t>(n, kHardMax)); }
    const std::vector<Chunk>& chunks() const { return chunks_; }
    const std::vector<FallingWreck>& fallingWrecks() const { return falling_; }
    static constexpr size_t kHardMax = 20000;
    void spawnSmokePuff(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col, float alpha);
    void spawnFire(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col);

private:
    void emitTrailPoint(long long key, Vector3 p, float width0, float width1, float maxAge, Vector3 col, float alpha,
                        float spacing, bool dark = false);
    void drawTrails(const Camera3D& cam, const Lighting& L);
    void billboard(Vector3 p, float size, float rot, Vector3 right, Vector3 up, Color c);
    /// A billboard whose corners are shaded by the light direction (a lit side and a shadow side):
    /// flat smoke sprites read as round, volumetric puffs.
    void billboardShaded(Vector3 p, float size, float rot, Vector3 right, Vector3 up, Vector3 lightDir, Color lit, Color dark);

    Texture2D puff_{}, glow_{}, spark_{};
    std::vector<Particle> particles_;
    std::unordered_map<long long, Trail> trails_;
    void onJetKilled(const CombatEvent& e, const Battle& b);
    void onGroundKilled(const CombatEvent& e, const Battle& b);
    void addChunk(Vector3 p, Vector3 v, Vector3 size, bool burning, bool smoking, Vector3 col);
    void groundImpact(Vector3 p, Vector3 v, float size, const Battle& b);
    bool room() const { return particles_.size() < budget_; }
    std::vector<Wreck> wrecks_;
    std::vector<Chunk> chunks_;
    std::vector<FallingWreck> falling_;
    std::vector<PendingBlast> pending_;
    std::unordered_map<int, JetFx> jetFx_;
    std::vector<Particle> born_;   // smoke left by burnt-out fire (added after the update loop)
    size_t budget_ = 14000;
    FlashLight flash_;
    Rng rng_{777};
    float shake_ = 0;
    double time_ = 0;
    long long debrisCounter_ = 0;
    Vector3 sunDir_{0, 1, 0};
    std::unordered_map<int, int> vaporGen_;
    std::unordered_map<int, bool> vaporOn_;
    Vector3 camPos_{};
    std::vector<std::pair<float, const Particle*>> smokeSort_;   // back-to-front order of smoke puffs
};

}  // namespace sky
