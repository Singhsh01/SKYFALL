#include "Effects.hpp"

#include "Convert.hpp"
#include "JetMesh.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {
constexpr float kSmokeScale = 4.0f;    // HDR range of the alpha-blended group
constexpr float kAddScale = 16.0f;     // HDR range of the additive group
Vector3 V(float x, float y, float z) { return {x, y, z}; }
}  // namespace

void Effects::init(Texture2D puff, Texture2D glow, Texture2D spark) {
    puff_ = puff; glow_ = glow; spark_ = spark;
    particles_.reserve(kHardMax);
}

void Effects::clear() {
    particles_.clear(); trails_.clear(); wrecks_.clear(); chunks_.clear(); falling_.clear(); pending_.clear(); jetFx_.clear();
    flash_ = FlashLight{};
}

void Effects::spawnSmokePuff(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col, float alpha) {
    if (!room()) return;
    Particle q;
    q.pos = p; q.vel = v; q.size0 = s0; q.size1 = s1; q.maxLife = life; q.color = col; q.alpha = alpha;
    q.kind = PKind::Smoke; q.drag = 0.9f; q.lift = 1.2f;
    q.rot = static_cast<float>(rng_.range(0, 6.28)); q.spin = static_cast<float>(rng_.range(-0.4, 0.4));
    particles_.push_back(q);
}

void Effects::spawnFire(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col) {
    if (!room()) return;
    Particle q;
    q.pos = p; q.vel = v; q.size0 = s0; q.size1 = s1; q.maxLife = life; q.color = col; q.alpha = 1;
    q.kind = PKind::Fire; q.drag = 2.0f; q.lift = 3.0f; q.heat = 0.7f;
    q.rot = static_cast<float>(rng_.range(0, 6.28)); q.spin = static_cast<float>(rng_.range(-1.5, 1.5));
    particles_.push_back(q);
}

void Effects::spawnExplosion(Vector3 p, float size, bool ground, bool water) {
    float s = size;
    flash_.pos = p;
    flash_.color = {1.0f, 0.55f, 0.2f};
    flash_.intensity = std::max(flash_.intensity, 6.0f * s);
    float d = Vector3Distance(p, camPos_);
    if (d < 2500.0f) shake_ += s * (1.0f - d / 2500.0f);
    if (water) {
        for (int i = 0; i < 40; ++i) {
            Vector3 v = V(static_cast<float>(rng_.range(-12, 12)), static_cast<float>(rng_.range(25, 70)) * s, static_cast<float>(rng_.range(-12, 12)));
            Particle q;
            q.pos = p; q.vel = v; q.size0 = 2 * s; q.size1 = 9 * s; q.maxLife = static_cast<float>(rng_.range(1.5, 3.0));
            q.color = {0.95f, 0.97f, 1.0f}; q.alpha = 0.8f; q.kind = PKind::Smoke; q.drag = 0.4f; q.lift = -9.8f;
            if (room()) particles_.push_back(q);
        }
        return;
    }
    // core flash
    Particle core;
    core.pos = p; core.vel = {0, 0, 0}; core.size0 = 18 * s; core.size1 = 40 * s; core.maxLife = 0.18f;
    core.color = {1.0f, 0.8f, 0.5f}; core.kind = PKind::Glow; core.alpha = 1;
    particles_.push_back(core);
    // fireball
    int nf = static_cast<int>(14 + 10 * s);
    for (int i = 0; i < nf; ++i) {
        Vector3 dir = Vector3Normalize(V(static_cast<float>(rng_.range(-1, 1)), static_cast<float>(rng_.range(-0.6, 1)), static_cast<float>(rng_.range(-1, 1))));
        float sp = static_cast<float>(rng_.range(8, 30)) * s;
        Vector3 col = Vector3Lerp(V(1.0f, 0.62f, 0.22f), V(1.0f, 0.35f, 0.08f), static_cast<float>(rng_.uniform()));
        spawnFire(Vector3Add(p, Vector3Scale(dir, 2.0f * s)), Vector3Scale(dir, sp), 6 * s, static_cast<float>(rng_.range(14, 26)) * s,
                  static_cast<float>(rng_.range(0.7, 1.6)), col);
    }
    // sparks
    for (int i = 0; i < static_cast<int>(8 * s + 4); ++i) {
        Vector3 dir = Vector3Normalize(V(static_cast<float>(rng_.range(-1, 1)), static_cast<float>(rng_.range(-0.4, 1)), static_cast<float>(rng_.range(-1, 1))));
        Particle q;
        q.pos = p; q.vel = Vector3Scale(dir, static_cast<float>(rng_.range(60, 180)) * std::sqrt(s));
        q.size0 = 0.8f * s; q.size1 = 0.3f; q.maxLife = static_cast<float>(rng_.range(0.6, 1.5));
        q.color = {1.0f, 0.75f, 0.35f}; q.kind = PKind::Spark; q.drag = 0.6f; q.lift = -9.8f; q.stretch = true;
        if (room()) particles_.push_back(q);
    }
    // smoke
    int ns = static_cast<int>(8 + 8 * s);
    for (int i = 0; i < ns; ++i) {
        Vector3 dir = Vector3Normalize(V(static_cast<float>(rng_.range(-1, 1)), static_cast<float>(rng_.range(-0.2, 1)), static_cast<float>(rng_.range(-1, 1))));
        float g = static_cast<float>(rng_.range(0.08, 0.2));
        spawnSmokePuff(Vector3Add(p, Vector3Scale(dir, 3.0f * s)), Vector3Scale(dir, static_cast<float>(rng_.range(4, 14)) * s), 7 * s,
                       static_cast<float>(rng_.range(22, 40)) * s, static_cast<float>(rng_.range(4, 8)), V(g, g, g), 0.85f);
    }
    if (ground) {
        for (int i = 0; i < 14; ++i) {
            float a = static_cast<float>(rng_.range(0, 6.283));
            Vector3 v = V(cosf(a) * 35 * s, static_cast<float>(rng_.range(2, 10)), sinf(a) * 35 * s);
            spawnSmokePuff(p, v, 4 * s, 18 * s, static_cast<float>(rng_.range(2, 4)), V(0.42f, 0.36f, 0.28f), 0.7f);
        }
        // dirt column
        for (int i = 0; i < 8; ++i)
            spawnSmokePuff(p, V(static_cast<float>(rng_.range(-6, 6)), static_cast<float>(rng_.range(20, 55)) * s, static_cast<float>(rng_.range(-6, 6))),
                           3 * s, 14 * s, 2.5f, V(0.3f, 0.26f, 0.2f), 0.85f);
    }
}

