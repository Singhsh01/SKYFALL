#include "Game.hpp"

#include "../render/Convert.hpp"
#include "../ui/Font.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <memory>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>

namespace sky {

using ui::Align;

namespace {
const Color kAccent = {150, 255, 205, 255};

Quat slerp(const Quat& a, const Quat& b, double t) {
    Quaternion r = QuaternionSlerp(toRlQ(a), toRlQ(b), static_cast<float>(t));
    return Quat{r.w, r.x, r.y, r.z}.normalized();
}

float panFor(const Camera3D& cam, Vector3 p) {
    Matrix v = GetCameraMatrix(cam);
    Vector3 right = {v.m0, v.m4, v.m8};
    Vector3 d = Vector3Normalize(Vector3Subtract(p, cam.position));
    return Vector3DotProduct(d, right);
}
}  // namespace

// ------------------------------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------------------------------
void Game::initWindow() {
    unsigned flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI;
    if (settings_.vsync && !capturing_) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    int w = opt_.width > 0 ? opt_.width : settings_.width;
    int h = opt_.height > 0 ? opt_.height : settings_.height;
    InitWindow(w, h, "SKYFALL - Combat Flight Simulator");
    SetExitKey(KEY_NULL);  // Esc opens the pause menu instead of quitting
    if (settings_.fullscreen && !opt_.windowed && !capturing_) ToggleFullscreen();
    SetTargetFPS(capturing_ ? 0 : 120);
    ui::loadFonts();
}

int Game::run(const Options& opt) {
    opt_ = opt;
    capturing_ = !opt.captureDir.empty() || opt.benchSeconds > 0;
    settingsPath_ = defaultSettingsPath();
    settings_.load(settingsPath_);
    if (opt.quality >= 0) settings_.quality = static_cast<GraphicsQuality>(std::clamp(opt.quality, 0, 2));
    initWindow();
    // splash while the world is generated
    BeginDrawing();
    ClearBackground(Color{8, 12, 16, 255});
    ui::logo(GetScreenWidth() * 0.5f - 200, GetScreenHeight() * 0.42f, 1.0f, 0);
    ui::text("GENERATING TERRAIN, AIRBASE AND AIRCRAFT...", GetScreenWidth() * 0.5f, GetScreenHeight() * 0.62f, 16, Color{200, 210, 210, 200}, Align::Center);
    EndDrawing();
    renderer_.init(terrain_, GetScreenWidth(), GetScreenHeight());
    cockpit_.init();
    if (!capturing_ || opt.benchSeconds > 0) audio_.init();
    applySettings();
    lighting_ = Lighting::preset(tod_);

    if (!opt.captureDir.empty()) { int r = runCapture(); shutdown(); return r; }
    if (opt.benchSeconds > 0) { int r = runBench(); shutdown(); return r; }

    if (opt.startMode == "arena") startArena(false);
    else if (opt.startMode == "m01") startMission01();
    else startTitle();

    auto last = std::chrono::steady_clock::now();
    while (!quit_ && !WindowShouldClose()) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        dt = std::min(dt, 0.25);
        frame(dt);
        stats_.push(dt * 1000.0);
        sessionFrames_.push_back(dt * 1000.0);
        if (sessionFrames_.size() > 200000) sessionFrames_.erase(sessionFrames_.begin(), sessionFrames_.begin() + 100000);
    }
    {
        // session summary for diagnostics (skip the first 120 frames: loading / warm-up)
        std::vector<double> v(sessionFrames_.begin() + std::min<size_t>(120, sessionFrames_.size()), sessionFrames_.end());
        Percentiles p = FrameStats::computeFrom(v);
        TraceLog(LOG_INFO, "SKYFALL: session %zu frames, mean %.2f ms (%.0f fps), p50 %.2f, p95 %.2f, p99 %.2f ms, scene %dx%d, window %dx%d, quality %d",
                 p.samples, p.mean, p.mean > 0 ? 1000.0 / p.mean : 0.0, p.p50, p.p95, p.p99, renderer_.width(), renderer_.height(),
                 GetScreenWidth(), GetScreenHeight(), static_cast<int>(settings_.quality));
    }
    settings_.save(settingsPath_);
    shutdown();
    return 0;
}

void Game::shutdown() {
    audio_.shutdown();
    cockpit_.unload();
    renderer_.shutdown();
    ui::unloadFonts();
    CloseWindow();
}

void Game::applySettings() {
    audio_.applySettings(settings_);
    battle_.player();  // ensure exists
    if (!battle_.jets().empty())
        battle_.player().fm->setHandling(settings_.simulationHandling ? HandlingMode::Simulation : HandlingMode::Assisted);
}

// ------------------------------------------------------------------------------------------
// Modes
// ------------------------------------------------------------------------------------------
void Game::startTitle() {
    screen_ = Screen::Title;
    mode_ = GameMode::None;
    mission_.reset();
    tod_ = TimeOfDay::GoldenHour;
    lighting_ = Lighting::preset(tod_);
    Jet& p = battle_.createPlayer(AircraftId::F22);
    p.fm->resetAirborne({-2000, 2650, 6000}, -0.15, 210.0, 0.8);
    // two wingmen for the cinematic formation (positions driven by the title scene)
    battle_.debugAddJet(Team::Blue, AircraftId::F22, {-2100, 2660, 6040}, -0.15, 210);
    battle_.debugAddJet(Team::Blue, AircraftId::F22, {-2100, 2640, 5960}, -0.15, 210);
    battle_.setAiEnabled(false);
    renderer_.effects().clear();
    hud_.clear();
    camMode_ = CameraMode::External;
    orbitYaw_ = 2.6; orbitPitch_ = 0.05; zoom_ = 1.6;
    nav_.focus = 0;
    playerDead_ = false;
    applySettings();
}

void Game::startMission01() {
    screen_ = Screen::Playing;
    mode_ = GameMode::Mission01;
    tod_ = TimeOfDay::Morning;
    lighting_ = Lighting::preset(tod_);
    Jet& p = battle_.createPlayer(AircraftId::F22);
    p.fm->resetOnRunway(false);
    battle_.setAiEnabled(true);
    mission_ = makeMission01();
    missionDone_ = false;
    radioStage_ = 0;
    playerDead_ = false;
    respawnTimer_ = -1;
    renderer_.effects().clear();
    hud_.clear();
    camMode_ = CameraMode::Cockpit;
    chaseInit_ = false;
    headYaw_ = headPitch_ = 0;
    applySettings();
    hud_.radio("TOWER", "Skyfall One, runway 09, wind calm. Start-up approved.", 7);
    audio_.play(Sfx::Radio);
}

void Game::startArena(bool keepPlayerState) {
    screen_ = Screen::Playing;
    mode_ = GameMode::Arena;
    if (!keepPlayerState) {
        tod_ = TimeOfDay::Morning;
        lighting_ = Lighting::preset(tod_);
        Jet& p = battle_.createPlayer(AircraftId::F22);
        p.fm->resetAirborne({-7500, 2100, 2600}, 0.0, 230.0, 0.8);
        camMode_ = CameraMode::Chase;
        renderer_.effects().clear();
    }
    mission_.reset();
    missionDone_ = false;
    battle_.setAiEnabled(true);
    BattleConfig cfg;
    cfg.enemyJets = 3;
    cfg.allyJets = 2;
    cfg.seed = static_cast<unsigned>(GetRandomValue(1, 1000000));
    battle_.start(cfg);
    playerDead_ = false;
    respawnTimer_ = -1;
    chaseInit_ = false;
    applySettings();
    hud_.radio("AWACS", "Skyfall One, multiple bandits bearing 090. Weapons free.", 6);
    audio_.play(Sfx::Radio);
}

