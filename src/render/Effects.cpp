#include "Effects.hpp"

#include "Convert.hpp"
#include "JetMesh.hpp"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {
constexpr size_t kMaxParticles = 14000;
constexpr float kSmokeScale = 4.0f;    // HDR range of the alpha-blended group
constexpr float kAddScale = 16.0f;     // HDR range of the additive group
Vector3 V(float x, float y, float z) { return {x, y, z}; }
}  // namespace

void Effects::init(Texture2D puff, Texture2D glow, Texture2D spark) {
    puff_ = puff; glow_ = glow; spark_ = spark;
    particles_.reserve(kMaxParticles);
}

void Effects::clear() {
    particles_.clear(); trails_.clear(); wrecks_.clear(); debris_.clear();
    flash_ = FlashLight{};
}

void Effects::spawnSmokePuff(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col, float alpha) {
    if (particles_.size() >= kMaxParticles) return;
    Particle q;
    q.pos = p; q.vel = v; q.size0 = s0; q.size1 = s1; q.maxLife = life; q.color = col; q.alpha = alpha;
    q.kind = PKind::Smoke; q.drag = 0.9f; q.lift = 1.2f;
    q.rot = static_cast<float>(rng_.range(0, 6.28)); q.spin = static_cast<float>(rng_.range(-0.4, 0.4));
    particles_.push_back(q);
}

void Effects::spawnFire(Vector3 p, Vector3 v, float s0, float s1, float life, Vector3 col) {
    if (particles_.size() >= kMaxParticles) return;
    Particle q;
    q.pos = p; q.vel = v; q.size0 = s0; q.size1 = s1; q.maxLife = life; q.color = col; q.alpha = 1;
    q.kind = PKind::Fire; q.drag = 2.0f; q.lift = 3.0f;
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
            if (particles_.size() < kMaxParticles) particles_.push_back(q);
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
        if (particles_.size() < kMaxParticles) particles_.push_back(q);
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
                if (particles_.size() < kMaxParticles) particles_.push_back(q);
            } else {
                spawnExplosion(p, static_cast<float>(e.size) * 0.6f, false, false);
            }
            break;
        case EventType::JetKilled: {
            spawnExplosion(p, 2.6f, false, false);
            const Jet* j = b.jetById(e.b);
            Vector3 v = j ? toRl(j->fm->state().vel) : V(0, 0, 0);
            for (int i = 0; i < 6; ++i) {
                Debris d;
                d.pos = p;
                d.vel = Vector3Add(Vector3Scale(v, 0.6f), V(static_cast<float>(rng_.range(-40, 40)), static_cast<float>(rng_.range(-10, 40)), static_cast<float>(rng_.range(-40, 40))));
                d.life = 0;
                d.trailKey = 3000000000LL + (debrisCounter_++);
                d.burning = i < 4;
                debris_.push_back(d);
            }
            break;
        }
        case EventType::GroundKilled:
            spawnExplosion(p, static_cast<float>(e.size), true, false);
            if (wrecks_.size() > 24) wrecks_.erase(wrecks_.begin());
            wrecks_.push_back({p, 0.0f, static_cast<float>(e.size)});
            break;
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
                if (particles_.size() < kMaxParticles) particles_.push_back(q);
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
    for (Particle& q : particles_) {
        q.life += fdt;
        q.vel = Vector3Scale(q.vel, std::max(0.0f, 1.0f - q.drag * fdt));
        q.vel.y += q.lift * fdt;
        q.pos = Vector3Add(q.pos, Vector3Scale(q.vel, fdt));
        q.rot += q.spin * fdt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& q) { return q.life >= q.maxLife; }), particles_.end());

    // trails age
    for (auto& [k, t] : trails_) {
        for (auto& p : t.pts) p.age += fdt;
        while (!t.pts.empty() && t.pts.front().age > t.maxAge) t.pts.pop_front();
        t.alive = false;
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
            // condensation cloud over the wing at very high G
            if (s.gLoad > 6.5 && rng_.uniform() < 0.6) {
                Vec3 c = s.pos + s.q.rotate(Vec3{-1.0, 0.8, rng_.range(-4, 4)});
                spawnSmokePuff(toRl(c), toRl(s.vel * 0.9), 2.0f, 5.0f, 0.25f, V(1, 1, 1), 0.35f);
            }
            vaporOn_[j.id] = true;
        } else if (vaporOn_[j.id]) {
            vaporOn_[j.id] = false;
            vaporGen_[j.id] = gen + 1;
        }
        // damage smoke
        if (j.health < 60 && rng_.uniform() < 0.5) {
            float g = j.health < 30 ? 0.08f : 0.25f;
            spawnSmokePuff(toRl(s.pos - s.q.forward() * 6.0), toRl(s.vel * 0.3), 1.5f, 10.0f, 3.0f, V(g, g, g), 0.7f);
            if (j.health < 30 && rng_.uniform() < 0.5) spawnFire(toRl(s.pos - s.q.forward() * 4.0), toRl(s.vel * 0.5), 1.5f, 3.0f, 0.25f, V(1, 0.5f, 0.15f));
        }
    }
    // debris
    for (Debris& d : debris_) {
        d.life += fdt;
        d.vel.y -= 9.8f * fdt;
        d.vel = Vector3Scale(d.vel, 1.0f - 0.25f * fdt);
        d.pos = Vector3Add(d.pos, Vector3Scale(d.vel, fdt));
        if (d.burning) {
            emitTrailPoint(d.trailKey, d.pos, 1.2f, 7.0f, 4.0f, V(0.07f, 0.07f, 0.07f), 0.75f, 5.0f, true);
            if (rng_.uniform() < 0.8) spawnFire(d.pos, V(0, 0, 0), 1.4f, 2.8f, 0.25f, V(1.0f, 0.5f, 0.15f));
        }
    }
    std::vector<Debris> keep;
    for (Debris& d : debris_) {
        bool ground = d.pos.y < 0 || d.life > 12.0f;
        if (ground) {
            if (d.burning && d.life < 12.0f) {
                Particle q; q.pos = d.pos; q.size0 = 4; q.size1 = 12; q.maxLife = 0.5f; q.color = {1, 0.6f, 0.2f}; q.kind = PKind::Fire;
                particles_.push_back(q);
            }
        } else keep.push_back(d);
    }
    debris_.swap(keep);
    // wrecks: rising smoke columns
    for (Wreck& w : wrecks_) {
        w.timer += fdt;
        float fade = 1.0f - std::min(1.0f, w.timer / 120.0f);
        if (fade > 0 && rng_.uniform() < 0.35 * fade) {
            spawnSmokePuff(Vector3Add(w.pos, V(static_cast<float>(rng_.range(-3, 3)), 2, static_cast<float>(rng_.range(-3, 3)))), V(1.5f, 7.0f, 0.5f), 4.0f, 34.0f * w.size * 0.5f,
                           10.0f, V(0.07f, 0.07f, 0.075f), 0.8f * fade);
            if (rng_.uniform() < 0.5) spawnFire(Vector3Add(w.pos, V(0, 1.5f, 0)), V(0, 2, 0), 2.0f, 4.0f, 0.5f, V(1.0f, 0.45f, 0.12f));
        }
    }
    // remove finished trails
    for (auto it = trails_.begin(); it != trails_.end();) {
        if (!it->second.alive && it->second.pts.empty()) it = trails_.erase(it);
        else ++it;
    }
    flash_.intensity *= expf(-fdt * 7.0f);
    (void)L;
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