void Effects::spawnBlast(Vector3 p, Vector3 inherit, float size, bool ground) {
    const float s = size;
    flash_.pos = p;
    flash_.color = {1.0f, 0.62f, 0.3f};
    flash_.intensity = std::max(flash_.intensity, 7.0f * s);
    float d = Vector3Distance(p, camPos_);
    if (d < 3000.0f) shake_ += s * (1.0f - d / 3000.0f);
    auto rnd = [&](double a, double b) { return static_cast<float>(rng_.range(a, b)); };
    auto dirRand = [&](float yLo) { return Vector3Normalize(V(rnd(-1, 1), rnd(yLo, 1), rnd(-1, 1))); };
    // 1. the flash: a small white-hot core and a wider, short orange bloom
    Particle core;
    core.pos = p; core.vel = Vector3Scale(inherit, 0.5f); core.size0 = 9 * s; core.size1 = 26 * s; core.maxLife = 0.09f;
    core.color = {1.0f, 0.95f, 0.85f}; core.kind = PKind::Glow; core.alpha = 1; core.drag = 2;
    if (room()) particles_.push_back(core);
    Particle bloom = core;
    bloom.size0 = 20 * s; bloom.size1 = 55 * s; bloom.maxLife = 0.22f; bloom.color = {1.0f, 0.55f, 0.22f};
    if (room()) particles_.push_back(bloom);
    // 2. the fireball: hot gas pushed out fast, slowing, rising; each lobe cools from white-yellow
    //    through orange to dull red and leaves its own puff of black smoke
    int nf = static_cast<int>(18 + 12 * s);
    for (int i = 0; i < nf && room(); ++i) {
        Vector3 dir = dirRand(-0.5f);
        float sp = rnd(10, 34) * s;
        Particle q;
        q.pos = Vector3Add(p, Vector3Scale(dir, rnd(0.5, 3.0) * s));
        q.vel = Vector3Add(Vector3Scale(dir, sp), Vector3Scale(inherit, 0.35f));
        q.size0 = rnd(4, 7) * s; q.size1 = rnd(13, 24) * s; q.maxLife = rnd(0.6, 1.5) * std::sqrt(s / 2.5f + 0.4f);
        q.color = {1, 1, 1}; q.kind = PKind::Fire; q.drag = 2.6f; q.lift = 4.0f; q.heat = rnd(0.6, 1.0); q.toSmoke = true;
        q.rot = rnd(0, 6.28); q.spin = rnd(-1.2, 1.2);
        particles_.push_back(q);
    }
    // 3. sparks and glowing embers on ballistic arcs
    for (int i = 0; i < static_cast<int>(16 * s + 6) && room(); ++i) {
        Vector3 dir = dirRand(-0.6f);
        Particle q;
        q.pos = p; q.vel = Vector3Add(Vector3Scale(dir, rnd(50, 170) * std::sqrt(s)), Vector3Scale(inherit, 0.5f));
        q.size0 = 0.9f * std::sqrt(s); q.size1 = 0.25f; q.maxLife = rnd(0.6, 2.2);
        q.color = {1.0f, 0.72f, 0.32f}; q.kind = PKind::Spark; q.drag = 0.5f; q.lift = -9.8f; q.stretch = true;
        particles_.push_back(q);
    }
    // 4. rolling smoke: dense and dark near the core, greyer at the edge; it is lit from inside by
    //    the fire for the first moments
    int ns = static_cast<int>(18 + 16 * s);
    for (int i = 0; i < ns && room(); ++i) {
        Vector3 dir = dirRand(-0.2f);
        float g = rnd(0.09, 0.24);
        Particle q;
        q.pos = Vector3Add(p, Vector3Scale(dir, rnd(1.5, 6.0) * s));
        q.vel = Vector3Add(Vector3Scale(dir, rnd(3, 14) * s), Vector3Scale(inherit, 0.25f));
        q.size0 = rnd(3, 7) * s; q.size1 = rnd(16, 40) * s; q.maxLife = rnd(6, 14); q.color = V(g, g * 0.97f, g * 0.93f); q.alpha = rnd(0.45, 0.7);
        q.kind = PKind::Smoke; q.drag = 0.7f; q.lift = 2.4f; q.heat = 0.9f;
        q.rot = rnd(0, 6.28); q.spin = rnd(-0.3, 0.3);
        particles_.push_back(q);
    }
    if (ground) {
        // dust ring racing out along the ground and a dirt column thrown up
        for (int i = 0; i < 22 && room(); ++i) {
            float a = rnd(0, 6.283);
            spawnSmokePuff(Vector3Add(p, V(0, -1.0f * s, 0)), V(cosf(a) * 42 * s, rnd(1, 6), sinf(a) * 42 * s), 4 * s, 20 * s, rnd(2.5, 4.5),
                           V(0.44f, 0.38f, 0.29f), 0.75f);
        }
        for (int i = 0; i < 10 && room(); ++i)
            spawnSmokePuff(p, V(rnd(-7, 7), rnd(22, 60) * s, rnd(-7, 7)), 3 * s, 15 * s, rnd(2.0, 3.2), V(0.28f, 0.24f, 0.19f), 0.85f);
    }
}

void Effects::addChunk(Vector3 p, Vector3 v, Vector3 size, bool burning, bool smoking, Vector3 col) {
    if (chunks_.size() >= 64) chunks_.erase(chunks_.begin());
    Chunk c;
    c.pos = p; c.vel = v; c.size = size; c.burning = burning; c.smoking = smoking; c.color = col;
    c.axis = Vector3Normalize(V(static_cast<float>(rng_.range(-1, 1)), static_cast<float>(rng_.range(-1, 1)), static_cast<float>(rng_.range(-1, 1))));
    c.angle = static_cast<float>(rng_.range(0, 6.28));
    c.angVel = static_cast<float>(rng_.range(2.0, 9.0));
    c.trailKey = 3000000000LL + (debrisCounter_++);
    chunks_.push_back(c);
}

void Effects::groundImpact(Vector3 p, Vector3 v, float size, const Battle& b) {
    const Terrain& T = b.terrain();
    if (T.isWater(p.x, p.z)) { spawnExplosion(p, size * 0.6f, false, true); return; }
    // the hit itself, skidding forward: a blast, then a burning wreck with a smoke column
    Vector3 skid = V(v.x, 0, v.z);
    spawnBlast(p, Vector3Scale(skid, 0.25f), size, true);
    pending_.push_back({0.35f, Vector3Add(p, Vector3Scale(Vector3Normalize(skid), 30.0f)), size * 0.55f, true});
    for (int i = 0; i < 6; ++i) {
        Vector3 cv = Vector3Add(Vector3Scale(skid, static_cast<float>(rng_.range(0.15, 0.45))),
                                V(static_cast<float>(rng_.range(-15, 15)), static_cast<float>(rng_.range(8, 30)), static_cast<float>(rng_.range(-15, 15))));
        addChunk(Vector3Add(p, V(0, 2, 0)), cv, V(static_cast<float>(rng_.range(0.6, 2.4)), static_cast<float>(rng_.range(0.15, 0.5)), static_cast<float>(rng_.range(0.6, 2.0))),
                 i < 3, true, V(0.06f, 0.06f, 0.065f));
    }
    if (wrecks_.size() > 24) wrecks_.erase(wrecks_.begin());
    Vector3 wp = V(p.x, static_cast<float>(T.surfaceHeight(p.x, p.z)), p.z);
    wrecks_.push_back({wp, 0.0f, size, 1.0f});
}

void Effects::onJetKilled(const CombatEvent& e, const Battle& b) {
    Vector3 p = toRl(e.pos);
    const Jet* j = b.jetById(e.b);
    Vector3 v = j ? toRl(j->fm->state().vel) : V(0, 0, 0);
    const Terrain& T = b.terrain();
    double agl = e.pos.y - T.surfaceHeight(e.pos.x, e.pos.z);
    jetFx_.erase(e.b);
    if (e.text == "CRASH" || agl < 25.0) {   // flown into the ground (or killed just above it)
        groundImpact(p, v, 3.0f, b);
        return;
    }
    // mid-air: how hard was it hit? a missile or a collision breaks the airframe up, cannon fire
    // usually leaves it in one burning piece
    bool breakup = e.text == "MISSILE" || e.text == "COLLISION" || e.text == "FLAK" || rng_.uniform() < 0.35;
    spawnBlast(p, v, breakup ? 2.8f : 1.8f, false);
    if (breakup) pending_.push_back({static_cast<float>(rng_.range(0.25, 0.6)), Vector3Add(p, Vector3Scale(v, 0.4f)), 1.4f, false});
    // the main body falls, tumbling and burning, until it hits the ground
    if (j) {
        if (falling_.size() >= 8) falling_.erase(falling_.begin());
        FallingWreck w;
        w.id = j->id; w.aircraft = j->fm->profile().id; w.team = j->team; w.skin = j->skin;
        w.pos = p; w.vel = Vector3Scale(v, breakup ? 0.7f : 0.9f);
        w.q = j->fm->state().q;
        w.omega = Vec3{rng_.range(-2.5, 2.5), rng_.range(-0.8, 0.8), rng_.range(-1.2, 1.2)} * (breakup ? 1.6 : 0.7);
        w.trailKey = 3500000000LL + (debrisCounter_++);
        w.fire = 1.0f;
        falling_.push_back(w);
    }
    // broken-off pieces: wing sections, tail surfaces, panels, each with its own momentum
    int n = breakup ? 9 : 3;
    for (int i = 0; i < n; ++i) {
        Vector3 cv = Vector3Add(Vector3Scale(v, static_cast<float>(rng_.range(0.4, 0.85))),
                                V(static_cast<float>(rng_.range(-45, 45)), static_cast<float>(rng_.range(-15, 45)), static_cast<float>(rng_.range(-45, 45))));
        bool big = i < 2 && breakup;
        Vector3 sz = big ? V(static_cast<float>(rng_.range(3.0, 5.0)), 0.25f, static_cast<float>(rng_.range(1.5, 2.6)))
                         : V(static_cast<float>(rng_.range(0.4, 1.4)), static_cast<float>(rng_.range(0.08, 0.3)), static_cast<float>(rng_.range(0.4, 1.2)));
        addChunk(p, cv, sz, big || rng_.uniform() < 0.4, true, V(0.09f, 0.09f, 0.1f));
    }
}