void Game::startHangar() {
    screen_ = Screen::Hangar;
    tod_ = TimeOfDay::Morning;
    lighting_ = Lighting::preset(tod_);
    Jet& p = battle_.createPlayer(static_cast<AircraftId>(hangarSel_));
    p.fm->resetOnRunway(false);
    const RunwayInfo& rw = terrain_.runway();
    p.fm->mutableState().pos = {rw.center.x, rw.center.y + FlightModel::kGearHeight, rw.center.z + 300};
    p.fm->mutableState().q = Quat::fromEuler(-0.6, 0, 0);
    battle_.setAiEnabled(false);
    camMode_ = CameraMode::External;
    orbitYaw_ = 2.2; orbitPitch_ = 0.18; zoom_ = 1.0;
    renderer_.effects().clear();
}

void Game::respawnPlayer() {
    Jet& p = battle_.player();
    const AircraftProfile& prof = p.fm->profile();
    p.fm = std::make_unique<FlightModel>(prof, terrain_);
    p.fm->resetAirborne({-7000, 2200, 2400}, 0.0, 230.0, 0.8);
    p.fm->setHandling(settings_.simulationHandling ? HandlingMode::Simulation : HandlingMode::Assisted);
    p.alive = true;
    p.health = 100;
    p.missiles = prof.missiles;
    p.flareCharges = 3;
    p.rounds = prof.cannonRounds;
    p.lockTarget = -1;
    p.lockProgress = 0;
    p.deadTimer = 0;
    playerDead_ = false;
    respawnTimer_ = -1;
    chaseInit_ = false;
    hud_.radio("AWACS", "Skyfall One, you're back in the fight.", 4);
}

// ------------------------------------------------------------------------------------------
// Frame
// ------------------------------------------------------------------------------------------
void Game::frame(double dt, bool render) {
    time_ += dt;
    input_.update(dt, settings_);
    handleGlobalKeys();
    hud_.update(dt);
    {
        // 3D scene resolution follows the window size in points (not Retina pixels) times a
        // quality scale; the HUD is still drawn at native resolution.
        double scale = settings_.quality == GraphicsQuality::Low ? 0.7 : (settings_.quality == GraphicsQuality::Medium ? 0.85 : 1.0);
        int rw = std::min(1920, static_cast<int>(GetScreenWidth() * scale)), rh = static_cast<int>(GetScreenHeight() * scale * std::min(1.0, 1920.0 / std::max(1.0, GetScreenWidth() * scale)));
        if (rw != renderer_.width() || rh != renderer_.height()) renderer_.resize(rw, rh);
    }

    switch (screen_) {
        case Screen::Playing: updatePlaying(dt); break;
        case Screen::Title: titleTime_ += dt; updateTitleScene(dt); break;
        case Screen::Hangar: updateTitleScene(dt); break;
        default: break;
    }
    updateCamera(dt);
    updateAudio();
    if (render) renderFrame();
}

void Game::handleGlobalKeys() {
    const InputState& in = input_.state();
    if (in.debug) showDebug_ = !showDebug_;
    if (in.timeOfDay) {
        tod_ = static_cast<TimeOfDay>((static_cast<int>(tod_) + 1) % static_cast<int>(TimeOfDay::Count));
        lighting_ = Lighting::preset(tod_);
    }
    if (in.screenshot) {
        std::filesystem::create_directories("screenshots");
        char name[128];
        std::snprintf(name, sizeof name, "screenshots/skyfall_%lld.png", static_cast<long long>(std::time(nullptr)));
        saveScreenshot(name);
        hud_.radio("SYSTEM", std::string("Screenshot saved: ") + name, 3);
    }
}

void Game::updateTitleScene(double dt) {
    // fly the lead aircraft straight and level; wingmen hold echelon positions
    int n = stepper_.advance(dt);
    for (int i = 0; i < n; ++i) battle_.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    if (screen_ == Screen::Title && battle_.jets().size() >= 3) {
        const FlightState& lead = battle_.player().fm->state();
        const Vec3 offs[2] = {{-28, -4, 24}, {-56, -8, 48}};
        for (int k = 0; k < 2; ++k) {
            FlightState& w = battle_.jets()[k + 1]->fm->mutableState();
            Vec3 o = offs[k];
            double t = time_ * 0.6 + k;
            o.y += std::sin(t) * 1.5;
            o.z += std::cos(t * 0.7) * 2.0;
            w.pos = lead.pos + lead.q.rotate(o);
            w.q = lead.q;
            w.vel = lead.vel;
            w.rpm = lead.rpm;
            w.throttle = lead.throttle;
            w.abLevel = lead.abLevel;
        }
        // keep the formation over the clouds
        if (lead.pos.x > 12000) battle_.player().fm->resetAirborne({-6000, 2650, 6000}, -0.15, 210.0, 0.8);
    }
    renderer_.effects().update(dt, battle_, lighting_, cam_.position, {});
    processEvents();
}

void Game::simulate(double dt, bool withPlayerInput) {
    const InputState& in = input_.state();
    ControlInputs ci;
    if (withPlayerInput) {
        ci.pitch = in.pitch;
        ci.roll = in.roll;
        ci.yaw = in.yaw;
        ci.throttleRate = in.throttleRate;
        ci.brake = in.brake;
        ci.airbrake = in.airbrake;
    }
    PlayerCommands cmd = pendingCmd_;
    cmd.fireGun = withPlayerInput && in.gun;
    int n = stepper_.advance(dt);
    for (int i = 0; i < n; ++i) {
        battle_.update(FlightModel::kDt, ci, cmd);
        cmd.fireMissile = cmd.flares = cmd.cycleTarget = false;  // edges only once
        if (mission_) {
            MissionContext ctx{&battle_.player().fm->state(), &battle_.player().fm->profile(), &terrain_, 0};
            mission_->update(ctx, FlightModel::kDt);
        }
    }
    if (n > 0) pendingCmd_ = PlayerCommands{};
}

