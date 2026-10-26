#include "render/ShaderLoad.hpp"
#include "Renderer.hpp"
#include "Props.hpp"

#include "Convert.hpp"
#include "MeshBuilder.hpp"
#include "Shaders.hpp"
#include "Skins.hpp"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <string>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
extern "C" void glClear(unsigned int mask);  // GL 1.0 entry point, present in every GL library
extern "C" void glFinish(void);
#endif

namespace sky {

namespace {
constexpr unsigned int kGlDepthBufferBit = 0x00000100;

Shader makeShader(const char* vs, const char* fsBody, bool atmosphere) {
    std::string fs = "#version 330\n";
    if (atmosphere) fs += shaders::kAtmosphere;
    fs += fsBody;
    Shader s = loadShaderGlsl(vs, fs.c_str());
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

Vector3 g_paintC{0, 0, 0};   // third camouflage colour for the jet drawn next
float g_paintRough = 0.42f;
float g_paintMetal = 0.12f;
Vector3 g_paintGlow{0, 0, 0};
float g_noseNumbers = 0, g_serial = 0;   // markings for the jet drawn next
Vector3 g_mark0{0, 0, 0};
long g_tris = 0;   // triangles submitted this frame (benchmark statistics)
long g_draws = 0;
  // mesh draw calls submitted this frame (exact: every mesh goes through drawMeshC)
inline void drawMeshC(const Mesh& m, const Material& mat, const Matrix& t) {
    g_tris += m.triangleCount > 0 ? m.triangleCount : m.vertexCount / 3;
    ++g_draws;
    DrawMesh(m, mat, t);
}

Vector3 paintFor(Team team, int skinIdx, const AircraftProfile& prof, Vector3& b, float& camo) {
    camo = 0;
    g_paintRough = 0.42f;
    g_paintMetal = 0.12f;
    g_paintGlow = {0, 0, 0};
    g_noseNumbers = 0; g_mark0 = {0, 0, 0};
    if (skinIdx > 0) {
        const Skin& k = skin(skinIdx);
        b = k.b;
        g_paintC = k.c;
        camo = static_cast<float>(static_cast<int>(k.pattern));
        g_paintRough = k.roughness;
        g_paintMetal = k.metal;
        g_paintGlow = k.glow;
        g_noseNumbers = k.noseNumbers ? 1.0f : 0.0f;
        g_mark0 = k.stencil;
        return k.a;
    }
    AirframeStyle st = prof.style;
    if (team == Team::Blue) {
        switch (st) {   // linear-space greys close to the real-world schemes of each family
            case AirframeStyle::StealthTwinTail:
                if (prof.id == AircraftId::Su57) { camo = 1; b = {0.07f, 0.09f, 0.12f}; return {0.20f, 0.23f, 0.27f}; }
                b = {0.112f, 0.122f, 0.134f}; return {0.158f, 0.168f, 0.180f};
            case AirframeStyle::StealthSingle: b = {0.085f, 0.09f, 0.098f}; return {0.118f, 0.124f, 0.132f};
            case AirframeStyle::DeltaCanard: b = {0.11f, 0.12f, 0.135f}; return {0.15f, 0.16f, 0.175f};
            case AirframeStyle::ViperSingle: b = {0.15f, 0.16f, 0.17f}; return {0.21f, 0.22f, 0.235f};
            case AirframeStyle::FlankerFamily: camo = 1; b = {0.10f, 0.14f, 0.20f}; return {0.26f, 0.31f, 0.37f};
            case AirframeStyle::Fulcrum: b = {0.16f, 0.18f, 0.20f}; return {0.24f, 0.26f, 0.29f};
            case AirframeStyle::Hornet: b = {0.17f, 0.18f, 0.195f}; return {0.27f, 0.28f, 0.30f};       // naval tactical greys
            case AirframeStyle::Typhoon: b = {0.22f, 0.23f, 0.24f}; return {0.31f, 0.32f, 0.335f};      // light air-superiority grey
            case AirframeStyle::Transport: b = {0.12f, 0.125f, 0.135f}; return {0.16f, 0.165f, 0.175f}; // dark transport grey
            default: break;
        }
    }
    // hostile "aggressor" desert splinter scheme: reads clearly against sky and sand
    camo = 1;
    b = {0.16f, 0.12f, 0.08f};
    g_paintC = {0.36f, 0.30f, 0.21f};
    return {0.36f, 0.30f, 0.21f};
}

/// Horizontal distance from the camera to the nearest point of a chunk group's ground rectangle
/// (a lower bound of the 3D distance to any of its chunks).
float nearestToRect(Vector3 cp, const ChunkGroup& g) {
    double dx = std::max({g.x0 - cp.x, 0.0, cp.x - (g.x0 + g.size)});
    double dz = std::max({g.z0 - cp.z, 0.0, cp.z - (g.z0 + g.size)});
    return static_cast<float>(std::sqrt(dx * dx + dz * dz));
}

Vector3 paintFor(const Jet& j, Vector3& b, float& camo) {
    Vector3 a = paintFor(j.team, j.skin, j.fm->profile(), b, camo);
    g_serial = static_cast<float>(101 + (j.id * 389 + 17) % 899);   // a stable three-digit airframe number
    return a;
}
}  // namespace

long Renderer::triangles() const { return g_tris; }

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

bool Renderer::init(const Terrain& t, const WorldLayout& layout, int width, int height) {
    terrain_ = &t;
    auto t0 = std::chrono::steady_clock::now();
    world_.build(t);
    dressing_.build(t, layout, BuildingStyle::Desert, true);
    for (int s = 0; s < kAirframeStyleCount; ++s) {
        jets_[s] = buildJetModel(static_cast<AirframeStyle>(s));
        TraceLog(LOG_INFO, "SKYFALL: jet model %d: LOD triangles %d / %d / %d", s, jets_[s].triangles(0), jets_[s].triangles(1), jets_[s].triangles(2));
    }
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
    particle_.sh = loadShaderGlsl(kParticleVS, kParticleFS);
    bright_.sh = loadShaderGlsl(kPostVS, kBrightFS);
    blur_.sh = loadShaderGlsl(kPostVS, kBlurFS);
    composite_.sh = loadShaderGlsl(kPostVS, kCompositeFS);
    lumDown_.sh = loadShaderGlsl(kPostVS, kLumDownFS);
    lumAvg_.sh = loadShaderGlsl(kPostVS, kLumAvgFS);
    adaptP_.sh = loadShaderGlsl(kPostVS, kAdaptFS);

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
    shadowP_.sh = loadShaderGlsl(kShadowVS, kShadowFS);
    shadowMat_ = LoadMaterialDefault();
    shadowMat_.shader = shadowP_.sh;
    shadowRT_.id = rlLoadFramebuffer();
    if (shadowRT_.id) {
        rlEnableFramebuffer(shadowRT_.id);
#ifdef __EMSCRIPTEN__
        const int colorFmt = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;  // WebGL 2 cannot render to LUMINANCE
#else
        const int colorFmt = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
#endif
        shadowRT_.texture.id = rlLoadTexture(nullptr, kShadowSize, kShadowSize, colorFmt, 1);
        shadowRT_.texture.width = shadowRT_.texture.height = kShadowSize;
        shadowRT_.texture.format = colorFmt; shadowRT_.texture.mipmaps = 1;
#ifdef __EMSCRIPTEN__
        // WebGL 2 needs a sized depth format (raylib passes the unsized WebGL 1 one)
        {
            GLuint id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            shadowRT_.depth.id = id;
        }
#else
        shadowRT_.depth.id = rlLoadTextureDepth(kShadowSize, kShadowSize, false);
#endif
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
    terrain_p_.sh.locs[SHADER_LOC_MAP_EMISSION] = terrain_p_.loc("uTerrainData");
    terrainMat_.maps[MATERIAL_MAP_EMISSION].texture = world_.terrainData;
    terrain_p_.sh.locs[SHADER_LOC_MAP_HEIGHT] = terrain_p_.loc("uDetailGrad");
    terrainMat_.maps[MATERIAL_MAP_HEIGHT].texture = world_.detailGrad;
    terrain_p_.sh.locs[SHADER_LOC_MAP_OCCLUSION] = terrain_p_.loc("uLandCover");
    terrainMat_.maps[MATERIAL_MAP_OCCLUSION].texture = world_.landCover;
    terrainMat_.shader = terrain_p_.sh;
    bindShadow(lit_, litMat_);
    bindShadow(jet_, jetMat_);
    bindShadow(runway_, runwayMat_);
    applyTerrainInfo();
    resize(width, height);
    return true;
}

void Renderer::applyTerrainInfo() {
    const TerrainInfo& ti = terrain_->info();
    terrain_p_.setF("uBiome", static_cast<float>(static_cast<int>(ti.biome)));
    terrain_p_.setF("uSnowLine", static_cast<float>(ti.snowLine));
    terrain_p_.setF("uTreeLine", static_cast<float>(ti.treeLine));
    terrain_p_.setF("uHighRock", static_cast<float>(ti.highRock));
    terrain_p_.setF("uCloudBase", static_cast<float>(ti.cloudBase));
}

void Renderer::rebuildWorld(const Terrain& t, const WorldLayout& layout) {
    terrain_ = &t;
    world_.unload();
    world_.build(t);
    BuildingStyle style = BuildingStyle::Desert;
    switch (t.info().biome) {
        case Biome::Alpine: style = BuildingStyle::Alpine; break;
        case Biome::Temperate: style = BuildingStyle::Temperate; break;
        case Biome::Tropical: style = BuildingStyle::Tropical; break;
        default: break;
    }
    dressing_.build(t, layout, style, !t.info().real);
    buildClouds(t);
    effects_.clear();
    effects_.init(world_.puff, world_.glow, world_.spark);
    // re-point the materials at the new world textures
    for (Material* m : {&terrainMat_, &waterMat_, &jetMat_, &flameMat_, &cloudMat_, &runwayMat_}) m->maps[MATERIAL_MAP_METALNESS].texture = world_.noise;
    waterMat_.maps[MATERIAL_MAP_NORMAL].texture = world_.depthMap;
    terrainMat_.maps[MATERIAL_MAP_EMISSION].texture = world_.terrainData;
    terrainMat_.maps[MATERIAL_MAP_HEIGHT].texture = world_.detailGrad;
    terrainMat_.maps[MATERIAL_MAP_OCCLUSION].texture = world_.landCover;
    applyTerrainInfo();
}

void Renderer::resize(int width, int height) {
    if (width == w_ && height == h_ && scene_.id) return;
    freeTarget(scene_); freeTarget(bloomA_); freeTarget(bloomB_); freeTarget(bloomC_); freeTarget(bloomD_);
    w_ = width; h_ = height;
    scene_ = makeTarget(w_, h_, true, true);
    hdr_ = scene_.id != 0;
    if (!hdr_) scene_ = makeTarget(w_, h_, false, true);
    // bloom is soft: at Retina / 4K sizes it is built from a quarter-resolution image (same look,
    // a quarter of the pixels)
    int d = w_ > 2000 ? 4 : 2;
    bloomA_ = makeTarget(w_ / d, h_ / d, hdr_, false);
    bloomB_ = makeTarget(w_ / d, h_ / d, hdr_, false);
    bloomC_ = makeTarget(w_ / (d * 2), h_ / (d * 2), hdr_, false);
    bloomD_ = makeTarget(w_ / (d * 2), h_ / (d * 2), hdr_, false);
    if (!lumA_.id && hdr_) {   // eye adaptation: fixed tiny targets, independent of the screen size
        lumA_ = makeTarget(64, 36, true, false);
        lumB_ = makeTarget(1, 1, true, false);
        adapt_[0] = makeTarget(1, 1, true, false);
        adapt_[1] = makeTarget(1, 1, true, false);
        for (RenderTexture2D* t : {&lumA_, &lumB_, &adapt_[0], &adapt_[1]}) SetTextureFilter(t->texture, TEXTURE_FILTER_POINT);
        adaptT_ = -1.0;
    }
}

void Renderer::shutdown() {
    world_.unload();
    dressing_.unload();
    if (trafficCap_) { UnloadMesh(traffic_); traffic_ = Mesh{}; trafficCap_ = 0; }
    for (auto& j : jets_) unloadJetModel(j);
    UnloadMesh(flame_); UnloadMesh(missile_);
    UnloadMesh(tank_); UnloadMesh(truck_); UnloadMesh(aaa_); UnloadMesh(sam_); UnloadMesh(bunker_); UnloadMesh(radar_);
    for (Mesh* m : {&gun_, &artillery_, &oilRig_, &fuelTank_, &hangar_, &command_, &ship_, &powerPlant_, &refinery_, &bridgeDeck_,
                    &bridgePier_, &commTower_, &factory_, &locomotive_, &wagon_, &launcher_, &ammo_, &substation_, &tanker_, &drogue_, &carrier_, &heli_, &heliAttack_, &heliRotor_, &cargo_, &chute_, &forkliftMesh_, &tugMesh_, &fuelTruckMesh_, &radarHeadMesh_})
        UnloadMesh(*m);
    if (obstacleMeshValid_) UnloadMesh(obstacleMesh_);
    if (ringMesh_.vertexCount) { UnloadMesh(ringMesh_); UnloadMesh(beaconMesh_); }
    for (Prog* p : {&sky_, &terrain_p_, &water_, &lit_, &jet_, &flameP_, &cloud_, &runway_, &particle_, &bright_, &blur_, &composite_, &lumDown_, &lumAvg_, &adaptP_})
        UnloadShader(p->sh);
    freeTarget(lumA_); freeTarget(lumB_); freeTarget(adapt_[0]); freeTarget(adapt_[1]);
    freeTarget(scene_); freeTarget(bloomA_); freeTarget(bloomB_); freeTarget(bloomC_); freeTarget(bloomD_);
    for (int k = 0; k < 2; ++k) {
        freeTarget(pipScene_[k]);
        if (pipOut_[k].id) UnloadRenderTexture(pipOut_[k]);
    }
}

void Renderer::buildGroundModels() {
    Color olive = {120, 118, 88, 255}, dark = {60, 62, 50, 255}, sand = {170, 150, 110, 255}, grey = {150, 150, 145, 255};
    {
        MeshBuilder m;  // supply pallet: skid, stacked crates, cargo net straps
        m.box({0, 0.12f, 0}, {2.7f, 0.24f, 2.2f}, Color{110, 96, 70, 255});
        m.box({-0.6f, 0.75f, -0.5f}, {1.2f, 1.0f, 1.0f}, olive);
        m.box({0.6f, 0.75f, -0.5f}, {1.2f, 1.0f, 1.0f}, Color{104, 104, 80, 255});
        m.box({-0.6f, 0.75f, 0.55f}, {1.2f, 1.0f, 1.0f}, Color{96, 100, 76, 255});
        m.box({0.6f, 0.75f, 0.55f}, {1.2f, 1.0f, 1.0f}, olive);
        m.box({0, 1.55f, 0}, {2.2f, 0.6f, 1.9f}, sand);
        for (float x : {-0.9f, 0.0f, 0.9f}) m.box({x, 1.0f, 0}, {0.06f, 1.95f, 2.25f}, dark);
        cargo_ = m.build();
    }
    {
        MeshBuilder m;  // round cargo parachute 30 m above the pallet, with rigging lines
        const int n = 16;
        const float R = 7.5f, top = 32.0f;
        Color cA = {200, 204, 190, 255}, cB = {160, 120, 70, 255};
        for (int i = 0; i < n; ++i) {
            float a0 = 2 * PI * i / n, a1 = 2 * PI * (i + 1) / n;
            Vector3 c = {0, top + 2.2f, 0};
            Vector3 p0 = {cosf(a0) * R, top, sinf(a0) * R}, p1 = {cosf(a1) * R, top, sinf(a1) * R};
            m.tri2(c, p0, p1, i % 2 ? cA : cB);
            Vector3 q0 = {cosf(a0) * R * 0.92f, top - 1.6f, sinf(a0) * R * 0.92f}, q1 = {cosf(a1) * R * 0.92f, top - 1.6f, sinf(a1) * R * 0.92f};
            m.tri2(p0, q0, q1, i % 2 ? cA : cB);
            m.tri2(p0, q1, p1, i % 2 ? cA : cB);
            if (i % 4 == 0) {
                Vector3 d = Vector3Subtract({0, 1.9f, 0}, q0);
                float L = Vector3Length(d);
                m.cylinder({0, 0, 0}, 0.03f, L, 3, dark,
                           MatrixMultiply(MatrixRotate(Vector3Normalize(Vector3CrossProduct({0, 1, 0}, d)), acosf(std::clamp(d.y / L, -1.0f, 1.0f))),
                                          MatrixTranslate(q0.x, q0.y, q0.z)));
            }
        }
        chute_ = m.build();
    }
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
    const Color sandbag = {176, 160, 122, 255}, concrete = {150, 146, 136, 255}, steel = {96, 98, 100, 255};
    const Color rigYellow = {196, 160, 60, 255}, white = {220, 220, 214, 255}, red = {170, 52, 40, 255}, navy = {104, 112, 120, 255};
    {
        MeshBuilder m;  // anti-aircraft gun emplacement: sandbag ring, turntable, twin cannon
        const int seg = 12;
        for (int i = 0; i < seg; ++i) {
            if (i == 3) continue;  // entrance gap
            float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg, am = 0.5f * (a0 + a1);
            m.box({cosf(am) * 4.6f, 0.6f, sinf(am) * 4.6f}, {1.1f, 1.2f, 2.5f}, sandbag, MatrixRotateY(-am));
        }
        m.cylinder({0, 0, 0}, 2.2f, 0.7f, 10, steel);
        m.box({0, 1.25f, 0}, {2.4f, 0.9f, 2.0f}, olive);
        m.box({-0.6f, 1.5f, 0}, {0.6f, 1.2f, 2.2f}, dark);   // shield
        for (float zz : {-0.35f, 0.35f}) m.box({1.9f, 2.4f, zz}, {3.8f, 0.14f, 0.14f}, dark, MatrixRotateZ(0.7f));
        m.box({-2.5f, 0.35f, 2.6f}, {1.0f, 0.7f, 0.6f}, olive);  // ammo boxes
        m.box({-2.9f, 0.35f, 1.8f}, {1.0f, 0.7f, 0.6f}, olive);
        gun_ = m.build();
    }
    {
        MeshBuilder m;  // towed field gun: split trails, wheels, shield, long barrel
        m.box({-2.6f, 0.35f, 0.7f}, {4.2f, 0.3f, 0.3f}, dark, MatrixRotateY(0.25f));
        m.box({-2.6f, 0.35f, -0.7f}, {4.2f, 0.3f, 0.3f}, dark, MatrixRotateY(-0.25f));
        for (float zz : {-1.15f, 1.15f}) m.cylinder({0, zz, 0}, 0.65f, 0.35f, 10, {40, 40, 38, 255}, MatrixMultiply(MatrixRotateX(PI / 2), MatrixTranslate(0, 0.65f, 0)));
        m.box({0.2f, 1.1f, 0}, {0.25f, 1.6f, 2.6f}, olive);   // shield
        m.box({0, 1.0f, 0}, {1.4f, 0.6f, 0.8f}, olive);       // cradle
        m.cylinder({0, 0, 0}, 0.13f, 6.5f, 8, dark, MatrixMultiply(MatrixRotateZ(-PI / 2 + 0.26f), MatrixTranslate(0.2f, 1.2f, 0)));
        artillery_ = m.build();
    }
    {
        MeshBuilder m;  // offshore oil platform (origin at sea level)
        for (int i = 0; i < 4; ++i) {
            float x = (i & 1) ? 13.0f : -13.0f, z = (i & 2) ? 13.0f : -13.0f;
            m.cylinder({x, -25.0f, z}, 1.7f, 44.0f, 10, steel);
            m.box({x * 0.5f, 6.0f, z}, {26.0f, 0.6f, 0.6f}, steel);   // bracing
            m.box({x, 6.0f, z * 0.5f}, {0.6f, 0.6f, 26.0f}, steel);
        }
        m.box({0, 18.5f, 0}, {34.0f, 3.0f, 34.0f}, rigYellow);        // cellar deck
        m.box({0, 23.0f, 0}, {32.0f, 2.2f, 30.0f}, {150, 150, 140, 255});  // main deck
        m.box({-9.0f, 28.0f, -8.0f}, {12.0f, 8.0f, 11.0f}, white);   // living quarters
        m.box({-9.0f, 32.6f, -8.0f}, {12.6f, 1.2f, 11.6f}, red);
        m.cylinder({-9.0f, 33.2f, -8.0f}, 6.2f, 0.4f, 16, {70, 120, 70, 255});   // helideck
        m.cylinder({7.0f, 24.0f, 6.0f}, 5.0f, 30.0f, 4, rigYellow, MatrixIdentity(), 1.2f);   // derrick
        m.box({7.0f, 26.0f, 6.0f}, {11.0f, 3.0f, 11.0f}, {120, 120, 112, 255});  // drill floor
        m.box({16.0f, 30.0f, -10.0f}, {22.0f, 0.8f, 0.8f}, steel, MatrixRotateZ(0.5f));   // flare boom
        m.cylinder({25.5f, 35.6f, -10.0f}, 0.9f, 1.6f, 8, {60, 60, 60, 255});
        m.box({4.0f, 27.0f, -11.0f}, {1.2f, 6.0f, 1.2f}, steel);    // crane
        m.box({10.0f, 30.5f, -11.0f}, {14.0f, 0.6f, 0.6f}, rigYellow, MatrixRotateZ(0.25f));
        for (int k = 0; k < 3; ++k) m.cylinder({-6.0f + k * 5.0f, 24.1f, 11.0f}, 1.8f, 4.0f, 10, white);   // separators
        oilRig_ = m.build();
    }
    {
        MeshBuilder m;  // fuel storage tank with containment berm
        m.cylinder({0, 0, 0}, 9.0f, 11.0f, 20, white);
        m.cylinder({0, 11.0f, 0}, 9.0f, 1.6f, 20, {200, 200, 196, 255}, MatrixIdentity(), 5.0f);
        m.box({9.2f, 5.5f, 0}, {0.4f, 11.0f, 0.8f}, steel);   // ladder
        for (int i = 0; i < 4; ++i)
            m.box({cosf(i * PI / 2) * 13.0f, 0.5f, sinf(i * PI / 2) * 13.0f}, {1.5f, 1.0f, 26.0f}, sandbag, MatrixRotateY(-i * PI / 2 + PI / 2));
        fuelTank_ = m.build();
    }
    {
        MeshBuilder m;  // arched aircraft shelter (quonset), door facing +X
        const int seg = 10;
        const float R = 11.0f, L = 30.0f;
        Color camo = {150, 136, 104, 255};
        for (int i = 0; i < seg; ++i) {
            float a0 = PI * i / seg, a1 = PI * (i + 1) / seg;
            Vector3 p0 = {-L / 2, sinf(a0) * R, cosf(a0) * R}, p1 = {-L / 2, sinf(a1) * R, cosf(a1) * R};
            Vector3 q0 = {L / 2, sinf(a0) * R, cosf(a0) * R}, q1 = {L / 2, sinf(a1) * R, cosf(a1) * R};
            m.quad(p0, q0, q1, p1, camo);
            m.tri({-L / 2, 0, 0}, p0, p1, {128, 116, 90, 255});          // back wall
            m.tri({L / 2 - 0.2f, 0, 0}, q1, q0, {44, 44, 42, 255});      // dark door
        }
        m.box({L / 2 + 6.0f, 0.1f, 0}, {12.0f, 0.2f, 16.0f}, concrete);  // apron
        hangar_ = m.build();
    }
    {
        MeshBuilder m;  // command post: concrete block, roof room, masts, dish, sandbags
        m.box({0, 3.0f, 0}, {16.0f, 6.0f, 12.0f}, concrete);
        m.box({-2.0f, 7.0f, -1.0f}, {7.0f, 2.0f, 6.0f}, {136, 132, 124, 255});
        m.cylinder({5.5f, 6.0f, 4.0f}, 0.15f, 14.0f, 6, steel);
        m.cylinder({-6.0f, 6.0f, -4.0f}, 0.12f, 10.0f, 6, steel);
        m.cylinder({4.0f, 6.0f, -3.5f}, 2.0f, 1.2f, 12, white, MatrixIdentity(), 0.3f);
        for (int i = 0; i < 5; ++i) m.box({-6.0f + i * 3.0f, 0.5f, 7.6f}, {2.6f, 1.0f, 1.0f}, sandbag);
        m.box({0, 1.2f, 6.05f}, {3.0f, 2.4f, 0.2f}, {40, 40, 40, 255});   // door
        command_ = m.build();
    }
    {
        MeshBuilder m;  // patrol boat (origin at the waterline), bow towards +X
        const float hl = 15.0f, hw = 3.2f, hd = 2.4f, fb = 1.6f;
        Vector3 sb = {-hl, -hd * 0.3f, hw}, pb = {-hl, -hd * 0.3f, -hw}, sm = {hl * 0.45f, -hd, hw}, pm = {hl * 0.45f, -hd, -hw};
        Vector3 bow = {hl, fb, 0}, keel = {hl * 0.7f, -hd, 0};
        Vector3 sTop = {-hl, fb, hw}, pTop = {-hl, fb, -hw}, smTop = {hl * 0.45f, fb, hw}, pmTop = {hl * 0.45f, fb, -hw};
        m.quad(sb, sm, smTop, sTop, navy); m.quad(pm, pb, pTop, pmTop, navy);           // sides
        m.tri(sm, keel, bow, navy); m.tri(smTop, sm, bow, navy);                       // bow starboard
        m.tri(keel, pm, bow, navy); m.tri(pm, pmTop, bow, navy);                       // bow port
        m.quad(sTop, smTop, pmTop, pTop, {132, 136, 136, 255}); m.tri(smTop, bow, pmTop, {132, 136, 136, 255});  // deck
        m.quad(pb, sb, sTop, pTop, navy);                                              // transom
        m.box({-3.0f, fb + 1.6f, 0}, {8.0f, 3.2f, 4.6f}, {170, 174, 174, 255});       // superstructure
        m.box({-2.0f, fb + 3.6f, 0}, {4.0f, 0.9f, 3.6f}, {60, 66, 70, 255});           // bridge windows
        m.cylinder({-4.0f, fb + 3.2f, 0}, 0.12f, 6.0f, 6, steel);                      // mast
        m.cylinder({7.5f, fb, 0}, 0.9f, 0.9f, 10, steel);                              // deck gun
        m.box({8.6f, fb + 1.1f, 0}, {2.6f, 0.18f, 0.18f}, dark, MatrixRotateZ(0.35f));
        ship_ = m.build();
    }
    const Color brick = {150, 112, 88, 255}, cool = {196, 192, 184, 255}, rust = {132, 82, 52, 255}, darkSteel = {70, 72, 74, 255};
    {
        MeshBuilder m;  // power plant: turbine hall, two hyperboloid cooling towers, stack
        m.box({0, 9.0f, 0}, {48.0f, 18.0f, 26.0f}, concrete);
        m.box({0, 19.0f, 0}, {50.0f, 2.0f, 28.0f}, {120, 118, 112, 255});
        m.box({-30.0f, 6.0f, 0}, {12.0f, 12.0f, 20.0f}, brick);
        for (float cx : {38.0f, 78.0f}) {
            // hourglass tower from stacked tapered rings
            const float rs[] = {19.0f, 16.0f, 13.5f, 12.5f, 13.0f, 14.0f};
            for (int k = 0; k < 5; ++k)
                m.cylinder({cx, k * 11.0f, 18.0f}, rs[k], 11.0f, 24, cool, MatrixIdentity(), rs[k + 1]);
        }
        m.cylinder({-14.0f, 20.0f, -8.0f}, 2.6f, 60.0f, 12, {170, 160, 150, 255}, MatrixIdentity(), 1.9f);
        for (int i = 0; i < 3; ++i) m.box({-16.0f + i * 16.0f, 2.0f, -24.0f}, {10.0f, 4.0f, 7.0f}, darkSteel);   // transformers
        powerPlant_ = m.build();
    }
    {
        MeshBuilder m;  // refinery: columns, furnaces, pipe racks, flare
        for (int i = 0; i < 4; ++i) m.cylinder({-14.0f + i * 9.0f, 0, -6.0f}, 2.0f + (i % 2) * 0.8f, 26.0f + i * 5.0f, 12, white);
        m.box({8.0f, 7.0f, -6.0f}, {14.0f, 14.0f, 10.0f}, rust);
        m.box({0, 6.0f, 8.0f}, {44.0f, 0.8f, 3.0f}, darkSteel);   // pipe rack
        for (int i = 0; i < 6; ++i) m.box({-20.0f + i * 8.0f, 3.0f, 8.0f}, {0.6f, 6.0f, 3.0f}, darkSteel);
        for (int i = 0; i < 4; ++i) m.cylinder({-20.0f + i * 0.0f, 6.6f + i * 0.0f, 7.0f + i * 0.7f}, 0.35f, 44.0f, 6, {150, 150, 140, 255}, MatrixMultiply(MatrixRotateZ(-PI / 2), MatrixTranslate(-20.0f, 6.6f, 7.0f + i * 0.7f)));
        m.cylinder({26.0f, 0, -14.0f}, 0.8f, 42.0f, 8, {150, 40, 30, 255});   // flare stack
        m.box({0, 0.2f, 0}, {60.0f, 0.4f, 40.0f}, concrete);
        refinery_ = m.build();
    }
    {
        MeshBuilder m;  // bridge deck, unit length along +X (scaled per bridge), girders and parapets
        m.box({0, -0.9f, 0}, {1.0f, 1.8f, 9.0f}, concrete);
        m.box({0, -2.6f, -2.4f}, {1.0f, 1.8f, 1.2f}, {120, 116, 108, 255});
        m.box({0, -2.6f, 2.4f}, {1.0f, 1.8f, 1.2f}, {120, 116, 108, 255});
        m.box({0, 0.6f, -4.3f}, {1.0f, 1.2f, 0.4f}, {175, 172, 165, 255});
        m.box({0, 0.6f, 4.3f}, {1.0f, 1.2f, 0.4f}, {175, 172, 165, 255});
        m.box({0, 0.05f, 0}, {1.0f, 0.1f, 6.0f}, {70, 66, 60, 255});
        bridgeDeck_ = m.build();
    }
    {
        MeshBuilder m;  // bridge pier: 90 m tall (sinks into the ground where the valley is shallow)
        m.box({0, -45.0f, 0}, {3.2f, 90.0f, 7.0f}, {160, 154, 142, 255});
        m.box({0, -3.0f, 0}, {4.0f, 2.0f, 9.0f}, {150, 146, 136, 255});
        bridgePier_ = m.build();
    }
    {
        MeshBuilder m;  // lattice communications mast with dishes
        m.cylinder({0, 0, 0}, 3.2f, 92.0f, 4, {200, 70, 60, 255}, MatrixIdentity(), 0.5f);
        for (int k = 0; k < 6; ++k) m.box({0, 8.0f + k * 14.0f, 0}, {5.0f - k * 0.6f, 0.5f, 5.0f - k * 0.6f}, white);
        for (int k = 0; k < 3; ++k) m.cylinder({1.8f, 30.0f + k * 18.0f, 0}, 1.6f, 0.6f, 12, white, MatrixMultiply(MatrixRotateZ(-PI / 2), MatrixTranslate(1.8f, 30.0f + k * 18.0f, 0)), 0.3f);
        m.box({6.0f, 2.0f, 6.0f}, {6.0f, 4.0f, 4.0f}, concrete);
        commTower_ = m.build();
    }
    {
        MeshBuilder m;  // factory: saw-tooth roof hall + chimney
        m.box({0, 6.0f, 0}, {60.0f, 12.0f, 32.0f}, brick);
        for (int i = 0; i < 6; ++i) {
            float x0 = -30.0f + i * 10.0f;
            m.quad({x0, 12.0f, -16.0f}, {x0, 12.0f, 16.0f}, {x0 + 10.0f, 17.0f, 16.0f}, {x0 + 10.0f, 17.0f, -16.0f}, {120, 120, 116, 255});
            m.quad({x0 + 10.0f, 17.0f, -16.0f}, {x0 + 10.0f, 17.0f, 16.0f}, {x0 + 10.0f, 12.0f, 16.0f}, {x0 + 10.0f, 12.0f, -16.0f}, {150, 170, 180, 255});
        }
        m.cylinder({-24.0f, 0, -20.0f}, 2.2f, 38.0f, 12, brick, MatrixIdentity(), 1.6f);
        m.box({36.0f, 4.0f, 0}, {12.0f, 8.0f, 20.0f}, concrete);
        factory_ = m.build();
    }
    {
        MeshBuilder m;  // diesel locomotive (along +X)
        m.box({0, 1.6f, 0}, {19.0f, 0.8f, 3.0f}, darkSteel);
        m.box({-1.0f, 3.4f, 0}, {15.0f, 2.8f, 2.9f}, {150, 60, 40, 255});
        m.box({7.6f, 3.6f, 0}, {3.4f, 3.2f, 3.0f}, {160, 66, 44, 255});
        m.box({8.6f, 4.4f, 0}, {1.2f, 1.0f, 2.6f}, {40, 50, 60, 255});
        for (float x : {-6.5f, 6.5f}) m.box({x, 0.6f, 0}, {4.0f, 1.2f, 2.6f}, {40, 40, 40, 255});
        locomotive_ = m.build();
    }
    {
        MeshBuilder m;  // armoured freight wagon
        m.box({0, 1.3f, 0}, {16.0f, 0.6f, 2.9f}, darkSteel);
        m.box({0, 3.0f, 0}, {15.0f, 2.8f, 2.9f}, {104, 98, 72, 255});
        m.box({0, 4.5f, 0}, {14.0f, 0.3f, 2.6f}, {86, 82, 60, 255});
        for (float x : {-5.5f, 5.5f}) m.box({x, 0.5f, 0}, {3.0f, 1.0f, 2.5f}, {40, 40, 40, 255});
        wagon_ = m.build();
    }
    {
        MeshBuilder m;  // mobile missile launcher: 8x8 truck with raised missile
        m.box({0, 1.2f, 0}, {13.0f, 1.2f, 3.0f}, olive);
        m.box({5.2f, 2.6f, 0}, {2.6f, 2.0f, 2.9f}, olive);
        for (int i = 0; i < 4; ++i)
            for (float z : {-1.4f, 1.4f}) m.cylinder({-4.5f + i * 2.6f, 0.6f, z}, 0.6f, 0.4f, 8, {30, 30, 30, 255}, MatrixMultiply(MatrixRotateX(PI / 2), MatrixTranslate(-4.5f + i * 2.6f, 0.6f, z)));
        m.cylinder({0, 0, 0}, 0.55f, 11.5f, 10, white, MatrixMultiply(MatrixRotateZ(-PI / 2 + 0.6f), MatrixTranslate(-5.0f, 2.2f, 0)), 0.2f);
        launcher_ = m.build();
    }
    {
        MeshBuilder m;  // earth-covered ammunition bunker (igloo) with concrete face
        const int seg = 8;
        const float R = 7.0f, L = 22.0f;
        Color earth = {150, 136, 100, 255};
        for (int i = 0; i < seg; ++i) {
            float a0 = PI * i / seg, a1 = PI * (i + 1) / seg;
            m.quad({-L / 2, sinf(a0) * R, cosf(a0) * R * 1.3f}, {L / 2, sinf(a0) * R, cosf(a0) * R * 1.3f},
                   {L / 2, sinf(a1) * R, cosf(a1) * R * 1.3f}, {-L / 2, sinf(a1) * R, cosf(a1) * R * 1.3f}, earth);
        }
        m.box({L / 2, 3.0f, 0}, {0.8f, 6.0f, 14.0f}, concrete);
        m.box({L / 2 + 0.45f, 2.2f, 0}, {0.2f, 4.4f, 5.0f}, {70, 74, 70, 255});
        ammo_ = m.build();
    }
    {
        MeshBuilder m;  // transformer yard
        m.box({0, 0.2f, 0}, {40.0f, 0.4f, 30.0f}, {120, 118, 110, 255});
        for (int i = 0; i < 3; ++i) {
            m.box({-12.0f + i * 12.0f, 2.5f, -4.0f}, {6.0f, 5.0f, 5.0f}, darkSteel);
            for (int k = 0; k < 3; ++k) m.cylinder({-14.0f + i * 12.0f + k * 2.0f, 5.0f, -4.0f}, 0.3f, 2.2f, 6, {200, 190, 170, 255});
        }
        for (int i = 0; i < 4; ++i) m.box({-15.0f + i * 10.0f, 8.0f, 8.0f}, {0.6f, 16.0f, 0.6f}, darkSteel);
        m.box({0, 15.5f, 8.0f}, {32.0f, 0.5f, 0.5f}, darkSteel);
        substation_ = m.build();
    }
    {
        // helicopters (flying targets): bodies without the main rotor, which spins separately
        MeshBuilder a, b, r;
        buildHelicopter(a, MatrixIdentity(), Color{92, 98, 84, 255}, false, false);
        buildHelicopter(b, MatrixIdentity(), Color{70, 76, 66, 255}, true, false);
        heli_ = a.build();
        heliAttack_ = b.build();
        Color blade = {40, 42, 44, 255};
        for (int k = 0; k < 4; ++k) r.box({3.9f, 0, 0}, {7.8f, 0.07f, 0.5f}, blade, MatrixRotateY(k * PI / 2));
        r.cylinder({0, -0.15f, 0}, 0.45f, 0.3f, 8, blade);
        heliRotor_ = r.build();
    }
    {
        MeshBuilder m;  // four-engine tanker / transport (original), nose towards +X
        Color grey = {150, 154, 158, 255}, dgrey = {110, 114, 118, 255};
        m.cylinder({0, 0, 0}, 3.0f, 44.0f, 16, grey, MatrixMultiply(MatrixRotateZ(-PI / 2), MatrixTranslate(-22.0f, 0, 0)));
        m.cylinder({0, 0, 0}, 3.0f, 6.0f, 16, grey, MatrixMultiply(MatrixRotateZ(-PI / 2), MatrixTranslate(22.0f, 0, 0)), 0.6f);   // nose
        m.cylinder({0, 0, 0}, 3.0f, 9.0f, 16, grey, MatrixMultiply(MatrixRotateZ(PI / 2), MatrixTranslate(-22.0f, 0, 0)), 0.8f);   // tail cone
        // high wing with slight sweep
        m.quad({4.0f, 2.6f, -1.0f}, {4.0f, 2.6f, 1.0f}, {-4.0f, 3.4f, 25.0f}, {-8.0f, 3.4f, 25.0f}, grey);
        m.quad({4.0f, 2.6f, 1.0f}, {4.0f, 2.6f, -1.0f}, {-8.0f, 3.4f, -25.0f}, {-4.0f, 3.4f, -25.0f}, grey);
        m.quad({-4.0f, 3.4f, 25.0f}, {4.0f, 2.6f, 1.0f}, {4.0f, 2.6f, -1.0f}, {-8.0f, 3.4f, 25.0f}, dgrey);
        for (float z : {-15.0f, -8.0f, 8.0f, 15.0f})
            m.cylinder({0, 0, 0}, 1.3f, 5.0f, 12, dgrey, MatrixMultiply(MatrixRotateZ(-PI / 2), MatrixTranslate(-2.0f + std::fabs(z) * -0.15f, 1.2f, z)));
        m.quad({-22.0f, 2.0f, 0.3f}, {-27.0f, 11.0f, 0.3f}, {-31.0f, 11.0f, 0.3f}, {-30.0f, 2.0f, 0.3f}, grey);    // fin
        m.quad({-30.0f, 2.0f, -0.3f}, {-31.0f, 11.0f, -0.3f}, {-27.0f, 11.0f, -0.3f}, {-22.0f, 2.0f, -0.3f}, grey);
        m.quad({-27.0f, 11.0f, 0}, {-31.0f, 11.0f, 9.0f}, {-33.0f, 11.0f, 9.0f}, {-31.0f, 11.0f, 0}, grey);        // T-tail
        m.quad({-27.0f, 11.0f, 0}, {-31.0f, 11.0f, 0}, {-33.0f, 11.0f, -9.0f}, {-31.0f, 11.0f, -9.0f}, grey);
        m.box({-26.0f, -2.8f, 0}, {2.5f, 1.2f, 1.6f}, dgrey);   // hose drum fairing
        tanker_ = m.build();
    }
    {
        MeshBuilder m;  // drogue basket (cone opening towards -X)
        m.cylinder({0, 0, 0}, 0.25f, 1.6f, 12, {60, 60, 60, 255}, MatrixMultiply(MatrixRotateZ(PI / 2), MatrixTranslate(0.6f, 0, 0)), 0.45f);
        m.cylinder({0, 0, 0}, 0.45f, 0.4f, 16, {230, 230, 230, 255}, MatrixMultiply(MatrixRotateZ(PI / 2), MatrixTranslate(-1.0f, 0, 0)), 0.45f);
        drogue_ = m.build();
    }
    buildCarrier();
}

// Aircraft carrier (Nimitz-like proportions). Origin at the deck centre on the flight deck
// surface, bow towards +X, port side towards -Z. The waterline is 19.5 m below the deck.
void Renderer::buildCarrier() {
    MeshBuilder m;
    const Color hull = {112, 118, 124, 255}, hullDark = {86, 91, 96, 255}, boot = {26, 26, 28, 255};
    const Color deck = {30, 31, 33, 255}, deckWorn = {38, 39, 41, 255}, white = {222, 222, 214, 255}, yellow = {214, 178, 52, 255};
    const Color island = {124, 130, 136, 255}, glass = {28, 34, 40, 255}, red = {150, 40, 32, 255};
    const float wl = -19.5f;
    // hull cross-sections: x, deck-edge half width, waterline half width, keel half width
    struct Sec { float x, top, water, keel; };
    const Sec secs[] = {{-160, 21, 19, 14}, {-120, 22, 20.5f, 16}, {-40, 22, 21, 17}, {40, 22, 21, 16.5f}, {95, 20, 17, 11},
                        {128, 15, 10, 5}, {150, 9, 4.5f, 1.5f}, {166, 2.5f, 0.6f, 0.2f}};
    const int ns = sizeof(secs) / sizeof(secs[0]);
    const float yTop = -2.0f, yKeel = wl - 11.0f, yBoot = wl + 1.2f;
    for (int i = 0; i + 1 < ns; ++i) {
        const Sec &a = secs[i], &b = secs[i + 1];
        for (float sd : {1.0f, -1.0f}) {
            Vector3 at = {a.x, yTop, sd * a.top}, bt = {b.x, yTop, sd * b.top};
            Vector3 ab = {a.x, yBoot, sd * a.water}, bb = {b.x, yBoot, sd * b.water};
            Vector3 aw = {a.x, wl - 0.6f, sd * a.water}, bw = {b.x, wl - 0.6f, sd * b.water};
            Vector3 ak = {a.x, yKeel, sd * a.keel}, bk = {b.x, yKeel, sd * b.keel};
            if (sd > 0) {
                m.quad(at, ab, bb, bt, i % 2 ? hull : hullDark);
                m.quad(ab, aw, bw, bb, boot);
                m.quad(aw, ak, bk, bw, {40, 30, 30, 255});
            } else {
                m.quad(at, bt, bb, ab, i % 2 ? hull : hullDark);
                m.quad(ab, bb, bw, aw, boot);
                m.quad(aw, bw, bk, ak, {40, 30, 30, 255});
            }
        }
    }
    // stern (transom) with the hangar-deck openings
    m.quad({-160, yTop, -21}, {-160, yTop, 21}, {-160, yKeel, 14}, {-160, yKeel, -14}, hullDark);
    m.box({-160.2f, -7.0f, 0}, {0.6f, 5.0f, 26.0f}, {30, 32, 34, 255});
    // flight deck slab: main deck, tapered bow, angled-deck sponson to port, island sponson to starboard
    const float dt = 0.0f, db = -2.0f;
    auto slab = [&](std::vector<Vector2> poly, Color c) {   // convex polygon in (x, z), CCW seen from above
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
            m.tri({poly[0].x, dt, poly[0].y}, {poly[i + 1].x, dt, poly[i + 1].y}, {poly[i].x, dt, poly[i].y}, c);
            m.tri({poly[0].x, db, poly[0].y}, {poly[i].x, db, poly[i].y}, {poly[i + 1].x, db, poly[i + 1].y}, hullDark);
        }
        for (size_t i = 0; i < poly.size(); ++i) {
            Vector2 a = poly[i], b = poly[(i + 1) % poly.size()];
            m.quad({a.x, dt, a.y}, {b.x, dt, b.y}, {b.x, db, b.y}, {a.x, db, a.y}, hullDark);
        }
    };
    slab({{-166, -24}, {-166, 24}, {120, 24}, {166, 7}, {166, -7}, {120, -24}}, deck);
    slab({{-150, -24}, {40, -24}, {-10, -38}, {-140, -38}}, deck);          // angled deck sponson (port)
    slab({{-60, 24}, {20, 24}, {14, 36}, {-54, 36}}, deck);                 // island sponson (starboard)
    // deck markings (a few cm above the deck)
    const float my = 0.04f;
    auto stripe = [&](Vector2 a, Vector2 b, float w, Color c) {
        Vector2 d = Vector2Normalize(Vector2Subtract(b, a));
        Vector2 n = {-d.y * w * 0.5f, d.x * w * 0.5f};
        m.quad({a.x - n.x, my, a.y - n.y}, {a.x + n.x, my, a.y + n.y}, {b.x + n.x, my, b.y + n.y}, {b.x - n.x, my, b.y - n.y}, c);
    };
    // angled deck: landing area edges and dashed centreline (axis 9 degrees to port)
    Vector2 la0 = {-160, -6}, la1 = {30, -36};
    Vector2 ax = Vector2Normalize(Vector2Subtract(la1, la0)), nx = {-ax.y, ax.x};
    stripe(Vector2Add(la0, Vector2Scale(nx, 12)), Vector2Add(la1, Vector2Scale(nx, 12)), 0.5f, white);
    stripe(Vector2Subtract(la0, Vector2Scale(nx, 12)), Vector2Subtract(la1, Vector2Scale(nx, 12)), 0.5f, white);
    for (int k = 0; k < 12; ++k) {
        float t0 = k / 12.0f, t1 = t0 + 0.045f;
        stripe(Vector2Lerp(la0, la1, t0), Vector2Lerp(la0, la1, t1), 0.6f, white);
    }
    // foul line and arresting-wire area
    stripe({-90, -24}, {-60, 24}, 0.35f, yellow);
    {
        const DeckInfo wires;   // the arresting wires at the same distances the flight model uses
        float len = Vector2Length(Vector2Subtract(la1, la0));
        for (int w = 0; w < 4; ++w) {
            float f = static_cast<float>(wires.wireT[w]) / len;
            stripe(Vector2Add(Vector2Lerp(la0, la1, f), Vector2Scale(nx, 11)), Vector2Subtract(Vector2Lerp(la0, la1, f), Vector2Scale(nx, 11)), 0.15f, {20, 20, 20, 255});
        }
    }
    // bow catapults (cat 1 is the player's: on the deck axis), shuttle tracks and hold-back lines
    for (float cz : {0.0f, -13.0f}) {
        stripe({40, cz}, {150, cz}, 0.35f, {24, 24, 26, 255});
        stripe({40, cz - 3}, {150, cz - 3}, 0.12f, yellow);
        stripe({40, cz + 3}, {150, cz + 3}, 0.12f, yellow);
        // jet blast deflector raised behind the launch position
        m.box({26.0f, 2.2f, cz}, {0.6f, 4.6f, 13.0f}, {80, 84, 88, 255}, MatrixMultiply(MatrixRotateZ(-0.95f), MatrixTranslate(0, 0, 0)));
    }
    // deck-edge safety line and elevator outlines (starboard)
    stripe({-166, 23.4f}, {120, 23.4f}, 0.3f, white);
    for (float ex : {-40.0f, 60.0f}) {
        stripe({ex - 13, 10}, {ex + 13, 10}, 0.25f, yellow);
        stripe({ex - 13, 10}, {ex - 13, 23}, 0.25f, yellow);
        stripe({ex + 13, 10}, {ex + 13, 23}, 0.25f, yellow);
    }
    m.box({-100.0f, my * 0.5f, 0}, {30.0f, my, 30.0f}, deckWorn);   // tyre-worn landing area
    // island superstructure (starboard, amidships)
    m.box({-22, 7.5f, 30.5f}, {36, 15, 9}, island);
    m.box({-26, 18.0f, 30.0f}, {24, 6, 8}, island);
    m.box({-26, 18.6f, 30.0f}, {24.2f, 1.6f, 8.2f}, glass);        // bridge windows
    m.box({-30, 23.0f, 30.0f}, {12, 4, 6.5f}, island);
    m.box({-30, 23.4f, 30.0f}, {12.2f, 1.0f, 6.7f}, glass);
    m.cylinder({-28, 25.0f, 30.0f}, 0.5f, 22.0f, 8, {150, 154, 158, 255});                     // main mast
    m.box({-28, 37.0f, 30.0f}, {0.4f, 0.4f, 9.0f}, {150, 154, 158, 255});                       // yardarm
    m.box({-28, 33.0f, 30.0f}, {3.0f, 2.2f, 0.4f}, {170, 174, 178, 255}, MatrixMultiply(MatrixRotateY(0.5f), MatrixTranslate(0, 0, 0)));  // radar
    m.cylinder({-14, 25.0f, 30.0f}, 1.6f, 1.2f, 14, {200, 202, 204, 255});                    // radome base
    m.cylinder({-14, 26.2f, 30.0f}, 1.6f, 1.4f, 14, {220, 222, 224, 255}, MatrixIdentity(), 0.3f);
    m.box({-22, 10.0f, 35.1f}, {6.0f, 3.0f, 0.2f}, red);                                         // hull number board
    // deck-edge gun mounts and antennas
    for (float gx : {-150.0f, 110.0f}) m.cylinder({gx, -2.0f, gx > 0 ? -20.0f : 20.0f}, 1.4f, 3.0f, 10, island);
    carrier_ = m.build();
}

void Renderer::drawCarrier(const FrameParams& f) {
    if (!terrain_ || !terrain_->deck().active) return;
    const DeckInfo& d = terrain_->deck();
    if (Vector3Distance(toRl(d.center), f.cam.position) > 40000) return;
    Matrix R = MatrixRotateY(static_cast<float>(-d.heading));
    Matrix M = MatrixMultiply(R, MatrixTranslate(static_cast<float>(d.center.x), static_cast<float>(d.center.y), static_cast<float>(d.center.z)));
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", f.L.cloudCover > 0.8f ? 0.32f : 0.18f);   // wet deck in the rain
    lit_.setF("uFogOn", 1.0f);
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    drawMeshC(carrier_, litMat_, M);
    lit_.setF("uSpecular", 0.2f);
    ++drawCalls_;
    // parked aircraft along the starboard side and on the angled-deck corner
    setAtmosphere(jet_, f);
    const float spots[][3] = {{-120, 16, 2.4f}, {-100, 16, 2.4f}, {-80, 16, 2.4f}, {95, 16, 2.6f}, {110, 15, 2.7f}, {-20, -30, 0.8f}};
    for (const auto& sp : spots) {
        Matrix L = MatrixMultiply(MatrixRotateY(sp[2]), MatrixTranslate(sp[0], 2.35f, sp[1]));
        drawJetModel(AirframeStyle::StealthSingle, MatrixMultiply(L, M), {0.32f, 0.34f, 0.36f}, {0.26f, 0.28f, 0.30f}, 0, 1, 0, 0, 0, 0, 0, f, true);
    }
}

// ------------------------------------------------------------------------------------------
// Training Academy: course obstacles (rock pillars, natural arches, lattice masts, a suspension
// bridge) built as one static mesh that matches the collision shapes in Terrain, and gate rings
// ------------------------------------------------------------------------------------------
void Renderer::rebuildObstacles() {
    if (obstacleMeshValid_) { UnloadMesh(obstacleMesh_); obstacleMeshValid_ = false; }
    mastLights_.clear();
    obstacleVer_ = terrain_ ? terrain_->obstacleVersion() : -1;
    if (!terrain_ || terrain_->obstacles().empty()) return;
    MeshBuilder m;
    const std::string& locId = terrain_->info().id;
    const bool redRock = locId == "grand_canyon";
    const bool karst = locId == "guilin";
    auto rockCol = [&](unsigned seed, int k, Biome b) {
        Rng r(seed * 977u + k * 131u + 7u);
        int v = static_cast<int>(r.range(-14, 14));
        Color base = b == Biome::Desert || redRock ? Color{176, 120, 82, 255} : karst ? Color{150, 152, 140, 255}
                                                                              : Color{122, 118, 110, 255};
        return Color{static_cast<unsigned char>(std::clamp(base.r + v, 0, 255)), static_cast<unsigned char>(std::clamp(base.g + v, 0, 255)),
                     static_cast<unsigned char>(std::clamp(base.b + v, 0, 255)), 255};
    };
    Biome biome = terrain_->info().biome;
    for (const Obstacle& o : terrain_->obstacles()) {
        Vector3 base = toRl(o.pos);
        switch (o.kind) {
            case Obstacle::Kind::Pillar: {
                // stacked, slightly offset rock drums that taper like the collision cylinder
                const int seg = 7;
                Rng r(o.seed + 11u);
                float y0 = -60.0f;
                for (int k = 0; k < seg; ++k) {
                    float y1 = static_cast<float>(o.height) * (k + 1) / seg;
                    float t0 = std::clamp(y0 / static_cast<float>(o.height), 0.0f, 1.0f), t1 = y1 / static_cast<float>(o.height);
                    float r0 = static_cast<float>(o.radius) * (1.0f - 0.22f * t0), r1 = static_cast<float>(o.radius) * (1.0f - 0.22f * t1);
                    float jx = static_cast<float>(r.range(-0.06, 0.06) * o.radius), jz = static_cast<float>(r.range(-0.06, 0.06) * o.radius);
                    m.cylinder({base.x + jx, base.y + y0, base.z + jz}, r0 * 1.02f, y1 - y0, 14, rockCol(o.seed, k, biome), MatrixIdentity(), r1 * 1.02f);
                    y0 = y1;
                }
                // a cap of scrub on top
                m.cylinder({base.x, base.y + static_cast<float>(o.height), base.z}, static_cast<float>(o.radius) * 0.8f, 6.0f, 12, Color{70, 96, 52, 255},
                           MatrixIdentity(), static_cast<float>(o.radius) * 0.3f);
                break;
            }
            case Obstacle::Kind::Arch: {
                float c = std::cos(static_cast<float>(o.heading)), sn = std::sin(static_cast<float>(o.heading));
                float half = static_cast<float>(o.span) * 0.5f, rad = static_cast<float>(o.radius);
                for (int side = -1; side <= 1; side += 2) {
                    Vector3 leg = {base.x + c * half * side, base.y - 60.0f, base.z + sn * half * side};
                    m.cylinder(leg, rad * 1.05f, static_cast<float>(o.height) + 60.0f - static_cast<float>(o.thickness) * 0.5f, 14, rockCol(o.seed, side + 3, biome),
                               MatrixIdentity(), rad * 0.85f);
                }
                // the span: a box along the span axis at the top (collision: |u| <= half + r, |v| <= 0.9 r)
                Matrix R = MatrixRotateY(-static_cast<float>(o.heading));
                Matrix T = MatrixTranslate(base.x, base.y + static_cast<float>(o.height - o.thickness * 0.5), base.z);
                m.box({0, 0, 0}, {2.0f * (half + rad), static_cast<float>(o.thickness), 1.8f * rad}, rockCol(o.seed, 9, biome), MatrixMultiply(R, T));
                m.box({0, static_cast<float>(o.thickness) * 0.5f + 3.0f, 0}, {2.0f * (half + rad) * 0.8f, 6.0f, 1.5f * rad}, Color{74, 98, 56, 255}, MatrixMultiply(R, T));
                break;
            }
            case Obstacle::Kind::Mast: {
                // red/white banded lattice mast with guy-wire anchors and a beacon on top
                const int bands = 8;
                float H = static_cast<float>(o.height), rad = static_cast<float>(o.radius);
                for (int k = 0; k < bands; ++k) {
                    float y0 = H * k / bands - (k == 0 ? 60.0f : 0.0f), y1 = H * (k + 1) / bands;
                    Color col = (k % 2 == 0) ? Color{205, 64, 52, 255} : Color{232, 232, 228, 255};
                    m.cylinder({base.x, base.y + y0, base.z}, rad, y1 - y0, 4, col, MatrixIdentity(), rad);
                }
                for (int k = 0; k < 3; ++k) {
                    float a = 2.0f * PI * k / 3.0f;
                    m.box({base.x + std::cos(a) * H * 0.35f, base.y + 1.0f, base.z + std::sin(a) * H * 0.35f}, {3, 2, 3}, Color{140, 140, 136, 255});
                }
                mastLights_.push_back({base.x, base.y + H + 2.0f, base.z});
                break;
            }
            case Obstacle::Kind::Deck: {
                // suspension bridge: deck slab (collision box) between two towers
                Matrix R = MatrixRotateY(-static_cast<float>(o.heading));
                float half = static_cast<float>(o.span) * 0.5f;
                Matrix T = MatrixTranslate(base.x, base.y + static_cast<float>(o.height + o.thickness * 0.5), base.z);
                m.box({0, 0, 0}, {static_cast<float>(o.span), static_cast<float>(o.thickness), static_cast<float>(o.width)}, Color{150, 146, 138, 255}, MatrixMultiply(R, T));
                m.box({0, static_cast<float>(o.thickness) * 0.5f + 0.6f, 0}, {static_cast<float>(o.span), 1.2f, static_cast<float>(o.width) * 0.8f}, Color{60, 60, 62, 255},
                      MatrixMultiply(R, T));
                float topY = static_cast<float>(o.height + o.thickness) + 70.0f;
                float c = std::cos(static_cast<float>(o.heading)), sn = std::sin(static_cast<float>(o.heading));
                for (int side = -1; side <= 1; side += 2) {
                    Vector3 tw = {base.x + c * half * side, base.y - 60.0f, base.z + sn * half * side};
                    m.cylinder(tw, static_cast<float>(o.radius), topY + 60.0f, 8, Color{190, 70, 52, 255}, MatrixIdentity(), static_cast<float>(o.radius) * 0.7f);
                    mastLights_.push_back({tw.x, base.y + topY + 2.0f, tw.z});
                }
                // main cables: sagging chain of thin boxes from tower top to tower top
                const int n = 16;
                for (int k = 0; k < n; ++k) {
                    float u0 = -half + o.span * k / n, u1 = -half + o.span * (k + 1) / n;
                    auto ch = [&](float u) { float t = u / half; return static_cast<float>(o.height + o.thickness) + 4.0f + 66.0f * t * t; };
                    for (int lane = -1; lane <= 1; lane += 2) {
                        float v = static_cast<float>(o.width) * 0.5f * lane;
                        Vector3 a = {base.x + c * u0 - sn * v, base.y + ch(u0), base.z + sn * u0 + c * v};
                        Vector3 b = {base.x + c * u1 - sn * v, base.y + ch(u1), base.z + sn * u1 + c * v};
                        Vector3 mid = Vector3Scale(Vector3Add(a, b), 0.5f);
                        Vector3 d = Vector3Subtract(b, a);
                        float L = Vector3Length(d);
                        Matrix RR = MatrixMultiply(MatrixRotateZ(std::atan2(d.y, std::hypot(d.x, d.z))), MatrixRotateY(-std::atan2(d.z, d.x)));
                        m.box({0, 0, 0}, {L, 0.8f, 0.8f}, Color{60, 60, 64, 255}, MatrixMultiply(RR, MatrixTranslate(mid.x, mid.y, mid.z)));
                    }
                }
                break;
            }
        }
    }
    obstacleMesh_ = m.build();
    obstacleMeshValid_ = true;
}

void Renderer::drawObstacles(const FrameParams& f) {
    if (!terrain_) return;
    if (terrain_->obstacleVersion() != obstacleVer_) rebuildObstacles();
    if (!obstacleMeshValid_) return;
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", 0.12f);
    lit_.setF("uFogOn", 1.0f);
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    drawMeshC(obstacleMesh_, litMat_, MatrixIdentity());
    ++drawCalls_;
    // aircraft-warning beacons on masts and bridge towers (blinking)
    bool on = std::fmod(f.time, 1.4) < 0.7;
    if (on)
        for (const Vector3& p : mastLights_)
            if (Vector3Distance(p, f.cam.position) < 15000.0f) lights_.push_back({p, 5.0f, Color{255, 60, 40, 255}});
}

void Renderer::drawGates(const FrameParams& f) {
    if (!f.gates || f.gates->empty()) return;
    if (ringMesh_.vertexCount == 0) {
        // unit ring in the YZ plane (normal +X): square-section torus
        MeshBuilder m;
        const int seg = 48;
        const float r0 = 0.94f, r1 = 1.0f, hw = 0.03f;
        for (int i = 0; i < seg; ++i) {
            float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
            auto P = [&](float a, float r, float x) { return Vector3{x, std::cos(a) * r, std::sin(a) * r}; };
            Color c = WHITE;
            m.quad(P(a0, r1, -hw), P(a1, r1, -hw), P(a1, r1, hw), P(a0, r1, hw), c);   // outer
            m.quad(P(a0, r0, hw), P(a1, r0, hw), P(a1, r0, -hw), P(a0, r0, -hw), c);   // inner
            m.quad(P(a0, r0, -hw), P(a1, r0, -hw), P(a1, r1, -hw), P(a0, r1, -hw), c); // back face
            m.quad(P(a0, r1, hw), P(a1, r1, hw), P(a1, r0, hw), P(a0, r0, hw), c);     // front face
        }
        ringMesh_ = m.build();
        MeshBuilder b;
        b.box({0, 0, 0}, {1, 1, 1}, WHITE);
        beaconMesh_ = b.build();
    }
    setAtmosphere(lit_, f);
    lit_.setF("uSpecular", 0.0f);
    lit_.setF("uFogOn", 0.0f);
    for (const GateMarker& g : *f.gates) {
        if (g.beacon) {
            // waypoint: a thin light column from the ground to 300 m above the waypoint height,
            // a few pixels wide at any range, with a brighter band at the waypoint height
            float dist = Vector3Distance(toRl(g.pos), f.cam.position);
            float w = std::clamp(dist / 260.0f, 3.0f, 60.0f);
            float y0 = static_cast<float>(g.ground), y1 = static_cast<float>(g.pos.y) + 300.0f;
            lit_.setF("uEmissive", g.glow);
            litMat_.maps[MATERIAL_MAP_DIFFUSE].color = g.color;
            drawMeshC(beaconMesh_, litMat_, MatrixMultiply(MatrixScale(w, y1 - y0, w), MatrixTranslate(static_cast<float>(g.pos.x), (y0 + y1) * 0.5f, static_cast<float>(g.pos.z))));
            float b = w * 4.0f;
            drawMeshC(beaconMesh_, litMat_, MatrixMultiply(MatrixMultiply(MatrixScale(b, b * 0.5f, b), MatrixRotateY(PI / 4)),
                                                          MatrixTranslate(static_cast<float>(g.pos.x), static_cast<float>(g.pos.y), static_cast<float>(g.pos.z))));
            drawCalls_ += 2;
            continue;
        }
        float r = std::max(g.radius, 12.0f);
        // keep the ring visibly thick at distance (at least ~2 px)
        float dist = Vector3Distance(toRl(g.pos), f.cam.position);
        float thick = std::max(1.0f, dist / (r * 380.0f));
        Matrix S = MatrixScale(thick, r, r);
        Matrix R = MatrixRotateY(-static_cast<float>(std::atan2(g.dir.z, g.dir.x)));
        Matrix T = MatrixTranslate(static_cast<float>(g.pos.x), static_cast<float>(g.pos.y), static_cast<float>(g.pos.z));
        lit_.setF("uEmissive", g.glow);
        litMat_.maps[MATERIAL_MAP_DIFFUSE].color = g.color;
        drawMeshC(ringMesh_, litMat_, MatrixMultiply(MatrixMultiply(S, R), T));
        ++drawCalls_;
    }
    if (f.corridor) {
        lit_.setF("uEmissive", 1.6f);
        litMat_.maps[MATERIAL_MAP_DIFFUSE].color = Color{255, 220, 120, 255};
        for (const Vec3& p : *f.corridor) {
            float dist = Vector3Distance(toRl(p), f.cam.position);
            float k = std::clamp(dist / 110.0f, 2.5f, 30.0f);   // a few pixels at any range
            drawMeshC(beaconMesh_, litMat_, MatrixMultiply(MatrixScale(k, k, k), MatrixTranslate(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z))));
        }
    }
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uFogOn", 1.0f);
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
}

void Renderer::drawRain(const FrameParams& f) {
    if (f.rain <= 0.0f) return;
    // streaks in a box that follows the camera; they fall at 9 m/s and smear with the relative wind
    const int n = qualityProfile(f.quality, f.detailDrop).rainDrops;
    const float box = 70.0f;
    Vector3 fall = {f.rainWind.x, -9.0f + f.rainWind.y, f.rainWind.z};
    float speed = Vector3Length(fall);
    Vector3 dir = Vector3Scale(fall, 1.0f / std::max(speed, 0.1f));
    float len = std::clamp(speed * 0.022f, 0.5f, 7.0f);
    Vector3 c = f.cam.position;
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ALPHA);
    rlBegin(RL_LINES);
    float sky = 0.45f + 0.4f * f.L.exposure;
    for (int i = 0; i < n; ++i) {
        uint32_t hsh = static_cast<uint32_t>(i) * 2654435761u;
        float rx = ((hsh & 1023) / 1023.0f), ry = (((hsh >> 10) & 1023) / 1023.0f), rz = (((hsh >> 20) & 1023) / 1023.0f);
        // advect then wrap into the box around the camera
        float t = static_cast<float>(f.time) * (0.85f + 0.3f * rx);
        Vector3 p = {rx * box + fall.x * t, ry * box + fall.y * t, rz * box + fall.z * t};
        p.x = c.x - box * 0.5f + std::fmod(std::fmod(p.x - (c.x - box * 0.5f), box) + box, box);
        p.y = c.y - box * 0.5f + std::fmod(std::fmod(p.y - (c.y - box * 0.5f), box) + box, box);
        p.z = c.z - box * 0.5f + std::fmod(std::fmod(p.z - (c.z - box * 0.5f), box) + box, box);
        float d = Vector3Distance(p, c);
        unsigned char a = static_cast<unsigned char>(std::clamp(120.0f * (1.0f - d / (box * 0.7f)) * f.rain, 0.0f, 120.0f));
        if (a < 4) continue;
        rlColor4ub(static_cast<unsigned char>(170 * sky), static_cast<unsigned char>(180 * sky), static_cast<unsigned char>(195 * sky), a);
        rlVertex3f(p.x, p.y, p.z);
        rlVertex3f(p.x - dir.x * len, p.y - dir.y * len, p.z - dir.z * len);
    }
    rlEnd();
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
}