void Effects::onGroundKilled(const CombatEvent& e, const Battle& b) {
    Vector3 p = toRl(e.pos);
    float size = static_cast<float>(e.size);
    GroundKind kind = GroundKind::Tank;
    bool known = e.b >= 0 && e.b < static_cast<int>(b.ground().size());
    if (known) kind = b.ground()[e.b].kind;
    const Terrain& T = b.terrain();
    bool airborne = e.pos.y - T.surfaceHeight(e.pos.x, e.pos.z) > 15.0;
    if (kind == GroundKind::Helicopter && airborne) {
        // a helicopter: the blast, rotor blades flung out, then the burning wreck spins down
        spawnBlast(p, V(0, 0, 0), 1.6f, false);
        for (int i = 0; i < 4; ++i) {
            float a = static_cast<float>(i * 1.5708 + rng_.range(-0.3, 0.3));
            addChunk(Vector3Add(p, V(0, 1.5f, 0)), V(cosf(a) * 55, static_cast<float>(rng_.range(5, 25)), sinf(a) * 55), V(5.5f, 0.06f, 0.35f), false, false,
                     V(0.06f, 0.06f, 0.06f));
        }
        return;
    }
    spawnBlast(p, V(0, 0, 0), size, true);
    // what the target was decides what follows
    bool vehicle = kind == GroundKind::Tank || kind == GroundKind::Truck || kind == GroundKind::AAA || kind == GroundKind::SAM ||
                   kind == GroundKind::Radar || kind == GroundKind::Launcher || kind == GroundKind::Artillery || kind == GroundKind::Helicopter;
    bool ammo = kind == GroundKind::Tank || kind == GroundKind::SAM || kind == GroundKind::Launcher || kind == GroundKind::Artillery ||
                kind == GroundKind::Ammo || kind == GroundKind::AAA;
    bool fuel = kind == GroundKind::FuelTank || kind == GroundKind::Refinery || kind == GroundKind::OilRig;
    if (vehicle) {   // turret / cab / panels thrown off
        int n = kind == GroundKind::Tank ? 3 : 2;
        for (int i = 0; i < n; ++i)
            addChunk(Vector3Add(p, V(0, 1.5f, 0)), V(static_cast<float>(rng_.range(-12, 12)), static_cast<float>(rng_.range(18, 38)), static_cast<float>(rng_.range(-12, 12))),
                     i == 0 && kind == GroundKind::Tank ? V(3.0f, 0.9f, 2.6f) : V(static_cast<float>(rng_.range(0.6, 1.6)), 0.2f, static_cast<float>(rng_.range(0.5, 1.4))),
                     i == 0, true, V(0.12f, 0.13f, 0.1f));
    }
    if (ammo) {      // ammunition cooking off: a few sharp secondary blasts over the next seconds
        int n = 2 + static_cast<int>(rng_.uniform() * 3);
        for (int i = 0; i < n; ++i)
            pending_.push_back({static_cast<float>(rng_.range(0.6, 4.0)),
                                Vector3Add(p, V(static_cast<float>(rng_.range(-4, 4)), static_cast<float>(rng_.range(0.5, 3)), static_cast<float>(rng_.range(-4, 4)))),
                                static_cast<float>(rng_.range(0.5, 1.1)), true});
    }
    if (fuel) {      // a fuel fire: a rising fireball, then a tall black column
        for (int i = 0; i < 3; ++i) pending_.push_back({0.2f + i * 0.35f, Vector3Add(p, V(0, 6.0f + i * 9.0f, 0)), size * 0.7f, false});
    }
    if (wrecks_.size() > 24) wrecks_.erase(wrecks_.begin());
    wrecks_.push_back({p, 0.0f, size, fuel ? 2.0f : 1.0f});
}

void Effects::onEvent(const CombatEvent& e, const Battle& b) {
    Vector3 p = toRl(e.pos);
    switch (e.type) {
        case EventType::Explosion: {
            spawnExplosion(p, static_cast<float>(e.size), false, false);
            break;
        }
        case EventType::SmallExplosion:
            if (e.size < 0.5) {
                // bullet impact: dust + sparks
                spawnSmokePuff(p, V(0, 4, 0), 1.0f, 5.0f, 1.2f, V(0.45f, 0.4f, 0.32f), 0.6f);
                Particle q;
                q.pos = p; q.vel = V(static_cast<float>(rng_.range(-15, 15)), static_cast<float>(rng_.range(10, 30)), static_cast<float>(rng_.range(-15, 15)));
                q.size0 = 0.5f; q.size1 = 0.2f; q.maxLife = 0.4f; q.color = {1, 0.7f, 0.3f}; q.kind = PKind::Spark; q.lift = -9.8f; q.stretch = true;
                if (room()) particles_.push_back(q);
            } else {
                spawnExplosion(p, static_cast<float>(e.size) * 0.6f, false, false);
            }
            break;
        case EventType::JetKilled:
            onJetKilled(e, b);
            break;
        case EventType::GroundKilled:
            onGroundKilled(e, b);
            break;
        case EventType::JetDamaged: {
            // a hit that does not (yet) bring the aircraft down
            const Jet* j = b.jetById(e.b);
            if (!j || !j->alive) break;
            const FlightState& s = j->fm->state();
            JetFx& fx = jetFx_[e.b];
            bool missile = e.text == "MISSILE" || e.size >= 15.0;
            if (missile) {
                // proximity blast on the airframe: fireball, sparks, a burst of black smoke, and
                // a fair chance of an engine fire or a punctured wing
                Vector3 hv = toRl(s.vel);
                spawnBlast(p, hv, 0.9f, false);
                fx.hitTimer = 0;
                fx.hitOffset = V(static_cast<float>(rng_.range(-6, 2)), static_cast<float>(rng_.range(-0.5, 1.0)), static_cast<float>(rng_.range(-4, 4)));
                if (rng_.uniform() < 0.6 || j->health < 50) fx.engineFire = std::max(fx.engineFire, 0.6f + static_cast<float>(rng_.uniform()) * 0.4f);
                if (rng_.uniform() < 0.6) { fx.leak = 25.0f; fx.leakSide = rng_.uniform() < 0.5 ? -1 : 1; }
                for (int i = 0; i < 2; ++i)
                    addChunk(p, Vector3Add(Vector3Scale(hv, 0.8f), V(static_cast<float>(rng_.range(-25, 25)), static_cast<float>(rng_.range(-5, 25)), static_cast<float>(rng_.range(-25, 25)))),
                             V(static_cast<float>(rng_.range(0.3, 0.9)), 0.06f, static_cast<float>(rng_.range(0.3, 0.8))), false, true, V(0.2f, 0.21f, 0.22f));
            } else {
                // cannon / flak strikes: bright sparks and a puff of smoke off the skin
                for (int i = 0; i < 8 && room(); ++i) {
                    Particle q;
                    q.pos = p; q.vel = Vector3Add(toRl(s.vel * 0.9), V(static_cast<float>(rng_.range(-35, 35)), static_cast<float>(rng_.range(-15, 35)), static_cast<float>(rng_.range(-35, 35))));
                    q.size0 = 0.6f; q.size1 = 0.2f; q.maxLife = 0.45f; q.color = {1, 0.82f, 0.45f}; q.kind = PKind::Spark; q.stretch = true; q.lift = -9.8f;
                    particles_.push_back(q);
                }
                spawnSmokePuff(p, toRl(s.vel * 0.8), 1.0f, 5.0f, 1.4f, V(0.12f, 0.12f, 0.12f), 0.6f);
                if (j->health < 40 && rng_.uniform() < 0.25) fx.engineFire = std::max(fx.engineFire, 0.5f);
            }
            break;
        }
        case EventType::Flak: {
            Particle f; f.pos = p; f.size0 = 6; f.size1 = 10; f.maxLife = 0.12f; f.color = {1, 0.7f, 0.35f}; f.kind = PKind::Glow;
            if (room()) particles_.push_back(f);
            for (int i = 0; i < 5; ++i)
                spawnSmokePuff(Vector3Add(p, V(static_cast<float>(rng_.range(-3, 3)), static_cast<float>(rng_.range(-3, 3)), static_cast<float>(rng_.range(-3, 3)))),
                               V(0, 0.5f, 0), 4.0f, 14.0f, 6.0f, V(0.05f, 0.05f, 0.05f), 0.85f);
            float d = Vector3Distance(p, camPos_);
            if (d < 500.0f) shake_ += 0.3f * (1.0f - d / 500.0f);
            break;
        }
        case EventType::Splash:
            spawnExplosion(p, 1.5f, false, true);
            break;
        case EventType::MissileLaunch:
            for (int i = 0; i < 6; ++i)
                spawnSmokePuff(p, V(static_cast<float>(rng_.range(-3, 3)), static_cast<float>(rng_.range(-3, 3)), static_cast<float>(rng_.range(-3, 3))), 0.8f, 3.5f, 1.2f, V(0.9f, 0.9f, 0.9f), 0.3f);
            break;
        case EventType::Hit:
        case EventType::PlayerHit:
            for (int i = 0; i < 6; ++i) {
                Particle q;
                q.pos = p; q.vel = V(static_cast<float>(rng_.range(-40, 40)), static_cast<float>(rng_.range(-20, 40)), static_cast<float>(rng_.range(-40, 40)));
                q.size0 = 0.6f; q.size1 = 0.2f; q.maxLife = 0.5f; q.color = {1, 0.8f, 0.4f}; q.kind = PKind::Spark; q.stretch = true;
                if (room()) particles_.push_back(q);
            }
            break;
        default: break;
    }
}