void Game::updatePlaying(double dt) {
    const InputState& in = input_.state();
    Jet& p = battle_.player();
    FlightModel& fm = *p.fm;
    if (in.pause) { screen_ = Screen::Paused; nav_.focus = 0; audio_.setPaused(true); return; }
    if (in.help) showHelp_ = !showHelp_;
    if (in.camera) { camMode_ = nextCamera(camMode_); chaseInit_ = false; headYaw_ = headPitch_ = 0; }
    if (in.reset) {
        if (mode_ == GameMode::Mission01 && !missionDone_) { startMission01(); return; }
        respawnPlayer();
    }
    if (!playerDead_) {
        if (in.engine) { fm.requestEngineStart(); audio_.play(Sfx::Click); }
        if (in.parkingBrake) { fm.toggleParkingBrake(); audio_.play(Sfx::Click); }
        if (in.gear) {
            if (fm.toggleGear()) audio_.play(Sfx::Gear, 0.6f);
            else hud_.radio("SYSTEM", "Gear handle locked: weight on wheels.", 3);
        }
        if (in.missile) {
            if (p.missiles <= 0) hud_.radio("SYSTEM", "Missiles reloading...", 2);
            pendingCmd_.fireMissile = true;
        }
        if (in.flares) pendingCmd_.flares = true;
        if (in.cycleTarget) pendingCmd_.cycleTarget = true;
    }
    if (mode_ == GameMode::Mission01 && missionDone_ && in.confirm) {
        startArena(true);
        hud_.radio("AWACS", "Bandits inbound from the east. Skyfall One, engage!", 6);
    }
    simulate(dt, !playerDead_);
    processEvents();

    // mission narration / completion
    if (mission_) {
        size_t idx = mission_->stepIndex();
        if (radioStage_ == 0 && idx >= 1) { hud_.radio("TOWER", "Engine start confirmed. Release brakes when ready.", 5); audio_.play(Sfx::Radio); radioStage_ = 1; }
        if (radioStage_ == 1 && idx >= 3) { hud_.radio("TOWER", "Skyfall One, cleared for take-off runway 09.", 5); audio_.play(Sfx::Radio); radioStage_ = 2; }
        if (radioStage_ == 2 && idx >= 6) { hud_.radio("TOWER", "Positive climb. Gear up, contact AWACS.", 5); audio_.play(Sfx::Radio); radioStage_ = 3; }
        if (mission_->status() == MissionStatus::Complete && !missionDone_) {
            missionDone_ = true;
            hud_.radio("AWACS", "Skyfall One, picture: bandits inbound from the east. Press ENTER to engage.", 9);
            audio_.play(Sfx::Radio);
        }
        if (mission_->status() == MissionStatus::Failed && !playerDead_) {
            playerDead_ = true;
            deathReason_ = "MISSION FAILED: " + mission_->failure();
            deathCamPos_ = cam_.position;
            deathTarget_ = toRl(fm.state().pos);
        }
    }
    // death / respawn
    if (!p.alive && !playerDead_) {
        playerDead_ = true;
        deathReason_ = fm.state().crashReason == CrashReason::Destroyed ? "SHOT DOWN" : crashReasonText(fm.state().crashReason);
        deathCamPos_ = cam_.position;
        deathTarget_ = toRl(fm.state().pos);
        if (mode_ == GameMode::Arena) respawnTimer_ = 5.0;
    }
    if (playerDead_ && respawnTimer_ > 0) {
        respawnTimer_ -= dt;
        if (respawnTimer_ <= 0) respawnPlayer();
    }

    // G effects and GPWS
    const FlightState& s = fm.state();
    float gTarget = settings_.gEffects ? static_cast<float>(std::clamp((s.gLoad - 7.0) / 2.5, 0.0, 1.0)) : 0.0f;
    float rTarget = settings_.gEffects ? static_cast<float>(std::clamp((-s.gLoad - 2.0) / 1.5, 0.0, 1.0)) : 0.0f;
    blackout_ += (gTarget - blackout_) * static_cast<float>(1.0 - std::exp(-dt * (gTarget > blackout_ ? 0.8 : 2.5)));
    redout_ += (rTarget - redout_) * static_cast<float>(1.0 - std::exp(-dt * 1.5));
    damagePulse_ = std::max(0.0, damagePulse_ - dt * 1.8);
    gpws_ = false;
    if (!s.onGround && s.airspeed > 60 && p.alive && s.gearPos < 0.5) {
        for (double t = 1.0; t <= 5.0; t += 1.0) {
            Vec3 q = s.pos + s.vel * t;
            if (q.y < terrain_.surfaceHeight(q.x, q.z) + 20.0) { gpws_ = true; break; }
        }
    }
}

void Game::processEvents() {
    const Jet& p = battle_.player();
    for (const CombatEvent& e : battle_.drainEvents()) {
        renderer_.effects().onEvent(e, battle_);
        Vector3 ep = toRl(e.pos);
        float dist = Vector3Distance(ep, cam_.position);
        float att = 1.0f / (1.0f + dist / 600.0f);
        float pan = panFor(cam_, ep);
        switch (e.type) {
            case EventType::Explosion: audio_.play(Sfx::Explosion, 0.4f + 1.4f * att, pan); break;
            case EventType::JetKilled:
                audio_.play(Sfx::BigExplosion, 0.5f + 1.5f * att, pan);
                if (e.a == p.id) {
                    hud_.popup("+100   ENEMY JET DESTROYED", Color{255, 225, 120, 255});
                    hud_.hitMarker(true);
                    static const char* lines[] = {"Splash one! Good kill.", "Bandit down!", "Nice shooting, Skyfall.", "Target destroyed."};
                    hud_.radio("WARDOG 2", lines[GetRandomValue(0, 3)], 3.5);
                }
                break;
            case EventType::GroundKilled:
                audio_.play(Sfx::Explosion, 0.6f + 1.2f * att, pan);
                if (e.a == p.id) { hud_.popup("+50   GROUND TARGET DESTROYED", Color{255, 225, 120, 255}); hud_.hitMarker(true); }
                break;
            case EventType::SmallExplosion: if (dist < 1500) audio_.play(Sfx::Impact, 0.6f * att, pan); break;
            case EventType::Splash: audio_.play(Sfx::Explosion, 0.5f * att, pan); break;
            case EventType::MissileLaunch:
                audio_.play(Sfx::MissileLaunch, e.a == p.id ? 1.0f : 0.25f + att, pan);
                if (e.text == "SAM" && e.b == p.id) hud_.radio("AWACS", "SAM launch! Skyfall One, defend!", 4);
                break;
            case EventType::FlaresOut: if (e.a == p.id) audio_.play(Sfx::Flares, 0.8f); break;
            case EventType::Hit: if (e.a == p.id) { hud_.hitMarker(false); audio_.play(Sfx::Hit, 0.35f); } break;
            case EventType::PlayerHit: damagePulse_ = 1.0; audio_.play(Sfx::Hit, 0.9f); shake_ += 0.6; break;
            case EventType::MissileWarningStart:
                hud_.radio("WARDOG 2", "Skyfall, missile on your six! Flares!", 3.5);
                break;
            case EventType::MissileMissed: hud_.popup("MISSILE DEFEATED", Color{150, 255, 205, 255}); break;
            default: break;
        }
    }
    shake_ += renderer_.effects().takeShake() * 0.5;
}