void Renderer::buildClouds(const Terrain& t) {
    Rng rng(31337);
    puffs_.clear();
    for (int c = 0; c < 170; ++c) {
        double base = t.info().cloudBase;
        Vector3 center = V3(rng.range(-28000, 28000), rng.range(base - 200, base + 400), rng.range(-28000, 28000));
        // keep the runway approach mostly clear
        if (std::fabs(center.x - t.runway().center.x) < 3000 && std::fabs(center.z - t.runway().center.z) < 2000) continue;
        // a cumulus is a cluster of large sprites on a common flat base
        int n = 4 + static_cast<int>(rng.uniform() * 6);
        float scale = static_cast<float>(rng.range(0.8, 1.9));
        for (int i = 0; i < n; ++i) {
            CloudPuff p;
            float ox = static_cast<float>(rng.range(-330, 330)) * scale, oz = static_cast<float>(rng.range(-260, 260)) * scale;
            p.size = static_cast<float>(rng.range(380, 620)) * scale;
            float oy = p.size * 0.22f + static_cast<float>(rng.range(0, 90)) * scale * (1.0f - std::fabs(ox) / (360.0f * scale));
            p.pos = {center.x + ox, center.y + oy, center.z + oz};
            p.shade = 1.0f;
            p.rot = 0.0f;
            p.tex = static_cast<int>(rng.next() % 4);
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
    p.setF("uNight", f.L.night);
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

// Sphere vs. the camera's view frustum (side planes only): skips terrain and tree chunks that
// are off to the side or behind, which is most of them at a normal field of view.
bool Renderer::sphereVisible(const FrameParams& f, Vector3 c, float r) const {
    static const bool off = std::getenv("SKYFALL_NOCULL") != nullptr;
    if (off) return Vector3DotProduct(Vector3Subtract(c, f.cam.position), Vector3Subtract(f.cam.target, f.cam.position)) > -r * Vector3Distance(f.cam.target, f.cam.position);
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, f.cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);
    Vector3 d = Vector3Subtract(c, f.cam.position);
    float z = Vector3DotProduct(d, fwd);
    if (z < -r) return false;
    float tv = tanf(f.cam.fovy * DEG2RAD * 0.5f);
    float th = tv * static_cast<float>(std::max(1, w_)) / static_cast<float>(std::max(1, h_));
    float x = std::fabs(Vector3DotProduct(d, right)), y = std::fabs(Vector3DotProduct(d, up));
    if (x > z * th + r * sqrtf(1.0f + th * th)) return false;
    if (y > z * tv + r * sqrtf(1.0f + tv * tv)) return false;
    return true;
}

void Renderer::drawTerrain(const FrameParams& f) {
    setAtmosphere(terrain_p_, f);
    Vector3 cp = f.cam.position;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    rlDisableBackfaceCulling();
    const QualityProfile qp = qualityProfile(f.quality, f.detailDrop);
    float hiDist = qp.terrainHiDist;
    float l0Dist = qp.terrainL0Dist;
    int buildBudget = f.isPip ? 0 : 2;   // at most two new 15.6 m chunks per frame (no hitches)
    auto drawChunk = [&](TerrainChunk& c) {
        Vector3 d = Vector3Subtract(c.center, cp);
        float dist = Vector3Length(d);
        if (dist > c.radius && Vector3DotProduct(d, fwd) < -c.radius) return;
        if (c.maxH < -1.5f) return;   // entirely under the sea
        if (!sphereVisible(f, c.center, c.radius)) return;
        float edge = dist - c.radius;
        const Mesh* m = edge < hiDist ? &c.hi : &c.lo;
        if (edge < l0Dist && world_.ensureL0(c, f.time, buildBudget)) m = &c.l0;
        drawMeshC(*m, terrainMat_, MatrixIdentity());
    };
    // far terrain: a 4 x 4 block of chunks whose nearest point is beyond the 31 m LOD range is one
    // merged draw; nearer blocks are drawn chunk by chunk with their own LOD
    const float groupLoDist = hiDist + 2100.0f;   // every chunk in the block would pick its 125 m mesh anyway
    for (ChunkGroup& g : world_.loGroups) {
        float near = nearestToRect(cp, g);
        if (near >= groupLoDist) {
            if (!g.has) continue;
            Vector3 d = Vector3Subtract(g.center, cp);
            if (Vector3Length(d) > g.radius && Vector3DotProduct(d, fwd) < -g.radius) continue;
            if (!sphereVisible(f, g.center, g.radius)) continue;
            drawMeshC(g.mesh, terrainMat_, MatrixIdentity());
            continue;
        }
        for (int ci : g.chunks) drawChunk(world_.chunks[ci]);
    }
    if (!f.isPip) world_.evictL0(cp, f.time);
    drawMeshC(world_.farRing, terrainMat_, MatrixIdentity());
    ++drawCalls_;
    // sea
    setAtmosphere(water_, f);
    water_.setF("uMapHalf", static_cast<float>(Terrain::kHalfSize));
    water_.setF("uCloudCover", f.L.cloudCover);
    drawMeshC(world_.water, waterMat_, MatrixIdentity());
    ++drawCalls_;
    rlEnableBackfaceCulling();
    if (std::getenv("SKYFALL_TRIS")) {
        static int n = 0;
        if (++n % 5 == 0) TraceLog(LOG_INFO, "SKYFALL: terrain pass %ld tris (water %d, far %d)", g_tris, world_.water.vertexCount / 3, world_.farRing.vertexCount / 3);
    }
}

void Renderer::drawStatics(const FrameParams& f) {
    // runway + taxiways
    setAtmosphere(runway_, f);
    const RunwayInfo& rw = terrain_->runway();
    runway_.setF("uLength", static_cast<float>(rw.length));
    runway_.setF("uWidth", static_cast<float>(rw.width));
    runway_.setF("uKind", 0.0f);
    drawMeshC(world_.runway, runwayMat_, MatrixIdentity());
    runway_.setF("uKind", 1.0f);
    drawMeshC(world_.taxiways, runwayMat_, MatrixIdentity());
    drawCalls_ += 2;
    // airbase, town, trees
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", 0.15f);
    lit_.setF("uFogOn", 1.0f);
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    rlDisableBackfaceCulling();
    drawMeshC(world_.airbase, litMat_, MatrixIdentity());
    drawMeshC(world_.town, litMat_, MatrixIdentity());
    drawDressing(f);
    lit_.setF("uSpecular", 0.0f);
    Vector3 cp = f.cam.position;
    float treeDist = qualityProfile(f.quality, f.detailDrop).treeDist;
    for (const ChunkGroup& g : world_.treeGroups) {   // trees merged 2 x 2 chunks per draw
        if (!g.has || nearestToRect(cp, g) > treeDist) continue;
        if (!sphereVisible(f, g.center, g.radius)) continue;
        drawMeshC(g.mesh, litMat_, MatrixIdentity());
    }
    rlEnableBackfaceCulling();
    drawCalls_ += 2;
    if (!f.isPip) profMark("scenery");
    // parked aircraft on the apron
    for (const Vector3& p : world_.parkedJets) {
        Vector3 pp = {p.x, static_cast<float>(rw.center.y + 2.35), p.z};
        if (Vector3Distance(pp, cp) > qualityProfile(f.quality, f.detailDrop).sceneryDist * 0.5f || !sphereVisible(f, pp, 15.0f)) continue;
        Matrix M = MatrixMultiply(QuaternionToMatrix(toRlQ(Quat::fromEuler(p.y, 0, 0))),
                                  MatrixTranslate(p.x, static_cast<float>(rw.center.y + 2.35), p.z));
        drawJetModel(AirframeStyle::StealthTwinTail, M, {0.158f, 0.168f, 0.180f}, {0.112f, 0.122f, 0.134f}, 0, 1, 0, 0, 0, 0, 0, f);
    }
}

void Renderer::drawGroundTargets(const FrameParams& f) {
    const Battle* gb = f.groundFrom ? f.groundFrom : f.battle;
    if (!gb) return;
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", 0.2f);
    lit_.setF("uFogOn", 1.0f);
    for (const GroundTarget& g : gb->ground()) {
        const Mesh* m = &tank_;
        switch (g.kind) {
            case GroundKind::Tank: m = &tank_; break;
            case GroundKind::Truck: m = &truck_; break;
            case GroundKind::AAA: m = &aaa_; break;
            case GroundKind::SAM: m = &sam_; break;
            case GroundKind::Bunker: m = &bunker_; break;
            case GroundKind::Radar: m = &radar_; break;
            case GroundKind::Gun: m = &gun_; break;
            case GroundKind::Artillery: m = &artillery_; break;
            case GroundKind::OilRig: m = &oilRig_; break;
            case GroundKind::FuelTank: m = &fuelTank_; break;
            case GroundKind::Hangar: m = &hangar_; break;
            case GroundKind::Command: m = &command_; break;
            case GroundKind::Ship: m = &ship_; break;
            case GroundKind::PowerPlant: m = &powerPlant_; break;
            case GroundKind::Refinery: m = &refinery_; break;
            case GroundKind::CommTower: m = &commTower_; break;
            case GroundKind::Factory: m = &factory_; break;
            case GroundKind::Locomotive: m = &locomotive_; break;
            case GroundKind::Wagon: m = &wagon_; break;
            case GroundKind::Launcher: m = &launcher_; break;
            case GroundKind::Ammo: m = &ammo_; break;
            case GroundKind::Substation: m = &substation_; break;
            case GroundKind::Bridge: m = &bridgeDeck_; break;
            default: break;
        }
        bool big = g.kind == GroundKind::OilRig || g.kind == GroundKind::Hangar || g.kind == GroundKind::FuelTank || g.kind == GroundKind::Command ||
                   g.kind == GroundKind::PowerPlant || g.kind == GroundKind::Refinery || g.kind == GroundKind::CommTower ||
                   g.kind == GroundKind::Factory || g.kind == GroundKind::Bridge;
        if (Vector3Distance(toRl(g.pos), f.cam.position) > (big ? 16000.0f : 9000.0f)) continue;
        Matrix R = MatrixRotateY(static_cast<float>(-g.heading));
        Matrix T = MatrixTranslate(static_cast<float>(g.pos.x), static_cast<float>(g.pos.y), static_cast<float>(g.pos.z));
        litMat_.maps[MATERIAL_MAP_DIFFUSE].color = g.alive ? WHITE : Color{40, 36, 32, 255};
        if (g.kind == GroundKind::ParkedJet) {
            Vector3 pb;
            float camo;
            const AircraftProfile& ap = profile(g.aircraft >= 0 && g.aircraft < kAircraftCount ? static_cast<AircraftId>(g.aircraft) : AircraftId::Su35);
            Vector3 pa = paintFor(g.team, 0, ap, pb, camo);
            setAtmosphere(jet_, f);
            Matrix M = MatrixMultiply(R, MatrixTranslate(static_cast<float>(g.pos.x), static_cast<float>(g.pos.y + 2.35), static_cast<float>(g.pos.z)));
            drawJetModel(ap.style, M, pa, pb, camo, 1, 0, 0, 0, 0, g.alive ? 0.0f : 1.0f, f);
            setAtmosphere(lit_, f);
            continue;
        }
        if (g.kind == GroundKind::Cargo) {
            // pallet: crates under a cargo net on a skid; a round parachute while it descends
            if (!g.alive) continue;
            drawMeshC(cargo_, litMat_, MatrixMultiply(R, T));
            ++drawCalls_;
            if (g.chuteV > 0) {
                float sway = 0.08f * std::sin(static_cast<float>(f.time) * 1.3f + static_cast<float>(g.pos.x));
                drawMeshC(chute_, litMat_, MatrixMultiply(MatrixMultiply(MatrixRotateZ(sway), R), T));
                ++drawCalls_;
            }
            continue;
        }
        if (g.kind == GroundKind::Bridge) {
            float L = static_cast<float>(std::max(20.0, g.length));
            if (g.alive) {
                drawMeshC(bridgeDeck_, litMat_, MatrixMultiply(MatrixMultiply(MatrixScale(L, 1, 1), R), T));
            } else {
                // collapsed: the two halves hinge down from the abutments into the valley
                for (int side = -1; side <= 1; side += 2) {
                    // unit deck -> half span, outer end at the hinge, rotated so the broken end drops, then to the abutment
                    Matrix half = MatrixMultiply(MatrixScale(L * 0.48f, 1, 1), MatrixTranslate(-side * 0.24f * L, 0, 0));
                    half = MatrixMultiply(half, MatrixRotateZ(side * 0.42f));
                    half = MatrixMultiply(half, MatrixTranslate(side * 0.5f * L, 0, 0));
                    drawMeshC(bridgeDeck_, litMat_, MatrixMultiply(MatrixMultiply(half, R), T));
                }
            }
            int piers = std::max(1, static_cast<int>(L / 36.0f));
            for (int k = 1; k < piers; ++k) {
                float x = -L / 2 + L * k / piers;
                if (!g.alive && std::fabs(x) < L * 0.2f) continue;
                drawMeshC(bridgePier_, litMat_, MatrixMultiply(MatrixMultiply(MatrixTranslate(x, 0, 0), R), T));
            }
            drawCalls_ += piers + 1;
            continue;
        }
        if (g.kind == GroundKind::Helicopter) {
            // tilted nose-down in forward flight; the main rotor spins (stops on the wreck)
            Matrix tilt = MatrixRotateZ(g.airborne() ? -0.10f : 0.0f);
            Matrix body = MatrixMultiply(MatrixMultiply(tilt, R), T);
            drawMeshC(g.attack ? heliAttack_ : heli_, litMat_, body);
            float hub = g.attack ? 4.12f : 4.9f;
            float spin = g.alive ? static_cast<float>(f.time * 21.0) : 0.6f;
            drawMeshC(heliRotor_, litMat_, MatrixMultiply(MatrixMultiply(MatrixRotateY(spin), MatrixTranslate(g.attack ? -0.2f : -0.4f, hub, 0)), body));
            drawCalls_ += 2;
            if (g.alive && f.L.night > 0.2f) {
                Vector3 c = toRl(g.pos);
                if (std::fmod(f.time + g.pathAngle, 1.2) < 0.15) lights_.push_back({Vector3Add(c, {0, 1.0f, 0}), 1.2f, Color{255, 40, 30, 255}});
            }
            continue;
        }
        drawMeshC(*m, litMat_, MatrixMultiply(R, T));
        ++drawCalls_;
    }
    drawTanker(f);
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
}

void Renderer::drawDressing(const FrameParams& f) {
    Vector3 cp = f.cam.position;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    float maxDist = qualityProfile(f.quality, f.detailDrop).sceneryDist;
    for (const Dressing::Piece& p : dressing_.statics) {
        Vector3 d = Vector3Subtract(p.center, cp);
        float dist = Vector3Length(d);
        if (dist - p.radius > maxDist) continue;
        if (dist > p.radius && Vector3DotProduct(d, fwd) < -p.radius) continue;
        if (!sphereVisible(f, p.center, p.radius)) continue;   // outside the view frustum
        drawMeshC(p.mesh, litMat_, MatrixIdentity());
        ++drawCalls_;
    }
    const WorldLayout* L = dressing_.layout;
    if (!L) return;
    if (!f.isPip) profMark("scenery");
    drawTraffic(f);
    if (!f.isPip) profMark("traffic");
    if (!f.isPip) drawAirbaseLife(f);
    if (!f.isPip) profMark("airbase life");
    if (f.L.night > 0.05f && !f.isPip) pushSceneLights(f);
    const Battle* gb = f.groundFrom ? f.groundFrom : f.battle;
    // factory halls (a mission may own them as targets, then they are drawn as ground targets)
    for (const Vec3& fc : L->factories) {
        bool owned = false;
        if (gb) for (const GroundTarget& g : gb->ground()) if (g.kind == GroundKind::Factory && std::hypot(g.pos.x - fc.x, g.pos.z - fc.z) < 20) owned = true;
        if (owned || Vector3Distance(toRl(fc), cp) > maxDist || !sphereVisible(f, toRl(fc), 90.0f)) continue;
        drawMeshC(factory_, litMat_, MatrixTranslate(static_cast<float>(fc.x), static_cast<float>(fc.y), static_cast<float>(fc.z)));
        ++drawCalls_;
    }
    // spinning wind-turbine rotors
    if (dressing_.rotor.vertexCount)
        for (size_t i = 0; i < dressing_.rotorHubs.size(); ++i) {
            Vector3 h = dressing_.rotorHubs[i];
            if (Vector3Distance(h, cp) > maxDist * 0.7f || !sphereVisible(f, h, 60.0f)) continue;
            Matrix M = MatrixMultiply(MatrixMultiply(MatrixRotateX(static_cast<float>(f.time * 1.3 + i * 0.7)), MatrixRotateY(-dressing_.rotorYaw[i])),
                                      MatrixTranslate(h.x, h.y, h.z));
            drawMeshC(dressing_.rotor, litMat_, M);
            ++drawCalls_;
        }
    // ambient freight train (only when no mission train is on the line)
    bool missionTrain = false;
    if (gb) for (const GroundTarget& g : gb->ground()) if (groundOnRail(g.kind)) missionTrain = true;
    double len = L->railway.length();
    if (!missionTrain && len > 2000) {
        double lead = std::fmod(f.time * 17.0, len);
        for (int k = 0; k < 13; ++k) {
            double sk = lead - (k == 0 ? 0.0 : 21.0 + 17.0 * (k - 1));
            if (sk < 0) break;
            Vec3 tan;
            Vec3 p = L->railway.at(sk, &tan);
            p.y = std::max(p.y, terrain_->surfaceHeight(p.x, p.z));
            if (Vector3Distance(toRl(p), cp) > maxDist * 0.6f || !sphereVisible(f, toRl(p), 20.0f)) continue;
            float yaw = static_cast<float>(std::atan2(tan.z, tan.x));
            Matrix M = MatrixMultiply(MatrixRotateY(-yaw), MatrixTranslate(static_cast<float>(p.x), static_cast<float>(p.y + 0.4), static_cast<float>(p.z)));
            drawMeshC(k == 0 ? locomotive_ : wagon_, litMat_, M);
            ++drawCalls_;
        }
    }
}

namespace {
constexpr int kTrafficVerts = 90000;
}

void Renderer::drawTraffic(const FrameParams& f) {
    const WorldLayout* L = dressing_.layout;
    if (!L || L->cars.empty()) return;
    if (carTpl_[0].pos.empty()) {
        const Color marker = {255, 0, 255, 255};
        for (int k = 0; k < 6; ++k) {
            MeshBuilder m;
            buildCarBox(m, MatrixIdentity(), marker, k);
            carTpl_[k].pos = m.pos; carTpl_[k].nrm = m.nrm; carTpl_[k].col = m.col;
        }
    }
    if (trafficCap_ == 0) {
        traffic_ = Mesh{};
        traffic_.vertexCount = kTrafficVerts;
        traffic_.triangleCount = kTrafficVerts / 3;
        traffic_.vertices = static_cast<float*>(MemAlloc(sizeof(float) * 3 * kTrafficVerts));
        traffic_.normals = static_cast<float*>(MemAlloc(sizeof(float) * 3 * kTrafficVerts));
        traffic_.colors = static_cast<unsigned char*>(MemAlloc(4 * kTrafficVerts));
        UploadMesh(&traffic_, true);
        trafficCap_ = kTrafficVerts;
    }
    Vector3 cp = f.cam.position;
    const float maxDist = std::min(6500.0f, qualityProfile(f.quality, f.detailDrop).sceneryDist * 0.5f);
    // the vertex buffer is rebuilt once per frame (the PiP reuses it)
    int n = 0;
    {
        float* P = traffic_.vertices;
        float* N = traffic_.normals;
        unsigned char* C = traffic_.colors;
        bool night = f.L.night > 0.3f;
        for (const Car& c : L->cars) {
            Vec3 fw;
            Vec3 p = L->carAt(c, f.time, &fw);
            double gy = terrain_->surfaceHeight(p.x, p.z);
            p.y = std::max(p.y - 0.1, gy);
            Vector3 rp = toRl(p);
            float dist = Vector3Distance(rp, cp);
            if (dist > maxDist || !sphereVisible(f, rp, 8.0f)) continue;
            const CarTemplate& tp = carTpl_[std::min<int>(c.kind, 5)];
            size_t nv = dist < 1500.0f ? tp.pos.size() / 3 : 36;
            if (n + static_cast<int>(nv) > trafficCap_) break;
            float cs = static_cast<float>(fw.x), sn = static_cast<float>(fw.z);
            Color body = sceneCarColor(c.color);
            if (c.kind == 3) body = Color{214, 196, 60, 255};
            if (nv == 36 && tp.pos.size() / 3 != 36) {
                // far: one box the size of the vehicle
                static CarTemplate far[6];
                CarTemplate& ft = far[std::min<int>(c.kind, 5)];
                if (ft.pos.empty()) {
                    int k = std::min<int>(c.kind, 5);
                    float len = k == 3 ? 11.5f : (k == 2 || k == 4 ? 7.0f : 4.4f), hgt = k >= 2 ? 3.0f : 1.5f;
                    MeshBuilder mb;
                    mb.box({0, hgt * 0.5f, 0}, {len, hgt, 2.2f}, WHITE);
                    ft.pos = mb.pos; ft.nrm = mb.nrm;
                }
                for (size_t v = 0; v < 36; ++v) {
                    float lx = ft.pos[v * 3], ly = ft.pos[v * 3 + 1], lz = ft.pos[v * 3 + 2];
                    P[(n + v) * 3] = rp.x + lx * cs - lz * sn; P[(n + v) * 3 + 1] = rp.y + ly; P[(n + v) * 3 + 2] = rp.z + lx * sn + lz * cs;
                    float nx = ft.nrm[v * 3], nz = ft.nrm[v * 3 + 2];
                    N[(n + v) * 3] = nx * cs - nz * sn; N[(n + v) * 3 + 1] = ft.nrm[v * 3 + 1]; N[(n + v) * 3 + 2] = nx * sn + nz * cs;
                    C[(n + v) * 4] = body.r; C[(n + v) * 4 + 1] = body.g; C[(n + v) * 4 + 2] = body.b; C[(n + v) * 4 + 3] = 255;
                }
            } else {
                for (size_t v = 0; v < nv; ++v) {
                    float lx = tp.pos[v * 3], ly = tp.pos[v * 3 + 1], lz = tp.pos[v * 3 + 2];
                    P[(n + v) * 3] = rp.x + lx * cs - lz * sn; P[(n + v) * 3 + 1] = rp.y + ly; P[(n + v) * 3 + 2] = rp.z + lx * sn + lz * cs;
                    float nx = tp.nrm[v * 3], nz = tp.nrm[v * 3 + 2];
                    N[(n + v) * 3] = nx * cs - nz * sn; N[(n + v) * 3 + 1] = tp.nrm[v * 3 + 1]; N[(n + v) * 3 + 2] = nx * sn + nz * cs;
                    const unsigned char* tc = &tp.col[v * 4];
                    bool mk = tc[0] == 255 && tc[1] == 0 && tc[2] == 255;
                    C[(n + v) * 4] = mk ? body.r : tc[0]; C[(n + v) * 4 + 1] = mk ? body.g : tc[1];
                    C[(n + v) * 4 + 2] = mk ? body.b : tc[2]; C[(n + v) * 4 + 3] = 255;
                }
            }
            n += static_cast<int>(nv);
            if (night && dist < 7000.0f && !f.isPip) {
                // headlights and tail lights
                float half = c.kind == 3 ? 5.8f : (c.kind == 2 || c.kind == 4 ? 3.6f : 2.2f);
                Vector3 fwd = {cs, 0, sn}, rt = {-sn, 0, cs};
                Vector3 front = Vector3Add(rp, Vector3Add(Vector3Scale(fwd, half), {0, 0.8f, 0}));
                Vector3 back = Vector3Add(rp, Vector3Add(Vector3Scale(fwd, -half), {0, 0.9f, 0}));
                float px = dist * 0.0024f;
                if (dist < 900.0f) {
                    for (float sd : {-0.7f, 0.7f}) {
                        lights_.push_back({Vector3Add(front, Vector3Scale(rt, sd)), std::max(0.55f, px), Color{255, 244, 214, 255}});
                        lights_.push_back({Vector3Add(back, Vector3Scale(rt, sd)), std::max(0.35f, px * 0.8f), Color{255, 30, 20, 220}});
                    }
                } else {
                    lights_.push_back({front, std::max(0.9f, px * 1.2f), Color{255, 244, 214, 230}});
                    lights_.push_back({back, std::max(0.6f, px), Color{255, 30, 20, 200}});
                }
            }
        }
        UpdateMeshBuffer(traffic_, 0, traffic_.vertices, n * 3 * sizeof(float), 0);
        UpdateMeshBuffer(traffic_, 2, traffic_.normals, n * 3 * sizeof(float), 0);
        UpdateMeshBuffer(traffic_, 3, traffic_.colors, n * 4, 0);
        traffic_.vertexCount = n;
        traffic_.triangleCount = n / 3;
    }
    if (traffic_.vertexCount <= 0) return;
    lit_.setF("uSpecular", 0.35f);
    drawMeshC(traffic_, litMat_, MatrixIdentity());
    lit_.setF("uSpecular", 0.0f);
    ++drawCalls_;
}

void Renderer::drawAirbaseLife(const FrameParams& f) {
    const WorldLayout* L = dressing_.layout;
    if (!L || !terrain_) return;
    if (forkliftMesh_.vertexCount == 0) {
        MeshBuilder a, b, c, d;
        buildGroundVehicle(a, MatrixIdentity(), PropKind::Forklift, WHITE);
        buildGroundVehicle(b, MatrixIdentity(), PropKind::Tug, WHITE);
        buildGroundVehicle(c, MatrixIdentity(), PropKind::FuelTruck, Color{94, 100, 72, 255});
        d.box({0, 0.6f, 0}, {1.2f, 1.6f, 7.5f}, Color{226, 226, 220, 255});   // radar antenna (on its turntable)
        d.box({-0.8f, 0.6f, 0}, {0.4f, 0.4f, 0.4f}, Color{78, 80, 82, 255});
        forkliftMesh_ = a.build();
        tugMesh_ = b.build();
        fuelTruckMesh_ = c.build();
        radarHeadMesh_ = d.build();
    }
    const RunwayInfo& rw = terrain_->runway();
    const double cx = rw.center.x, cz = rw.center.z, ry = rw.center.y;
    const float T = static_cast<float>(f.time);
    const Vector3 cam = f.cam.position;
    auto near = [&](double x, double y, double z, float d) { return Vector3Distance({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)}, cam) < d; };
    auto at = [&](const Mesh& m, double x, double z, double yaw, double lift = 0.0) {
        double y = terrain_->surfaceHeight(x, z) + lift;
        drawMeshC(m, litMat_, MatrixMultiply(MatrixRotateY(-static_cast<float>(yaw)), MatrixTranslate(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z))));
        ++drawCalls_;
    };
    /// position along a there-and-back polyline at time t (speed v, a pause at each end)
    auto shuttle = [](const std::vector<Vector2>& P, double t, double v, double pause, double& yaw) {
        double len = 0;
        for (size_t i = 1; i < P.size(); ++i) len += std::hypot(P[i].x - P[i - 1].x, P[i].y - P[i - 1].y);
        // out (leg), pause at the far end, back (leg), pause at the start
        double leg = len / v, cyc = 2.0 * (leg + pause);
        double u = std::fmod(t, cyc);
        bool back = u >= leg + pause;
        double s;
        if (u < leg) s = u * v;
        else if (u < leg + pause) s = len;
        else if (u < 2.0 * leg + pause) s = len - (u - leg - pause) * v;
        else s = 0.0;
        s = std::clamp(s, 0.0, len);
        double rest = s;
        for (size_t i = 1; i < P.size(); ++i) {
            double l = std::hypot(P[i].x - P[i - 1].x, P[i].y - P[i - 1].y);
            if (rest <= l || i + 1 == P.size()) {
                double k = l > 1e-6 ? std::clamp(rest / l, 0.0, 1.0) : 1.0;
                yaw = std::atan2(P[i].y - P[i - 1].y, P[i].x - P[i - 1].x) + (back ? kPi : 0.0);
                return Vector2{P[i - 1].x + (P[i].x - P[i - 1].x) * static_cast<float>(k), P[i - 1].y + (P[i].y - P[i - 1].y) * static_cast<float>(k)};
            }
            rest -= l;
        }
        return P.back();
    };
    setAtmosphere(lit_, f);
    lit_.setF("uEmissive", 0.0f);
    lit_.setF("uSpecular", 0.25f);
    // rotating radar antennas on the masts (one turn every 6 s)
    for (const SceneProp& p : L->props) {
        if (p.kind != PropKind::RadarMast || !near(p.pos.x, p.pos.y, p.pos.z, 6000.0f)) continue;
        Matrix M = MatrixMultiply(MatrixRotateY(T * 1.05f + static_cast<float>(p.pos.x) * 0.01f),
                                  MatrixTranslate(static_cast<float>(p.pos.x), static_cast<float>(p.pos.y) + 21.8f, static_cast<float>(p.pos.z)));
        drawMeshC(radarHeadMesh_, litMat_, M);
        ++drawCalls_;
    }
    // the parked C-17s: ramp down, a forklift shuttling pallets between the ramp and the stacks
    for (const SceneProp& p : L->props) {
        if (p.kind != PropKind::ParkedTransport || !near(p.pos.x, p.pos.y, p.pos.z, 9000.0f)) continue;
        Vector3 pb;
        float camo;
        Vector3 pa = paintFor(Team::Blue, 0, profile(AircraftId::C17), pb, camo);
        JetPose pose;
        pose.gear = 1.0f;
        pose.ramp = 1.0f;
        pose.compress = 0.12f;
        pose.lights = f.L.night > 0.2f;
        pose.flaps = 0;
        Matrix M = MatrixMultiply(MatrixRotateY(-static_cast<float>(p.yaw)),
                                  MatrixTranslate(static_cast<float>(p.pos.x), static_cast<float>(p.pos.y + FlightModel::kGearHeight), static_cast<float>(p.pos.z)));
        setAtmosphere(jet_, f);
        drawJetModel(AirframeStyle::Transport, M, pa, pb, camo, pose, f);
        setAtmosphere(lit_, f);
        // the ramp is at the tail (-X in the aircraft frame): the forklift runs out to the stacks
        Vec3 back{-std::cos(p.yaw), 0, -std::sin(p.yaw)};
        Vec3 r0 = p.pos + back * 34.0, r1 = p.pos + back * 64.0;
        double yaw = 0;
        Vector2 q = shuttle({{static_cast<float>(r0.x), static_cast<float>(r0.z)}, {static_cast<float>(r1.x), static_cast<float>(r1.z)}}, f.time, 3.0, 6.0, yaw);
        at(forkliftMesh_, q.x, q.y, yaw);
        // a pallet on the forks on the way out
        double u = std::fmod(f.time, 2.0 * (30.0 / 3.0 + 6.0));
        if (u < 30.0 / 3.0 + 6.0) {
            Vec3 fw{std::cos(yaw), 0, std::sin(yaw)};
            drawMeshC(cargo_, litMat_, MatrixMultiply(MatrixMultiply(MatrixScale(0.8f, 0.8f, 0.8f), MatrixRotateY(-static_cast<float>(yaw))),
                                                      MatrixTranslate(static_cast<float>(q.x + fw.x * 1.9), static_cast<float>(terrain_->surfaceHeight(q.x, q.y) + 0.35),
                                                                      static_cast<float>(q.y + fw.z * 1.9))));
            ++drawCalls_;
        }
    }
    if (!near(cx, ry, cz, 14000.0f)) return;
    // a tug and a fuel truck doing the rounds of the apron (the fuel farm -> the parked jets)
    {
        double yaw = 0;
        Vector2 q = shuttle({{static_cast<float>(cx + 700), static_cast<float>(cz + 520)}, {static_cast<float>(cx + 620), static_cast<float>(cz + 470)},
                             {static_cast<float>(cx + 300), static_cast<float>(cz + 470)}, {static_cast<float>(cx + 290), static_cast<float>(cz + 395)}},
                            f.time + 40.0, 7.0, 25.0, yaw);
        at(fuelTruckMesh_, q.x, q.y, yaw);
        q = shuttle({{static_cast<float>(cx - 420), static_cast<float>(cz + 455)}, {static_cast<float>(cx + 160), static_cast<float>(cz + 455)}}, f.time, 5.0, 15.0, yaw);
        at(tugMesh_, q.x, q.y, yaw);
    }
    // the player's own jet on the ground keeps the runway to itself
    bool playerOnRunway = false;
    if (f.battle && !f.battle->jets().empty()) {
        const FlightState& ps = f.battle->player().fm->state();
        playerOnRunway = ps.onGround || (ps.agl < 300.0 && std::fabs(ps.pos.z - cz) < 1500.0 && std::fabs(ps.pos.x - cx) < rw.length);
    }
    const float W = static_cast<float>(rw.width), Lr = static_cast<float>(rw.length);
    // a fighter taxiing between the apron and the holding point at the east end
    {
        double yaw = 0;
        std::vector<Vector2> taxi = {{static_cast<float>(cx + 120), static_cast<float>(cz + 300)}, {static_cast<float>(cx + 120), static_cast<float>(cz + 210)},
                                     {static_cast<float>(cx + 350), static_cast<float>(cz + 190)}, {static_cast<float>(cx + Lr / 2 - 150), static_cast<float>(cz + 190)},
                                     {static_cast<float>(cx + Lr / 2 - 150), static_cast<float>(cz + W / 2 + 40)}};
        Vector2 q = shuttle(taxi, f.time, 9.0, 40.0, yaw);
        Vector3 pb;
        float camo;
        Vector3 pa = paintFor(Team::Blue, 0, profile(AircraftId::F16), pb, camo);
        JetPose pose;
        pose.gear = 1.0f;
        pose.throttle = 0.15f;
        pose.lights = true;
        pose.compress = 0.12f;
        pose.wheelSpin = T * 20.0f;
        double y = terrain_->surfaceHeight(q.x, q.y) + FlightModel::kGearHeight;
        Matrix M = MatrixMultiply(MatrixRotateY(-static_cast<float>(yaw)), MatrixTranslate(q.x, static_cast<float>(y), q.y));
        setAtmosphere(jet_, f);
        drawJetModel(AirframeStyle::ViperSingle, M, pa, pb, camo, pose, f);
    }
    // a pair departing every 150 s when the runway is free: roll, rotate, climb out east
    if (!playerOnRunway) {
        const double cyc = 150.0;
        for (int k = 0; k < 2; ++k) {
            double t = std::fmod(f.time + 37.0 - k * 7.0, cyc);
            if (t > 75.0) continue;
            double accel = 3.4, vr = 76.0, tr = vr / accel;
            double x0 = cx - Lr / 2 + 200.0;
            double sx, h = 0, v;
            if (t < tr) { sx = 0.5 * accel * t * t; v = accel * t; }
            else {
                double ta = t - tr;
                v = vr + 6.0 * ta;
                sx = 0.5 * accel * tr * tr + vr * ta + 3.0 * ta * ta;
                h = std::max(0.0, 0.18 * (sx - 0.5 * accel * tr * tr) - 20.0);
            }
            double px = x0 + sx, pz = cz - (k ? 12.0 : -12.0);
            if (!near(px, ry + h, pz, 12000.0f)) continue;
            double climb = t < tr ? 0.0 : std::atan(0.18) * std::min(1.0, (t - tr) / 3.0);
            Vector3 pb;
            float camo;
            AircraftId type = k ? AircraftId::FA18 : AircraftId::F22;
            Vector3 pa = paintFor(Team::Blue, 0, profile(type), pb, camo);
            JetPose pose;
            pose.gear = h > 60.0 ? std::max(0.0f, 1.0f - static_cast<float>((h - 60.0) / 80.0)) : 1.0f;
            pose.throttle = 1.0f;
            pose.ab = 0.9f;
            pose.lights = true;
            pose.mach = static_cast<float>(v / 340.0);
            pose.altitude = static_cast<float>(ry + h);
            pose.wheelSpin = static_cast<float>(sx / 0.42);
            Matrix M = MatrixMultiply(MatrixRotateZ(static_cast<float>(climb)),
                                      MatrixTranslate(static_cast<float>(px), static_cast<float>(ry + FlightModel::kGearHeight + h), static_cast<float>(pz)));
            setAtmosphere(jet_, f);
            drawJetModel(profile(type).style, M, pa, pb, camo, pose, f);
        }
    }
    setAtmosphere(lit_, f);
    // a helicopter flying circuits round the airfield at 150 m
    {
        double a = f.time * (2.0 * kPi / 95.0);
        double ex = 2200.0, ez = 1300.0;
        double hx = cx + std::cos(a) * ex, hz = cz + std::sin(a) * ez;
        double hy = terrain_->surfaceHeight(hx, hz) + 150.0;
        if (near(hx, hy, hz, 8000.0f)) {
            double yaw = std::atan2(std::cos(a) * ez, -std::sin(a) * ex);
            Matrix body = MatrixMultiply(MatrixMultiply(MatrixRotateZ(-0.10f), MatrixRotateY(-static_cast<float>(yaw))),
                                         MatrixTranslate(static_cast<float>(hx), static_cast<float>(hy), static_cast<float>(hz)));
            drawMeshC(heli_, litMat_, body);
            drawMeshC(heliRotor_, litMat_, MatrixMultiply(MatrixMultiply(MatrixRotateY(T * 21.0f), MatrixTranslate(-0.4f, 4.9f, 0)), body));
            drawCalls_ += 2;
            if (f.L.night > 0.2f && std::fmod(f.time, 1.2) < 0.15) lights_.push_back({{static_cast<float>(hx), static_cast<float>(hy + 1.0), static_cast<float>(hz)}, 1.2f, Color{255, 40, 30, 255}});
        }
    }
}

