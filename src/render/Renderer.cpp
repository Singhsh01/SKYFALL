#include "Renderer.hpp"

#include "Convert.hpp"
#include "MeshBuilder.hpp"
#include "Shaders.hpp"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

extern "C" void glClear(unsigned int mask);  // GL 1.0 entry point, present in every GL library

namespace sky {

namespace {
constexpr unsigned int kGlDepthBufferBit = 0x00000100;

Shader makeShader(const char* vs, const char* fsBody, bool atmosphere) {
    std::string fs = "#version 330\n";
    if (atmosphere) fs += shaders::kAtmosphere;
    fs += fsBody;
    Shader s = LoadShaderFromMemory(vs, fs.c_str());
    if (s.id == 0 || s.id == rlGetShaderIdDefault()) TraceLog(LOG_WARNING, "SKYFALL: shader failed to compile, using default");
    return s;
}

const char* kParticleVS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() { fragTexCoord = vertexTexCoord; fragColor = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)GLSL";
const char* kParticleFS = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float uIntensity;
out vec4 finalColor;
void main() {
    vec4 t = texture(texture0, fragTexCoord);
    finalColor = vec4(t.rgb * fragColor.rgb * uIntensity, t.a * fragColor.a);
}
)GLSL";

const char* kShadowVS = R"GLSL(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)GLSL";
const char* kShadowFS = R"GLSL(#version 330
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)GLSL";
constexpr int kShadowSize = 2048;

Vector3 paintFor(const Jet& j, Vector3& b, float& camo) {
    camo = 0;
    if (j.team == Team::Blue) { b = {0.25f, 0.26f, 0.28f}; return {0.33f, 0.345f, 0.36f}; }
    switch (j.fm->profile().style) {
        case AirframeStyle::FlankerFamily: camo = 1; b = {0.17f, 0.22f, 0.30f}; return {0.40f, 0.46f, 0.53f};
        case AirframeStyle::StealthTwinTail: camo = 1; b = {0.18f, 0.20f, 0.25f}; return {0.44f, 0.48f, 0.54f};
        default: b = {0.26f, 0.28f, 0.31f}; return {0.40f, 0.42f, 0.45f};
    }
}
}  // namespace

int Prog::loc(const char* name) {
    auto it = locs.find(name);
    if (it != locs.end()) return it->second;
    int l = GetShaderLocation(sh, name);
    locs[name] = l;
    return l;
}
void Prog::setF(const char* n, float v) { int l = loc(n); if (l >= 0) SetShaderValue(sh, l, &v, SHADER_UNIFORM_FLOAT); }
void Prog::setV2(const char* n, Vector2 v) { int l = loc(n); if (l >= 0) SetShaderValue(sh, l, &v, SHADER_UNIFORM_VEC2); }
void Prog::setV3(const char* n, Vector3 v) { int l = loc(n); if (l >= 0) SetShaderValue(sh, l, &v, SHADER_UNIFORM_VEC3); }
void Prog::setV4(const char* n, Vector4 v) { int l = loc(n); if (l >= 0) SetShaderValue(sh, l, &v, SHADER_UNIFORM_VEC4); }
void Prog::setM(const char* n, Matrix m) { int l = loc(n); if (l >= 0) SetShaderValueMatrix(sh, l, m); }

