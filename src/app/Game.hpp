// Application: screens, game modes, fixed-step simulation loop, cameras, audio/HUD glue,
// screenshot capture and benchmark modes.
#pragma once

#include "../audio/Audio.hpp"
#include "../combat/Combat.hpp"
#include "../mission/Mission.hpp"
#include "../render/Lighting.hpp"
#include "../render/Renderer.hpp"
#include "../telemetry/FrameStats.hpp"
#include "../ui/Cockpit.hpp"
#include "../ui/Hud.hpp"
#include "../ui/Menus.hpp"
#include "CameraMode.hpp"
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

enum class Screen { Title, Campaign, Hangar, Settings, Playing, Paused, Help };
enum class GameMode { None, Mission01, Arena };

class Game {
public:
    int run(const Options& opt);

private:
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
    Battle battle_{terrain_};
    std::optional<Mission> mission_;
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
    int missionNo_ = 1;
    std::vector<double> sessionFrames_;
};

}  // namespace sky