// ------------------------------------------------------------------------------------------
// Cameras
// ------------------------------------------------------------------------------------------
void Game::updateCamera(double dt) {
    const InputState& in = input_.state();
    const Jet& p = battle_.player();
    const FlightState& s = p.fm->state();
    const AircraftProfile& prof = p.fm->profile();
    const JetModel& jm = renderer_.jetModel(prof.style);
    if (screen_ == Screen::Playing || screen_ == Screen::Paused) {
        zoom_ = std::clamp(zoom_ - in.zoom * 0.08, 0.5, 2.5);
    }
    shake_ *= std::exp(-dt * 3.0);
    double shakeAmp = settings_.cameraShake * std::min(1.0, shake_ + std::max(0.0, (s.airspeed - 280.0) / 600.0) +
                                                      (s.alpha > 0.25 ? 0.15 : 0.0) + s.abLevel * 0.05);
    Vector3 jitter = {static_cast<float>((GetRandomValue(-100, 100) / 100.0) * shakeAmp),
                      static_cast<float>((GetRandomValue(-100, 100) / 100.0) * shakeAmp),
                      static_cast<float>((GetRandomValue(-100, 100) / 100.0) * shakeAmp)};

    double fovTarget = settings_.fovDeg;
    if (playerDead_ && screen_ == Screen::Playing) {
        cam_.position = deathCamPos_;
        cam_.target = deathTarget_;
        cam_.up = {0, 1, 0};
        cam_.fovy = static_cast<float>(settings_.fovDeg);
        nearPlane_ = 0.5f;
        return;
    }
    switch (camMode_) {
        case CameraMode::Cockpit: {
            if (in.looking) { headYaw_ += in.lookYaw; headPitch_ += in.lookPitch; }
            else { headYaw_ *= std::exp(-dt * 2.0); headPitch_ *= std::exp(-dt * 2.0); }
            headYaw_ = std::clamp(headYaw_, -2.6, 2.6);
            headPitch_ = std::clamp(headPitch_, -0.6, 1.2);
            cockpit_.setEye(jm.eye);
            Vector3 eye = cockpit_.eyeOffset();
            double gSag = std::clamp((s.gLoad - 1.0) * 0.006, -0.03, 0.05);
            Vec3 eyeW = s.pos + s.q.rotate(Vec3{eye.x, eye.y - gSag, eye.z});
            Vec3 lookB{std::cos(headPitch_) * std::cos(headYaw_), std::sin(headPitch_) - 0.11, std::cos(headPitch_) * std::sin(headYaw_)};
            Vec3 tgt = eyeW + s.q.rotate(lookB) * 10.0;
            cam_.position = Vector3Add(toRl(eyeW), Vector3Scale(jitter, 0.004f));
            cam_.target = toRl(tgt);
            cam_.up = toRl(s.q.up());
            fovTarget = settings_.fovDeg + 2;
            nearPlane_ = 1.0f;
            break;
        }
        case CameraMode::Chase: {
            if (!chaseInit_) { chaseQ_ = s.q; chaseInit_ = true; }
            double k = 1.0 - std::exp(-dt * 4.5);
            chaseQ_ = slerp(chaseQ_, s.q, k);
            if (in.looking) { orbitYaw_ += in.lookYaw; orbitPitch_ = std::clamp(orbitPitch_ + in.lookPitch, -0.5, 1.0); }
            double dist = prof.chaseDistance * zoom_;
            double height = prof.chaseHeight * zoom_;
            Quat q = chaseQ_;
            if (s.onGround) {
                Euler e = toEuler(s.q);
                q = Quat::fromEuler(e.heading, 0, 0);
                dist *= 1.15; height *= 1.3;
            }
            Vec3 back = q.forward() * -dist + q.up() * height;
            Vec3 pos = s.pos + back;
            double gy = terrain_.surfaceHeight(pos.x, pos.z) + 2.5;
            if (terrain_.onRunway(pos.x, pos.z, 50)) gy = std::max(gy, terrain_.runway().center.y + 2.5);
            pos.y = std::max(pos.y, gy);
            cam_.position = Vector3Add(toRl(pos), Vector3Scale(jitter, 0.12f));
            cam_.target = toRl(s.pos + q.forward() * 30.0 + q.up() * 2.5);
            Vec3 up = normalize(Vec3{0, 1, 0} * 0.2 + q.up() * 0.8);
            cam_.up = toRl(up);
            fovTarget = settings_.fovDeg + std::clamp((s.airspeed - 120.0) / 300.0, 0.0, 1.0) * 12.0 + s.abLevel * 4.0;
            nearPlane_ = 0.5f;
            break;
        }
        case CameraMode::External: {
            if (in.looking) { orbitYaw_ += in.lookYaw; orbitPitch_ = std::clamp(orbitPitch_ + in.lookPitch, -1.2, 1.4); }
            else if (screen_ == Screen::Playing) orbitYaw_ += dt * 0.08;
            double half = std::tan(settings_.fovDeg * 0.5 * DEG2RAD);
            double radius = jm.length * 0.5;
            // ~5 m from the airframe surface, pushed out just enough to frame the whole jet
            double dist = std::max(5.0 + radius, radius * 1.15 / half) * zoom_;
            Euler e = toEuler(s.q);
            double yaw = e.heading + orbitYaw_;
            Vec3 off{std::cos(yaw) * std::cos(orbitPitch_), std::sin(orbitPitch_), std::sin(yaw) * std::cos(orbitPitch_)};
            Vec3 pos = s.pos + off * dist;
            double gy = terrain_.surfaceHeight(pos.x, pos.z) + 1.5;
            if (terrain_.onRunway(pos.x, pos.z, 50)) gy = std::max(gy, terrain_.runway().center.y + 1.5);
            pos.y = std::max(pos.y, gy);
            cam_.position = Vector3Add(toRl(pos), Vector3Scale(jitter, 0.05f));
            cam_.target = toRl(s.pos);
            if (screen_ == Screen::Title) {
                // frame the aircraft in the right third, leaving room for the menu
                Vector3 fwdC = Vector3Normalize(Vector3Subtract(cam_.target, cam_.position));
                Vector3 rightC = Vector3Normalize(Vector3CrossProduct(fwdC, {0, 1, 0}));
                cam_.target = Vector3Subtract(cam_.target, Vector3Scale(rightC, static_cast<float>(dist * 0.42)));
            }
            cam_.up = {0, 1, 0};
            fovTarget = settings_.fovDeg;
            nearPlane_ = 0.3f;
            break;
        }
        default: break;
    }
    fovCur_ += (fovTarget - fovCur_) * (1.0 - std::exp(-dt * 3.0));
    cam_.fovy = static_cast<float>(fovCur_);
    cam_.projection = CAMERA_PERSPECTIVE;
}

void Game::updateAudio() {
    if (!audio_.ready()) return;
    const Jet& p = battle_.player();
    const FlightState& s = p.fm->state();
    bool paused = screen_ == Screen::Paused || screen_ == Screen::Settings;
    audio_.setPaused(paused);
    float rpm = s.engine == EngineState::Off ? static_cast<float>(s.rpm) : static_cast<float>(s.rpm);
    bool playing = screen_ == Screen::Playing;
    audio_.setEngine(rpm, static_cast<float>(s.abLevel), static_cast<float>(s.airspeed), camMode_ == CameraMode::Cockpit,
                     p.alive && (playing || screen_ == Screen::Title || screen_ == Screen::Hangar));
    audio_.setGun(p.gunFiring && playing);
    int lock = p.lockTarget == -1 ? 0 : (p.lockProgress >= 1.0 ? 2 : (p.lockProgress > 0.05 ? 1 : 0));
    audio_.setLockState(playing && mode_ == GameMode::Arena ? lock : 0);
    audio_.setMissileWarning(playing && battle_.playerMissileWarning());
    audio_.setStallWarning(playing && s.stalled);
}

// ------------------------------------------------------------------------------------------
// Rendering
// ------------------------------------------------------------------------------------------
FrameParams Game::makeFrameParams() {
    FrameParams f;
    f.cam = cam_;
    f.nearPlane = nearPlane_;
    f.farPlane = farPlane_;
    f.battle = &battle_;
    f.hideJetId = (camMode_ == CameraMode::Cockpit && (screen_ == Screen::Playing || screen_ == Screen::Paused) && !playerDead_) ? battle_.player().id : -1;
    f.L = lighting_;
    f.time = time_;
    f.blackout = blackout_;
    f.redout = redout_;
    f.damage = static_cast<float>(damagePulse_);
    f.speedFx = static_cast<float>(std::clamp((battle_.player().fm->state().airspeed - 250.0) / 300.0, 0.0, 1.0));
    f.showRunwayLights = tod_ == TimeOfDay::GoldenHour || mode_ == GameMode::Mission01;
    f.bloom = settings_.bloom;
    f.quality = settings_.quality;
    f.shadowFocus = toRl(battle_.player().fm->state().pos);
    return f;
}

