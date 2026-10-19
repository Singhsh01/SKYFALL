// Heads-up display: flight symbology, targeting, weapons, minimap, kill feed, warnings,
// mission checklist, radio captions and telemetry overlay.
#pragma once

#include "../app/CameraMode.hpp"
#include "../app/PipState.hpp"
#include "../telemetry/Replay.hpp"
#include "../app/Settings.hpp"
#include "../combat/Combat.hpp"
#include "../mission/Campaign.hpp"
#include "../mission/Mission.hpp"
#include "../mission/Training.hpp"
#include "../telemetry/FrameStats.hpp"
#include "raylib.h"

#include <deque>
#include <string>

namespace sky {

struct RadioMsg { std::string who, text; double t = 0, dur = 5; };
struct Popup { std::string text; double t = 0; Color color; };

struct HudContext {
    const Battle* battle = nullptr;
    const Jet* player = nullptr;
    Camera3D cam{};
    int w = 1280, h = 720;
    CameraMode camMode = CameraMode::Chase;
    const Mission* mission = nullptr;
    const Settings* settings = nullptr;
    double time = 0;
    Percentiles frame{};
    double worldMs = 0;
    double simMs = 0, hudMs = 0;
    int drawCalls = 0;
    size_t particles = 0;
    bool showDebug = false;
    bool showHelp = false;
    bool arena = false;
    const char* timeOfDay = "";
    Rectangle combiner{0, 0, 0, 0};   // cockpit HUD glass in screen space
    Texture2D minimap{};
    double minimapHalf = 20000.0;   // metres from the map centre to the texture edge
    bool gpws = false;
    double respawnTimer = -1;
    bool dead = false;
    std::string deathReason;
    bool missionDone = false;
    std::string missionNext;
    double fixedHz = 120;
    int droppedSteps = 0;
    bool gamepad = false;
    // picture-in-picture camera
    bool pipActive = false;
    const PipState* pip = nullptr;
    Texture2D pipTex{};
    int pipW = 480, pipH = 300;
    // replay
    bool replay = false;
    double replayT = 0, replayFrom = 0, replayTo = 0, deathTime = 0;
    const LossReport* loss = nullptr;
    // missions
    RunwayInfo runway{};
    int approachDir = 1;          // mission 3 glide path: +1 runway 09, -1 runway 27
    DeckInfo deck{};                 // aircraft carrier (inactive unless a carrier mission)
    const CampaignMissionDef* campaign = nullptr;
    bool showGlidePath = false;
    // Training Academy
    const TrainingSession* training = nullptr;
    bool trainingDone = false;
    const ScoreCard* trainingResult = nullptr;
    bool trainingNewBest = false;
    std::string trainingTitle;          // "LEVEL 4  OBSTACLE MANOEUVRING"
    std::string trainingNext;           // prompt under the debrief card
};

class Hud {
public:
    void update(double dt);
    void draw(const HudContext& c);
    void radio(const std::string& who, const std::string& text, double dur = 5.0);
    void popup(const std::string& text, Color col);
    void hitMarker(bool kill) { hit_ = 0.25; killHit_ = kill; }
    void clear() { radio_.clear(); popups_.clear(); hit_ = 0; }

private:
    void flightSymbology(const HudContext& c, bool cockpit);
    void pitchLadder(const HudContext& c);
    void targets(const HudContext& c);
    void weapons(const HudContext& c);
    void minimap(const HudContext& c);
    void killFeed(const HudContext& c);
    void warnings(const HudContext& c);
    void missionPanel(const HudContext& c);
    void glidePath(const HudContext& c);
    void objectiveMarkers(const HudContext& c);
    void refuelCues(const HudContext& c);
    void trainingPanel(const HudContext& c);
    void trainingMarkers(const HudContext& c);
    void trainingApproach(const HudContext& c);
    void trainingDebrief(const HudContext& c);
    void minimalSymbology(const HudContext& c);
    void hintsBar(const HudContext& c);
    void pipPanel(const HudContext& c);
    void replayOverlay(const HudContext& c);
    void radioPanel(const HudContext& c);
    void popups(const HudContext& c);
    void debug(const HudContext& c);
    void help(const HudContext& c);
    Vector2 project(const HudContext& c, Vector3 p, bool* inFront = nullptr) const;

    std::deque<RadioMsg> radio_;
    std::deque<Popup> popups_;
    double hit_ = 0;
    bool killHit_ = false;
    double time_ = 0;
};

}  // namespace sky