void Renderer::pushSceneLights(const FrameParams& f) {
    const WorldLayout* L = dressing_.layout;
    Vector3 cp = f.cam.position;
    const float night = std::clamp(f.L.night, 0.0f, 1.0f);
    const float pxK = 2.0f * std::tan(f.cam.fovy * DEG2RAD * 0.5f) / std::max(1, h_);   // metres per pixel per metre
    const Biome biome = terrain_->info().biome;
    const bool sodium = biome == Biome::Desert || biome == Biome::Tropical;
    auto push = [&](Vector3 p, float base, Color c, float maxD, float minPx) {
        float d = Vector3Distance(p, cp);
        if (d > maxD) return;
        if (!sphereVisible(f, p, base * 2.0f)) return;
        float fade = 1.0f - std::clamp((d - maxD * 0.6f) / (maxD * 0.4f), 0.0f, 1.0f);
        c.a = static_cast<unsigned char>(c.a * fade * std::min(1.0f, night * 1.4f));
        lights_.push_back({p, std::max(base, d * pxK * minPx), c});
    };
    // pools of light on the ground under lamps and floodlights (close range only)
    auto pool = [&](const Vector3& p, float r, Color c) {
        float d = Vector3Distance(p, cp);
        if (d > 4500.0f || !sphereVisible(f, p, r)) return;
        float gy = static_cast<float>(terrain_->surfaceHeight(p.x, p.z));
        float fade = 1.0f - std::clamp((d - 2500.0f) / 2000.0f, 0.0f, 1.0f);
        c.a = static_cast<unsigned char>(c.a * fade * std::min(1.0f, night * 1.3f));
        pools_.push_back({{p.x, gy + 0.35f, p.z}, r, c});
    };
    for (const LightPoint& lp : L->lights) {
        Vector3 p = toRl(lp.pos);
        switch (lp.kind) {
            case LightKind::Street:
                push(p, 2.6f, sodium ? Color{255, 168, 74, 255} : Color{236, 238, 255, 255}, 11000.0f, 1.4f);
                pool(p, 13.0f, sodium ? Color{255, 150, 60, 110} : Color{200, 210, 240, 90});
                break;
            case LightKind::Flood:
                push(p, 5.5f, Color{255, 250, 236, 255}, 14000.0f, 2.0f);
                pool(p, 42.0f, Color{235, 236, 226, 80});
                break;
            case LightKind::Helipad: push(p, 0.8f, Color{60, 255, 120, 255}, 5000.0f, 1.0f); break;
            case LightKind::Canopy: push(p, 6.0f, Color{255, 252, 240, 255}, 9000.0f, 1.8f); pool(p, 16.0f, Color{255, 250, 236, 120}); break;
            case LightKind::Billboard: push(p, 4.0f, Color{220, 236, 255, 230}, 8000.0f, 1.4f); break;
            case LightKind::Checkpoint: push(p, 2.5f, Color{255, 220, 150, 255}, 7000.0f, 1.2f); pool(p, 12.0f, Color{255, 220, 150, 100}); break;
            case LightKind::Beacon: break;   // drawn with the other obstruction beacons
        }
    }
    // red obstruction beacons: masts, cranes, towers, wind turbines (slow blink, staggered)
    for (size_t i = 0; i < dressing_.beacons.size(); ++i)
        if (std::fmod(f.time + i * 0.37, 1.6) < 0.8) push(dressing_.beacons[i], 1.6f, Color{255, 40, 30, 255}, 16000.0f, 1.6f);
    for (size_t i = 0; i < dressing_.rotorHubs.size(); ++i)
        if (std::fmod(f.time, 2.0) < 1.0) push(Vector3Add(dressing_.rotorHubs[i], {0, 2.4f, 0}), 1.8f, Color{255, 40, 30, 255}, 16000.0f, 1.6f);
    // runway: approach light bars with a sequenced strobe ("rabbit") and green threshold bars
    const RunwayInfo& rw = terrain_->runway();
    float y = static_cast<float>(rw.center.y) + 0.6f;
    for (int end = -1; end <= 1; end += 2) {
        float thx = static_cast<float>(rw.center.x + end * rw.length * 0.5);
        float cz = static_cast<float>(rw.center.z);
        for (int k = 0; k < 9; ++k) push({thx, y, cz - 20.0f + k * 5.0f}, 0.9f, Color{80, 255, 120, 255}, 12000.0f, 1.1f);
        int rabbit = static_cast<int>(std::fmod(f.time * 2.0, 1.0) * 30.0);
        for (int k = 1; k <= 30; ++k) {
            float x = thx + end * k * 30.0f;
            float gy = static_cast<float>(terrain_->surfaceHeight(x, cz)) + 1.2f;
            push({x, gy, cz}, 1.1f, Color{255, 246, 220, 220}, 12000.0f, 1.2f);
            if (k % 5 == 0)
                for (float z : {-9.0f, -4.5f, 4.5f, 9.0f}) push({x, gy, cz + z}, 0.9f, Color{255, 246, 220, 200}, 9000.0f, 1.0f);
            if (30 - k == rabbit) push({x, gy + 0.3f, cz}, 3.0f, Color{255, 255, 255, 255}, 12000.0f, 2.6f);
        }
    }
}