void Game::renderFrame() {
    FrameParams f = makeFrameParams();
    // effects need per-jet models for wingtips
    std::vector<const JetModel*> models;
    for (const auto& j : battle_.jets()) models.push_back(&renderer_.jetModel(j->fm->profile().style));
    if (screen_ == Screen::Playing && !capturing_)
        renderer_.effects().update(GetFrameTime() > 0 ? std::min(GetFrameTime(), 0.1f) : 1.0 / 60.0, battle_, lighting_, cam_.position, models);

    bool cockpitView = f.hideJetId >= 0;
    if (cockpitView) cockpit_.updateDisplays(battle_, battle_.player(), time_, mode_ == GameMode::Mission01);
    renderer_.renderWorld(f);
    Rectangle combiner{0, 0, 0, 0};
    if (cockpitView) {
        // own airframe (no canopy) + cockpit interior with a near plane of 3 cm
        // camera-relative coordinates: millimetre cockpit detail stays precise far from the origin
        Vec3 origin = toSky(cam_.position);
        Camera3D local = cam_;
        local.position = {0, 0, 0};
        local.target = Vector3Subtract(cam_.target, cam_.position);
        renderer_.beginOverlay3D(local, 0.03f, 80.0f);
        FrameParams fo = f;
        fo.cam = local;
        fo.shadows = false;
        fo.nearPlane = 0.03f;
        fo.farPlane = 80.0f;
        renderer_.setAtmosphere(renderer_.jetProg(), fo);
        renderer_.drawJet(battle_.player(), fo, false, origin);
        renderer_.clearDepth();  // the cockpit tub always occludes the airframe skin around it
        cockpit_.draw3D(renderer_, battle_.player(), fo, origin);
        renderer_.endOverlay3D();
        combiner = cockpit_.combinerRect(cam_, battle_.player(), GetScreenWidth(), GetScreenHeight());
        rlSetClipPlanes(nearPlane_, farPlane_);
    }

    BeginDrawing();
    ClearBackground(BLACK);
    renderer_.composite(f);
    rlSetClipPlanes(nearPlane_, farPlane_);
    renderer_.drawSunFlare(f, GetScreenWidth(), GetScreenHeight());
    if ((screen_ == Screen::Playing || screen_ == Screen::Paused) && !hideHud_) {
        HudContext hc;
        hc.battle = &battle_;
        hc.player = &battle_.player();
        hc.cam = cam_;
        hc.w = GetScreenWidth();
        hc.h = GetScreenHeight();
        hc.camMode = camMode_;
        hc.mission = mission_ ? &*mission_ : nullptr;
        hc.settings = &settings_;
        hc.time = time_;
        hc.frame = stats_.compute();
        hc.worldMs = renderer_.lastWorldMs();
        hc.drawCalls = renderer_.drawCalls();
        hc.particles = renderer_.effects().particleCount();
        hc.showDebug = showDebug_;
        hc.showHelp = showHelp_;
        hc.arena = mode_ == GameMode::Arena;
        hc.timeOfDay = lighting_.name;
        hc.combiner = combiner;
        hc.minimap = renderer_.world().minimap;
        hc.gpws = gpws_;
        hc.dead = playerDead_;
        hc.deathReason = deathReason_;
        hc.respawnTimer = respawnTimer_;
        hc.missionDone = missionDone_;
        hc.droppedSteps = stepper_.droppedSteps;
        hc.gamepad = input_.gamepadConnected();
        hud_.draw(hc);
        if (settings_.showFps && !showDebug_) {
            Percentiles pc = stats_.compute();
            char b[64];
            std::snprintf(b, sizeof b, "%.0f FPS  %.1f ms", pc.mean > 0 ? 1000.0 / pc.mean : 0.0, pc.mean);
            ui::text(b, GetScreenWidth() - 12.0f, GetScreenHeight() - 156.0f * GetScreenHeight() / 720.0f, 13, Color{255, 255, 255, 140}, Align::Right);
        }
    }
    drawMenus();
    EndDrawing();
}

void Game::saveScreenshot(const std::string& path) {
    Image img = LoadImageFromScreen();
    ExportImage(img, path.c_str());
    UnloadImage(img);
}

// ------------------------------------------------------------------------------------------
// Menus
// ------------------------------------------------------------------------------------------
void Game::drawMenus() {
    switch (screen_) {
        case Screen::Title: drawTitle(); break;
        case Screen::Hangar: drawHangar(); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::Paused: drawPause(); break;
        default: break;
    }
}

void Game::drawTitle() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    // left gradient for legibility
    DrawRectangleGradientH(0, 0, static_cast<int>(w * 0.55f), static_cast<int>(h), Color{0, 0, 0, 170}, Color{0, 0, 0, 0});
    ui::logo(70 * s, 70 * s, s, titleTime_);
    nav_.begin(5, in.up, in.down, in.confirm, in.left, in.right);
    float x = 70 * s, y = 250 * s, bw = 430 * s, bh = 52 * s, gap = 10 * s;
    if (ui::button(nav_, 0, {x, y, bw, bh}, "CAMPAIGN", "Mission 01 - Cold Start & Takeoff")) { audio_.play(Sfx::Click); startMission01(); return; }
    y += bh + gap;
    if (ui::button(nav_, 1, {x, y, bw, bh}, "DOGFIGHT ARENA", "Waves of fictional bandits, SAM sites and ground targets")) { audio_.play(Sfx::Click); startArena(false); return; }
    y += bh + gap;
    if (ui::button(nav_, 2, {x, y, bw, bh}, "HANGAR", "8 aircraft slots - data and roadmap")) { audio_.play(Sfx::Click); startHangar(); nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 3, {x, y, bw, bh}, "SETTINGS", "Audio, graphics, controls")) { audio_.play(Sfx::Click); settingsReturn_ = Screen::Title; screen_ = Screen::Settings; nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 4, {x, y, bw, bh}, "QUIT")) { quit_ = true; return; }
    ui::text("v0.3  -  C++20 / raylib  -  fictional simulation, illustrative aircraft data", 70 * s, h - 34 * s, 13 * s, Color{255, 255, 255, 120}, Align::Left, false);
    ui::text("ENTER select   ARROWS navigate", w - 30 * s, h - 34 * s, 13 * s, Color{255, 255, 255, 120}, Align::Right, false);
}

