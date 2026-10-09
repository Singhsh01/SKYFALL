// Heads-up display: flight symbology, targeting, weapons, minimap, kill feed, warnings,
// mission checklist, radio captions and telemetry overlay.
#pragma once

#include "../app/CameraMode.hpp"
#include "../app/Settings.hpp"
#include "../combat/Combat.hpp"
#include "../mission/Mission.hpp"
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
    int drawCalls = 0;
    size_t particles = 0;
    bool showDebug = false;
    bool showHelp = false;
    bool arena = false;
    const char* timeOfDay = "";
    Rectangle combiner{0, 0, 0, 0};   // cockpit HUD glass in screen space
    Texture2D minimap{};
    bool gpws = false;
    double respawnTimer = -1;
    bool dead = false;
    std::string deathReason;
    bool missionDone = false;
    double fixedHz = 120;
    int droppedSteps = 0;
    bool gamepad = false;
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