void Renderer::drawTanker(const FrameParams& f) {
    const Battle* b = f.battle;
    if (!b || !b->tanker().active) return;
    const Tanker& tk = b->tanker();
    if (Vector3Distance(toRl(tk.pos), f.cam.position) > 30000) return;
    Matrix R = MatrixRotateY(static_cast<float>(-tk.heading));
    litMat_.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    lit_.setF("uSpecular", 0.45f);
    drawMeshC(tanker_, litMat_, MatrixMultiply(R, MatrixTranslate(static_cast<float>(tk.pos.x), static_cast<float>(tk.pos.y), static_cast<float>(tk.pos.z))));
    Vec3 bk = tk.basket();
    drawMeshC(drogue_, litMat_, MatrixMultiply(R, MatrixTranslate(static_cast<float>(bk.x), static_cast<float>(bk.y), static_cast<float>(bk.z))));
    lit_.setF("uSpecular", 0.2f);
    // hose: a sagging curve from the tail fairing to the basket
    Vec3 fwd{std::cos(tk.heading), 0, std::sin(tk.heading)};
    Vec3 a = tk.pos - fwd * 26.0 - Vec3{0, 2.8, 0};
    Vector3 prev = toRl(a);
    for (int i = 1; i <= 12; ++i) {
        double u = i / 12.0;
        Vec3 p = a + (bk - a) * u - Vec3{0, std::sin(u * kPi) * 2.0, 0};
        Vector3 cur = toRl(p);
        DrawLine3D(prev, cur, Color{40, 40, 40, 255});
        prev = cur;
    }
    drawCalls_ += 3;
}