void Game::drawHangar() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangleGradientH(0, 0, static_cast<int>(w * 0.45f), static_cast<int>(h), Color{0, 0, 0, 190}, Color{0, 0, 0, 0});
    ui::title("HANGAR", 60 * s, 40 * s, 38 * s);
    const auto& all = allProfiles();
    nav_.begin(9, in.up, in.down, in.confirm, in.left, in.right);
    float y = 110 * s;
    for (int i = 0; i < 8; ++i) {
        const AircraftProfile& p = all[i];
        if (ui::button(nav_, i, {60 * s, y, 380 * s, 44 * s}, p.name, p.playableNow ? "PLAYABLE" : "PLANNED - needs licensed model + tuning")) {}
        if (nav_.focus == i && hangarSel_ != i) { hangarSel_ = i; startHangar(); }
        y += 50 * s;
    }
    if (ui::button(nav_, 8, {60 * s, y + 10 * s, 380 * s, 44 * s}, "BACK") || in.back) { startTitle(); return; }
    // stats panel
    const AircraftProfile& p = all[hangarSel_];
    Rectangle r = {w - 400 * s, 110 * s, 360 * s, 300 * s};
    ui::panel(r, Color{0, 0, 0, 160}, Color{150, 255, 205, 60});
    float ty = r.y + 14 * s;
    ui::text(p.name, r.x + 16 * s, ty, 24 * s, WHITE);
    ty += 40 * s;
    auto row = [&](const char* k, const std::string& v) {
        ui::text(k, r.x + 16 * s, ty, 15 * s, Color{170, 190, 185, 230}, Align::Left, false);
        ui::text(v, r.x + r.width - 16 * s, ty, 15 * s, WHITE, Align::Right);
        ty += 24 * s;
    };
    char b[64];
    std::snprintf(b, sizeof b, "%.0f kg", p.emptyMassKg); row("Empty mass", b);
    std::snprintf(b, sizeof b, "%.1f m2", p.wingAreaM2); row("Wing area", b);
    std::snprintf(b, sizeof b, "%.1f / %.1f m", p.spanM, p.lengthM); row("Span / length", b);
    std::snprintf(b, sizeof b, "%.0f / %.0f kN", p.thrustMilN / 1000, p.thrustAbN / 1000); row("Thrust dry / AB", b);
    std::snprintf(b, sizeof b, "%.0f g", p.gLimit); row("FBW G limit", b);
    std::snprintf(b, sizeof b, "%.0f kt", p.rotateSpeedMs * 1.943844); row("Rotate speed", b);
    row("Status", p.playableNow ? "Playable" : "Planned");
    ui::text("Public, rounded figures; aero data tuned & illustrative.", r.x + 16 * s, r.y + r.height - 28 * s, 12 * s, Color{170, 190, 185, 200}, Align::Left, false);
}

void Game::drawSettings() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangle(0, 0, static_cast<int>(w), static_cast<int>(h), Color{0, 0, 0, 170});
    ui::title("SETTINGS", 60 * s, 34 * s, 38 * s);
    nav_.begin(17, in.up, in.down, in.confirm, in.left, in.right);
    float cw = 420 * s, rh = 42 * s, g = 6 * s;
    float x1 = 60 * s, x2 = 60 * s + cw + 40 * s;
    float y = 110 * s;
    ui::text("AUDIO", x1, y - 26 * s, 15 * s, kAccent);
    bool changed = false;
    changed |= ui::toggle(nav_, 0, {x1, y, cw, rh}, "In-game background music", settings_.musicEnabled); y += rh + g;
    changed |= ui::slider(nav_, 1, {x1, y, cw, rh}, "Master", settings_.master); y += rh + g;
    changed |= ui::slider(nav_, 2, {x1, y, cw, rh}, "Music", settings_.music); y += rh + g;
    changed |= ui::slider(nav_, 3, {x1, y, cw, rh}, "Engine", settings_.engine); y += rh + g;
    changed |= ui::slider(nav_, 4, {x1, y, cw, rh}, "Radio / instructor", settings_.radio); y += rh + g;
    changed |= ui::slider(nav_, 5, {x1, y, cw, rh}, "Effects", settings_.effects); y += rh + g;
    changed |= ui::toggle(nav_, 6, {x1, y, cw, rh}, "Radio captions", settings_.captions); y += rh + g;
    ui::text("Music from other apps keeps playing; in-game music is off by default.", x1, y + 4 * s, 12 * s, Color{180, 190, 190, 200}, Align::Left, false);
    float y2 = 110 * s;
    ui::text("GRAPHICS & COMFORT", x2, y2 - 26 * s, 15 * s, kAccent);
    int q = static_cast<int>(settings_.quality);
    if (ui::cycle(nav_, 7, {x2, y2, cw, rh}, "Quality", q, {"LOW", "MEDIUM", "HIGH"})) { settings_.quality = static_cast<GraphicsQuality>(q); changed = true; }
    y2 += rh + g;
    int t = static_cast<int>(tod_);
    if (ui::cycle(nav_, 8, {x2, y2, cw, rh}, "Time of day", t, {"MORNING", "GOLDEN HOUR", "NOON"})) { tod_ = static_cast<TimeOfDay>(t); lighting_ = Lighting::preset(tod_); }
    y2 += rh + g;
    changed |= ui::toggle(nav_, 9, {x2, y2, cw, rh}, "Bloom", settings_.bloom); y2 += rh + g;
    changed |= ui::slider(nav_, 10, {x2, y2, cw, rh}, "Field of view", settings_.fovDeg, 55, 95, false); y2 += rh + g;
    changed |= ui::slider(nav_, 11, {x2, y2, cw, rh}, "Camera shake", settings_.cameraShake); y2 += rh + g;
    changed |= ui::toggle(nav_, 12, {x2, y2, cw, rh}, "G blackout / redout", settings_.gEffects); y2 += rh + g;
    changed |= ui::toggle(nav_, 13, {x2, y2, cw, rh}, "Invert pitch", settings_.invertPitch); y2 += rh + g;
    changed |= ui::toggle(nav_, 14, {x2, y2, cw, rh}, "Handling", settings_.simulationHandling, "SIMULATION", "ASSISTED"); y2 += rh + g;
    changed |= ui::slider(nav_, 15, {x2, y2, cw, rh}, "Control sensitivity", settings_.sensitivity, 0.3, 2.0); y2 += rh + g;
    if (ui::button(nav_, 16, {x1, h - 80 * s, 200 * s, 46 * s}, "BACK") || in.back) {
        settings_.save(settingsPath_);
        screen_ = settingsReturn_;
        nav_.focus = 0;
        if (screen_ == Screen::Paused) audio_.setPaused(true);
    }
    if (changed) { applySettings(); audio_.play(Sfx::Click, 0.5f); }
}

void Game::drawPause() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangle(0, 0, static_cast<int>(w), static_cast<int>(h), Color{0, 0, 0, 150});
    ui::title("PAUSED", 70 * s, 70 * s, 40 * s);
    nav_.begin(6, in.up, in.down, in.confirm, in.left, in.right);
    float x = 70 * s, y = 170 * s, bw = 380 * s, bh = 50 * s, gap = 10 * s;
    bool resume = ui::button(nav_, 0, {x, y, bw, bh}, "RESUME") || (in.pause && screen_ == Screen::Paused && !in.confirm);
    y += bh + gap;
    if (resume) { screen_ = Screen::Playing; audio_.setPaused(false); stepper_.accumulator = 0; return; }
    if (ui::button(nav_, 1, {x, y, bw, bh}, "RESTART")) { if (mode_ == GameMode::Mission01) startMission01(); else startArena(false); return; }
    y += bh + gap;
    if (ui::button(nav_, 2, {x, y, bw, bh}, "SETTINGS")) { settingsReturn_ = Screen::Paused; screen_ = Screen::Settings; nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 3, {x, y, bw, bh}, "CONTROLS")) { showHelp_ = true; screen_ = Screen::Playing; audio_.setPaused(false); return; }
    y += bh + gap;
    if (ui::button(nav_, 4, {x, y, bw, bh}, "MAIN MENU")) { startTitle(); return; }
    y += bh + gap;
    if (ui::button(nav_, 5, {x, y, bw, bh}, "QUIT TO DESKTOP")) { quit_ = true; return; }
}

