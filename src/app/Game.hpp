// Application: screens, game modes, fixed-step simulation loop, cameras, audio/HUD glue,
// screenshot capture and benchmark modes.
#pragma once
#include <chrono>

#include "../audio/Audio.hpp"
#include "../combat/Combat.hpp"
#include "../audio/Speech.hpp"
#include "TerrainDownload.hpp"
#include "../mission/Campaign.hpp"
#include "../mission/Mission.hpp"
#include "../render/Lighting.hpp"
#include "../render/Renderer.hpp"
#include "../telemetry/FrameStats.hpp"
#include "../telemetry/Replay.hpp"
#include "../ui/Cockpit.hpp"
#include "../ui/Hud.hpp"
#include "../ui/Menus.hpp"
#include "CameraMode.hpp"
#include "PipState.hpp"
#include "Input.hpp"
#include "Settings.hpp"

#include <functional>
#include <optional>
#include <string>

namespace sky {

struct Options {
    std::string captureDir;     // --capture <dir>: scripted screenshots then exit
    double benchSeconds = 0;    // --bench <s>: scripted benchmark then exit
    std::string startMode;      // --mode arena|m01 (skip the title screen)
    int width = 0, height = 0;  // override window size
    bool windowed = false;
    int quality = -1;
};

enum class Screen { Title, Campaign, Hangar, Settings, Playing, Paused, Help, Briefing, Locations };
enum class GameMode { None, Mission01, Arena };

class Game {
public:
    int run(const Options& opt);
#ifdef __EMSCRIPTEN__
    void webTick();
#endif

private:
#ifdef __EMSCRIPTEN__
    std::chrono::steady_clock::time_point webLast_{};
#endif
    // lifecycle
    void initWindow();
    void shutdown();
    // modes
    void startTitle();
    void startMission01() { startMission(1); }
    void startMission(int n);
    void startArena(bool keepPlayerState);
    void startHangar();
    void respawnPlayer();
    // per-frame
    void frame(double dt, bool render = true);
    void handleGlobalKeys();
    void updatePlaying(double dt);
    void updateTitleScene(double dt);
    void simulate(double dt, bool withPlayerInput);
    void processEvents();
    void updateCamera(double dt);
    void updateAudio();
    void renderFrame();
    // cinematics (Cinematics.cpp)
    void updatePip(double dt);
    void recordFrame();
    void beginReplay();
    void updateReplay(double dt);
    void endReplay();
    FrameParams makeReplayFrameParams();
    void drawMenus();
    void drawTitle();
    void drawCampaign();
    void drawHangar();
    void drawSettings();
    void drawPause();
    FrameParams makeFrameParams();
    void applySettings();
    void saveScreenshot(const std::string& path);
    // scripted modes
    int runCapture();
    int runBench();
    void scriptedStep(double seconds, const std::function<void(double)>& pilot, bool effects = true);