RenderTexture2D Renderer::makeTarget(int w, int h, bool hdr, bool depth) {
    RenderTexture2D rt = {0};
    rt.id = rlLoadFramebuffer();
    if (rt.id == 0) return rt;
    rlEnableFramebuffer(rt.id);
    int fmt = hdr ? PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    rt.texture.id = rlLoadTexture(nullptr, w, h, fmt, 1);
    rt.texture.width = w; rt.texture.height = h; rt.texture.format = fmt; rt.texture.mipmaps = 1;
    rlFramebufferAttach(rt.id, rt.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    if (depth) {
        rt.depth.id = rlLoadTextureDepth(w, h, true);
        rt.depth.width = w; rt.depth.height = h;
        rlFramebufferAttach(rt.id, rt.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    }
    bool ok = rlFramebufferComplete(rt.id);
    rlDisableFramebuffer();
    if (!ok) {
        TraceLog(LOG_WARNING, "SKYFALL: framebuffer incomplete (hdr=%d)", hdr);
        rlUnloadFramebuffer(rt.id);
        rt.id = 0;
    }
    SetTextureFilter(rt.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(rt.texture, TEXTURE_WRAP_CLAMP);
    return rt;
}

void Renderer::freeTarget(RenderTexture2D& rt) {
    if (rt.id) UnloadRenderTexture(rt);
    rt = RenderTexture2D{};
}

bool Renderer::init(const Terrain& t, int width, int height) {
    terrain_ = &t;
    auto t0 = std::chrono::steady_clock::now();
    world_.build(t);
    for (int s = 0; s < 6; ++s) jets_[s] = buildJetModel(static_cast<AirframeStyle>(s));
    flame_ = buildFlameMesh();
    missile_ = buildMissileMesh();
    buildGroundModels();
    buildClouds(t);
    effects_.init(world_.puff, world_.glow, world_.spark);
    TraceLog(LOG_INFO, "SKYFALL: world built in %.0f ms",
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());

    using namespace shaders;
    sky_.sh = makeShader(kSkyVS, kSkyFS, true);
    terrain_p_.sh = makeShader(kTerrainVS, kTerrainFS, true);
    water_.sh = makeShader(kWaterVS, kWaterFS, true);
    lit_.sh = makeShader(kLitVS, kLitFS, true);
    jet_.sh = makeShader(kJetVS, kJetFS, true);
    flameP_.sh = makeShader(kJetVS, kFlameFS, true);
    cloud_.sh = makeShader(kWaterVS, kCloudFS, true);
    runway_.sh = makeShader(kRunwayVS, kRunwayFS, true);
    particle_.sh = LoadShaderFromMemory(kParticleVS, kParticleFS);
    bright_.sh = LoadShaderFromMemory(kPostVS, kBrightFS);
    blur_.sh = LoadShaderFromMemory(kPostVS, kBlurFS);
    composite_.sh = LoadShaderFromMemory(kPostVS, kCompositeFS);

    auto mat = [&](Prog& p, bool noise, bool depthMap) {
        Material m = LoadMaterialDefault();
        m.shader = p.sh;
        if (noise) {
            p.sh.locs[SHADER_LOC_MAP_METALNESS] = p.loc("uNoise");
            m.maps[MATERIAL_MAP_METALNESS].texture = world_.noise;
        }
        if (depthMap) {
            p.sh.locs[SHADER_LOC_MAP_NORMAL] = p.loc("uDepthMap");
            m.maps[MATERIAL_MAP_NORMAL].texture = world_.depthMap;
        }
        m.shader = p.sh;
        return m;
    };
    // sun shadow map (depth texture) around the player aircraft
    shadowP_.sh = LoadShaderFromMemory(kShadowVS, kShadowFS);
    shadowMat_ = LoadMaterialDefault();
    shadowMat_.shader = shadowP_.sh;
    shadowRT_.id = rlLoadFramebuffer();
    if (shadowRT_.id) {
        rlEnableFramebuffer(shadowRT_.id);
        shadowRT_.texture.id = rlLoadTexture(nullptr, kShadowSize, kShadowSize, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE, 1);
        shadowRT_.texture.width = shadowRT_.texture.height = kShadowSize;
        shadowRT_.texture.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE; shadowRT_.texture.mipmaps = 1;
        shadowRT_.depth.id = rlLoadTextureDepth(kShadowSize, kShadowSize, false);
        shadowRT_.depth.width = shadowRT_.depth.height = kShadowSize;
        rlFramebufferAttach(shadowRT_.id, shadowRT_.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
        rlFramebufferAttach(shadowRT_.id, shadowRT_.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);
        bool ok = rlFramebufferComplete(shadowRT_.id);
        rlDisableFramebuffer();
        if (!ok) { TraceLog(LOG_WARNING, "SKYFALL: shadow map unavailable"); rlUnloadFramebuffer(shadowRT_.id); shadowRT_.id = 0; }
        shadowDepth_ = shadowRT_.depth;
        shadowDepth_.mipmaps = 1;
        shadowDepth_.format = PIXELFORMAT_UNCOMPRESSED_R32;
    }
    auto bindShadow = [&](Prog& p, Material& m) {
        if (!shadowRT_.id) return;
        p.sh.locs[SHADER_LOC_MAP_ROUGHNESS] = p.loc("uShadowMap");
        m.maps[MATERIAL_MAP_ROUGHNESS].texture = shadowDepth_;
        m.shader = p.sh;
    };
    terrainMat_ = mat(terrain_p_, true, false);
    waterMat_ = mat(water_, true, true);
    litMat_ = mat(lit_, false, false);
    jetMat_ = mat(jet_, true, false);
    flameMat_ = mat(flameP_, true, false);
    cloudMat_ = mat(cloud_, true, false);
    runwayMat_ = mat(runway_, true, false);
    bindShadow(terrain_p_, terrainMat_);
    bindShadow(lit_, litMat_);
    bindShadow(jet_, jetMat_);
    bindShadow(runway_, runwayMat_);
    resize(width, height);
    return true;
}

void Renderer::resize(int width, int height) {
    if (width == w_ && height == h_ && scene_.id) return;
    freeTarget(scene_); freeTarget(bloomA_); freeTarget(bloomB_); freeTarget(bloomC_); freeTarget(bloomD_);
    w_ = width; h_ = height;
    scene_ = makeTarget(w_, h_, true, true);
    hdr_ = scene_.id != 0;
    if (!hdr_) scene_ = makeTarget(w_, h_, false, true);
    bloomA_ = makeTarget(w_ / 2, h_ / 2, hdr_, false);
    bloomB_ = makeTarget(w_ / 2, h_ / 2, hdr_, false);
    bloomC_ = makeTarget(w_ / 4, h_ / 4, hdr_, false);
    bloomD_ = makeTarget(w_ / 4, h_ / 4, hdr_, false);
}

void Renderer::shutdown() {
    world_.unload();
    for (auto& j : jets_) unloadJetModel(j);
    UnloadMesh(flame_); UnloadMesh(missile_);
    UnloadMesh(tank_); UnloadMesh(truck_); UnloadMesh(aaa_); UnloadMesh(sam_); UnloadMesh(bunker_); UnloadMesh(radar_);
    for (Prog* p : {&sky_, &terrain_p_, &water_, &lit_, &jet_, &flameP_, &cloud_, &runway_, &particle_, &bright_, &blur_, &composite_})
        UnloadShader(p->sh);
    freeTarget(scene_); freeTarget(bloomA_); freeTarget(bloomB_); freeTarget(bloomC_); freeTarget(bloomD_);
}

void Renderer::buildGroundModels() {
    Color olive = {120, 118, 88, 255}, dark = {60, 62, 50, 255}, sand = {170, 150, 110, 255}, grey = {150, 150, 145, 255};
    {
        MeshBuilder m;  // tank
        m.box({0, 1.0f, 0}, {7.0f, 1.4f, 3.6f}, olive);
        m.box({0, 0.45f, 0}, {7.4f, 0.9f, 3.9f}, dark);
        m.box({-0.3f, 2.1f, 0}, {3.2f, 0.9f, 2.6f}, olive);
        m.box({2.6f, 2.15f, 0}, {4.2f, 0.22f, 0.22f}, dark);
        tank_ = m.build();
    }
    {
        MeshBuilder m;  // truck
        m.box({1.8f, 1.5f, 0}, {2.0f, 2.0f, 2.4f}, sand);
        m.box({-1.2f, 1.7f, 0}, {4.4f, 2.4f, 2.5f}, olive);
        m.box({0, 0.5f, 0}, {6.4f, 0.8f, 2.2f}, dark);
        truck_ = m.build();
    }
    {
        MeshBuilder m;  // AAA
        m.box({0, 0.6f, 0}, {4.5f, 1.2f, 3.0f}, olive);
        m.box({0, 1.6f, 0}, {2.0f, 1.0f, 2.0f}, dark);
        m.box({1.6f, 2.4f, 0.4f}, {3.2f, 0.16f, 0.16f}, dark, MatrixRotateZ(0.6f));
        m.box({1.6f, 2.4f, -0.4f}, {3.2f, 0.16f, 0.16f}, dark, MatrixRotateZ(0.6f));
        aaa_ = m.build();
    }
    {
        MeshBuilder m;  // SAM launcher
        m.box({0, 0.8f, 0}, {8.0f, 1.6f, 3.2f}, olive);
        for (int i = 0; i < 4; ++i)
            m.box({0, 2.6f + (i / 2) * 0.9f, -0.6f + (i % 2) * 1.2f}, {6.5f, 0.8f, 0.8f}, grey, MatrixRotateZ(0.5f));
        sam_ = m.build();
    }
    {
        MeshBuilder m;  // bunker
        m.box({0, 1.5f, 0}, {14.0f, 3.0f, 10.0f}, {130, 128, 118, 255});
        m.box({0, 3.3f, 0}, {12.0f, 0.6f, 8.0f}, {110, 108, 100, 255});
        bunker_ = m.build();
    }
    {
        MeshBuilder m;  // radar
        m.box({0, 1.0f, 0}, {5.0f, 2.0f, 3.0f}, olive);
        m.cylinder({0, 2.0f, 0}, 0.25f, 3.0f, 6, dark);
        m.box({0, 5.5f, 0}, {0.3f, 3.0f, 6.0f}, grey);
        radar_ = m.build();
    }
}

void Renderer::buildClouds(const Terrain& t) {
    Rng rng(31337);
    puffs_.clear();
    for (int c = 0; c < 110; ++c) {
        Vector3 center = V3(rng.range(-26000, 26000), rng.range(1500, 2150), rng.range(-26000, 26000));
        // keep the runway approach mostly clear
        if (std::fabs(center.x - t.runway().center.x) < 3000 && std::fabs(center.z - t.runway().center.z) < 2000) continue;
        int n = 7 + static_cast<int>(rng.uniform() * 9);
        float scale = static_cast<float>(rng.range(0.7, 1.5));
        for (int i = 0; i < n; ++i) {
            CloudPuff p;
            float ox = static_cast<float>(rng.range(-260, 260)) * scale, oz = static_cast<float>(rng.range(-200, 200)) * scale;
            float oy = static_cast<float>(rng.range(-40, 140)) * scale * (1.0f - (ox * ox + oz * oz) / (300.0f * 300.0f * scale * scale) * 0.5f);
            p.pos = {center.x + ox, center.y + oy, center.z + oz};
            p.size = static_cast<float>(rng.range(140, 280)) * scale;
            p.shade = std::clamp((oy + 40.0f * scale) / (180.0f * scale), 0.0f, 1.0f);
            p.rot = static_cast<float>(rng.range(0, 6.28));
            puffs_.push_back(p);
        }
    }
}

void Renderer::setAtmosphere(Prog& p, const FrameParams& f) {
    p.setV3("uSunDir", f.L.sunDir);
    p.setV3("uSunColor", f.L.sunColor);
    p.setV3("uZenith", f.L.zenith);
    p.setV3("uHorizon", f.L.horizon);
    p.setV3("uGroundHaze", f.L.groundHaze);
    p.setV3("uCamPos", f.cam.position);
    p.setF("uFogDensity", f.L.fogDensity);
    p.setF("uFogFalloff", f.L.fogFalloff);
    p.setF("uTime", static_cast<float>(f.time));
    p.setF("uCloudCover", f.L.cloudCover);
    p.setF("uShadowOn", (shadowsActive_ && f.shadows) ? 1.0f : 0.0f);
    p.setM("uLightVP", lightVP_);
    const FlashLight& fl = effects_.flash();
    p.setV3("uFlashPos", fl.pos);
    p.setV3("uFlashColor", Vector3Scale(fl.color, fl.intensity));
}

void Renderer::drawSky(const FrameParams& f) {
    float aspect = static_cast<float>(w_) / static_cast<float>(h_);
    Matrix view = MatrixLookAt(f.cam.position, f.cam.target, f.cam.up);
    Matrix proj = MatrixPerspective(f.cam.fovy * DEG2RAD, aspect, f.nearPlane, f.farPlane);
    Matrix inv = MatrixInvert(MatrixMultiply(view, proj));
    setAtmosphere(sky_, f);
    sky_.setM("uInvViewProj", inv);
    sky_.setV2("uResolution", {static_cast<float>(w_), static_cast<float>(h_)});
    sky_.setF("uCirrus", f.L.cirrus);
    BeginShaderMode(sky_.sh);
    int nl = sky_.loc("uNoise");
    if (nl >= 0) SetShaderValueTexture(sky_.sh, nl, world_.noise);
    DrawRectangle(0, 0, w_, h_, WHITE);
    EndShaderMode();
}

void Renderer::drawTerrain(const FrameParams& f) {
    setAtmosphere(terrain_p_, f);
    Vector3 cp = f.cam.position;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    rlDisableBackfaceCulling();
    float hiDist = f.quality == GraphicsQuality::Low ? 2500.0f : (f.quality == GraphicsQuality::Medium ? 4000.0f : 6000.0f);
    for (auto& c : world_.chunks) {
        Vector3 d = Vector3Subtract(c.center, cp);
        float dist = Vector3Length(d);
        if (dist > c.radius && Vector3DotProduct(d, fwd) < -c.radius) continue;
        bool hi = dist - c.radius < hiDist;
        DrawMesh(hi ? c.hi : c.lo, terrainMat_, MatrixIdentity());
        ++drawCalls_;
    }
    DrawMesh(world_.farRing, terrainMat_, MatrixIdentity());
    ++drawCalls_;
    // sea
    setAtmosphere(water_, f);
    water_.setF("uMapHalf", static_cast<float>(Terrain::kHalfSize));
    DrawMesh(world_.water, waterMat_, MatrixIdentity());
    ++drawCalls_;
    rlEnableBackfaceCulling();
}

void Renderer::drawStatics(const FrameParams& f) {
    // runway + taxiways
    setAtmosphere(runway_, f);
    const RunwayInfo& rw = terrain_->runway();
    runway_.setF("uLength", static_cast<float>(rw.length));
    runway_.setF("uWidth", static_cast<float>(rw.width));
    runway_.setF("uKind", 0.0f);
    DrawMesh(world_.runway, runwayMat_, MatrixIdentity());
    runway_.setF("uKind", 1.0f);
    DrawMesh(world_.taxiways, runwayMat_, MatrixIdentity());
    drawCalls_ += 2;
    // airbase, town, trees
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", 0.15f);
    lit_.setF("uFogOn", 1.0f);
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    rlDisableBackfaceCulling();
    DrawMesh(world_.airbase, litMat_, MatrixIdentity());
    DrawMesh(world_.town, litMat_, MatrixIdentity());
    lit_.setF("uSpecular", 0.0f);
    Vector3 cp = f.cam.position;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    float treeDist = f.quality == GraphicsQuality::Low ? 3000.0f : (f.quality == GraphicsQuality::Medium ? 6000.0f : 9000.0f);
    for (auto& c : world_.chunks) {
        if (!c.hasTrees) continue;
        Vector3 d = Vector3Subtract(c.center, cp);
        float dist = Vector3Length(d);
        if (dist - c.radius > treeDist) continue;
        if (dist > c.radius && Vector3DotProduct(d, fwd) < -c.radius) continue;
        DrawMesh(c.trees, litMat_, MatrixIdentity());
        ++drawCalls_;
    }
    rlEnableBackfaceCulling();
    drawCalls_ += 2;
    // parked aircraft on the apron
    for (const Vector3& p : world_.parkedJets) {
        Matrix M = MatrixMultiply(QuaternionToMatrix(toRlQ(Quat::fromEuler(p.y, 0, 0))),
                                  MatrixTranslate(p.x, static_cast<float>(rw.center.y + 2.35), p.z));
        drawJetModel(AirframeStyle::StealthTwinTail, M, {0.33f, 0.345f, 0.36f}, {0.25f, 0.26f, 0.28f}, 0, 1, 0, 0, 0, 0, 0, f);
    }
}

void Renderer::drawGroundTargets(const FrameParams& f) {
    if (!f.battle) return;
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", 0.2f);
    lit_.setF("uFogOn", 1.0f);
    for (const GroundTarget& g : f.battle->ground()) {
        const Mesh* m = &tank_;
        switch (g.kind) {
            case GroundKind::Tank: m = &tank_; break;
            case GroundKind::Truck: m = &truck_; break;
            case GroundKind::AAA: m = &aaa_; break;
            case GroundKind::SAM: m = &sam_; break;
            case GroundKind::Bunker: m = &bunker_; break;
            case GroundKind::Radar: m = &radar_; break;
        }
        if (Vector3Distance(toRl(g.pos), f.cam.position) > 9000) continue;
        Matrix M = MatrixMultiply(MatrixRotateY(static_cast<float>(-g.heading)), MatrixTranslate(static_cast<float>(g.pos.x), static_cast<float>(g.pos.y), static_cast<float>(g.pos.z)));
        litMat_.maps[MATERIAL_MAP_DIFFUSE].color = g.alive ? WHITE : Color{40, 36, 32, 255};
        DrawMesh(*m, litMat_, M);
        ++drawCalls_;
    }
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
}

void Renderer::drawJetModel(AirframeStyle style, const Matrix& M, Vector3 paintA, Vector3 paintB, float camo,
                            float gearPos, float stabDeg, float throttle, float ab, float hitFlash, float damage,
                            const FrameParams& f, bool canopy) {
    const JetModel& jm = jets_[static_cast<int>(style)];
    jet_.setV3("uPaintA", paintA);
    jet_.setV3("uPaintB", paintB);
    jet_.setF("uCamo", camo);
    jet_.setF("uRoughness", 0.52f);
    jet_.setF("uHitFlash", hitFlash);
    jet_.setF("uDamage", damage);
    rlDisableBackfaceCulling();
    DrawMesh(jm.body, jetMat_, M);
    if (canopy) DrawMesh(jm.canopy, jetMat_, M);
    drawCalls_ += 2;
    if (jm.stabL.vertexCount) {
        float a = stabDeg * DEG2RAD;
        Matrix sl = MatrixMultiply(MatrixMultiply(MatrixRotateZ(a), MatrixTranslate(jm.stabPivotL.x, jm.stabPivotL.y, jm.stabPivotL.z)), M);
        Matrix sr = MatrixMultiply(MatrixMultiply(MatrixRotateZ(a), MatrixTranslate(jm.stabPivotR.x, jm.stabPivotR.y, jm.stabPivotR.z)), M);
        DrawMesh(jm.stabL, jetMat_, sl);
        DrawMesh(jm.stabR, jetMat_, sr);
        drawCalls_ += 2;
    }
    if (gearPos > 0.02f) {
        Matrix g = MatrixMultiply(MatrixTranslate(0, (1.0f - gearPos) * 1.6f, 0), M);
        DrawMesh(jm.gear, jetMat_, g);
        ++drawCalls_;
    }
    rlEnableBackfaceCulling();
    if (throttle > 0.02f || ab > 0.01f) {
        setAtmosphere(flameP_, f);
        flameP_.setF("uThrottle", throttle);
        flameP_.setF("uAb", ab);
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        BeginBlendMode(BLEND_ADDITIVE);
        for (int e = 0; e < jm.nozzleCount; ++e) {
            float len = 1.2f + 1.5f * throttle + 7.5f * ab;
            float r = jm.nozzleRadius * (0.85f + 0.15f * ab);
            Matrix F = MatrixMultiply(MatrixMultiply(MatrixScale(len, r, r), MatrixTranslate(jm.nozzles[e].x, jm.nozzles[e].y, jm.nozzles[e].z)), M);
            DrawMesh(flame_, flameMat_, F);
            ++drawCalls_;
        }
        EndBlendMode();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
    }
}

void Renderer::drawJet(const Jet& j, const FrameParams& f, bool canopy, Vec3 origin) {
    const FlightState& s = j.fm->state();
    Matrix M = bodyMatrix(s.pos - origin, s.q);
    Vector3 pb;
    float camo;
    Vector3 pa = paintFor(j, pb, camo);
    float throttleVis = static_cast<float>(std::clamp((s.rpm - 0.6) / 0.4, 0.0, 1.0));
    float stab = static_cast<float>(-j.lastInput.pitch * 15.0 + j.lastInput.roll * 0.0);
    setAtmosphere(jet_, f);
    drawJetModel(j.fm->profile().style, M, pa, pb, camo, static_cast<float>(s.gearPos), stab, throttleVis,
                 static_cast<float>(s.abLevel), static_cast<float>(j.hitFlash * 3.0), static_cast<float>(1.0 - j.health / 100.0), f, canopy);
}

void Renderer::drawShadows(const FrameParams& f) {
    if (!f.battle) return;
    BeginShaderMode(particle_.sh);
    float one = 1.0f;
    particle_.setF("uIntensity", one);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    rlSetTexture(world_.glow.id);
    rlBegin(RL_QUADS);
    Vector3 sd = f.L.sunDir;
    for (const auto& jp : f.battle->jets()) {
        const Jet& j = *jp;
        if (!j.alive) continue;
        const FlightState& s = j.fm->state();
        if (s.agl > 260 || sd.y < 0.05f) continue;
        float h = static_cast<float>(s.agl);
        Vector3 p = toRl(s.pos);
        Vector3 sp = Vector3Subtract(p, Vector3Scale(sd, h / sd.y));
        bool rw = terrain_->onRunway(sp.x, sp.z, 10.0);
        float gy = static_cast<float>(rw ? terrain_->runway().center.y + 0.05 : terrain_->surfaceHeight(sp.x, sp.z) + 0.3);
        Vec3 fw = s.q.forward();
        Vector3 fwd = Vector3Normalize({static_cast<float>(fw.x), 0, static_cast<float>(fw.z)});
        Vector3 rt = {-fwd.z, 0, fwd.x};
        float L = static_cast<float>(j.fm->profile().lengthM * 0.55), W = static_cast<float>(j.fm->profile().spanM * 0.5);
        float a = 0.6f * (1.0f - h / 260.0f);
        rlColor4ub(0, 0, 0, static_cast<unsigned char>(a * 255));
        Vector3 c0 = Vector3Add(Vector3Add(sp, Vector3Scale(fwd, L)), Vector3Scale(rt, -W));
        Vector3 c1 = Vector3Add(Vector3Add(sp, Vector3Scale(fwd, L)), Vector3Scale(rt, W));
        Vector3 c2 = Vector3Add(Vector3Add(sp, Vector3Scale(fwd, -L)), Vector3Scale(rt, W));
        Vector3 c3 = Vector3Add(Vector3Add(sp, Vector3Scale(fwd, -L)), Vector3Scale(rt, -W));
        rlTexCoord2f(0, 0); rlVertex3f(c0.x, gy, c0.z);
        rlTexCoord2f(0, 1); rlVertex3f(c3.x, gy, c3.z);
        rlTexCoord2f(1, 1); rlVertex3f(c2.x, gy, c2.z);
        rlTexCoord2f(1, 0); rlVertex3f(c1.x, gy, c1.z);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    EndShaderMode();
}

void Renderer::drawCloudLayer(const FrameParams& f) {
    setAtmosphere(cloud_, f);
    float sx = std::floor(f.cam.position.x / 1000.0f) * 1000.0f, sz = std::floor(f.cam.position.z / 1000.0f) * 1000.0f;
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    // two stacked sheets (base + tops) give parallax and apparent thickness
    const float layers[2] = {2300.0f, 2420.0f};
    const float shifts[2] = {0.0f, 0.012f};
    bool camAbove = f.cam.position.y > 2360.0f;
    for (int k = 0; k < 2; ++k) {
        int i = camAbove ? k : 1 - k;  // draw the far sheet first
        cloud_.setF("uLayerY", layers[i]);
        cloud_.setF("uLayerShift", shifts[i]);
        float dy = std::fabs(f.cam.position.y - layers[i]);
        cloud_.setF("uLayerFade", std::clamp((dy - 15.0f) / 120.0f, 0.0f, 1.0f));
        DrawMesh(world_.cloudPlane, cloudMat_, MatrixTranslate(sx, layers[i], sz));
        ++drawCalls_;
    }
    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void Renderer::drawCloudPuffs(const FrameParams& f) {
    Vector3 cp = f.cam.position;
    Matrix view = GetCameraMatrix(f.cam);
    Vector3 right = {view.m0, view.m4, view.m8};
    Vector3 up = {view.m1, view.m5, view.m9};
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    std::vector<std::pair<float, const CloudPuff*>> vis;
    vis.reserve(puffs_.size());
    for (const CloudPuff& p : puffs_) {
        Vector3 d = Vector3Subtract(p.pos, cp);
        float dist2 = Vector3LengthSqr(d);
        if (dist2 > 36000.0f * 36000.0f) continue;
        if (Vector3DotProduct(d, fwd) < -p.size) continue;
        vis.push_back({dist2, &p});
    }
    std::sort(vis.begin(), vis.end(), [](auto& a, auto& b) { return a.first > b.first; });
    const float scale = 4.0f;
    BeginShaderMode(particle_.sh);
    particle_.setF("uIntensity", scale);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    rlSetTexture(world_.puff.id);
    rlBegin(RL_QUADS);
    const Lighting& L = f.L;
    Vector3 lit = Vector3Add(Vector3Scale(L.sunColor, 0.92f), Vector3Scale(L.ambient(1), 0.9f));
    Vector3 shade = Vector3Add(Vector3Scale(L.ambient(-0.2f), 1.35f), Vector3Scale(L.sunColor, 0.1f));
    for (auto& [d2, p] : vis) {
        float dist = sqrtf(d2);
        Vector3 viewDir = Vector3Scale(Vector3Subtract(p->pos, cp), 1.0f / std::max(dist, 1.0f));
        float towardSun = powf(std::max(Vector3DotProduct(viewDir, L.sunDir), 0.0f), 5.0f);
        Vector3 col = Vector3Lerp(shade, lit, p->shade);
        col = Vector3Add(col, Vector3Scale(L.sunColor, towardSun * 0.35f));
        float fog = L.fogAmount(cp, p->pos);
        col = Vector3Lerp(col, L.fogColor(cp, p->pos), fog);
        float near = std::clamp((dist - p->size * 0.35f) / (p->size * 0.8f), 0.0f, 1.0f);
        float a = 0.92f * near;
        if (a <= 0.01f) continue;
        float c = cosf(p->rot) * p->size * 0.5f, s = sinf(p->rot) * p->size * 0.5f;
        Vector3 r = Vector3Add(Vector3Scale(right, c), Vector3Scale(up, s));
        Vector3 u = Vector3Add(Vector3Scale(up, c), Vector3Scale(right, -s));
        Color cc = hdrColor(col, scale, a);
        Vector3 q0 = Vector3Subtract(Vector3Subtract(p->pos, r), u), q1 = Vector3Subtract(Vector3Add(p->pos, r), u);
        Vector3 q2 = Vector3Add(Vector3Add(p->pos, r), u), q3 = Vector3Add(Vector3Subtract(p->pos, r), u);
        rlColor4ub(cc.r, cc.g, cc.b, cc.a);
        rlTexCoord2f(0, 1); rlVertex3f(q0.x, q0.y, q0.z);
        rlTexCoord2f(1, 1); rlVertex3f(q1.x, q1.y, q1.z);
        rlTexCoord2f(1, 0); rlVertex3f(q2.x, q2.y, q2.z);
        rlTexCoord2f(0, 0); rlVertex3f(q3.x, q3.y, q3.z);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    EndShaderMode();
}

void Renderer::renderShadows(const FrameParams& f) {
    shadowsActive_ = false;
    if (!shadowRT_.id || !f.battle || !f.shadows || f.quality == GraphicsQuality::Low) return;
    Vector3 focus = f.shadowFocus;
    Vector3 sd = f.L.sunDir;
    if (sd.y < 0.03f) return;
    Camera3D lc{};
    lc.position = Vector3Add(focus, Vector3Scale(sd, 300.0f));
    lc.target = focus;
    lc.up = std::fabs(sd.y) > 0.95f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    lc.fovy = 72.0f;  // orthographic height in metres
    lc.projection = CAMERA_ORTHOGRAPHIC;
    BeginTextureMode(shadowRT_);
    ClearBackground(WHITE);
    rlSetClipPlanes(10.0, 700.0);
    BeginMode3D(lc);
    lightVP_ = MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection());
    rlDisableBackfaceCulling();
    for (const auto& jp : f.battle->jets()) {
        const Jet& j = *jp;
        if (!j.alive) continue;
        const FlightState& s = j.fm->state();
        if (Vector3Distance(toRl(s.pos), focus) > 60.0f) continue;
        const JetModel& jm = jets_[static_cast<int>(j.fm->profile().style)];
        Matrix M = bodyMatrix(s.pos, s.q);
        DrawMesh(jm.body, shadowMat_, M);
        DrawMesh(jm.canopy, shadowMat_, M);
        if (jm.stabL.vertexCount) {
            float a = static_cast<float>(-j.lastInput.pitch * 15.0) * DEG2RAD;
            DrawMesh(jm.stabL, shadowMat_, MatrixMultiply(MatrixMultiply(MatrixRotateZ(a), MatrixTranslate(jm.stabPivotL.x, jm.stabPivotL.y, jm.stabPivotL.z)), M));
            DrawMesh(jm.stabR, shadowMat_, MatrixMultiply(MatrixMultiply(MatrixRotateZ(a), MatrixTranslate(jm.stabPivotR.x, jm.stabPivotR.y, jm.stabPivotR.z)), M));
        }
        if (s.gearPos > 0.02) DrawMesh(jm.gear, shadowMat_, MatrixMultiply(MatrixTranslate(0, (1.0f - static_cast<float>(s.gearPos)) * 1.6f, 0), M));
    }
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
    shadowsActive_ = true;
}

void Renderer::renderWorld(const FrameParams& f) {
    auto t0 = std::chrono::steady_clock::now();
    drawCalls_ = 0;
    renderShadows(f);
    BeginTextureMode(scene_);
    ClearBackground(BLACK);
    drawSky(f);
    rlSetClipPlanes(f.nearPlane, f.farPlane);
    BeginMode3D(f.cam);
    drawTerrain(f);
    drawStatics(f);
    drawGroundTargets(f);
    if (f.battle) {
        setAtmosphere(jet_, f);
        for (const auto& jp : f.battle->jets()) {
            if (!jp->alive || jp->id == f.hideJetId) continue;
            if (Vector3Distance(toRl(jp->fm->state().pos), f.cam.position) > 30000) continue;
            drawJet(*jp, f);
        }
        // missiles
        setAtmosphere(lit_, f);
        lit_.setF("uEmissive", 0.0f);
        lit_.setF("uSpecular", 0.6f);
        lit_.setF("uFogOn", 1.0f);
        for (const Missile& m : f.battle->missiles()) {
            Vec3 d = normalize(m.vel);
            Quat q = Quat::fromEuler(std::atan2(d.z, d.x), std::asin(clampd(d.y, -1, 1)), 0);
            DrawMesh(missile_, litMat_, bodyMatrix(m.pos, q));
            ++drawCalls_;
        }
    }
    drawShadows(f);
    // transparent: cloud layer then puffs then particles
    if (f.cam.position.y < 2360.0f) {
        drawCloudPuffs(f);
        drawCloudLayer(f);
    } else {
        drawCloudLayer(f);
        drawCloudPuffs(f);
    }
    if (f.battle)
        effects_.draw(f.cam, *f.battle, f.L, particle_.sh, particle_.loc("uIntensity"), world_.edgeLights, f.showRunwayLights);
    EndMode3D();
    EndTextureMode();
    worldMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void Renderer::beginOverlay3D(const Camera3D& cam, float nearP, float farP) {
    BeginTextureMode(scene_);
    rlDrawRenderBatchActive();
    glClear(kGlDepthBufferBit);
    rlSetClipPlanes(nearP, farP);
    BeginMode3D(cam);
}

void Renderer::clearDepth() {
    rlDrawRenderBatchActive();
    glClear(kGlDepthBufferBit);
}

void Renderer::endOverlay3D() {
    EndMode3D();
    EndTextureMode();
}

void Renderer::composite(const FrameParams& f) {
    Rectangle full = {0, 0, static_cast<float>(w_), static_cast<float>(-h_)};
    if (f.bloom && bloomA_.id && bloomC_.id) {
        // bright pass -> half res
        BeginTextureMode(bloomA_);
        ClearBackground(BLACK);
        BeginShaderMode(bright_.sh);
        bright_.setF("uThreshold", 2.2f);
        DrawTexturePro(scene_.texture, full, {0, 0, static_cast<float>(w_ / 2), static_cast<float>(h_ / 2)}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
        auto blur = [&](RenderTexture2D& src, RenderTexture2D& dst, Vector2 dir, int w, int h) {
            BeginTextureMode(dst);
            ClearBackground(BLACK);
            BeginShaderMode(blur_.sh);
            blur_.setV2("uDir", dir);
            DrawTexturePro(src.texture, {0, 0, static_cast<float>(src.texture.width), static_cast<float>(-src.texture.height)},
                           {0, 0, static_cast<float>(w), static_cast<float>(h)}, {0, 0}, 0, WHITE);
            EndShaderMode();
            EndTextureMode();
        };
        blur(bloomA_, bloomB_, {1, 0}, w_ / 2, h_ / 2);
        blur(bloomB_, bloomA_, {0, 1}, w_ / 2, h_ / 2);
        blur(bloomA_, bloomD_, {2, 0}, w_ / 4, h_ / 4);
        blur(bloomD_, bloomC_, {0, 2}, w_ / 4, h_ / 4);
        blur(bloomC_, bloomD_, {3, 0}, w_ / 4, h_ / 4);
        blur(bloomD_, bloomC_, {0, 3}, w_ / 4, h_ / 4);
    }
    BeginShaderMode(composite_.sh);
    composite_.setF("uExposure", f.L.exposure * f.exposureBoost);
    composite_.setF("uBloomStrength", f.bloom ? 0.16f : 0.0f);
    composite_.setF("uVignette", 0.35f);
    composite_.setF("uBlackout", f.blackout);
    composite_.setF("uRedout", f.redout);
    composite_.setF("uDamage", f.damage);
    composite_.setF("uTime", static_cast<float>(fmod(f.time, 100.0)));
    composite_.setF("uGrain", 0.018f);
    composite_.setF("uSpeedFx", f.speedFx);
    int l1 = composite_.loc("uBloom1"), l2 = composite_.loc("uBloom2");
    if (l1 >= 0) SetShaderValueTexture(composite_.sh, l1, bloomA_.texture);
    if (l2 >= 0) SetShaderValueTexture(composite_.sh, l2, bloomC_.texture);
    DrawTexturePro(scene_.texture, full, {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, {0, 0}, 0, WHITE);
    EndShaderMode();
}

void Renderer::drawSunFlare(const FrameParams& f, int w, int h) {
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    float facing = Vector3DotProduct(fwd, f.L.sunDir);
    if (facing < 0.55f) return;
    // terrain occlusion test along the sun ray
    Vec3 o = toSky(f.cam.position), d = toSky(f.L.sunDir);
    for (double t = 50; t < 20000; t *= 1.35) {
        Vec3 p = o + d * t;
        if (p.y < terrain_->surfaceHeight(p.x, p.z)) return;
    }
    Vector2 sp = GetWorldToScreenEx(Vector3Add(f.cam.position, Vector3Scale(f.L.sunDir, 10000.0f)), f.cam, w, h);
    Vector2 c = {w * 0.5f, h * 0.5f};
    float vis = std::clamp((facing - 0.55f) / 0.3f, 0.0f, 1.0f);
    if (sp.x < -200 || sp.x > w + 200 || sp.y < -200 || sp.y > h + 200) return;
    BeginBlendMode(BLEND_ADDITIVE);
    Texture2D g = world_.glow;
    auto blob = [&](float t, float size, Color col) {
        Vector2 p = {sp.x + (c.x - sp.x) * t, sp.y + (c.y - sp.y) * t};
        DrawTexturePro(g, {0, 0, static_cast<float>(g.width), static_cast<float>(g.height)}, {p.x, p.y, size, size}, {size / 2, size / 2}, 0, col);
    };
    unsigned char a = static_cast<unsigned char>(vis * 255);
    blob(0.0f, h * 0.7f, Color{255, 220, 170, static_cast<unsigned char>(a * 0.14f)});
    blob(0.45f, h * 0.05f, Color{120, 200, 255, static_cast<unsigned char>(a * 0.16f)});
    blob(0.75f, h * 0.10f, Color{255, 170, 90, static_cast<unsigned char>(a * 0.10f)});
    blob(1.2f, h * 0.04f, Color{160, 255, 180, static_cast<unsigned char>(a * 0.12f)});
    blob(1.6f, h * 0.16f, Color{120, 140, 255, static_cast<unsigned char>(a * 0.07f)});
    // anamorphic streak
    DrawTexturePro(g, {0, 0, static_cast<float>(g.width), static_cast<float>(g.height)}, {sp.x, sp.y, w * 0.9f, h * 0.025f},
                   {w * 0.45f, h * 0.0125f}, 0, Color{255, 210, 160, static_cast<unsigned char>(a * 0.2f)});
    EndBlendMode();
}

}  // namespace sky