// ------------------------------------------------------------------------------------------
// Scripted capture / benchmark
// ------------------------------------------------------------------------------------------
void Game::scriptedStep(double seconds, const std::function<void(double)>& pilot, bool effects) {
    // advance the simulation in 1/30 s chunks without rendering, keeping effects alive
    const double chunk = 1.0 / 30.0;
    std::vector<const JetModel*> models;
    for (double t = 0; t < seconds; t += chunk) {
        int steps = static_cast<int>(std::round(chunk / FlightModel::kDt));
        for (int i = 0; i < steps; ++i) {
            pilot(t);
            ControlInputs ci = battle_.player().lastInput;
            battle_.update(FlightModel::kDt, ci, pendingCmd_);
            pendingCmd_ = PlayerCommands{};
            if (mission_) {
                MissionContext ctx{&battle_.player().fm->state(), &battle_.player().fm->profile(), &terrain_, 0};
                mission_->update(ctx, FlightModel::kDt);
            }
        }
        time_ += chunk;
        damagePulse_ = std::max(0.0, damagePulse_ - chunk * 1.8);
        processEvents();
        if (effects) {
            models.clear();
            for (const auto& j : battle_.jets()) models.push_back(&renderer_.jetModel(j->fm->profile().style));
            renderer_.effects().update(chunk, battle_, lighting_, cam_.position, models);
        }
        hud_.update(chunk);
    }
}

int Game::runCapture() {
    namespace fs = std::filesystem;
    fs::create_directories(opt_.captureDir);
    int shot = 0;
    auto snap = [&](const std::string& name, int warmFrames = 2) {
        for (int i = 0; i < warmFrames; ++i) { updateCamera(1.0 / 30.0); renderFrame(); }
        updateCamera(1.0 / 30.0);
        renderFrame();
        char path[512];
        std::snprintf(path, sizeof path, "%s/%02d_%s.png", opt_.captureDir.c_str(), ++shot, name.c_str());
        saveScreenshot(path);
        std::printf("captured %s\n", path);
        std::fflush(stdout);
    };
    auto setInput = [&](double pitch, double roll, double yaw, double thr) {
        ControlInputs& c = battle_.player().lastInput;
        c.pitch = pitch; c.roll = roll; c.yaw = yaw; c.throttleRate = thr;
    };
    const char* setEnv = std::getenv("SKYFALL_SET");
    std::string set = setEnv ? setEnv : "all";
    if (set == "jet") {
        hideHud_ = true;
        const AircraftId ids[] = {AircraftId::F22, AircraftId::Su35, AircraftId::MiG29, AircraftId::F16, AircraftId::Rafale, AircraftId::F35};
        const double angles[][2] = {{0.7, 0.18}, {2.0, 0.55}, {3.6, 0.1}};
        for (AircraftId id : ids) {
            startArena(false);
            battle_.clearCombatants();
            Jet& p = battle_.createPlayer(id);
            if (id != AircraftId::F22) p.team = Team::Red;
            p.fm->resetAirborne({-4000, 2900, 1000}, 0.3, 220, 0.8);
            screen_ = Screen::Playing;
            for (auto& a : angles) {
                camMode_ = CameraMode::External; orbitYaw_ = a[0]; orbitPitch_ = a[1]; zoom_ = 1.0;
                scriptedStep(0.3, [&](double) { setInput(0, 0, 0, 0); });
                snap(std::string("jet_") + p.fm->profile().shortName);
            }
        }
        return 0;
    }
    if (set == "shadow") {
        hideHud_ = true;
        startMission01();
        tod_ = TimeOfDay::Noon; lighting_ = Lighting::preset(tod_);
        camMode_ = CameraMode::External; orbitYaw_ = 1.2; orbitPitch_ = 0.9; zoom_ = 1.4;
        scriptedStep(0.2, [&](double) {});
        snap("shadow_noon_runway", 2);
        tod_ = TimeOfDay::Morning; lighting_ = Lighting::preset(tod_);
        orbitYaw_ = 3.9; orbitPitch_ = 0.5;
        snap("shadow_morning_runway", 2);
        startArena(false);
        battle_.clearCombatants();
        battle_.player().fm->resetAirborne({-4000, 2200, 1000}, 0.0, 230, 0.8);
        tod_ = TimeOfDay::Noon; lighting_ = Lighting::preset(tod_);
        orbitYaw_ = 0.6; orbitPitch_ = 1.0; zoom_ = 1.0;
        scriptedStep(0.2, [&](double) {});
        snap("shadow_self_air", 2);
        return 0;
    }
    if (set == "cockpit") {
        startMission01();
        scriptedStep(1.0, [&](double) { setInput(0, 0, 0, 0); });
        snap("cockpit_runway", 3);
        cockpit_.debugExport(opt_.captureDir.c_str());
        startArena(false);
        battle_.player().fm->resetAirborne({-4000, 2200, 1000}, 0.0, 230, 0.8);
        battle_.clearCombatants();
        battle_.setAiEnabled(false);
        battle_.debugAddJet(Team::Red, AircraftId::Su35, {-3400, 2260, 1060}, 0.3, 200);
        camMode_ = CameraMode::Cockpit;
        scriptedStep(1.5, [&](double) { setInput(0, 0, 0, 0); });
        snap("cockpit_air", 3);
        return 0;
    }
    if (set == "fx") {
        hideHud_ = false;
        startArena(false);
        battle_.clearCombatants();
        battle_.setAiEnabled(false);
        Jet& p = battle_.player();
        p.fm->resetAirborne({-4000, 2200, 1000}, 0.0, 230, 0.8);
        Jet& e = battle_.debugAddJet(Team::Red, AircraftId::Su35, {-3000, 2230, 1030}, 0.05, 200);
        p.lockTarget = e.id; p.lockProgress = 1.0;
        battle_.fireMissile(p);
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        scriptedStep(0.5, [&](double) { setInput(0, 0, 0, 0); });
        snap("fx_missile_05s", 3);
        scriptedStep(0.7, [&](double) { setInput(0, 0, 0, 0); });
        snap("fx_missile_12s", 3);
        scriptedStep(1.0, [&](double) { setInput(0, 0, 0, 0); });
        snap("fx_missile_22s", 3);
        Vec3 ex = p.fm->state().pos + p.fm->state().q.forward() * 160.0;
        renderer_.effects().spawnExplosion(toRl(ex), 2.5f, false, false);
        scriptedStep(0.15, [&](double) { setInput(0, 0, 0, 0); });
        snap("fx_explosion_015", 1);
        scriptedStep(0.6, [&](double) { setInput(0, 0, 0, 0); });
        snap("fx_explosion_075", 1);
        battle_.dropFlares(p);
        scriptedStep(1.5, [&](double) { setInput(0, 0, 0, 0); });
        camMode_ = CameraMode::External; orbitYaw_ = 3.6; orbitPitch_ = 0.25; zoom_ = 2.5;
        snap("fx_flares", 2);
        std::printf("particles %zu\n", renderer_.effects().particleCount());
        return 0;
    }
    // Disable the keyboard's influence: the scripted pilot writes lastInput directly.
    // 1. Title
    startTitle();
    scriptedStep(4.0, [&](double) { setInput(0, 0, 0, 0); });
    titleTime_ = 2.0;
    snap("title_screen");

    // 2. Mission 01 cockpit cold start
    startMission01();
    scriptedStep(1.0, [&](double) { setInput(0, 0, 0, 0); });
    snap("m01_cockpit_cold_start");

    // 3. Takeoff roll (chase)
    battle_.player().fm->requestEngineStart();
    scriptedStep(7.0, [&](double) { setInput(0, 0, 0, 0); });
    battle_.player().fm->setParkingBrake(false);
    scriptedStep(9.0, [&](double) { setInput(0, 0, 0, battle_.player().fm->state().throttle < 0.97 ? 1 : 0); });
    camMode_ = CameraMode::Chase; chaseInit_ = false;
    snap("m01_chase_takeoff_roll", 6);

    // 4. Rotation (external)
    scriptedStep(4.0, [&](double) {
        const FlightState& s = battle_.player().fm->state();
        setInput(s.airspeed > 70 ? (toEuler(s.q).pitch < 10 * kDeg ? 0.6 : 0.1) : 0, 0, 0, 0);
    });
    camMode_ = CameraMode::External; orbitYaw_ = 2.3; orbitPitch_ = 0.08; zoom_ = 1.0;
    snap("m01_external_rotation");

    // 5. Climb + gear up, cockpit HUD
    scriptedStep(5.0, [&](double) {
        const FlightState& s = battle_.player().fm->state();
        setInput(toEuler(s.q).pitch < 12 * kDeg ? 0.3 : 0.0, -toEuler(s.q).bank, 0, 0);
        if (!s.onGround && s.agl > 40 && s.gearDown) battle_.player().fm->toggleGear();
    });
    camMode_ = CameraMode::Cockpit;
    snap("m01_cockpit_climb_gear_up", 4);
    camMode_ = CameraMode::Chase; chaseInit_ = false;
    snap("m01_chase_climb", 6);

    // 6. Arena: chase view with enemies
    startArena(false);
    {
        Jet& p = battle_.player();
        p.fm->resetAirborne({-2500, 1900, 1800}, 0.12, 230, 0.85);
        // put one bandit ahead for a readable first frame
        Jet& e = battle_.debugAddJet(Team::Red, AircraftId::Su35, {-1500, 1960, 1960}, 0.6, 220);
        e.ai = AiState::Engage;
    }
    battle_.setAiEnabled(true);
    scriptedStep(3.0, [&](double) { setInput(0, 0, 0, 0); });
    camMode_ = CameraMode::Chase; chaseInit_ = false;
    snap("arena_chase_bandit_ahead", 8);
    // lock + missile
    {
        Jet& p = battle_.player();
        int best = -1; double bd = 1e9;
        for (auto& j : battle_.jets()) if (j->team == Team::Red && j->alive) { double d = length(j->fm->state().pos - p.fm->state().pos); if (d < bd) { bd = d; best = j->id; } }
        p.lockTarget = best; p.lockProgress = 1.0;
        battle_.fireMissile(p);
    }
    scriptedStep(1.2, [&](double) { setInput(0, 0, 0, 0); });
    snap("arena_missile_away", 6);
    scriptedStep(1.6, [&](double) { setInput(0, 0, 0, 0); });
    snap("arena_missile_impact", 2);
    // cockpit dogfight
    camMode_ = CameraMode::Cockpit;
    scriptedStep(2.0, [&](double) { setInput(0.2, 0, 0, 0); });
    snap("arena_cockpit", 4);
    // flares (external)
    battle_.dropFlares(battle_.player());
    scriptedStep(1.4, [&](double) { setInput(0.4, 0.3, 0, 0); });
    camMode_ = CameraMode::External; orbitYaw_ = 2.7; orbitPitch_ = 0.15; zoom_ = 1.6;
    snap("arena_flares_external", 4);
    // gun run on the town
    {
        Jet& p = battle_.player();
        Vec3 t = terrain_.townCenter();
        p.fm->resetAirborne({t.x - 2600, t.y + 500, t.z - 100}, 0.0, 220, 0.8);
        p.fm->mutableState().q = Quat::fromEuler(0.0, -9 * kDeg, 0);
        p.fm->mutableState().vel = p.fm->state().q.forward() * 220.0;
    }
    scriptedStep(4.0, [&](double) { setInput(0, 0, 0, 0); pendingCmd_.fireGun = true; });
    camMode_ = CameraMode::Chase; chaseInit_ = false;
    snap("arena_gun_run_town", 6);

    // 7. Golden hour formation
    startTitle();
    tod_ = TimeOfDay::GoldenHour; lighting_ = Lighting::preset(tod_);
    scriptedStep(3.0, [&](double) { setInput(0, 0, 0, 0); });
    screen_ = Screen::Playing; mode_ = GameMode::None;
    camMode_ = CameraMode::External; orbitYaw_ = 3.3; orbitPitch_ = 0.25; zoom_ = 2.2;
    snap("golden_hour_formation_external", 3);
    camMode_ = CameraMode::Chase; chaseInit_ = false;
    snap("golden_hour_chase", 6);

    // 8. Low level along the coast / mountains, noon
    startArena(false);
    battle_.clearCombatants();
    tod_ = TimeOfDay::Noon; lighting_ = Lighting::preset(tod_);
    battle_.player().fm->resetAirborne({5000, 450, -3000}, 0.4, 240, 0.85);
    scriptedStep(2.0, [&](double) { setInput(0, 0.2, 0, 0); });
    camMode_ = CameraMode::Chase; chaseInit_ = false;
    snap("noon_low_level_coast", 6);
    battle_.player().fm->resetAirborne({-3000, 2600, -2000}, -1.2, 230, 0.85);
    scriptedStep(2.0, [&](double) { setInput(0, 0, 0, 0); });
    snap("mountains_north", 6);

    // 9. Hangar + settings
    hangarSel_ = 0;
    startHangar();
    scriptedStep(0.5, [&](double) {});
    snap("hangar");
    settingsReturn_ = Screen::Title;
    startTitle();
    screen_ = Screen::Settings;
    snap("settings");
    return 0;
}

