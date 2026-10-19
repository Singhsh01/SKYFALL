// Rendering orchestration: HDR scene target, sky, terrain, water, clouds, aircraft, effects,
// bloom + ACES tone mapping post-process.
#pragma once

#include "Dressing.hpp"

#include "../app/Settings.hpp"
#include "../combat/Combat.hpp"
#include "Effects.hpp"
#include "JetMesh.hpp"
#include "Lighting.hpp"
#include "WorldMeshes.hpp"
#include "raylib.h"

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

namespace sky {

struct Prog {
    Shader sh{};
    std::unordered_map<std::string, int> locs;
    int loc(const char* name);
    void setF(const char* n, float v);
    void setV2(const char* n, Vector2 v);
    void setV3(const char* n, Vector3 v);
    void setV4(const char* n, Vector4 v);
    void setM(const char* n, Matrix m);
};

struct CloudPuff { Vector3 pos; float size; float shade; float rot; int tex = 0; };

/// An aircraft drawn from recorded data (replays) instead of the live battle.
struct ExtraJet {
    AirframeStyle style = AirframeStyle::StealthTwinTail;
    Vec3 pos;
    Quat q;
    Team team = Team::Blue;
    AircraftId id = AircraftId::F22;
    float gear = 0, throttle = 0.6f, ab = 0;
    int skin = 0;
};
struct ExtraMissile { Vec3 pos, dir; };

/// Everything that animates on an aircraft model for one frame.
struct JetPose {
    float gear = 0, throttle = 0, ab = 0, hitFlash = 0, damage = 0;
    float pitch = 0, roll = 0, yaw = 0;     // control inputs -1..1 (surfaces follow them)
    float flaps = 0;                        // degrees down
    float wheelSpin = 0;                    // radians
    float compress = 0;                     // gear compression (m)
    float mach = 0, altitude = 0;           // plume shape
    bool lights = false;                    // navigation / strobe / landing lights
    int id = 0;
};

/// A Training Academy gate drawn as a ring in the world.
struct GateMarker {
    Vec3 pos, dir;
    float radius = 100;
    Color color = {120, 255, 190, 255};
    float glow = 1.0f;       // emissive strength
};

struct FrameParams {
    Camera3D cam{};
    float nearPlane = 0.5f, farPlane = 70000.0f;
    const Battle* battle = nullptr;
    int hideJetId = -1;           // jet not drawn (player in cockpit view)
    Lighting L;
    double time = 0;
    float blackout = 0, redout = 0, damage = 0, speedFx = 0;
    bool showRunwayLights = true;
    bool bloom = true;
    GraphicsQuality quality = GraphicsQuality::High;
    float exposureBoost = 1.0f;
    bool shadows = true;
    bool isPip = false;   // secondary small view (missile / target camera)
    const Battle* groundFrom = nullptr;                  // ground units source if `battle` is a stand-in
    const std::vector<ExtraJet>* extraJets = nullptr;    // replay aircraft
    const std::vector<ExtraMissile>* extraMissiles = nullptr;
    Vector3 shadowFocus{0, 0, 0};
    float rain = 0;                 // 0..1 rain intensity
    Vector3 rainWind{0, 0, 0};      // rain velocity relative to the camera (m/s, without the fall speed)
    const std::vector<GateMarker>* gates = nullptr;   // training gates (rings), current first
    const std::vector<Vec3>* corridor = nullptr;      // optional corridor markers along the route
};

class Renderer {
public:
    bool init(const Terrain& t, const WorldLayout& layout, int width, int height);
    const Dressing& dressing() const { return dressing_; }
    /// Swap to another terrain (location change): rebuilds terrain meshes, textures, scenery, clouds.
    void rebuildWorld(const Terrain& t, const WorldLayout& layout);
    void applyTerrainInfo();
    void shutdown();
    void resize(int width, int height);

    /// Renders the 3D world into the HDR target. Leaves the HDR target bound if keepBound.
    void renderWorld(const FrameParams& f);
    /// Additional 3D pass into the HDR target with a cleared depth buffer (cockpit).
    void beginOverlay3D(const Camera3D& cam, float nearP, float farP);
    void endOverlay3D();
    void clearDepth();
    /// Bloom + tonemap into the currently bound framebuffer (call between BeginDrawing/EndDrawing).
    void composite(const FrameParams& f);
    /// Render a second, small view (missile / target / hit camera) into its own targets.
    void renderPip(const FrameParams& f, int w, int h);
    Texture2D pipTexture() const { return pipOut_.texture; }
    /// Lens flare / sun glare sprites (after composite, 2D).
    void drawSunFlare(const FrameParams& f, int w, int h);