void Effects::emitTrailPoint(long long key, Vector3 p, float w0, float w1, float maxAge, Vector3 col, float alpha,
                             float spacing, bool dark) {
    Trail& t = trails_[key];
    t.alive = true;
    t.maxAge = maxAge; t.width0 = w0; t.width1 = w1; t.color = col; t.alpha = alpha; t.dark = dark;
    if (t.hasLast && Vector3Distance(t.lastEmit, p) < spacing) {
        // keep the head of the ribbon glued to the emitter
        if (!t.pts.empty()) t.pts.back().pos = p;
        if (t.pts.size() >= 2) return;
    }
    t.pts.push_back({p, 0.0f, 1.0f});
    t.lastEmit = p;
    t.hasLast = true;
    if (t.pts.size() > 600) t.pts.pop_front();
}

void Effects::update(double dt, const Battle& b, const Lighting& L, Vector3 camPos, const std::vector<const JetModel*>& models) {
    time_ += dt;
    camPos_ = camPos;
    float fdt = static_cast<float>(dt);
    // particles
    born_.clear();
    for (Particle& q : particles_) {
        q.life += fdt;
        q.vel = Vector3Scale(q.vel, std::max(0.0f, 1.0f - q.drag * fdt));
        q.vel.y += q.lift * fdt;
        q.pos = Vector3Add(q.pos, Vector3Scale(q.vel, fdt));
        q.rot += q.spin * fdt;
        if (q.kind == PKind::Smoke) q.heat *= std::exp(-fdt * 2.5f);
        if (q.toSmoke && q.life >= q.maxLife) {
            // burnt-out fire leaves dark smoke behind, still glowing faintly at first
            Particle m;
            m.pos = q.pos; m.vel = Vector3Scale(q.vel, 0.5f);
            float sz = q.size1;
            m.size0 = sz * 0.8f; m.size1 = sz * 2.6f; m.maxLife = 5.0f + 3.0f * static_cast<float>(rng_.uniform());
            float g = 0.07f + 0.1f * static_cast<float>(rng_.uniform());
            m.color = V(g, g * 0.97f, g * 0.94f); m.alpha = 0.5f + 0.2f * static_cast<float>(rng_.uniform()); m.kind = PKind::Smoke; m.drag = 0.8f; m.lift = 1.8f; m.heat = 0.45f;
            m.rot = q.rot; m.spin = q.spin * 0.2f;
            born_.push_back(m);
        }
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& q) { return q.life >= q.maxLife; }), particles_.end());
    for (const Particle& m : born_) if (room()) particles_.push_back(m);

    // trails age
    for (auto& [k, t] : trails_) {
        for (auto& p : t.pts) p.age += fdt;
        while (!t.pts.empty() && t.pts.front().age > t.maxAge) t.pts.pop_front();
        t.alive = false;
    }

    // speed streaks around the camera (sense of speed): denser, longer and brighter as the jet
    // goes faster, so the rushing air reads even over featureless sky or sea
    if (!b.jets().empty() && b.player().alive) {
        const FlightState& ps = b.player().fm->state();
        float sp = static_cast<float>(ps.airspeed);
        if (sp > 140.0f && !ps.onGround) {
            float fast = std::clamp((sp - 140.0f) / 400.0f, 0.0f, 1.0f);
            float rate = (sp - 140.0f) * (0.25f + 0.35f * fast) * fdt;
            int n = static_cast<int>(rate) + (rng_.uniform() < (rate - std::floor(rate)) ? 1 : 0);
            Vector3 vel = toRl(ps.vel);
            Vector3 dir = Vector3Normalize(vel);
            for (int i = 0; i < n && room(); ++i) {
                Vector3 off = V(static_cast<float>(rng_.range(-30, 30)), static_cast<float>(rng_.range(-18, 18)), static_cast<float>(rng_.range(-30, 30)));
                Particle q;
                q.pos = Vector3Add(Vector3Add(camPos, Vector3Scale(dir, 60.0f)), off);
                q.size0 = 0.05f + 0.04f * fast; q.size1 = q.size0; q.maxLife = 0.45f - 0.15f * fast; q.kind = PKind::Spark; q.stretch = true;
                float c = 0.12f + 0.22f * fast;
                q.color = {c, c, c * 1.04f}; q.drag = 0; q.lift = 0;
                q.vel = Vector3Scale(dir, -sp * (0.5f + 0.3f * fast));  // streaks rush past the camera
                particles_.push_back(q);
            }
            // very low and fast: the jet's wake kicks up dust (or spray over water) under it
            const Terrain& T = b.terrain();
            double ground = T.surfaceHeight(ps.pos.x, ps.pos.z);
            double agl = ps.pos.y - ground;
            if (agl < 70.0 && sp > 160.0) {
                bool water = T.isWater(ps.pos.x, ps.pos.z);
                float k = static_cast<float>((1.0 - agl / 70.0) * std::min(1.0, (sp - 160.0) / 200.0));
                float expect = 90.0f * k * fdt;   // puffs per frame (90 per second at full strength)
                int m = static_cast<int>(expect) + (rng_.uniform() < expect - std::floor(expect) ? 1 : 0);
                Vector3 back = Vector3Scale(dir, -18.0f);
                for (int i = 0; i < m && room(); ++i) {
                    Particle q;
                    q.pos = V(static_cast<float>(ps.pos.x + back.x + rng_.range(-10, 10)), static_cast<float>(ground + 1.0),
                              static_cast<float>(ps.pos.z + back.z + rng_.range(-10, 10)));
                    q.vel = V(static_cast<float>(dir.x * sp * 0.08f + rng_.range(-6, 6)), static_cast<float>(rng_.range(4, 12)),
                              static_cast<float>(dir.z * sp * 0.08f + rng_.range(-6, 6)));
                    q.size0 = 3.0f; q.size1 = 16.0f + 14.0f * k; q.maxLife = 1.6f + rng_.uniform();
                    q.kind = PKind::Dust; q.drag = 1.2f; q.lift = 0.5f;
                    q.color = water ? Vector3{0.85f, 0.88f, 0.92f} : Vector3{0.55f, 0.47f, 0.36f};
                    particles_.push_back(q);
                }
            }
        }
    }
    // missiles: thick white smoke ribbons
    for (const Missile& m : b.missiles()) {
        Vector3 pos = toRl(m.pos);
        Vector3 dir = Vector3Normalize(toRl(m.vel));
        Vector3 tail = Vector3Subtract(pos, Vector3Scale(dir, 2.0f));
        if (m.motor > 0) {
            float w = m.surfaceLaunched ? 2.0f : 1.4f;
            emitTrailPoint(1000000000LL + m.id, tail, w, w * 6.5f, 7.5f, V(0.92f, 0.92f, 0.93f), 0.8f, 7.0f);
        }
    }
    // flares: burning magnesium + smoke
    for (const Flare& f : b.flares()) {
        if (!f.active) continue;
        if (rng_.uniform() < 0.7)
            spawnSmokePuff(toRl(f.pos), V(0, 0, 0), 0.8f, 6.0f, 2.8f, V(0.85f, 0.85f, 0.86f), 0.5f);
    }
    // jets
    const auto& jets = b.jets();
    for (size_t i = 0; i < jets.size(); ++i) {
        const Jet& j = *jets[i];
        if (!j.alive) continue;
        const FlightState& s = j.fm->state();
        const JetModel* jm = i < models.size() ? models[i] : nullptr;
        bool vapor = (s.gLoad > 5.0 || (s.alpha > 13.0 * kDeg && s.airspeed > 110)) && !s.onGround;
        int gen = vaporGen_[j.id];
        if (vapor && jm) {
            for (int side = 0; side < 2; ++side) {
                Vec3 wt = s.pos + s.q.rotate(Vec3{jm->wingtips[side].x, jm->wingtips[side].y, jm->wingtips[side].z});
                long long key = 2000000000LL + j.id * 10LL + side + gen * 100000LL;
                emitTrailPoint(key, toRl(wt), 0.25f, 1.6f, 1.3f, V(0.95f, 0.96f, 1.0f), 0.55f, 3.0f);
            }
            // condensation sheets over the wings: denser and wider with G (moist low-level air helps)
            float humid = static_cast<float>(std::clamp(1.0 - s.pos.y / 7000.0, 0.25, 1.0));
            int sheets = s.gLoad > 6.5 ? 12 : (s.gLoad > 5.0 ? 7 : 3);
            for (int k = 0; k < sheets; ++k) {
                if (rng_.uniform() > 0.75 * humid) continue;
                double span = rng_.range(1.2, std::max(1.5, std::fabs(jm->wingtips[1].z) - 0.5)) * (rng_.uniform() < 0.5 ? -1.0 : 1.0);
                Vec3 c = s.pos + s.q.rotate(Vec3{rng_.range(-3.0, 0.5), 0.55, span});
                spawnSmokePuff(toRl(c), toRl(s.vel * 0.985), 0.7f, 1.7f, 0.17f, V(1, 1, 1), static_cast<float>(0.10 + 0.025 * (s.gLoad - 4.0)));
            }
            vaporOn_[j.id] = true;
        } else if (vaporOn_[j.id]) {
            vaporOn_[j.id] = false;
            vaporGen_[j.id] = gen + 1;
        }
        // transonic shock collar ("vapour cone") just behind the canopy near Mach 1 in moist air
        if (s.mach > 0.94 && s.mach < 1.05 && s.pos.y < 6500 && jm) {
            float k = static_cast<float>(1.0 - std::fabs(s.mach - 0.995) / 0.055);
            float humid = static_cast<float>(std::clamp(1.0 - s.pos.y / 6500.0, 0.2, 1.0));
            for (int i = 0; i < 16; ++i) {
                if (rng_.uniform() > k * humid) continue;
                double a = rng_.range(0, 2 * kPi), r = rng_.range(1.4, 2.6) * (jm->length / 19.0);
                Vec3 c = s.pos + s.q.rotate(Vec3{rng_.range(-3.5, -1.0), std::sin(a) * r * 0.8, std::cos(a) * r});
                spawnSmokePuff(toRl(c), toRl(s.vel * 0.99), 0.9f, 2.0f, 0.18f, V(1, 1, 1), 0.28f * k);
            }
        }
        // damage: smoke trail, engine fire, leaking wing, burning fuselage; the worse the damage,
        // the heavier the effects
        {
            auto it = jetFx_.find(j.id);
            JetFx* fx = it != jetFx_.end() ? &it->second : nullptr;
            float hp = static_cast<float>(j.health);
            float len = static_cast<float>(j.fm->profile().lengthM);
            Vector3 tailP = toRl(s.pos - s.q.forward() * (len * 0.5 + 0.5));
            Vector3 sv = toRl(s.vel);
            if (fx) {
                fx->hitTimer += fdt;
                if (hp < 35) fx->engineFire = std::max(fx->engineFire, 0.7f);
            }
            float fire = fx ? fx->engineFire : (hp < 35 ? 0.7f : 0.0f);
            if (hp < 70 || fire > 0) {
                // a smoke ribbon from the tail: light grey when damaged, black when burning
                float g = fire > 0.3f || hp < 35 ? 0.05f : 0.32f;
                float w = hp < 35 ? 2.4f : 1.4f;
                emitTrailPoint(4000000000LL + j.id, tailP, w, w * 6.0f, hp < 35 ? 9.0f : 5.0f, V(g, g, g), hp < 35 ? 0.85f : 0.55f, 6.0f, g < 0.1f);
            }
            if (fire > 0) {
                // engine fire: flickering tongues of flame at the nozzles, stronger in bursts
                float pulse = 0.6f + 0.4f * sinf(static_cast<float>(time_) * 9.0f + j.id) * sinf(static_cast<float>(time_) * 3.3f);
                int nfl = static_cast<int>(3 * fire * pulse + rng_.uniform());
                for (int k = 0; k < nfl && room(); ++k) {
                    Particle q;
                    q.pos = Vector3Add(tailP, V(static_cast<float>(rng_.range(-0.6, 0.6)), static_cast<float>(rng_.range(-0.4, 0.4)), static_cast<float>(rng_.range(-0.6, 0.6))));
                    q.vel = Vector3Scale(sv, 0.82f);
                    q.size0 = 1.2f + 1.6f * fire; q.size1 = 3.5f + 3.0f * fire; q.maxLife = 0.22f + 0.2f * fire;
                    q.kind = PKind::Fire; q.drag = 1.0f; q.heat = 0.7f; q.toSmoke = false; q.color = {1, 1, 1};
                    q.rot = static_cast<float>(rng_.range(0, 6.28));
                    particles_.push_back(q);
                }
            }
            if (hp < 25) {
                // fuselage on fire: flames along the body and a trail of burning fuel
                Vector3 mid = toRl(s.pos + s.q.rotate(Vec3{rng_.range(-len * 0.35, len * 0.1), 0.4, rng_.range(-1.0, 1.0)}));
                if (room()) spawnFire(mid, Vector3Scale(sv, 0.9f), 1.6f, 4.5f, 0.35f, V(1, 1, 1));
                if (rng_.uniform() < 0.6 && room()) {
                    Particle q;
                    q.pos = tailP; q.vel = Vector3Scale(sv, 0.55f);
                    q.size0 = 2.0f; q.size1 = 6.0f; q.maxLife = 0.9f; q.kind = PKind::Fire; q.drag = 0.8f; q.lift = 1.0f; q.heat = 0.5f; q.toSmoke = true;
                    q.color = {1, 1, 1};
                    particles_.push_back(q);
                }
            }
            if (fx && fx->leak > 0) {
                // a punctured wing streams a white mist of fuel / hydraulic fluid
                fx->leak -= fdt;
                if (jm) {
                    int side = fx->leakSide > 0 ? 1 : 0;
                    Vec3 wt = s.pos + s.q.rotate(Vec3{jm->wingtips[side].x + 1.5, jm->wingtips[side].y, jm->wingtips[side].z * 0.6});
                    emitTrailPoint(4100000000LL + j.id, toRl(wt), 0.4f, 3.5f, 2.5f, V(0.92f, 0.93f, 0.95f), 0.4f * std::min(1.0f, fx->leak / 5.0f), 4.0f);
                }
            }
            if (fx && fx->hitTimer < 1.2f) {
                // the hit point keeps burning for a moment after a missile strike
                Vector3 hp3 = toRl(s.pos + s.q.rotate(Vec3{fx->hitOffset.x, fx->hitOffset.y, fx->hitOffset.z}));
                if (room()) spawnFire(hp3, Vector3Scale(sv, 0.85f), 1.5f, 4.0f, 0.3f, V(1, 1, 1));
            }
        }
    }
    // secondary explosions (fuel, ammunition) scheduled by earlier blasts
    for (PendingBlast& pb : pending_) pb.t -= fdt;
    for (size_t i = 0; i < pending_.size();) {
        if (pending_[i].t <= 0) {
            PendingBlast pb = pending_[i];
            pending_.erase(pending_.begin() + static_cast<long>(i));
            spawnBlast(pb.pos, V(0, 0, 0), pb.size, pb.ground);
        } else {
            ++i;
        }
    }
    // the falling bodies of destroyed aircraft: tumbling, burning, trailing black smoke
    const Terrain& T = b.terrain();
    for (FallingWreck& w : falling_) {
        w.life += fdt;
        w.vel.y -= 9.8f * fdt;
        w.vel = Vector3Scale(w.vel, 1.0f - 0.12f * fdt);
        w.pos = Vector3Add(w.pos, Vector3Scale(w.vel, fdt));
        w.q = w.q.integrated(w.omega, fdt);
        emitTrailPoint(w.trailKey, w.pos, 2.0f, 13.0f, 12.0f, V(0.09f, 0.088f, 0.085f), 0.75f, 6.0f, true);
        for (int k = 0; k < 3 && room(); ++k) {
            Particle q;
            q.pos = Vector3Add(w.pos, V(static_cast<float>(rng_.range(-2, 2)), static_cast<float>(rng_.range(-1, 1)), static_cast<float>(rng_.range(-2, 2))));
            q.vel = Vector3Scale(w.vel, 0.7f);
            q.size0 = 2.5f; q.size1 = 7.0f * w.fire; q.maxLife = 0.45f; q.kind = PKind::Fire; q.drag = 1.2f; q.lift = 2.0f; q.heat = 0.6f; q.toSmoke = true;
            q.color = {1, 1, 1};
            particles_.push_back(q);
        }
        // its fire lights up the surroundings
        if (Vector3Distance(w.pos, camPos) < 4000.0f) {
            flash_.pos = w.pos;
            flash_.color = {1.0f, 0.5f, 0.18f};
            flash_.intensity = std::max(flash_.intensity, 2.2f + 0.8f * static_cast<float>(rng_.uniform()));
        }
    }
    for (size_t i = 0; i < falling_.size();) {
        FallingWreck& w = falling_[i];
        double gy = T.surfaceHeight(w.pos.x, w.pos.z);
        if (w.pos.y <= gy + 1.5 || w.life > 40.0f) {
            if (w.life <= 40.0f) groundImpact(V(w.pos.x, static_cast<float>(gy + 1.0), w.pos.z), w.vel, 2.6f, b);
            falling_.erase(falling_.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }
    // helicopters shot down: the wreck spins down on fire
    for (const GroundTarget& g : b.ground()) {
        if (g.alive || g.kind != GroundKind::Helicopter) continue;
        double agl = g.pos.y - T.surfaceHeight(g.pos.x, g.pos.z);
        if (agl < 2.0) continue;
        Vector3 hp3 = toRl(g.pos + Vec3{0, 1.5, 0});
        emitTrailPoint(4200000000LL + (&g - b.ground().data()), hp3, 1.5f, 10.0f, 8.0f, V(0.09f, 0.09f, 0.09f), 0.75f, 5.0f, true);
        if (room()) spawnFire(hp3, V(0, 0, 0), 1.6f, 4.5f, 0.4f, V(1, 1, 1));
    }
    // broken-off pieces
    for (Chunk& c : chunks_) {
        c.life += fdt;
        c.vel.y -= 9.8f * fdt;
        c.vel = Vector3Scale(c.vel, 1.0f - (0.25f + 0.4f * (c.size.y < 0.15f ? 1.0f : 0.0f)) * fdt);   // flat panels flutter
        c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, fdt));
        c.angle += c.angVel * fdt;
        if (c.burning) {
            emitTrailPoint(c.trailKey, c.pos, 0.8f, 5.0f, 3.5f, V(0.1f, 0.1f, 0.1f), 0.6f, 4.0f, true);
            if (rng_.uniform() < 0.8 && room()) spawnFire(c.pos, Vector3Scale(c.vel, 0.6f), 0.9f, 2.4f, 0.25f, V(1, 1, 1));
        } else if (c.smoking && c.life < 3.0f) {
            emitTrailPoint(c.trailKey, c.pos, 0.3f, 2.0f, 1.5f, V(0.2f, 0.2f, 0.2f), 0.5f, 3.0f);
        }
    }
    chunks_.erase(std::remove_if(chunks_.begin(), chunks_.end(), [&](const Chunk& c) {
        double gy = T.surfaceHeight(c.pos.x, c.pos.z);
        bool down = c.pos.y < gy + 0.3 || c.life > 30.0f;
        if (down && c.burning && c.life < 30.0f && room()) {
            Particle q; q.pos = c.pos; q.size0 = 3; q.size1 = 9; q.maxLife = 0.6f; q.kind = PKind::Fire; q.heat = 0.8f; q.toSmoke = true; q.color = {1, 1, 1};
            particles_.push_back(q);
        }
        return down;
    }), chunks_.end());
    // wrecks: rising smoke columns
    for (Wreck& w : wrecks_) {
        w.timer += fdt;
        float fade = 1.0f - std::min(1.0f, w.timer / 120.0f);
        if (fade > 0 && rng_.uniform() < 0.35 * fade) {
            // a rising column that leans with the wind
            spawnSmokePuff(Vector3Add(w.pos, V(static_cast<float>(rng_.range(-3, 3)), 2, static_cast<float>(rng_.range(-3, 3)))),
                           V(2.5f + static_cast<float>(rng_.range(-1, 1)), 9.0f, 1.0f), 4.0f, 34.0f * w.size * 0.5f,
                           11.0f + 4.0f * w.fire, V(0.1f, 0.098f, 0.095f), 0.6f * fade);
            if (!particles_.empty()) { particles_.back().drag = 0.22f; particles_.back().lift = 0.7f; }
            if (!particles_.empty()) particles_.back().heat = 0.5f * w.fire * fade;
            float burn = std::max(0.0f, 1.0f - w.timer / (40.0f * w.fire));
            int nf = static_cast<int>(2 * w.fire * burn + rng_.uniform());
            for (int k = 0; k < nf && room(); ++k)
                spawnFire(Vector3Add(w.pos, V(static_cast<float>(rng_.range(-2, 2)) * w.size, 1.0f, static_cast<float>(rng_.range(-2, 2)) * w.size)),
                          V(0, 3, 0), 1.6f * w.size, 4.5f * w.size, 0.7f, V(1, 1, 1));
            if (nf > 0 && !particles_.empty() && particles_.back().kind == PKind::Fire) { particles_.back().heat = 0.75f; particles_.back().toSmoke = true; }
        }
    }
    // remove finished trails
    for (auto it = trails_.begin(); it != trails_.end();) {
        if (!it->second.alive && it->second.pts.empty()) it = trails_.erase(it);
        else ++it;
    }
    flash_.intensity *= expf(-fdt * 7.0f);
    sunDir_ = L.sunDir;
}