void Renderer::drawJetModel(AirframeStyle style, const Matrix& M, Vector3 paintA, Vector3 paintB, float camo,
                            float gearPos, float stabDeg, float throttle, float ab, float hitFlash, float damage,
                            const FrameParams& f, bool canopy) {
    JetPose p;
    p.gear = gearPos; p.pitch = -stabDeg / 15.0f; p.throttle = throttle; p.ab = ab; p.hitFlash = hitFlash; p.damage = damage;
    p.flaps = gearPos > 0.5f ? 20.0f : 0.0f;
    drawJetModel(style, M, paintA, paintB, camo, p, f, canopy);
}

void Renderer::drawJetModel(AirframeStyle style, const Matrix& M, Vector3 paintA, Vector3 paintB, float camo, const JetPose& pose,
                            const FrameParams& f, bool canopy) {
    const JetModel& jm = jets_[static_cast<int>(style)];
    Vector3 origin = {M.m12, M.m13, M.m14};
    int lod = jetLod(Vector3Distance(origin, f.cam.position) * (f.cam.fovy / 70.0f));
    const JetLod& L = jm.lod[lod];
    jet_.setV3("uPaintA", paintA);
    jet_.setV3("uPaintB", paintB);
    jet_.setV3("uPaintC", g_paintC);
    jet_.setF("uCamo", camo);
    jet_.setF("uRoughness", g_paintRough);
    jet_.setF("uMetal", g_paintMetal);
    jet_.setV3("uGlowColor", g_paintGlow);
    jet_.setF("uGlowPulse", 0.35f + 0.45f * pose.throttle + 0.9f * pose.ab);   // accents brighten with the engine
    jet_.setV4("uWingMark", jm.wingMark);
    jet_.setV4("uFinMark", jm.finMark);
    jet_.setF("uFinTan", jm.finTan);
    jet_.setV4("uNoseMark", jm.noseMark);
    jet_.setV4("uWalk", jm.walk);
    jet_.setF("uNoseNumbers", g_noseNumbers);
    jet_.setF("uSerial", g_serial);
    jet_.setV3("uMark0", g_mark0);
    jet_.setF("uMarkOn", lod < 2 ? 1.0f : 0.0f);
    g_noseNumbers = 0; g_serial = 0; g_mark0 = {0, 0, 0};
    g_paintC = {0, 0, 0};
    g_paintRough = 0.42f;
    g_paintMetal = 0.12f;
    g_paintGlow = {0, 0, 0};
    jet_.setF("uHitFlash", pose.hitFlash);
    jet_.setF("uDamage", pose.damage);
    rlDisableBackfaceCulling();
    drawMeshC(L.body, jetMat_, M);
    if (canopy) drawMeshC(L.canopy, jetMat_, M);
    drawCalls_ += 2;
    jet_.setF("uMarkOn", 0.0f);   // moving parts are modelled about their own pivots: no markings
    // moving parts (LOD 0): control surfaces follow the stick, nozzles vector, wheels roll
    const float D = DEG2RAD;
    Matrix gearM = MatrixMultiply(MatrixTranslate(0, (1.0f - pose.gear) * 1.6f + pose.compress, 0), M);
    auto nozzleAngle = [&]() { return jm.vectoring ? -pose.pitch * 15.0f * D : 0.0f; };
    for (const JetPart& part : L.parts) {
        float a = 0;
        Matrix base = M;
        switch (part.kind) {
            case JetPartKind::Aileron: {
                // a damaged left wing loses its aileron
                if (pose.damage > 0.72f && part.side < 0) continue;
                float down = -pose.roll * 20.0f * part.side;
                if (jm.tailless) down += -pose.pitch * 14.0f;
                a = down * part.side * D;
                break;
            }
            case JetPartKind::Flap: {
                float down = pose.flaps;
                if (jm.tailless) down += -pose.pitch * 14.0f - pose.roll * 10.0f * part.side;
                a = down * part.side * D;
                break;
            }
            case JetPartKind::Rudder: a = pose.yaw * 22.0f * D; break;
            case JetPartKind::Canard: a = pose.pitch * 12.0f * D; break;
            case JetPartKind::Stab:
                if (pose.damage > 0.86f && part.side > 0) continue;   // shot off
                a = (-pose.pitch * 15.0f + pose.roll * 4.0f * part.side) * D;
                break;
            case JetPartKind::Nozzle: a = nozzleAngle(); break;
            case JetPartKind::Ramp: a = -pose.ramp * 0.62f; break;   // lowers aft end to the ground
            case JetPartKind::Wheel:
                if (pose.gear <= 0.02f) continue;
                a = -pose.wheelSpin;
                base = gearM;
                break;
        }
        Matrix T = MatrixMultiply(MatrixMultiply(MatrixRotate(part.axis, a), MatrixTranslate(part.pivot.x, part.pivot.y, part.pivot.z)), base);
        drawMeshC(part.mesh, jetMat_, T);
        ++drawCalls_;
    }
    if (pose.gear > 0.02f) {
        drawMeshC(lod == 0 ? jm.gear : jm.gearLo, jetMat_, gearM);
        ++drawCalls_;
    }
    rlEnableBackfaceCulling();
    // lights: red / green wingtips, white tail, flashing strobe, landing light with the gear down
    if (pose.lights && lod < 2) {
        auto world = [&](Vector3 b) { return Vector3Transform(b, M); };
        float d = Vector3Distance(origin, f.cam.position);
        float s0 = std::clamp(0.35f + d * 0.004f, 0.35f, 4.0f);
        lights_.push_back({world(jm.wingtips[0]), s0, Color{255, 40, 30, 255}});
        lights_.push_back({world(jm.wingtips[1]), s0, Color{40, 255, 90, 255}});
        lights_.push_back({world(jm.tailLight), s0 * 0.8f, Color{255, 255, 255, 220}});
        double ph = std::fmod(f.time * 1.1 + pose.id * 0.37, 1.0);
        if (ph < 0.06) lights_.push_back({world(jm.strobe), s0 * 2.2f, Color{255, 255, 255, 255}});
        if (pose.gear > 0.9f)
            lights_.push_back({Vector3Transform(Vector3Add(jm.landingLight, {0.1f, 0, 0}), gearM), s0 * 2.5f, Color{255, 246, 220, 255}});
    }
    if ((pose.throttle > 0.02f || pose.ab > 0.01f) && !jm.turbofan) {
        setAtmosphere(flameP_, f);
        flameP_.setF("uHue", jm.plumeHue);
        flameP_.setF("uThrottle", pose.throttle);
        flameP_.setF("uAb", pose.ab);
        // speed and altitude shape the plume: thinner air -> longer, wider plume; high dynamic
        // pressure -> shorter, tighter plume with closer shock diamonds
        float rho = std::exp(-pose.altitude / 8500.0f);
        float speedK = std::clamp(1.0f - 0.22f * pose.mach * rho, 0.6f, 1.0f);
        flameP_.setF("uDiamond", 23.0f * (1.0f + 0.5f * pose.mach * rho));
        float na = lod == 0 ? nozzleAngle() : 0.0f;
        Vector3 axis = Vector3Transform({-cosf(na), -sinf(na), 0}, MatrixMultiply(M, MatrixTranslate(-origin.x, -origin.y, -origin.z)));
        flameP_.setV3("uAxis", Vector3Normalize(axis));
        rlDisableDepthMask();
        rlDisableBackfaceCulling();
        BeginBlendMode(BLEND_ADDITIVE);
        for (int e = 0; e < jm.nozzleCount; ++e) {
            float pulse = 1.0f + 0.06f * std::sin(static_cast<float>(f.time) * 31.0f + e * 1.7f) * pose.ab;
            float len = (1.2f + 1.5f * pose.throttle + 7.0f * pose.ab * (0.75f + 0.5f * (1.0f - rho))) * pulse * speedK * jm.plumeScale;
            float r = jm.nozzleRadius * (0.85f + 0.2f * pose.ab) * (1.0f + 0.25f * (1.0f - rho));
            Vector3 pv = jm.nozzlePivot[e], ex = jm.nozzles[e];
            Matrix vec = MatrixMultiply(MatrixMultiply(MatrixTranslate(-pv.x, -pv.y, -pv.z), MatrixRotateZ(na)), MatrixTranslate(pv.x, pv.y, pv.z));
            Matrix F = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(len, r, r), MatrixTranslate(ex.x, ex.y, ex.z)), vec), M);
            drawMeshC(flame_, flameMat_, F);
            ++drawCalls_;
        }
        EndBlendMode();
        rlEnableBackfaceCulling();
        rlEnableDepthMask();
    }
}