void Effects::drawTrails(const Camera3D& cam, const Lighting& L) {
    rlSetTexture(glow_.id);
    rlBegin(RL_QUADS);
    for (auto& [k, t] : trails_) {
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

    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    BeginShaderMode(ps);
    // ---------------- alpha-blended smoke ----------------
    float smokeScale = kSmokeScale;
    SetShaderValue(ps, intensityLoc, &smokeScale, SHADER_UNIFORM_FLOAT);
    BeginBlendMode(BLEND_ALPHA);
    drawTrails(cam, L);
    std::vector<std::pair<float, const Particle*>> smoke;
    smoke.reserve(particles_.size());
    for (const Particle& q : particles_)
        if (q.kind == PKind::Smoke || q.kind == PKind::Dust) smoke.push_back({Vector3DistanceSqr(q.pos, camPos), &q});
    std::sort(smoke.begin(), smoke.end(), [](auto& a, auto& c) { return a.first > c.first; });
    rlSetTexture(puff_.id);
    rlBegin(RL_QUADS);
    Vector3 litBase = Vector3Add(Vector3Scale(L.sunColor, 0.55f), Vector3Scale(L.ambient(1), 0.95f));
    for (auto& [d2, q] : smoke) {
        float t = q->life / q->maxLife;
        float size = q->size0 + (q->size1 - q->size0) * sqrtf(t);
        float a = q->alpha * (1.0f - t) * std::min(1.0f, q->life * 8.0f);
        Vector3 lit = Vector3Multiply(q->color, litBase);
        float fog = L.fogAmount(camPos, q->pos);
        lit = Vector3Lerp(lit, L.fogColor(camPos, q->pos), fog);
        billboard(q->pos, size, q->rot, right, up, hdrColor(lit, kSmokeScale, a));
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
        if (q.kind == PKind::Fire) col = Vector3Lerp(col, Vector3Scale(V(0.9f, 0.25f, 0.04f), 1.6f * (1 - t)), std::min(1.0f, t * 1.5f));
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
        float ls = std::clamp(dist * 0.004f, 0.5f, 12.0f);
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
            float s = std::clamp(dist * 0.0035f, 0.6f, 9.0f);
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
        float w = std::clamp(dist * 0.0018f, 0.25f, 3.0f);
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