void Effects::billboard(Vector3 p, float size, float rot, Vector3 right, Vector3 up, Color c) {
    float cr = cosf(rot) * size * 0.5f, sr = sinf(rot) * size * 0.5f;
    Vector3 r = Vector3Add(Vector3Scale(right, cr), Vector3Scale(up, sr));
    Vector3 u = Vector3Add(Vector3Scale(up, cr), Vector3Scale(right, -sr));
    Vector3 a = Vector3Subtract(Vector3Subtract(p, r), u), bb = Vector3Subtract(Vector3Add(p, r), u);
    Vector3 cc = Vector3Add(Vector3Add(p, r), u), d = Vector3Add(Vector3Subtract(p, r), u);
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlTexCoord2f(0, 1); rlVertex3f(a.x, a.y, a.z);
    rlTexCoord2f(1, 1); rlVertex3f(bb.x, bb.y, bb.z);
    rlTexCoord2f(1, 0); rlVertex3f(cc.x, cc.y, cc.z);
    rlTexCoord2f(0, 0); rlVertex3f(d.x, d.y, d.z);
}

void Effects::billboardShaded(Vector3 p, float size, float rot, Vector3 right, Vector3 up, Vector3 lightDir, Color lit, Color dark) {
    float cr = cosf(rot) * size * 0.5f, sr = sinf(rot) * size * 0.5f;
    Vector3 r = Vector3Add(Vector3Scale(right, cr), Vector3Scale(up, sr));
    Vector3 u = Vector3Add(Vector3Scale(up, cr), Vector3Scale(right, -sr));
    Vector3 corner[4] = {Vector3Subtract(Vector3Subtract(p, r), u), Vector3Subtract(Vector3Add(p, r), u),
                         Vector3Add(Vector3Add(p, r), u), Vector3Add(Vector3Subtract(p, r), u)};
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int i = 0; i < 4; ++i) {
        Vector3 off = Vector3Subtract(corner[i], p);
        float k = 0.5f + 0.5f * Vector3DotProduct(Vector3Normalize(off), lightDir);   // 1 on the lit side
        Color c = {static_cast<unsigned char>(dark.r + (lit.r - dark.r) * k), static_cast<unsigned char>(dark.g + (lit.g - dark.g) * k),
                   static_cast<unsigned char>(dark.b + (lit.b - dark.b) * k), static_cast<unsigned char>(dark.a + (lit.a - dark.a) * k)};
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(uv[i][0], uv[i][1]);
        rlVertex3f(corner[i].x, corner[i].y, corner[i].z);
    }
}