void Renderer::drawLightPools(const FrameParams& f) {
    if (pools_.empty()) return;
    BeginShaderMode(particle_.sh);
    particle_.setF("uIntensity", 1.6f);
    rlDisableDepthMask();
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(world_.glow.id);
    rlBegin(RL_QUADS);
    for (const LightSprite& l : pools_) {
        float r = l.size;
        rlColor4ub(l.col.r, l.col.g, l.col.b, l.col.a);
        rlTexCoord2f(0, 0); rlVertex3f(l.pos.x - r, l.pos.y, l.pos.z - r);
        rlTexCoord2f(0, 1); rlVertex3f(l.pos.x - r, l.pos.y, l.pos.z + r);
        rlTexCoord2f(1, 1); rlVertex3f(l.pos.x + r, l.pos.y, l.pos.z + r);
        rlTexCoord2f(1, 0); rlVertex3f(l.pos.x + r, l.pos.y, l.pos.z - r);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    EndShaderMode();
    pools_.clear();
}

void Renderer::drawLightSprites(const FrameParams& f) {
    drawLightPools(f);
    if (!smokeSprites_.empty()) {
        // alpha-blended soft puffs (replay smoke trails): drawn before the glows
        std::swap(lights_, smokeSprites_);
        drawSpriteBatch(f, false);
        std::swap(lights_, smokeSprites_);
        smokeSprites_.clear();
    }
    drawSpriteBatch(f, true);
}

void Renderer::drawSpriteBatch(const FrameParams& f, bool additive) {
    if (lights_.empty()) return;
    BeginShaderMode(particle_.sh);
    particle_.setF("uIntensity", additive ? 3.0f : 1.0f);
    rlDisableDepthMask();
    BeginBlendMode(additive ? BLEND_ADDITIVE : BLEND_ALPHA);
    rlSetTexture(world_.glow.id);
    Vector3 fwd = Vector3Normalize(Vector3Subtract(f.cam.target, f.cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, f.cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);
    rlBegin(RL_QUADS);
    for (const LightSprite& l : lights_) {
        Vector3 r = Vector3Scale(right, l.size), u = Vector3Scale(up, l.size);
        rlColor4ub(l.col.r, l.col.g, l.col.b, l.col.a);
        Vector3 c0 = Vector3Subtract(Vector3Subtract(l.pos, r), u), c1 = Vector3Add(Vector3Subtract(l.pos, u), r);
        Vector3 c2 = Vector3Add(Vector3Add(l.pos, r), u), c3 = Vector3Add(Vector3Subtract(l.pos, r), u);
        rlTexCoord2f(0, 1); rlVertex3f(c0.x, c0.y, c0.z);
        rlTexCoord2f(1, 1); rlVertex3f(c1.x, c1.y, c1.z);
        rlTexCoord2f(1, 0); rlVertex3f(c2.x, c2.y, c2.z);
        rlTexCoord2f(0, 0); rlVertex3f(c3.x, c3.y, c3.z);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    EndShaderMode();
    lights_.clear();
}

void Renderer::drawJet(const Jet& j, const FrameParams& f, bool canopy, Vec3 origin) {
    const FlightState& s = j.fm->state();
    Matrix M = bodyMatrix(s.pos - origin, s.q);
    Vector3 pb;
    float camo;
    Vector3 pa = paintFor(j, pb, camo);
    JetPose p;
    p.gear = static_cast<float>(s.gearPos);
    p.throttle = static_cast<float>(std::clamp((s.rpm - 0.6) / 0.4, 0.0, 1.0));
    p.ab = static_cast<float>(s.abLevel);
    p.hitFlash = static_cast<float>(j.hitFlash * 3.0);
    p.damage = static_cast<float>(1.0 - j.health / 100.0);
    p.pitch = static_cast<float>(std::clamp(j.lastInput.pitch, -1.0, 1.0));
    p.roll = static_cast<float>(std::clamp(j.lastInput.roll, -1.0, 1.0));
    p.yaw = static_cast<float>(std::clamp(j.lastInput.yaw, -1.0, 1.0));
    p.flaps = s.gearDown ? 22.0f : static_cast<float>(std::clamp((150.0 - s.airspeed) / 60.0, 0.0, 1.0) * 10.0);
    p.mach = static_cast<float>(s.mach);
    p.altitude = static_cast<float>(s.pos.y);
    p.lights = true;
    p.id = j.id;
    // wheels roll with the ground speed; the oleos compress with weight on wheels
    Spin& spin = wheelSpin_[j.id];
    if (!f.isPip) {
        double dt = spin.t < 0 ? 0.0 : std::clamp(f.time - spin.t, 0.0, 0.1);
        spin.t = f.time;
        if (s.onGround) spin.angle = static_cast<float>(std::fmod(spin.angle + s.airspeed / 0.42 * dt, 2.0 * kPi * 1000.0));
    }
    p.wheelSpin = spin.angle;
    p.compress = s.onGround ? 0.12f : 0.0f;
    p.ramp = static_cast<float>(j.ramp);
    setAtmosphere(jet_, f);
    drawJetModel(j.fm->profile().style, M, pa, pb, camo, p, f, canopy);
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
    const float cb = static_cast<float>(terrain_->info().cloudBase);
    const float layers[2] = {cb + 200.0f, cb + 320.0f};
    const float shifts[2] = {0.0f, 0.012f};
    bool camAbove = f.cam.position.y > cb + 260.0f;
    for (int k = 0; k < 2; ++k) {
        int i = camAbove ? k : 1 - k;  // draw the far sheet first
        cloud_.setF("uLayerY", layers[i]);
        cloud_.setF("uLayerShift", shifts[i]);
        float dy = std::fabs(f.cam.position.y - layers[i]);
        cloud_.setF("uLayerFade", std::clamp((dy - 15.0f) / 120.0f, 0.0f, 1.0f));
        drawMeshC(world_.cloudPlane, cloudMat_, MatrixTranslate(sx, layers[i], sz));
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
    const Lighting& L = f.L;
    Vector3 lit = Vector3Add(Vector3Scale(L.sunColor, 0.85f), Vector3Scale(L.ambient(1), 0.8f));
    // upright billboards: horizontal right vector, world up
    Vector3 hr = Vector3Normalize({right.x, 0.0f, right.z});
    if (Vector3Length(hr) < 0.1f) hr = right;
    Vector3 vu = {0, 1, 0};
    rlSetTexture(world_.cloudAtlas.id);
    rlBegin(RL_QUADS);
    for (auto& [d2, p] : vis) {
        float dist = sqrtf(d2);
        Vector3 viewDir = Vector3Scale(Vector3Subtract(p->pos, cp), 1.0f / std::max(dist, 1.0f));
        float towardSun = powf(std::max(Vector3DotProduct(viewDir, L.sunDir), 0.0f), 5.0f);
        Vector3 col = Vector3Add(lit, Vector3Scale(L.sunColor, towardSun * 0.3f));
        float fog = L.fogAmount(cp, p->pos);
        col = Vector3Lerp(col, L.fogColor(cp, p->pos), fog * 0.85f);
        float near = std::clamp((dist - p->size * 0.35f) / (p->size * 0.8f), 0.0f, 1.0f);
        float a = 0.97f * near;
        if (a <= 0.01f) continue;
        // atlas cell of this sprite (a 2 px inset keeps mip levels from bleeding)
        const float u0 = (p->tex % 2) * 0.5f + 0.004f, u1 = u0 + 0.492f;
        const float v0 = (p->tex / 2) * 0.5f + 0.004f, v1 = v0 + 0.492f;
        float h = p->size * 0.5f;
        Vector3 r = Vector3Scale(hr, h), u = Vector3Scale(vu, h * 0.75f);
        Color cc = hdrColor(col, scale, a);
        Vector3 q0 = Vector3Subtract(Vector3Subtract(p->pos, r), u), q1 = Vector3Subtract(Vector3Add(p->pos, r), u);
        Vector3 q2 = Vector3Add(Vector3Add(p->pos, r), u), q3 = Vector3Add(Vector3Subtract(p->pos, r), u);
        rlColor4ub(cc.r, cc.g, cc.b, cc.a);
        rlTexCoord2f(u0, v1); rlVertex3f(q0.x, q0.y, q0.z);
        rlTexCoord2f(u1, v1); rlVertex3f(q1.x, q1.y, q1.z);
        rlTexCoord2f(u1, v0); rlVertex3f(q2.x, q2.y, q2.z);
        rlTexCoord2f(u0, v0); rlVertex3f(q3.x, q3.y, q3.z);
    }
    rlEnd();
    rlSetTexture(0);
    rlDrawRenderBatchActive();
    EndBlendMode();
    rlEnableDepthMask();
    EndShaderMode();
}

void Renderer::renderShadows(const FrameParams& f) {
    if (f.isPip) return;   // the secondary view reuses this frame's shadow map
    shadowsActive_ = false;
    if (!shadowRT_.id || !f.battle || !f.shadows || !qualityProfile(f.quality, f.detailDrop).shadows) return;
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
        // the shadow uses the merged LOD 1 mesh (stabilisers included, a fraction of the triangles)
        drawMeshC(jm.lod[1].body, shadowMat_, M);
        drawMeshC(jm.lod[1].canopy, shadowMat_, M);
        if (s.gearPos > 0.02) drawMeshC(jm.gearLo, shadowMat_, MatrixMultiply(MatrixTranslate(0, (1.0f - static_cast<float>(s.gearPos)) * 1.6f, 0), M));
    }
    rlEnableBackfaceCulling();
    EndMode3D();
    EndTextureMode();
    shadowsActive_ = true;
}

void Renderer::profMark(const char* pass) {
    if (!profiling_) return;
    rlDrawRenderBatchActive();
    glFinish();   // serialise so each pass is charged with its own GPU work
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - profLast_).count();
    profLast_ = now;
    long dc = g_draws - profDraws_;
    profDraws_ = g_draws;
    for (size_t i = 0; i < prof_.size(); ++i) if (prof_[i].first == pass) { prof_[i].second += ms; profCalls_[i] += dc; return; }
    prof_.push_back({pass, ms});
    profCalls_.push_back(dc);
}

void Renderer::profBegin() {
    if (!profiling_) return;
    rlDrawRenderBatchActive();
    glFinish();
    profLast_ = std::chrono::steady_clock::now();
    profDraws_ = 0;
    ++profFrames_;
}

std::string Renderer::profReport() const {
    std::string r;
    double total = 0;
    for (auto& e : prof_) total += e.second;
    char b[128];
    for (size_t i = 0; i < prof_.size(); ++i) {
        const auto& e = prof_[i];
        std::snprintf(b, sizeof b, "  %-14s %6.2f ms  %4.0f%%  %5.1f draws\n", e.first.c_str(), e.second / std::max(1, profFrames_),
                      100.0 * e.second / std::max(1e-9, total), static_cast<double>(profCalls_[i]) / std::max(1, profFrames_));
        r += b;
    }
    return r;
}

void Renderer::renderWorld(const FrameParams& f) {
    auto t0 = std::chrono::steady_clock::now();
    if (!f.isPip) profBegin();
    drawCalls_ = 0;
    g_tris = 0;
    g_draws = 0;
    if (Vector3DotProduct(world_.bakedSun, f.L.sunDir) < 0.99999f) {
        auto tb = std::chrono::steady_clock::now();
        world_.bakeSunShadows(f.L.sunDir);
        TraceLog(LOG_INFO, "SKYFALL: terrain sun shadows baked in %.0f ms",
                 std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tb).count());
    }
    const bool pm = !f.isPip;
    renderShadows(f);
    if (pm) profMark("shadow map");
    BeginTextureMode(scene_);
    ClearBackground(BLACK);
    drawSky(f);
    if (pm) profMark("sky");
    rlSetClipPlanes(f.nearPlane, f.farPlane);
    BeginMode3D(f.cam);
    drawTerrain(f);
    if (pm) profMark("terrain+sea");
    drawStatics(f);
    if (pm) profMark("parked jets");
    drawCarrier(f);
    drawObstacles(f);
    drawGroundTargets(f);
    if (pm) profMark("targets");
    if (f.battle) {
        setAtmosphere(jet_, f);
        for (const auto& jp : f.battle->jets()) {
            if (!jp->alive || jp->id == f.hideJetId) continue;
            if (Vector3Distance(toRl(jp->fm->state().pos), f.cam.position) > 30000) continue;
            drawJet(*jp, f);
        }
        if (f.extraJets) {
            for (const ExtraJet& e : *f.extraJets) {
                Vector3 pb;
                float camo;
                Vector3 pa = paintFor(e.team, e.skin, profile(e.id), pb, camo);
                setAtmosphere(jet_, f);
                drawJetModel(e.style, bodyMatrix(e.pos, e.q), pa, pb, camo, e.gear, 0, e.throttle, e.ab, 0, 0, f);
            }
        }
        // missiles
        setAtmosphere(lit_, f);
        lit_.setF("uEmissive", 0.0f);
        lit_.setF("uSpecular", 0.6f);
        lit_.setF("uMetal", 0.5f);   // bare-metal missile bodies
        lit_.setF("uFogOn", 1.0f);
        for (const Missile& m : f.battle->missiles()) {
            Vec3 d = normalize(m.vel);
            Quat q = Quat::fromEuler(std::atan2(d.z, d.x), std::asin(clampd(d.y, -1, 1)), 0);
            drawMeshC(missile_, litMat_, bodyMatrix(m.pos, q));
            ++drawCalls_;
        }
        if (f.extraMissiles)
            for (const ExtraMissile& m : *f.extraMissiles) {
                Vec3 d = normalize(m.dir);
                Quat q = Quat::fromEuler(std::atan2(d.z, d.x), std::asin(clampd(d.y, -1, 1)), 0);
                drawMeshC(missile_, litMat_, bodyMatrix(m.pos, q));
            }
    }
    lit_.setF("uMetal", 0.0f);
    if (pm) profMark("aircraft");
    drawShadows(f);
    if (pm) profMark("blob shadows");
    // transparent: cloud layer then puffs then particles
    if (f.cam.position.y < 2360.0f) {
        drawCloudPuffs(f);
        drawCloudLayer(f);
    } else {
        drawCloudLayer(f);
        drawCloudPuffs(f);
    }
    if (pm) profMark("clouds");
    if (f.battle && f.drawEffects)
        effects_.draw(f.cam, *f.battle, f.L, particle_.sh, particle_.loc("uIntensity"), world_.edgeLights, f.showRunwayLights);
    if (f.sprites)
        for (const WorldSprite& sp : *f.sprites) (sp.additive ? lights_ : smokeSprites_).push_back({sp.pos, sp.size, sp.col});
    if (pm) profMark("particles");
    drawGates(f);
    drawLightSprites(f);
    drawRain(f);
    EndMode3D();
    EndTextureMode();
    if (pm) profMark("rain");
    drawCalls_ = static_cast<int>(g_draws);   // exact count of mesh draws (shadow pass included)
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

void Renderer::updateAdaptation(const FrameParams& f) {
    if (!lumA_.id || !lumB_.id || !adapt_[0].id || !adapt_[1].id) return;
    auto pass = [&](Prog& p, const Texture2D& src, RenderTexture2D& dst) {
        BeginTextureMode(dst);
        BeginShaderMode(p.sh);
        DrawTexturePro(src, {0, 0, static_cast<float>(src.width), static_cast<float>(-src.height)},
                       {0, 0, static_cast<float>(dst.texture.width), static_cast<float>(dst.texture.height)}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
    };
    lumDown_.setV2("uOut", {64.0f, 36.0f});
    pass(lumDown_, scene_.texture, lumA_);
    pass(lumAvg_, lumA_.texture, lumB_);
    // adaptation speed: opening up into darkness is slower than closing down for glare
    double dt = adaptT_ < 0 || f.time < adaptT_ ? 1e9 : f.time - adaptT_;
    adaptT_ = f.time;
    float up = static_cast<float>(1.0 - std::exp(-dt * 1.8)), down = static_cast<float>(1.0 - std::exp(-dt * 0.9));
    int next = 1 - adaptIdx_;
    adaptP_.setF("uRateUp", up);
    adaptP_.setF("uRateDown", down);
    int lp = adaptP_.loc("uPrev");
    BeginTextureMode(adapt_[next]);
    BeginShaderMode(adaptP_.sh);
    if (lp >= 0) SetShaderValueTexture(adaptP_.sh, lp, adapt_[adaptIdx_].texture);
    DrawTexturePro(lumB_.texture, {0, 0, 1, -1}, {0, 0, 1, 1}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
    adaptIdx_ = next;
    if (std::getenv("SKYFALL_EXPOSURE_LOG")) {   // debug: read the adapted value back (stalls the GPU)
        Image im = LoadImageFromTexture(adapt_[adaptIdx_].texture);
        if (im.data && im.format == PIXELFORMAT_UNCOMPRESSED_R16G16B16A16) {
            unsigned short hbits = static_cast<unsigned short*>(im.data)[0];
            int e = (hbits >> 10) & 31, m = hbits & 1023;
            float v = e == 0 ? std::ldexp(static_cast<float>(m), -24) : std::ldexp(static_cast<float>(m + 1024), e - 25);
            adaptedLog2_ = (hbits & 0x8000) ? -v : v;
            static int n = 0;
            if (++n % 30 == 0) std::printf("exposure: adapted log2 %.2f, exposed %.2f\n", adaptedLog2_, adaptedLog2_ + std::log2(f.L.exposure * f.exposureBoost));
        }
        UnloadImage(im);
    }
}

void Renderer::composite(const FrameParams& f) {
    Rectangle full = {0, 0, static_cast<float>(w_), static_cast<float>(-h_)};
    if (f.bloom && bloomA_.id && bloomC_.id) {
        // bright pass -> half res
        BeginTextureMode(bloomA_);
        ClearBackground(BLACK);
        BeginShaderMode(bright_.sh);
        bright_.setF("uThreshold", 2.2f);
        DrawTexturePro(scene_.texture, full, {0, 0, static_cast<float>(bloomA_.texture.width), static_cast<float>(bloomA_.texture.height)}, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
        // keep the blur radius the same on screen whatever the bloom buffer resolution
        const float bloomK = 2.0f * static_cast<float>(bloomA_.texture.width) / static_cast<float>(std::max(1, w_));
        auto blur = [&](RenderTexture2D& src, RenderTexture2D& dst, Vector2 dir, int w, int h) {
            BeginTextureMode(dst);
            ClearBackground(BLACK);
            BeginShaderMode(blur_.sh);
            blur_.setV2("uDir", Vector2Scale(dir, bloomK));
            DrawTexturePro(src.texture, {0, 0, static_cast<float>(src.texture.width), static_cast<float>(-src.texture.height)},
                           {0, 0, static_cast<float>(w), static_cast<float>(h)}, {0, 0}, 0, WHITE);
            EndShaderMode();
            EndTextureMode();
        };
        const int hw = bloomA_.texture.width, hh = bloomA_.texture.height, qw = bloomC_.texture.width, qh = bloomC_.texture.height;
        blur(bloomA_, bloomB_, {1, 0}, hw, hh);
        blur(bloomB_, bloomA_, {0, 1}, hw, hh);
        blur(bloomA_, bloomD_, {2, 0}, qw, qh);
        blur(bloomD_, bloomC_, {0, 2}, qw, qh);
        blur(bloomC_, bloomD_, {3, 0}, qw, qh);
        blur(bloomD_, bloomC_, {0, 3}, qw, qh);
    }
    {
        // heat-haze footprints behind the nozzles of nearby jets running hot
        float hz[16] = {0};
        int nh = 0;
        if (f.battle && !f.isPip) {
            float th = tanf(f.cam.fovy * DEG2RAD * 0.5f);
            for (const auto& jp : f.battle->jets()) {
                if (nh >= 4) break;
                const Jet& j = *jp;
                if (!j.alive) continue;
                const FlightState& s = j.fm->state();
                double heat = std::clamp((s.rpm - 0.75) / 0.25, 0.0, 1.0) * 0.6 + s.abLevel * 0.6;
                if (heat < 0.05) continue;
                const JetModel& jm = jets_[static_cast<int>(j.fm->profile().style)];
                for (int e = 0; e < jm.nozzleCount && nh < 4; ++e) {
                    Vec3 ex = s.pos + s.q.rotate(Vec3{jm.nozzles[e].x - 2.5, jm.nozzles[e].y, jm.nozzles[e].z});
                    Vector3 p = toRl(ex);
                    Vector3 dv = Vector3Subtract(p, f.cam.position);
                    float dist = Vector3Length(dv);
                    if (dist > 400.0f || dist < 1.0f) continue;
                    if (Vector3DotProduct(dv, Vector3Subtract(f.cam.target, f.cam.position)) <= 0) continue;
                    Vector2 sp = GetWorldToScreenEx(p, f.cam, w_, h_);
                    float u = sp.x / w_, v = 1.0f - sp.y / h_;
                    if (u < -0.2f || u > 1.2f || v < -0.2f || v > 1.2f) continue;
                    float rad = (jm.nozzleRadius * 3.2f / dist) / th * 0.5f;
                    hz[nh * 4 + 0] = u; hz[nh * 4 + 1] = v; hz[nh * 4 + 2] = std::clamp(rad, 0.004f, 0.25f);
                    hz[nh * 4 + 3] = static_cast<float>(heat);
                    ++nh;
                }
            }
        }
        int loc = composite_.loc("uHaze");
        if (loc >= 0) SetShaderValueV(composite_.sh, loc, hz, SHADER_UNIFORM_VEC4, 4);
        composite_.setF("uHazeCount", static_cast<float>(nh));
    }
    const bool autoExp = f.autoExposure > 0.0f && hdr_ && lumA_.id;
    if (autoExp) updateAdaptation(f);
    BeginShaderMode(composite_.sh);
    composite_.setF("uExposure", f.L.exposure * f.exposureBoost);
    composite_.setF("uAutoExp", autoExp ? f.autoExposure : 0.0f);
    composite_.setF("uAdaptKey", f.adaptKey);
    if (autoExp) { int la = composite_.loc("uAdapt"); if (la >= 0) SetShaderValueTexture(composite_.sh, la, adapt_[adaptIdx_].texture); }
    composite_.setF("uBloomStrength", f.bloom ? 0.16f : 0.0f);
    composite_.setF("uVignette", 0.35f);
    composite_.setF("uBlackout", f.blackout);
    composite_.setF("uRedout", f.redout);
    composite_.setF("uDamage", f.damage);
    composite_.setF("uTime", static_cast<float>(fmod(f.time, 100.0)));
    composite_.setF("uGrain", 0.018f);
    composite_.setF("uSpeedFx", f.speedFx);
    composite_.setF("uRush", f.rush);
    {
        static float grade = std::getenv("SKYFALL_GRADE") ? static_cast<float>(std::atof(std::getenv("SKYFALL_GRADE"))) : 1.0f;
        static float gk = std::getenv("SKYFALL_GK") ? static_cast<float>(std::atof(std::getenv("SKYFALL_GK"))) : 0.5f;
        composite_.setF("uGrade", grade);
        composite_.setF("uGradeK", gk);
        float up = static_cast<float>(GetRenderWidth()) / static_cast<float>(std::max(1, w_));
        composite_.setF("uSharpen", std::clamp(0.12f + (up - 1.0f) * 0.35f, 0.0f, 0.5f));
    }
    int l1 = composite_.loc("uBloom1"), l2 = composite_.loc("uBloom2");
    if (l1 >= 0) SetShaderValueTexture(composite_.sh, l1, bloomA_.texture);
    if (l2 >= 0) SetShaderValueTexture(composite_.sh, l2, bloomC_.texture);
    DrawTexturePro(scene_.texture, full, {0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, {0, 0}, 0, WHITE);
    EndShaderMode();
}

void Renderer::renderPip(const FrameParams& f, int w, int h, int slot) {
    slot = slot == 1 ? 1 : 0;
    RenderTexture2D& pipScene_ = this->pipScene_[slot];
    RenderTexture2D& pipOut_ = this->pipOut_[slot];
    if (w != pipW_[slot] || h != pipH_[slot] || !pipScene_.id) {
        freeTarget(pipScene_);
        if (pipOut_.id) UnloadRenderTexture(pipOut_);
        pipW_[slot] = w; pipH_[slot] = h;
        pipScene_ = makeTarget(w, h, hdr_, true);
        pipOut_ = LoadRenderTexture(w, h);
        SetTextureFilter(pipOut_.texture, TEXTURE_FILTER_BILINEAR);
    }
    if (!pipScene_.id) return;
    double ms = worldMs_;
    int dc = drawCalls_;
    std::swap(scene_, pipScene_);
    int sw = w_, sh = h_;
    w_ = w; h_ = h;
    FrameParams pf = f;
    pf.isPip = true;
    renderWorld(pf);
    std::swap(scene_, pipScene_);
    w_ = sw; h_ = sh;
    worldMs_ = ms;
    drawCalls_ = dc;
    // tone map into the LDR picture-in-picture texture (no bloom, a little more grain)
    BeginTextureMode(pipOut_);
    ClearBackground(BLACK);
    composite_.setF("uHazeCount", 0.0f);
    BeginShaderMode(composite_.sh);
    composite_.setF("uExposure", f.L.exposure * f.exposureBoost);
    composite_.setF("uBloomStrength", 0.0f);
    composite_.setF("uVignette", 0.55f);
    composite_.setF("uBlackout", 0.0f);
    composite_.setF("uRedout", 0.0f);
    composite_.setF("uDamage", 0.0f);
    composite_.setF("uTime", static_cast<float>(fmod(f.time, 100.0)));
    composite_.setF("uGrain", 0.045f);
    composite_.setF("uGrade", 1.0f);
    composite_.setF("uGradeK", 0.5f);
    composite_.setF("uSharpen", 0.0f);
    composite_.setF("uSpeedFx", 0.0f);
    composite_.setF("uRush", 0.0f);
    composite_.setF("uAutoExp", 0.0f);   // the small replay windows keep the fixed exposure
    int l1 = composite_.loc("uBloom1"), l2 = composite_.loc("uBloom2");
    if (l1 >= 0) SetShaderValueTexture(composite_.sh, l1, bloomA_.texture);
    if (l2 >= 0) SetShaderValueTexture(composite_.sh, l2, bloomC_.texture);
    DrawTexturePro(pipScene_.texture, {0, 0, static_cast<float>(w), static_cast<float>(-h)}, {0, 0, static_cast<float>(w), static_cast<float>(h)}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
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