int Game::runBench() {
    startArena(false);
    battle_.player().fm->resetAirborne({-4000, 2000, 2000}, 0.0, 230, 0.85);
    camMode_ = CameraMode::Chase;
    stats_.keepAll(true);
    auto last = std::chrono::steady_clock::now();
    double elapsed = 0;
    while (elapsed < opt_.benchSeconds && !WindowShouldClose()) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        // gentle scripted turn so the view changes
        battle_.player().lastInput.roll = std::sin(elapsed * 0.3) * 0.4;
        frame(std::min(dt, 0.25));
        if (elapsed > 1.0) stats_.push(dt * 1000.0);  // skip warm-up
        elapsed += dt;
    }
    std::filesystem::create_directories("benchmarks");
    std::string renderer = std::string(reinterpret_cast<const char*>(rlGetVersion() == RL_OPENGL_33 ? "OpenGL 3.3" : "OpenGL"));
    stats_.writeCsv("benchmarks/frame_times.csv");
    stats_.writeJson("benchmarks/benchmark.json", "arena_chase", GetRenderWidth(), GetRenderHeight(), renderer);
    Percentiles p = FrameStats::computeFrom(stats_.all());
    std::printf("benchmark: %zu frames, mean %.2f ms (%.0f fps), p50 %.2f, p95 %.2f, p99 %.2f ms\n", p.samples, p.mean,
                p.mean > 0 ? 1000.0 / p.mean : 0, p.p50, p.p95, p.p99);
    return 0;
}

}  // namespace sky