    Options opt_;
    Settings settings_;
    std::string settingsPath_;
    Terrain terrain_;
    WorldLayout layout_ = WorldLayout::build(terrain_);   // towns, roads, railway, bridges
    Battle battle_{terrain_};
    std::optional<Mission> mission_;
    // pre-mission briefing by Captain Avery Langlois (Wing Operations)
    Speech speech_;
    std::string briefHeader_, briefTitle_, briefText_;
    std::vector<std::string> briefObjectives_;
    double briefT_ = 0;
    CameraMode briefCam_ = CameraMode::Chase;
    void beginBriefing(int missionNo);   // 0 = Dogfight Arena
    void updateBriefing(double dt);
    void drawBriefing();
    void launchFromBriefing();
    void radio(Speaker who, const std::string& text, double dur, bool voice = true);
    void startMissionBriefed(int n) { startMission(n); if (switchTo_.empty()) beginBriefing(n); }
    void startArenaBriefed() { freeFlight_ = false; startArena(false); beginBriefing(0); }
    // ---- world locations (real terrain) ----
    bool freeFlight_ = false;
    std::string locStatus_;          // message on the locations screen
    std::string locPending_;         // web: location being downloaded
    size_t voicedStep_ = 0;
    bool rain_ = false;              // rain and overcast (carrier missions)          // last campaign objective announced by voice
    std::string voiceDir();
    std::string terrainDir();
    TerrainDownloader download_;
    Screen locReturn_ = Screen::Title;
    std::string switchTo_;           // location switch requested from a menu (done at the next frame start)
    int switchMission_ = 0;          // campaign mission to start after the switch
    bool loadLocation(const std::string& id, bool showProgress = true);
    bool locationAvailable(const std::string& id);
    std::string terrainPath(const std::string& id);
    double safeAltitude(Vec3 a, Vec3 b, double margin) const;
    void drawLocations();
    const CampaignMissionDef* campaignDef_ = nullptr;   // missions 04-40
    CampaignDirector director_;
    // dynamic resolution: 3D scene scale chosen from measured frame times
    double dynScale_ = 1.0;
    double dynTimer_ = 0, dynAccum_ = 0;
    int dynFrames_ = 0;
    double dynUpBlockedUntil_ = 0;
    double dynLastUp_ = -100;
    // CPU cost of the last frame's simulation and HUD (ms), for F3 and the benchmark
    double simMs_ = 0, hudMs_ = 0;
    double chimneyTimer_ = 0;
    std::vector<double> benchSim_, benchHud_;
    bool startModeFromString(const std::string& m);
    void updateDynamicResolution(double dt);
    MissionContext missionContext();
    Renderer renderer_;
    Hud hud_;
    Cockpit cockpit_;
    Audio audio_;
    Input input_;
    FrameStats stats_{900};
    FixedStepper stepper_;
    Lighting lighting_;
    TimeOfDay tod_ = TimeOfDay::Morning;

    Screen screen_ = Screen::Title;
    Screen settingsReturn_ = Screen::Title;
    GameMode mode_ = GameMode::None;
    CameraMode camMode_ = CameraMode::Chase;
    bool quit_ = false;
    bool showDebug_ = false, showHelp_ = false;
    double time_ = 0;
    // pending one-frame player commands
    PlayerCommands pendingCmd_{};
    // player status
    bool playerDead_ = false;
    double respawnTimer_ = -1;
    std::string deathReason_;
    bool missionDone_ = false;
    int radioStage_ = 0;
    double damagePulse_ = 0;
    float blackout_ = 0, redout_ = 0;
    bool gpws_ = false;
    // camera
    Camera3D cam_{};
    Quat chaseQ_;
    bool chaseInit_ = false;
    double headYaw_ = 0, headPitch_ = 0;
    double orbitYaw_ = 2.4, orbitPitch_ = 0.12, zoom_ = 1.0;
    double shake_ = 0;
    double fovCur_ = 70;
    Vector3 deathCamPos_{}, deathTarget_{};
    float nearPlane_ = 0.5f, farPlane_ = 70000.0f;
    // menus
    ui::MenuNav nav_;
    int hangarSel_ = 0;
    double titleTime_ = 0;
    bool capturing_ = false;
    bool hideHud_ = false;
    // picture-in-picture + replay
    PipState pip_;
    FlightRecorder recorder_;
    Battle replayStage_{terrain_};
    bool replayActive_ = false, replayExploded_ = false;
    double replayT_ = 0, replayFrom_ = 0, replayTo_ = 0, deathTime_ = 0, deathWait_ = -1;
    Vec3 crashPos_, obsPos_, obsPos2_;
    LossReport lossReport_;
    LossCause lossCause_ = LossCause::Crash;
    std::string killerName_;
    std::vector<ExtraJet> replayJets_;
    std::vector<ExtraMissile> replayMissiles_;
    double lastKillTime_ = -10, lastHitTime_ = -10;
    Vec3 lastKillPos_, lastHitPos_;
    int missionNo_ = 1;
    bool pauseJustOpened_ = false;
    int rollDir_ = 0;          // active aileron roll (double-tap)
    double rollDone_ = 0;      // radians rolled so far
    std::vector<double> sessionFrames_;
};

}  // namespace sky
