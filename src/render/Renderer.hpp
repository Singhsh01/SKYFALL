// Rendering orchestration: HDR scene target, sky, terrain, water, clouds, aircraft, effects,
// bloom + ACES tone mapping post-process.
#pragma once

#include "../app/Settings.hpp"
#include "../combat/Combat.hpp"
#include "Effects.hpp"
#include "JetMesh.hpp"
#include "Lighting.hpp"
#include "WorldMeshes.hpp"
#include "raylib.h"

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
};
struct ExtraMissile { Vec3 pos, dir; };

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
};

class Renderer {
public:
    bool init(const Terrain& t, int width, int height);
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
    int width() const { return w_; }
    int height() const { return h_; }
    bool hdr() const { return hdr_; }
    double lastWorldMs() const { return worldMs_; }
    int drawCalls() const { return drawCalls_; }

private:
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