void Effects::drawTrails(const Camera3D& cam, const Lighting& L) {
    rlSetTexture(glow_.id);
    rlBegin(RL_QUADS);
    for (auto& kv : trails_) {
        auto& t = kv.second;
        if (t.pts.size() < 2) continue;
        for (size_t i = 0; i + 1 < t.pts.size(); ++i) {
            const TrailPoint& a = t.pts[i];
            const TrailPoint& b = t.pts[i + 1];
            Vector3 seg = Vector3Subtract(b.pos, a.pos);
            float segLen = Vector3Length(seg);
            if (segLen < 1e-3f || segLen > 400.0f) continue;
            auto widthAt = [&](const TrailPoint& p) {
                float f = std::min(1.0f, p.age / t.maxAge);
                return t.width0 + (t.width1 - t.width0) * sqrtf(f);
            };
            auto alphaAt = [&](const TrailPoint& p, bool head) {
                float f = std::min(1.0f, p.age / t.maxAge);
                float a = t.alpha * (1.0f - f) * (1.0f - f * 0.3f);
                if (head) a *= 0.6f;
                return a;
            };
            Vector3 toCamA = Vector3Subtract(cam.position, a.pos);
            Vector3 side = Vector3Normalize(Vector3CrossProduct(seg, toCamA));
            float wa = widthAt(a), wb = widthAt(b);
            // light the smoke: sun-facing brightness + ambient, then fog
            Vector3 lit;
            if (t.dark) lit = Vector3Multiply(t.color, Vector3Add(Vector3Scale(L.sunColor, 0.5f), L.ambient(1)));
            else lit = Vector3Multiply(t.color, Vector3Add(Vector3Scale(L.sunColor, 0.62f), Vector3Scale(L.ambient(1), 0.9f)));
            Vector3 mid = Vector3Scale(Vector3Add(a.pos, b.pos), 0.5f);
            float fog = L.fogAmount(cam.position, mid);
            lit = Vector3Lerp(lit, L.fogColor(cam.position, mid), fog);
            Color ca = hdrColor(lit, kSmokeScale, alphaAt(a, false));
            Color cb = hdrColor(lit, kSmokeScale, alphaAt(b, i + 2 == t.pts.size()));
            Vector3 a0 = Vector3Subtract(a.pos, Vector3Scale(side, wa)), a1 = Vector3Add(a.pos, Vector3Scale(side, wa));
            Vector3 b0 = Vector3Subtract(b.pos, Vector3Scale(side, wb)), b1 = Vector3Add(b.pos, Vector3Scale(side, wb));
            rlColor4ub(ca.r, ca.g, ca.b, ca.a);
            rlTexCoord2f(0, 0.5f); rlVertex3f(a0.x, a0.y, a0.z);
            rlTexCoord2f(1, 0.5f); rlVertex3f(a1.x, a1.y, a1.z);
            rlColor4ub(cb.r, cb.g, cb.b, cb.a);
            rlTexCoord2f(1, 0.5f); rlVertex3f(b1.x, b1.y, b1.z);
            rlTexCoord2f(0, 0.5f); rlVertex3f(b0.x, b0.y, b0.z);
        }
    }
    rlEnd();
    rlSetTexture(0);
}