    Effects& effects() { return effects_; }
    WorldMeshes& world() { return world_; }
    const JetModel& jetModel(AirframeStyle s) const { return jets_[static_cast<int>(s)]; }
    Prog& lit() { return lit_; }
    Material& litMaterial() { return litMat_; }
    Prog& particle() { return particle_; }
    Prog& jetProg() { return jet_; }
    Material& jetMaterial() { return jetMat_; }
    void setAtmosphere(Prog& p, const FrameParams& f);
    void drawJet(const Jet& j, const FrameParams& f, bool canopy = true, Vec3 origin = Vec3{});
    void drawJetModel(AirframeStyle style, const Matrix& M, Vector3 paintA, Vector3 paintB, float camo,
                      float gearPos, float stabDeg, float throttle, float ab, float hitFlash, float damage,
                      const FrameParams& f, bool canopy = true);
    void drawJetModel(AirframeStyle style, const Matrix& M, Vector3 paintA, Vector3 paintB, float camo, const JetPose& pose,
                      const FrameParams& f, bool canopy = true);
    /// Level of detail used for an aircraft at `dist` metres (0 = full detail).
    static int jetLod(float dist) { return dist < 230.0f ? 0 : (dist < 1100.0f ? 1 : 2); }
    int width() const { return w_; }
    int height() const { return h_; }
    bool hdr() const { return hdr_; }
    double lastWorldMs() const { return worldMs_; }
    int drawCalls() const { return drawCalls_; }
    long triangles() const;
    // GPU pass profiler (serialises the pipeline; for --bench / F3 diagnostics only)
    void setProfiling(bool on) { profiling_ = on; prof_.clear(); profFrames_ = 0; }
    bool profiling() const { return profiling_; }
    void profMark(const char* pass);
    std::string profReport() const;

private:
    void profBegin();
    bool profiling_ = false;
    std::vector<std::pair<std::string, double>> prof_;
    int profFrames_ = 0;
    std::chrono::steady_clock::time_point profLast_;
    void buildGroundModels();
    void renderShadows(const FrameParams& f);
    void buildClouds(const Terrain& t);
    void drawSky(const FrameParams& f);
    void drawTerrain(const FrameParams& f);
    void drawStatics(const FrameParams& f);
    void drawGroundTargets(const FrameParams& f);
    void drawCloudLayer(const FrameParams& f);
    void drawCloudPuffs(const FrameParams& f);
    void drawShadows(const FrameParams& f);
    RenderTexture2D makeTarget(int w, int h, bool hdr, bool depth);
    void freeTarget(RenderTexture2D& rt);

    const Terrain* terrain_ = nullptr;
    WorldMeshes world_;
    Effects effects_;
    JetModel jets_[6];
    Mesh flame_{}, missile_{};
    Mesh tank_{}, truck_{}, aaa_{}, sam_{}, bunker_{}, radar_{};
    Mesh gun_{}, artillery_{}, oilRig_{}, fuelTank_{}, hangar_{}, command_{}, ship_{};
    Mesh powerPlant_{}, refinery_{}, bridgeDeck_{}, bridgePier_{}, commTower_{}, factory_{}, locomotive_{}, wagon_{},
         launcher_{}, ammo_{}, substation_{}, tanker_{}, drogue_{}, carrier_{};
    void buildCarrier();
    void drawObstacles(const FrameParams& f);
    void drawGates(const FrameParams& f);
    void rebuildObstacles();
    Mesh obstacleMesh_{}, ringMesh_{}, beaconMesh_{};
    bool obstacleMeshValid_ = false;
    int obstacleVer_ = -1;
    std::vector<Vector3> mastLights_;
    struct LightSprite { Vector3 pos; float size; Color col; };
    std::vector<LightSprite> lights_;
    struct Spin { float angle = 0; double t = -1; };
    std::unordered_map<int, Spin> wheelSpin_;
    void drawLightSprites(const FrameParams& f);
    bool sphereVisible(const FrameParams& f, Vector3 c, float r) const;
    void drawCarrier(const FrameParams& f);
    void drawRain(const FrameParams& f);
    void drawTanker(const FrameParams& f);
    void drawDressing(const FrameParams& f);
    Dressing dressing_;
    std::vector<CloudPuff> puffs_;

    Prog sky_, terrain_p_, water_, lit_, jet_, flameP_, cloud_, runway_, particle_, bright_, blur_, composite_;
    Material terrainMat_{}, waterMat_{}, litMat_{}, jetMat_{}, flameMat_{}, cloudMat_{}, runwayMat_{};

    RenderTexture2D scene_{}, bloomA_{}, bloomB_{}, bloomC_{}, bloomD_{};
    RenderTexture2D shadowRT_{};
    RenderTexture2D pipScene_{}, pipOut_{};
    int pipW_ = 0, pipH_ = 0;
    Texture2D shadowDepth_{};
    Matrix lightVP_{};
    bool shadowsActive_ = false;
    Prog shadowP_;
    Material shadowMat_{};
    int w_ = 0, h_ = 0;
    bool hdr_ = true;
    double worldMs_ = 0;
    int drawCalls_ = 0;
};

}  // namespace sky