void Effects::draw(const Camera3D& cam, const Battle& b, const Lighting& L, Shader ps, int intensityLoc,
                   const std::vector<Vector3>& runwayLights, bool showRunwayLights) {
    Matrix view = GetCameraMatrix(cam);
    Vector3 right = {view.m0, view.m4, view.m8};
    Vector3 up = {view.m1, view.m5, view.m9};
    Vector3 camPos = cam.position;
    // distance-scaled sprites keep a constant on-screen size: account for zoomed cameras
    const float fovScale = std::clamp(cam.fovy / 60.0f, 0.02f, 1.5f);

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginShaderMode(ps);
    // ---------------- alpha-blended smoke ----------------
    float smokeScale = kSmokeScale;
    SetShaderValue(ps, intensityLoc, &smokeScale, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ALPHA);
    drawTrails(cam, L);
    auto& smoke = smokeSort_;   // reused buffer: no allocation per frame
    smoke.clear();
    smoke.reserve(particles_.size());
    for (const Particle& q : particles_)
        if (q.kind == PKind::Smoke || q.kind == PKind::Dust) smoke.push_back({Vector3DistanceSqr(q.pos, camPos), &q});
    std::sort(smoke.begin(), smoke.end(), [](auto& a, auto& c) { return a.first > c.first; });
    rlSetTexture(puff_.id);
    rlBegin(RL_QUADS);
    Vector3 litBase = Vector3Add(Vector3Scale(L.sunColor, 0.55f), Vector3Scale(L.ambient(1), 0.95f));
    Vector3 sunSide = Vector3Add(Vector3Scale(L.sunColor, 1.0f), Vector3Scale(L.ambient(1), 0.85f));   // facing the sun
    Vector3 shadeSide = Vector3Scale(L.ambient(1), 0.85f);                                              // facing away
    Vector3 sunDir = L.sunDir;
    for (auto& [d2, q] : smoke) {
        float t = q->life / q->maxLife;
        float size = q->size0 + (q->size1 - q->size0) * sqrtf(t);
        float a = q->alpha * (1.0f - t) * std::min(1.0f, q->life * 8.0f);
        float dcam = sqrtf(d2);
        a *= std::clamp((dcam - size * 0.3f) / (size * 1.2f), 0.0f, 1.0f);  // fade when the camera is inside the puff
        if (a < 0.004f) continue;
        float fog = L.fogAmount(camPos, q->pos);
        Vector3 fogC = L.fogColor(camPos, q->pos);
        if (q->kind == PKind::Smoke && size > 3.0f) {
            // thick smoke: lit on the sun side, in its own shadow on the other, and glowing orange
            // from the fire inside while it is fresh
            Vector3 glow = Vector3Scale(V(1.6f, 0.55f, 0.15f), q->heat * (1.0f - std::min(1.0f, t * 3.0f)));
            Vector3 cl = Vector3Add(Vector3Multiply(q->color, sunSide), Vector3Scale(glow, 0.4f));
            Vector3 cd = Vector3Add(Vector3Multiply(q->color, shadeSide), glow);
            cl = Vector3Lerp(cl, fogC, fog);
            cd = Vector3Lerp(cd, fogC, fog);
            billboardShaded(q->pos, size, q->rot, right, up, sunDir, hdrColor(cl, kSmokeScale, a), hdrColor(cd, kSmokeScale, a));
        } else {
            Vector3 lit = Vector3Multiply(q->color, litBase);
            lit = Vector3Lerp(lit, fogC, fog);
            billboard(q->pos, size, q->rot, right, up, hdrColor(lit, kSmokeScale, a));
        }
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();

    // ---------------- additive: fire, sparks, glows, lights, tracers ----------------
    float addScale = kAddScale;
    SetShaderValue(ps, intensityLoc, &addScale, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(glow_.id);
    rlBegin(RL_QUADS);
    for (const Particle& q : particles_) {
        if (q.kind == PKind::Smoke || q.kind == PKind::Dust) continue;
        float t = q.life / q.maxLife;
        float size = q.size0 + (q.size1 - q.size0) * t;
        float bright = q.kind == PKind::Glow ? 6.0f : (q.kind == PKind::Spark ? 5.0f : 3.2f * (1.0f - t) + 0.4f);
        Vector3 col = Vector3Scale(q.color, bright * (1.0f - t * t));
        if (q.kind == PKind::Fire) {
            // combustion temperature: white-hot core -> yellow -> orange -> deep red -> out
            float h = std::clamp(q.heat - t * 1.1f + 0.15f, 0.0f, 1.0f);
            Vector3 hot = V(1.0f, 0.93f, 0.78f), warm = V(1.0f, 0.55f, 0.13f), cool = V(0.75f, 0.14f, 0.03f);
            Vector3 c = h > 0.5f ? Vector3Lerp(warm, hot, (h - 0.5f) * 2.0f) : Vector3Lerp(cool, warm, h * 2.0f);
            float flick = 0.85f + 0.3f * static_cast<float>(rng_.uniform());
            col = Vector3Scale(c, (0.5f + 2.6f * h) * (1.0f - t) * flick);
        }
        Color c = hdrColor(col, kAddScale, 1.0f);
        if (q.stretch) {
            Vector3 v = q.vel;
            float sp = Vector3Length(v);
            Vector3 dir = sp > 1e-3f ? Vector3Scale(v, 1.0f / sp) : up;
            Vector3 tail = Vector3Subtract(q.pos, Vector3Scale(dir, std::min(sp * 0.04f, 8.0f) + size));
            Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(dir, Vector3Subtract(camPos, q.pos))), size * 0.5f);
            rlColor4ub(c.r, c.g, c.b, c.a);
            rlTexCoord2f(0, 0.5f); rlVertex3f(tail.x - side.x, tail.y - side.y, tail.z - side.z);
            rlTexCoord2f(1, 0.5f); rlVertex3f(tail.x + side.x, tail.y + side.y, tail.z + side.z);
            rlTexCoord2f(1, 0.5f); rlVertex3f(q.pos.x + side.x, q.pos.y + side.y, q.pos.z + side.z);
            rlTexCoord2f(0, 0.5f); rlVertex3f(q.pos.x - side.x, q.pos.y - side.y, q.pos.z - side.z);
        } else {
            billboard(q.pos, size, q.rot, right, up, c);
        }
    }
    // missile motors
    for (const Missile& m : b.missiles()) {
        if (m.motor <= 0) continue;
        Vector3 dir = Vector3Normalize(toRl(m.vel));
        Vector3 tail = Vector3Subtract(toRl(m.pos), Vector3Scale(dir, 2.0f));
        float fl = 0.8f + 0.4f * static_cast<float>(rng_.uniform());
        billboard(tail, 7.0f * fl, 0, right, up, hdrColor(V(12.0f, 8.0f, 4.0f), kAddScale, 1));
        billboard(tail, 2.5f, 0, right, up, hdrColor(V(16.0f, 15.0f, 13.0f), kAddScale, 1));
    }
    // flares
    for (const Flare& f : b.flares()) {
        if (!f.active) continue;
        float fl = 0.75f + 0.5f * static_cast<float>(rng_.uniform());
        float fade = 1.0f - std::min(1.0f, static_cast<float>(f.life) / 4.5f) * 0.6f;
        billboard(toRl(f.pos), 9.0f * fl * fade, 0, right, up, hdrColor(V(16.0f, 12.0f, 7.0f), kAddScale, 1));
        billboard(toRl(f.pos), 3.0f, 0, right, up, hdrColor(V(16.0f, 16.0f, 16.0f), kAddScale, 1));
    }
    // jet lights + afterburner glow + muzzle flash
    for (const auto& jp : b.jets()) {
        const Jet& j = *jp;
        if (!j.alive) continue;
        const FlightState& s = j.fm->state();
        float span = static_cast<float>(j.fm->profile().spanM * 0.5);
        Vector3 lw = toRl(s.pos + s.q.rotate(Vec3{-2.0, 0, -span}));
        Vector3 rw = toRl(s.pos + s.q.rotate(Vec3{-2.0, 0, span}));
        float dist = Vector3Distance(camPos, toRl(s.pos));
        float ls = std::clamp(dist * 0.004f * fovScale, 0.15f, 12.0f);
        billboard(lw, ls, 0, right, up, hdrColor(V(6.0f, 0.4f, 0.3f), kAddScale, 1));
        billboard(rw, ls, 0, right, up, hdrColor(V(0.3f, 6.0f, 0.8f), kAddScale, 1));
        if (fmod(time_ + j.id * 0.37, 1.2) < 0.07) {
            Vector3 tl = toRl(s.pos + s.q.rotate(Vec3{-8.5, 3.0, 0}));
            billboard(tl, ls * 1.6f, 0, right, up, hdrColor(V(14.0f, 14.0f, 14.0f), kAddScale, 1));
        }
        if (s.abLevel > 0.05 || s.rpm > 0.7) {
            Vector3 noz = toRl(s.pos - s.q.forward() * (j.fm->profile().lengthM * 0.5 + 0.3));
            float ab = static_cast<float>(s.abLevel);
            float g = ab * 1.0f + static_cast<float>(std::max(0.0, s.rpm - 0.7)) * 0.4f;
            billboard(noz, 2.5f + 4.5f * ab, 0, right, up, hdrColor(Vector3Scale(V(1.0f, 0.55f, 0.25f), 1.4f * g), kAddScale, 1));
        }
        if (j.gunFiring) {
            Vector3 mz = toRl(s.pos + s.q.rotate(Vec3{6.5, 0.6, 1.0}));
            float fl = static_cast<float>(rng_.uniform());
            billboard(mz, 2.5f + 2.5f * fl, fl * 6, right, up, hdrColor(V(12.0f, 8.0f, 3.0f), kAddScale, 1));
        }
    }
    // runway edge lights
    if (showRunwayLights) {
        for (const Vector3& p : runwayLights) {
            float dist = Vector3Distance(camPos, p);
            if (dist > 9000) continue;
            float s = std::clamp(dist * 0.0035f * fovScale, 0.2f, 9.0f);
            billboard(p, s, 0, right, up, hdrColor(V(5.0f, 4.2f, 2.6f), kAddScale, 1));
        }
    }
    rlEnd();
    rlSetTexture(0);

    // tracers (stretched streaks)
    rlSetTexture(glow_.id);
    rlBegin(RL_QUADS);
    for (const Bullet& bu : b.bullets()) {
        Vector3 p = toRl(bu.pos);
        Vector3 v = toRl(bu.vel);
        float sp = Vector3Length(v);
        if (sp < 1) continue;
        Vector3 dir = Vector3Scale(v, 1.0f / sp);
        float len = bu.owner >= 0 ? 22.0f : 16.0f;
        Vector3 tail = Vector3Subtract(p, Vector3Scale(dir, len));
        float dist = Vector3Distance(camPos, p);
        float w = std::clamp(dist * 0.0018f * fovScale, 0.1f, 3.0f);
        Vector3 side = Vector3Scale(Vector3Normalize(Vector3CrossProduct(dir, Vector3Subtract(camPos, p))), w);
        Color c = bu.owner >= 0 ? hdrColor(V(14.0f, 11.0f, 5.0f), kAddScale, 1) : hdrColor(V(14.0f, 4.0f, 1.5f), kAddScale, 1);
        rlColor4ub(c.r, c.g, c.b, c.a);
        rlTexCoord2f(0, 0.5f); rlVertex3f(tail.x - side.x, tail.y - side.y, tail.z - side.z);
        rlTexCoord2f(1, 0.5f); rlVertex3f(tail.x + side.x, tail.y + side.y, tail.z + side.z);
        rlTexCoord2f(1, 0.5f); rlVertex3f(p.x + side.x, p.y + side.y, p.z + side.z);
        rlTexCoord2f(0, 0.5f); rlVertex3f(p.x - side.x, p.y - side.y, p.z - side.z);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    EndShaderMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

}  // namespace sky
