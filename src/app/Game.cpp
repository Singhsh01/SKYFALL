#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include "Game.hpp"
#include "../sim/Locations.hpp"
#include "../render/Skins.hpp"

#include "../render/Convert.hpp"
#include "../ui/Font.hpp"
#include "raymath.h"
#include "rlgl.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <chrono>
#include <thread>
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
#ifdef __EMSCRIPTEN__
    unsigned flags = FLAG_WINDOW_RESIZABLE;  // canvas follows the browser window; 1:1 pixels (no HiDPI cost)
#else
    unsigned flags = FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI;
#endif
    if (settings_.vsync && !capturing_) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    int w = opt_.width > 0 ? opt_.width : settings_.width;
    int h = opt_.height > 0 ? opt_.height : settings_.height;
#ifdef __EMSCRIPTEN__
    w = EM_ASM_INT({ return window.innerWidth; });
    h = EM_ASM_INT({ return window.innerHeight; });
#endif
    InitWindow(w, h, "SKYFALL - Combat Flight Simulator");
    SetExitKey(KEY_NULL);  // Esc opens the pause menu instead of quitting
    if (settings_.fullscreen && !opt_.windowed && !capturing_) ToggleFullscreen();
#ifdef __EMSCRIPTEN__
    SetTargetFPS(0);  // the browser paces frames (requestAnimationFrame)
#else
    SetTargetFPS(capturing_ ? 0 : settings_.fpsCap);
#endif
    ui::loadFonts();
}

int Game::run(const Options& opt) {
    opt_ = opt;
    battle_.setLayout(&layout_);
    replayStage_.setLayout(&layout_);
    capturing_ = !opt.captureDir.empty() || opt.benchSeconds > 0;
    settingsPath_ = defaultSettingsPath();
    settings_.load(settingsPath_);
    if (opt.quality >= 0) settings_.quality = static_cast<GraphicsQuality>(std::clamp(opt.quality, 0, 3));
    initWindow();
    // splash while the world is generated
    BeginDrawing();
    ClearBackground(Color{8, 12, 16, 255});
    ui::logo(GetScreenWidth() * 0.5f - 200, GetScreenHeight() * 0.42f, 1.0f, 0);
    ui::text("GENERATING TERRAIN, AIRBASE AND AIRCRAFT...", GetScreenWidth() * 0.5f, GetScreenHeight() * 0.62f, 16, Color{200, 210, 210, 200}, Align::Center);
    EndDrawing();
    renderer_.init(terrain_, layout_, GetScreenWidth(), GetScreenHeight());
    replayStage_.createPlayer(AircraftId::F22);
    replayStage_.player().fm->resetAirborne({0, -5000, 0}, 0.0, 1.0, 0.0);
    replayStage_.player().alive = false;
    cockpit_.init();
    if (!capturing_ || opt.benchSeconds > 0) audio_.init();
    speech_.setVoiceDir(voiceDir());
    applySettings();
    lighting_ = Lighting::preset(tod_);

    if (settings_.location != "desert" && !capturing_) loadLocation(settings_.location, false);
    if (!opt.captureDir.empty()) { int r = runCapture(); shutdown(); return r; }
    if (opt.benchSeconds > 0) { int r = runBench(); shutdown(); return r; }

    if (!startModeFromString(opt.startMode)) startTitle();

#ifdef __EMSCRIPTEN__
    webLast_ = std::chrono::steady_clock::now();
    emscripten_set_main_loop_arg([](void* g) { static_cast<Game*>(g)->webTick(); }, this, 0, 1);
    return 0;
#endif
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

#ifdef __EMSCRIPTEN__
void Game::webTick() {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - webLast_).count();
    webLast_ = now;
    if (quit_) { quit_ = false; startTitle(); }  // a browser tab has no desktop to quit to
    frame(std::min(dt, 0.25));
    stats_.push(dt * 1000.0);
}
#endif

void Game::updateDynamicResolution(double dt) {
    // Holds ~60 fps by lowering the 3D scene resolution in 10 % steps (HUD stays native).
    // Down quickly when frames are slow; up only after sustained headroom, and not again soon
    // after an up-step that had to be undone (prevents oscillation and its stutter).
    if (!settings_.dynamicResolution || capturing_) { dynScale_ = 1.0; return; }
    if (screen_ != Screen::Playing || replayActive_) { dynTimer_ = dynAccum_ = 0; dynFrames_ = 0; return; }
    dynAccum_ += dt; ++dynFrames_; dynTimer_ += dt;
    if (dynTimer_ < 1.0) return;
    double meanMs = dynAccum_ / std::max(1, dynFrames_) * 1000.0;
    dynTimer_ = dynAccum_ = 0; dynFrames_ = 0;
    // budget follows the frame-rate cap (75 fps -> 13.3 ms)
    double budget = 1000.0 / (settings_.fpsCap > 0 ? settings_.fpsCap : 75);
    if (meanMs > budget * 1.12 && dynScale_ > 0.55) {
        dynScale_ = std::max(0.5, dynScale_ - 0.1);
        dynUpBlockedUntil_ = time_ + ((time_ - dynLastUp_) < 6.0 ? 60.0 : 8.0);
    } else if (meanMs < budget * 1.02 && dynScale_ < 1.0 && time_ > dynUpBlockedUntil_) {
        dynScale_ = std::min(1.0, dynScale_ + 0.1);
        dynLastUp_ = time_;
        dynUpBlockedUntil_ = time_ + 4.0;
    }
}

void Game::beginBriefing(int missionNo) {
    briefT_ = 0;
    Briefing br = makeBriefing(missionNo);
    briefHeader_ = br.header;
    briefTitle_ = br.title;
    briefObjectives_ = br.objectives;
    briefText_ = br.display;
    briefCam_ = camMode_;
    camMode_ = CameraMode::External;
    orbitYaw_ = 2.4; orbitPitch_ = 0.20; zoom_ = 1.25;
    screen_ = Screen::Briefing;
    nav_.focus = 0;
    speech_.setEnabled(settings_.voiceBriefings && !capturing_);
    speech_.say(Speaker::Captain, br.speech, true);
}

void Game::radio(Speaker who, const std::string& text, double dur, bool voice) {
    hud_.radio(speakerName(who), text, dur);
    if (voice && !capturing_) speech_.say(who, text);
}

void Game::launchFromBriefing() {
    speech_.stop();
    screen_ = Screen::Playing;
    camMode_ = briefCam_;
    chaseInit_ = false;
    audio_.play(Sfx::Radio);
}

void Game::updateBriefing(double dt) {
    briefT_ += dt;
    orbitYaw_ += dt * 0.12;   // slow fly-around of your aircraft while the captain talks
    const InputState& in = input_.state();
    if (briefT_ > 0.25 && (in.confirm || IsKeyPressed(KEY_SPACE))) { launchFromBriefing(); return; }
    if ((in.left || in.right) && !battle_.jets().empty()) {
        // change the camouflage of the jet you are about to fly, right here in the briefing
        Jet& p = battle_.player();
        int id = static_cast<int>(p.fm->profile().id);
        int sk = (settings_.skinFor(id) + (in.right ? 1 : kSkinCount - 1)) % kSkinCount;
        settings_.setSkin(id, sk);
        settings_.save(settingsPath_);
        p.skin = sk;
        audio_.play(Sfx::Click, 0.5f);
    }
    if (in.back || in.pause) {
        speech_.stop();
        startTitle();
        if (mode_ == GameMode::None) screen_ = briefHeader_ == "DOGFIGHT ARENA" ? Screen::Title : Screen::Campaign;
        nav_.focus = 0;
    }
}

void Game::drawBriefing() {
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    const Color accent = {150, 255, 205, 255}, amber = {255, 176, 60, 255};
    DrawRectangleGradientH(0, 0, static_cast<int>(w * 0.5f), static_cast<int>(h), Color{0, 6, 10, 230}, Color{0, 6, 10, 170});
    DrawRectangleGradientH(static_cast<int>(w * 0.5f), 0, static_cast<int>(w * 0.32f) + 1, static_cast<int>(h), Color{0, 6, 10, 170}, Color{0, 6, 10, 0});
    float x = 56 * s, y = 46 * s;
    ui::text("PRE-MISSION BRIEFING", x, y, 13 * s, amber);
    ui::text(briefHeader_, x, y + 20 * s, 16 * s, accent);
    ui::title(briefTitle_, x, y + 42 * s, 34 * s);
    // the briefing officer: a stylised comms portrait (headset silhouette) with a live voice meter
    float py = y + 104 * s;
    Vector2 c = {x + 44 * s, py + 44 * s};
    DrawCircleV(c, 44 * s, Color{18, 40, 44, 255});
    DrawCircleLinesV(c, 44 * s, ui::withAlpha(accent, 0.6f));
    BeginScissorMode(static_cast<int>(c.x - 44 * s), static_cast<int>(c.y - 44 * s), static_cast<int>(88 * s), static_cast<int>(88 * s));
    Color sil = {120, 200, 180, 255};
    DrawEllipse(static_cast<int>(c.x), static_cast<int>(c.y - 6 * s), 19 * s, 21 * s, Color{70, 130, 120, 255});      // hair
    DrawEllipse(static_cast<int>(c.x + 13 * s), static_cast<int>(c.y + 6 * s), 7 * s, 15 * s, Color{70, 130, 120, 255}); // ponytail
    DrawCircleV({c.x, c.y - 4 * s}, 15 * s, sil);                                                                    // face
    DrawRectangleRounded({c.x - 30 * s, c.y + 18 * s, 60 * s, 40 * s}, 0.6f, 8, sil);                              // shoulders
    DrawRing({c.x, c.y - 4 * s}, 16 * s, 19 * s, 180, 360, 24, Color{30, 60, 60, 255});                             // headset band
    DrawRectangleRounded({c.x - 21 * s, c.y - 8 * s, 6 * s, 11 * s}, 0.5f, 4, Color{30, 60, 60, 255});             // ear cup
    DrawLineEx({c.x - 17 * s, c.y + 2 * s}, {c.x - 6 * s, c.y + 8 * s}, 2 * s, Color{30, 60, 60, 255});           // mic boom
    EndScissorMode();
    float nx = x + 104 * s;
    ui::text("CAPT. AVERY LANGLOIS", nx, py + 8 * s, 18 * s, WHITE);
    ui::text("WING OPERATIONS  -  CALLSIGN NORTHSTAR", nx, py + 32 * s, 12 * s, ui::withAlpha(accent, 0.85f), Align::Left, false);
    // voice meter
    bool talking = speech_.speaking() || (!speech_.available() && briefT_ < briefText_.size() / 15.0);
    for (int i = 0; i < 18; ++i) {
        float a = talking ? 0.25f + 0.75f * std::fabs(std::sin(static_cast<float>(briefT_) * (7.0f + i * 1.3f) + i * 0.7f)) : 0.12f;
        float bh = (4 + 18 * a) * s;
        DrawRectangle(static_cast<int>(nx + i * 7 * s), static_cast<int>(py + 72 * s - bh), static_cast<int>(4 * s), static_cast<int>(bh), ui::withAlpha(accent, talking ? 0.9f : 0.35f));
    }
    if (!speech_.available() && settings_.voiceBriefings)
        ui::text("(voice unavailable here - captions only)", nx + 140 * s, py + 58 * s, 11 * s, Color{170, 180, 180, 200}, Align::Left, false);
    // typed-out briefing text, roughly at speaking pace
    size_t shown = std::min(briefText_.size(), static_cast<size_t>(briefT_ * 17.0 + 1));
    std::string partial = briefText_.substr(0, shown);
    float ty = py + 108 * s;
    float maxW = w * 0.52f;
    std::string line, word;
    auto emit = [&](bool force) {
        if (!line.empty() && (force || ui::measure(line + " " + word, 17 * s, false) > maxW)) {
            ui::text(line, x, ty, 17 * s, Color{230, 236, 236, 255}, Align::Left, false);
            ty += 25 * s;
            line.clear();
        }
    };
    for (char ch : partial) {
        if (ch == ' ') { emit(false); line += line.empty() ? word : " " + word; word.clear(); }
        else word += ch;
    }
    emit(false);
    line += line.empty() ? word : " " + word;
    emit(true);
    // objectives
    ty += 14 * s;
    ui::text("OBJECTIVES", x, ty, 13 * s, amber);
    ty += 22 * s;
    for (size_t i = 0; i < briefObjectives_.size() && i < 9; ++i) {
        char b[16];
        std::snprintf(b, sizeof b, "%zu", i + 1);
        DrawRectangle(static_cast<int>(x), static_cast<int>(ty + 1 * s), static_cast<int>(18 * s), static_cast<int>(18 * s), ui::withAlpha(accent, 0.18f));
        ui::text(b, x + 9 * s, ty + 3 * s, 12 * s, accent, Align::Center);
        ui::text(briefObjectives_[i], x + 28 * s, ty + 2 * s, 15 * s, WHITE);
        ty += 24 * s;
    }
    // footer
    float pulse = 0.6f + 0.4f * std::sin(static_cast<float>(briefT_) * 3.0f);
    ui::textShadow("ENTER  LAUNCH", x, h - 60 * s, 20 * s, ui::withAlpha(accent, pulse));
    ui::text("ESC  BACK", x + 200 * s, h - 57 * s, 15 * s, Color{200, 210, 210, 200});
    ui::text(battle_.player().fm->profile().name, w - 40 * s, h - 80 * s, 15 * s, Color{220, 228, 228, 230}, Align::Right);
    ui::textShadow(std::string("<  CAMOUFLAGE: ") + skin(battle_.player().skin).name + "  >", w - 40 * s, h - 57 * s, 16 * s, accent, Align::Right);
}


// ------------------------------------------------------------------------------------------
// World locations
// ------------------------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
EM_JS(void, sky_fetch_terrain, (const char* id), {
    var name = UTF8ToString(id);
    fetch('terrain/' + name + '.skt').then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.arrayBuffer();
    }).then(function (buf) {
        try { FS.mkdir('/terrain'); } catch (e) {}
        FS.writeFile('/terrain/' + name + '.skt', new Uint8Array(buf));
    }).catch(function (e) {
        try { FS.mkdir('/terrain'); } catch (e2) {}
        FS.writeFile('/terrain/' + name + '.missing', 'x');
    });
});
#endif

std::string Game::terrainPath(const std::string& id) {
#ifdef __EMSCRIPTEN__
    return "/terrain/" + id + ".skt";
#else
    std::string app = GetApplicationDirectory();
    for (const std::string& p : {std::string("assets/terrain/") + id + ".skt", app + "assets/terrain/" + id + ".skt",
                                 app + "../assets/terrain/" + id + ".skt", std::string("../assets/terrain/") + id + ".skt"})
        if (FileExists(p.c_str())) return p;
    return "assets/terrain/" + id + ".skt";
#endif
}

std::string Game::terrainDir() {
#ifdef __EMSCRIPTEN__
    return "/terrain";
#else
    std::string app = GetApplicationDirectory();
    for (const std::string& p : {std::string("assets/terrain"), app + "assets/terrain", app + "../assets/terrain", std::string("../assets/terrain")})
        if (DirectoryExists(p.c_str())) return p;
    return app + "assets/terrain";
#endif
}

std::string Game::voiceDir() {
#ifdef __EMSCRIPTEN__
    return "voice";
#else
    std::string app = GetApplicationDirectory();
    for (const std::string& p : {std::string("assets/voice"), app + "assets/voice", app + "../assets/voice", app + "../Resources/assets/voice", std::string("../assets/voice")})
        if (DirectoryExists(p.c_str())) return p;
    return "assets/voice";
#endif
}

bool Game::locationAvailable(const std::string& id) {
    if (id == "desert") return true;
    return FileExists(terrainPath(id).c_str());
}

double Game::safeAltitude(Vec3 a, Vec3 b, double margin) const {
    double top = 0;
    for (int k = 0; k <= 40; ++k) {
        Vec3 p = a + (b - a) * (k / 40.0);
        for (double dz : {-1500.0, 0.0, 1500.0}) top = std::max(top, terrain_.surfaceHeight(p.x, p.z + dz));
    }
    return std::max(2100.0, top + margin);
}

bool Game::loadLocation(const std::string& id, bool showProgress) {
    if (terrain_.info().id == id) return true;
    const LocationDef* loc = findLocation(id);
    if (!loc) return false;
    Heightmap hm;
    if (id != "desert") {
        std::string err;
        if (!loadHeightmap(terrainPath(id), hm, err)) {
            locStatus_ = loc->name + ": " + err + ". Run  python3 tools/fetch_real_terrain.py  (or the 'real terrain' GitHub action).";
            return false;
        }
    }
    if (showProgress && IsWindowReady()) {
        BeginDrawing();
        ClearBackground(Color{8, 12, 16, 255});
        ui::text("LOADING  " + loc->name, GetScreenWidth() * 0.5f, GetScreenHeight() * 0.45f, 26, WHITE, Align::Center);
        ui::text(loc->region, GetScreenWidth() * 0.5f, GetScreenHeight() * 0.45f + 36, 16, Color{150, 255, 205, 220}, Align::Center);
        EndDrawing();
    }
    auto t0 = std::chrono::steady_clock::now();
    if (id == "desert") terrain_ = Terrain();
    else terrain_ = Terrain(hm, terrainInfoFor(*loc));
    layout_ = WorldLayout::build(terrain_);
    renderer_.rebuildWorld(terrain_, layout_);
    battle_.setLayout(&layout_);
    replayStage_.setLayout(&layout_);
    settings_.location = id;
    settings_.save(settingsPath_);
    TraceLog(LOG_INFO, "SKYFALL: location %s loaded in %.0f ms (runway at %.0f m)", id.c_str(),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), terrain_.runway().center.y);
    locStatus_.clear();
    return true;
}

void Game::drawLocations() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangleGradientH(0, 0, static_cast<int>(w * 0.62f), static_cast<int>(h), Color{0, 0, 0, 200}, Color{0, 0, 0, 30});
    ui::title("WORLD LOCATIONS", 50 * s, 26 * s, 32 * s);
    const auto& L = locations();
    const int n = static_cast<int>(L.size());
    nav_.begin(n + 4, in.up, in.down, in.confirm, in.left, in.right);
#ifdef __EMSCRIPTEN__
    // finish a pending download
    if (!locPending_.empty()) {
        if (FileExists(terrainPath(locPending_).c_str())) { switchTo_ = locPending_; locPending_.clear(); }
        else if (FileExists(("/terrain/" + locPending_ + ".missing").c_str())) {
            locStatus_ = "This location's terrain has not been published with the web build yet.";
            locPending_.clear();
        }
    }
#endif
    float x = 50 * s, y = 84 * s, bw = 420 * s, bh = 34 * s;
    for (int i = 0; i < n; ++i) {
        const LocationDef& l = L[i];
        bool current = terrain_.info().id == l.id;
        bool avail = locationAvailable(l.id);
        std::string label = l.name + (current ? "   (CURRENT)" : "");
        if (ui::button(nav_, i, {x, y, bw, bh}, label, "", true)) {
            audio_.play(Sfx::Click);
#ifdef __EMSCRIPTEN__
            if (!avail) { locPending_ = l.id; locStatus_ = "Downloading " + l.name + " terrain..."; sky_fetch_terrain(l.id.c_str()); }
            else
#endif
            if (!avail) {
                if (download_.busy()) locStatus_ = "Another download is running - " + download_.status();
                else if (download_.start({l.id}, terrainDir(), terrainDir() + "/.cache")) locStatus_.clear();
            } else if (l.id != terrain_.info().id) {
                switchTo_ = l.id;
            }
            return;
        }
        bool loading = download_.busy() && download_.current() == l.name;
        ui::text(avail ? l.region : loading ? "DOWNLOADING..." : "NOT DOWNLOADED - CLICK TO GET", x + bw - 10 * s, y + 10 * s, 12 * s,
                 avail ? Color{150, 255, 205, 200} : loading ? Color{255, 220, 120, 240} : Color{255, 150, 110, 220}, Align::Right, false);
        y += bh + 3 * s;
    }
    // details of the focused location
    if (nav_.focus < n) {
        const LocationDef& l = L[nav_.focus];
        Rectangle r = {x + bw + 30 * s, 84 * s, std::min(460.0f * s, w - (x + bw + 60 * s)), 150 * s};
        ui::panel(r, Color{0, 0, 0, 150}, Color{150, 255, 205, 60});
        ui::text(l.name, r.x + 14 * s, r.y + 12 * s, 22 * s, WHITE);
        ui::text(l.region, r.x + 14 * s, r.y + 40 * s, 14 * s, Color{255, 176, 60, 230});
        float ty = r.y + 64 * s;
        std::string line, word;
        for (char ch : l.blurb + " ") {
            if (ch != ' ') { word += ch; continue; }
            if (ui::measure(line + " " + word, 14 * s, false) > r.width - 28 * s) { ui::text(line, r.x + 14 * s, ty, 14 * s, Color{220, 230, 230, 230}, Align::Left, false); ty += 20 * s; line = word; }
            else line = line.empty() ? word : line + " " + word;
            word.clear();
        }
        if (!line.empty()) ui::text(line, r.x + 14 * s, ty, 14 * s, Color{220, 230, 230, 230}, Align::Left, false);
    }
    // actions at the current location
    float ax = x + bw + 30 * s, ay = 260 * s, aw = std::min(460.0f * s, w - (x + bw + 60 * s));
    if (ui::button(nav_, n, {ax, ay, aw, 46 * s}, "FLY DOGFIGHT ARENA HERE", terrain_.info().name)) { audio_.play(Sfx::Click); startArenaBriefed(); return; }
    if (ui::button(nav_, n + 1, {ax, ay + 54 * s, aw, 46 * s}, "FREE FLIGHT HERE", "No enemies - explore the terrain")) {
        audio_.play(Sfx::Click);
        freeFlight_ = true;
        startArena(false);
        return;
    }
    if (ui::button(nav_, n + 2, {ax, ay + 108 * s, aw, 40 * s}, "BACK") || in.back) {
        if (locReturn_ == Screen::Settings) { screen_ = Screen::Settings; nav_.focus = 21; locReturn_ = Screen::Title; return; }
        startTitle(); nav_.focus = 2; return;
    }
    // map downloads
    int missing = 0;
    std::vector<std::string> missingIds;
    for (const LocationDef& l : L) if (!locationAvailable(l.id)) { ++missing; missingIds.push_back(l.id); }
    float dy = ay + 158 * s;
#ifndef __EMSCRIPTEN__
    if (download_.busy()) {
        if (ui::button(nav_, n + 3, {ax, dy, aw, 40 * s}, "CANCEL DOWNLOAD", download_.current())) download_.cancel();
        Rectangle pb = {ax, dy + 48 * s, aw, 10 * s};
        DrawRectangleRec(pb, Color{255, 255, 255, 40});
        DrawRectangleRec({pb.x, pb.y, pb.width * download_.progress(), pb.height}, Color{150, 255, 205, 230});
        ui::text(download_.status(), ax, dy + 64 * s, 13 * s, Color{220, 230, 230, 230}, Align::Left, false);
    } else {
        std::string sub = missing ? std::to_string(missing) + " maps, about 2 MB each" : "All maps are downloaded";
        if (ui::button(nav_, n + 3, {ax, dy, aw, 40 * s}, missing ? "DOWNLOAD ALL MISSING MAPS" : "ALL MAPS DOWNLOADED", sub) && missing)
            download_.start(missingIds, terrainDir(), terrainDir() + "/.cache");
        if (!download_.status().empty()) ui::text(download_.status(), ax, dy + 48 * s, 13 * s, Color{220, 230, 230, 230}, Align::Left, false);
    }
    dy += 84 * s;
#endif
    if (!locStatus_.empty()) {
        float ty = dy;
        std::string line, word;
        for (char ch : locStatus_ + " ") {
            if (ch != ' ') { word += ch; continue; }
            if (ui::measure(line + " " + word, 14 * s, false) > aw) { ui::text(line, ax, ty, 14 * s, Color{255, 170, 120, 240}, Align::Left, false); ty += 20 * s; line = word; }
            else line = line.empty() ? word : line + " " + word;
            word.clear();
        }
        if (!line.empty()) ui::text(line, ax, ty, 14 * s, Color{255, 170, 120, 240}, Align::Left, false);
    }
    if (terrain_.info().real)
        ui::text(terrain_.info().attribution, 50 * s, h - 30 * s, 11 * s, Color{255, 255, 255, 120}, Align::Left, false);
}

bool Game::startModeFromString(const std::string& m) {
    // "arena", "m01".."m03" or "c04".."c40" (any mission number with either prefix)
    if (m == "arena") { startArena(false); return true; }
    if (m.size() >= 2 && (m[0] == 'm' || m[0] == 'c')) {
        int n = std::atoi(m.c_str() + 1);
        if (n >= 1 && n <= kCampaignMissions) { startMission(n); return true; }
    }
    return false;
}

MissionContext Game::missionContext() {
    return MissionContext{&battle_.player().fm->state(), &battle_.player().fm->profile(), &terrain_, 0, &battle_};
}

void Game::shutdown() {
    std::fflush(stdout);
    speech_.stop();          // free any voice clip before the audio device goes away
    download_.cancel();
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
        battle_.player().skin = settings_.skinFor(static_cast<int>(battle_.player().fm->profile().id));
    if (!battle_.jets().empty())
        battle_.player().fm->setHandling(static_cast<HandlingMode>(settings_.handling));
}

// ------------------------------------------------------------------------------------------
// Modes
// ------------------------------------------------------------------------------------------
void Game::startTitle() {
    pip_ = PipState{};
    replayActive_ = false;
    deathWait_ = -1;
    screen_ = Screen::Title;
    mode_ = GameMode::None;
    mission_.reset();
    terrain_.clearDeck();
    rain_ = false;
    tod_ = TimeOfDay::GoldenHour;
    lighting_ = Lighting::preset(tod_);
    Jet& p = battle_.createPlayer(AircraftId::F22);
    double ta = safeAltitude({-8000, 0, 6000}, {14000, 0, 3000}, 450.0);
    p.fm->resetAirborne({-2000, ta, 6000}, -0.15, 210.0, 0.8);
    // two wingmen for the cinematic formation (positions driven by the title scene)
    battle_.debugAddJet(Team::Blue, AircraftId::F22, {-2100, ta + 10, 6040}, -0.15, 210);
    battle_.debugAddJet(Team::Blue, AircraftId::F22, {-2100, ta - 10, 5960}, -0.15, 210);
    battle_.setAiEnabled(false);
    renderer_.effects().clear();
    hud_.clear();
    camMode_ = CameraMode::External;
    orbitYaw_ = 2.6; orbitPitch_ = 0.05; zoom_ = 1.6;
    nav_.focus = 0;
    playerDead_ = false;
    applySettings();
}

void Game::startMission(int n) {
    if (terrain_.info().id != "desert") {
        // the campaign is set in the Skyfall Desert: switch first (next frame), then start
        switchTo_ = "desert";
        switchMission_ = n;
        return;
    }
    freeFlight_ = false;
    screen_ = Screen::Playing;
    mode_ = GameMode::Mission01;   // "guided mission" mode
    missionNo_ = n;
    campaignDef_ = n >= 4 ? campaignMission(n) : nullptr;
    director_ = CampaignDirector{};
    tod_ = n == 3 ? TimeOfDay::Noon : TimeOfDay::Morning;
    if (campaignDef_) tod_ = campaignDef_->timeOfDay == 1 ? TimeOfDay::GoldenHour : campaignDef_->timeOfDay == 2 ? TimeOfDay::Noon
                           : campaignDef_->timeOfDay == 3 ? TimeOfDay::Storm : TimeOfDay::Morning;
    lighting_ = Lighting::preset(tod_);
    rain_ = campaignDef_ && campaignDef_->rain;
    if (campaignDef_ && campaignDef_->carrier) terrain_.setDeck(campaignCarrierDeck());
    else terrain_.clearDeck();
    Jet& p = battle_.createPlayer(static_cast<AircraftId>(settings_.aircraft));
    battle_.setAiEnabled(true);
    const RunwayInfo& rw = terrain_.runway();
    if (campaignDef_) {
        const CampaignMissionDef& d = *campaignDef_;
        if (d.carrier) p.fm->resetOnCatapult();
        else p.fm->resetAirborne({d.startX, d.startAlt, d.startZ}, campaignStartHeading(d), d.startSpeed, 0.85);
        mission_ = buildCampaignMission(d);
        camMode_ = CameraMode::Chase;
        if (d.startFuel >= 0) p.fm->mutableState().fuelKg = d.startFuel * p.fm->profile().fuelMassKg;
        director_.begin(d, battle_);           // targets, allies (player must already be placed)
        director_.update(*mission_, battle_);  // waves owned by the first objective
    } else if (n == 2) {
        p.fm->resetAirborne({rw.center.x + 2000.0, 1250.0, rw.center.z}, 0.0, 200.0, 0.62);
        mission_ = makeMission02();
        camMode_ = CameraMode::Chase;
    } else if (n == 3) {
        Vec3 th = rw.threshold();
        double d = 4800.0;   // stays inside the 32 km map (threshold is 5.5 km from the west edge)
        p.fm->resetAirborne({th.x - d, rw.center.y + glidePathHeight(d), th.z}, 0.0, 95.0, 0.45);
        p.fm->mutableState().q = Quat::fromEuler(0.0, 2.0 * kDeg, 0.0);
        p.fm->mutableState().vel = Quat::fromEuler(0.0, -3.0 * kDeg, 0.0).forward() * 95.0;
        mission_ = makeMission03();
        camMode_ = CameraMode::Cockpit;
    } else {
        p.fm->resetOnRunway(false);
        mission_ = makeMission01();
        camMode_ = CameraMode::Cockpit;
    }
    missionDone_ = false;
    voicedStep_ = 0;
    radioStage_ = 0;
    playerDead_ = false;
    respawnTimer_ = -1;
    renderer_.effects().clear();
    hud_.clear();
    chaseInit_ = false;
    headYaw_ = headPitch_ = 0;
    recorder_.clear();
    pip_ = PipState{};
    replayActive_ = false;
    deathWait_ = -1;
    lossCause_ = LossCause::Crash;
    killerName_.clear();

    applySettings();
    if (campaignDef_ && campaignDef_->carrier) radio(Speaker::Tower, lines::kCarrierCat, 8);
    else if (campaignDef_) radio(Speaker::Awacs, campaignDef_->briefing, 9, false);   // just heard in the briefing
    else if (n == 1) radio(Speaker::Tower, lines::kTowerStartup, 7, false);
    else if (n == 2) radio(Speaker::Instructor, lines::kInstructorStart, 6, false);
    else radio(Speaker::Tower, lines::kTowerLanding, 7, false);
    audio_.play(Sfx::Radio);
}

void Game::startArena(bool keepPlayerState) {
    screen_ = Screen::Playing;
    mode_ = GameMode::Arena;
    if (!keepPlayerState) {
        terrain_.clearDeck();
        rain_ = false;
        tod_ = TimeOfDay::Morning;
        lighting_ = Lighting::preset(tod_);
        Jet& p = battle_.createPlayer(static_cast<AircraftId>(settings_.aircraft));
        // start east of the runway, heading east, clear of the terrain ahead
        const RunwayInfo& rw0 = terrain_.runway();
        Vec3 sp{rw0.center.x + 1500, 0, rw0.center.z - 400};
        sp.y = std::max(rw0.center.y + 1400.0, safeAltitude(sp, sp + Vec3{9000, 0, 0}, 500.0));
        p.fm->resetAirborne(sp, 0.0, 230.0, 0.8);
        camMode_ = CameraMode::Chase;
        renderer_.effects().clear();
    }
    mission_.reset();
    campaignDef_ = nullptr;
    missionDone_ = false;
    battle_.setAiEnabled(true);
    recorder_.clear();
    pip_ = PipState{};
    replayActive_ = false;
    deathWait_ = -1;
    lossCause_ = LossCause::Crash;
    killerName_.clear();
    BattleConfig cfg;
    cfg.enemyJets = 3;
    cfg.allyJets = 2;
    cfg.seed = static_cast<unsigned>(GetRandomValue(1, 1000000));
    if (freeFlight_) { cfg.enemyJets = 0; cfg.allyJets = 0; cfg.groundTargets = false; cfg.endlessWaves = false; }
    battle_.start(cfg);
    playerDead_ = false;
    respawnTimer_ = -1;
    chaseInit_ = false;
    applySettings();
    if (freeFlight_) radio(Speaker::Tower, freeFlightLine(terrain_.info().name), 6);
    else radio(Speaker::Awacs, lines::kArenaStart, 6, false);
    audio_.play(Sfx::Radio);
}

void Game::startHangar() {
    pip_ = PipState{};
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
    p.skin = settings_.skinFor(hangarSel_);
}

void Game::respawnPlayer() {
    recorder_.clear();
    pip_ = PipState{};
    replayActive_ = false;
    deathWait_ = -1;
    lossCause_ = LossCause::Crash;
    killerName_.clear();
    Jet& p = battle_.player();
    const AircraftProfile& prof = p.fm->profile();
    p.fm = std::make_unique<FlightModel>(prof, terrain_);
    {
        const RunwayInfo& rw0 = terrain_.runway();
        Vec3 sp{rw0.center.x + 2000, 0, rw0.center.z - 600};
        sp.y = std::max(rw0.center.y + 1500.0, safeAltitude(sp, sp + Vec3{9000, 0, 0}, 500.0));
        p.fm->resetAirborne(sp, 0.0, 230.0, 0.8);
    }
    p.fm->setHandling(static_cast<HandlingMode>(settings_.handling));
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
    radio(Speaker::Awacs, lines::kRespawn, 4);
}

// ------------------------------------------------------------------------------------------
// Frame
// ------------------------------------------------------------------------------------------
void Game::frame(double dt, bool render) {
    speech_.update();
    if (!switchTo_.empty()) {
        // menus request location changes; the world is rebuilt here, outside any drawing
        std::string id = switchTo_;
        int mission = switchMission_;
        switchTo_.clear();
        switchMission_ = 0;
        bool ok = loadLocation(id, true);
        if (mission > 0) { startMissionBriefed(mission); }
        else if (ok) { startTitle(); screen_ = Screen::Locations; }
    }
    time_ += dt;
    input_.update(dt, settings_);
    handleGlobalKeys();
    hud_.update(dt);
    {
        // 3D scene resolution follows the window size in points (not Retina pixels) times a
        // quality scale; the HUD is still drawn at native resolution.
        updateDynamicResolution(dt);
        double scale = settings_.quality == GraphicsQuality::Low ? 0.7 : (settings_.quality == GraphicsQuality::Medium ? 0.85 : 1.0);
        scale *= dynScale_;
        // Ultra renders at the display's real pixel count (Retina / 4K), up to 3840 wide
        bool ultra = settings_.quality == GraphicsQuality::Ultra;
        double baseW = ultra ? GetRenderWidth() : GetScreenWidth(), baseH = ultra ? GetRenderHeight() : GetScreenHeight();
        double maxW = ultra ? 3840.0 : 1920.0;
        int rw = std::min(static_cast<int>(maxW), static_cast<int>(baseW * scale));
        int rh = static_cast<int>(baseH * scale * std::min(1.0, maxW / std::max(1.0, baseW * scale)));
        if (rw != renderer_.width() || rh != renderer_.height()) renderer_.resize(rw, rh);
    }

    switch (screen_) {
        case Screen::Playing: updatePlaying(dt); break;
        case Screen::Title: titleTime_ += dt; updateTitleScene(dt); break;
        case Screen::Campaign: titleTime_ += dt; if (mode_ == GameMode::None) updateTitleScene(dt); break;
        case Screen::Hangar: updateTitleScene(dt); break;
        case Screen::Briefing: updateBriefing(dt); break;
        case Screen::Locations: titleTime_ += dt; updateTitleScene(dt); break;
        default: break;
    }
    updateCamera(dt);
    if (screen_ == Screen::Briefing) {
        // frame the aircraft on the right-hand side, clear of the briefing text
        Vector3 fwd = Vector3Normalize(Vector3Subtract(cam_.target, cam_.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam_.up));
        float d = Vector3Distance(cam_.target, cam_.position);
        Vector3 shift = Vector3Scale(right, -0.42f * d);
        cam_.position = Vector3Add(cam_.position, shift);
        cam_.target = Vector3Add(cam_.target, shift);
    }
    updateAudio();
    if (render) renderFrame();
}

void Game::handleGlobalKeys() {
    const InputState& in = input_.state();
    if (IsKeyPressed(KEY_Q) && (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) quit_ = true;
    if (in.hudToggle) settings_.hudStyle = 1 - settings_.hudStyle;
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
    if ((screen_ == Screen::Title || screen_ == Screen::Locations) && battle_.jets().size() >= 3) {
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
        if (lead.pos.x > 12000) battle_.player().fm->resetAirborne({-6000, safeAltitude({-8000, 0, 6000}, {14000, 0, 3000}, 450.0), 6000}, -0.15, 210.0, 0.8);
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
        if (rollDir_ != 0) {
            ci.roll = rollDir_;
            ci.pitch = std::max(ci.pitch, 0.12);   // keep the nose up through the roll
        }
    }
    PlayerCommands cmd = pendingCmd_;
    cmd.fireGun = withPlayerInput && in.gun;
    int n = stepper_.advance(dt);
    for (int i = 0; i < n; ++i) {
        battle_.update(FlightModel::kDt, ci, cmd);
        cmd.fireMissile = cmd.flares = cmd.cycleTarget = false;  // edges only once
        if (rollDir_ != 0) {
            rollDone_ += std::fabs(battle_.player().fm->state().omega.x) * FlightModel::kDt;
            if (rollDone_ >= 2.0 * kPi - 0.15) { rollDir_ = 0; rollDone_ = 0; }
            else ci.roll = rollDir_;
        }
        if (mission_) {
            mission_->update(missionContext(), FlightModel::kDt);
            if (campaignDef_) director_.update(*mission_, battle_);
        }
    }
    if (n > 0) pendingCmd_ = PlayerCommands{};
}

void Game::updatePlaying(double dt) {
    const InputState& in = input_.state();
    Jet& p = battle_.player();
    FlightModel& fm = *p.fm;
    if (in.pause) { screen_ = Screen::Paused; nav_.focus = 0; audio_.setPaused(true); pauseJustOpened_ = true; return; }
    if (replayActive_) { updateReplay(dt); return; }
    if (in.help) showHelp_ = !showHelp_;
    if (in.camera) { camMode_ = nextCamera(camMode_); chaseInit_ = false; headYaw_ = headPitch_ = 0; }
    if (in.reset) {
        if (mode_ == GameMode::Mission01 && !missionDone_) { startMission(missionNo_); return; }
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
            if ((p.weaponAG ? p.agms : p.missiles) <= 0) hud_.radio("SYSTEM", "Missiles reloading...", 2);
            pendingCmd_.fireMissile = true;
        }
        if (in.flares) pendingCmd_.flares = true;
        if (in.cycleTarget) pendingCmd_.cycleTarget = true;
        bool wantAG = p.weaponAG;
        if (in.weaponToggle) wantAG = !wantAG;
        if (in.selectATA) wantAG = false;
        if (in.selectATS) wantAG = true;
        if (wantAG != p.weaponAG) {
            p.weaponAG = wantAG;
            p.lockTarget = -1;
            p.lockProgress = 0;
            audio_.play(Sfx::Click);
            hud_.popup(wantAG ? "ATS  AIR-TO-SURFACE MISSILES" : "ATA  AIR-TO-AIR MISSILES", Color{150, 255, 205, 255});
        }
        if (in.barrelRoll != 0 && rollDir_ == 0 && !fm.state().onGround) { rollDir_ = in.barrelRoll; rollDone_ = 0; }
    }
    if (mode_ == GameMode::Mission01 && missionDone_ && in.confirm) {
        if (missionNo_ < kCampaignMissions) {
            startMissionBriefed(missionNo_ + 1);
        } else {
            startTitle();
            screen_ = Screen::Campaign;
            nav_.focus = 0;
        }
        return;
    }
    {
        auto t0 = std::chrono::steady_clock::now();
        simulate(dt, !playerDead_);
        simMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    processEvents();

    // mission narration / completion
    if (mission_) {
        size_t idx = mission_->stepIndex();
        if (missionNo_ != 1) radioStage_ = 99;
        if (radioStage_ == 0 && idx >= 1) { radio(Speaker::Tower, lines::kTowerEngine, 5); audio_.play(Sfx::Radio); radioStage_ = 1; }
        if (radioStage_ == 1 && idx >= 3) { radio(Speaker::Tower, lines::kTowerTakeoff, 5); audio_.play(Sfx::Radio); radioStage_ = 2; }
        if (radioStage_ == 2 && idx >= 6) { radio(Speaker::Tower, lines::kTowerClimb, 5); audio_.play(Sfx::Radio); radioStage_ = 3; }
        if (campaignDef_ && idx != voicedStep_ && mission_->status() == MissionStatus::Running) {
            if (idx > voicedStep_ && mission_->current()) {
                if (campaignDef_->carrier && mission_->current()->title == "GEAR UP") radio(Speaker::Tower, lines::kCarrierAirborne, 5);
                else speech_.say(Speaker::Awacs, nextObjectiveLine(mission_->current()->title));
            }
            voicedStep_ = idx;
        }
        if (mission_->status() == MissionStatus::Complete && !missionDone_) {
            missionDone_ = true;
            settings_.markCompleted(missionNo_);
            settings_.save(settingsPath_);
            if (campaignDef_) radio(Speaker::Awacs, campaignDef_->debrief, 9);
            else if (missionNo_ == 1) radio(Speaker::Tower, lines::kTowerDone1, 9);
            else if (missionNo_ == 2) radio(Speaker::Instructor, lines::kInstructorDone2, 8);
            else radio(Speaker::Tower, lines::kTowerDone3, 8);
            audio_.play(Sfx::Radio);
        }
        // A crash or shoot-down fails the mission too, but it takes the normal loss path below
        // (live impact, then the side-observer replay with the "what went wrong" analysis).
        if (mission_->status() == MissionStatus::Failed && !playerDead_ && p.alive && !fm.state().crashed) {
            playerDead_ = true;
            deathReason_ = "MISSION FAILED: " + mission_->failure();
            if (!capturing_) speech_.say(Speaker::Awacs, lines::kLost, true);
            deathCamPos_ = cam_.position;
            deathTarget_ = toRl(fm.state().pos);
        }
    }
    // death / respawn
    if (!p.alive && !playerDead_) {
        playerDead_ = true;
        deathReason_ = fm.state().crashReason == CrashReason::Destroyed ? "SHOT DOWN" : crashReasonText(fm.state().crashReason);
        if (mode_ == GameMode::Mission01) {
            deathReason_ = "MISSION FAILED: " + deathReason_;
            if (!capturing_) speech_.say(Speaker::Awacs, lines::kLost, true);
        }
        deathCamPos_ = cam_.position;
        deathTarget_ = toRl(fm.state().pos);
        deathTime_ = time_;
        crashPos_ = fm.state().pos;
        if (fm.state().crashReason != CrashReason::Destroyed) lossCause_ = LossCause::Crash;
        lossReport_ = analyzeLoss(recorder_, deathTime_, fm.state().crashReason, lossCause_, killerName_);
        deathWait_ = 1.6;   // watch the impact live, then the replay starts
    }
    if (playerDead_ && deathWait_ > 0) {
        deathWait_ -= dt;
        if (deathWait_ <= 0) {
            if (settings_.crashReplay) beginReplay();
            if (!replayActive_ && mode_ == GameMode::Arena) respawnTimer_ = 3.0;
        }
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
    recordFrame();
    updatePip(dt);
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
                if (e.a == p.id) { lastKillTime_ = time_; lastKillPos_ = e.pos; }
                if (e.a == p.id) {
                    hud_.popup("+100   ENEMY JET DESTROYED", Color{255, 225, 120, 255});
                    hud_.hitMarker(true);
                    radio(Speaker::Wingman, lines::kKills[GetRandomValue(0, 3)], 3.5);
                }
                break;
            case EventType::GroundKilled:
                audio_.play(Sfx::Explosion, 0.6f + 1.2f * att, pan);
                if (e.a == p.id) { lastKillTime_ = time_; lastKillPos_ = e.pos; }
                if (e.a == p.id) {
                    std::string what = "GROUND TARGET";
                    bool objective = false;
                    if (e.b >= 0 && e.b < static_cast<int>(battle_.ground().size())) {
                        const GroundTarget& g = battle_.ground()[e.b];
                        what = g.label.empty() ? groundKindName(g.kind) : g.label;
                        objective = g.group > 0;
                    }
                    hud_.popup(std::string(objective ? "+100   " : "+50   ") + what + " DESTROYED", Color{255, 225, 120, 255});
                    hud_.hitMarker(true);
                }
                break;
            case EventType::SmallExplosion: if (dist < 1500) audio_.play(Sfx::Impact, 0.6f * att, pan); break;
            case EventType::Splash: audio_.play(Sfx::Explosion, 0.5f * att, pan); break;
            case EventType::MissileLaunch:
                audio_.play(Sfx::MissileLaunch, e.a == p.id ? 1.0f : 0.25f + att, pan);
                if (e.a == p.id && e.c >= 0) {
                    pip_.mode = PipMode::Missile;
                    pip_.missileId = e.c;
                    pip_.ats = e.text == "ATS";
                    pip_.age = 0;
                    pip_.launchRange = pip_.haveTarget ? pip_.lastDist : 0.0;
                    pip_.lastPos = e.pos;
                    pip_.lastDir = p.fm->state().q.forward();
                }
                if (e.text == "SAM" && e.b == p.id) radio(Speaker::Awacs, lines::kSamLaunch, 4);
                break;
            case EventType::FlaresOut: if (e.a == p.id) audio_.play(Sfx::Flares, 0.8f); break;
            case EventType::Hit:
                if (e.a == p.id) { hud_.hitMarker(false); audio_.play(Sfx::Hit, 0.35f); lastHitTime_ = time_; lastHitPos_ = e.pos; }
                break;
            case EventType::PlayerKilled: {
                lossCause_ = e.text == "MISSILE" ? LossCause::ShotDownMissile
                           : (e.text == "CANNON" || e.text == "FLAK") ? LossCause::ShotDownGun : LossCause::Crash;
                const Jet* killer = battle_.jetById(e.a);
                killerName_ = killer ? killer->callsign : (lossCause_ == LossCause::ShotDownMissile ? "SAM SITE" : (lossCause_ == LossCause::ShotDownGun ? "AAA" : ""));
                break;
            }
            case EventType::PlayerHit: damagePulse_ = 1.0; audio_.play(Sfx::Hit, 0.9f); shake_ += 0.6; break;
            case EventType::MissileWarningStart:
                radio(Speaker::Wingman, lines::kMissileSix, 3.5);
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
    if (replayActive_) return;   // the replay director owns the camera
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
            double k = 1.0 - std::exp(-dt * 7.0);
            chaseQ_ = slerp(chaseQ_, s.q, k);
            if (in.looking) { orbitYaw_ += in.lookYaw; orbitPitch_ = std::clamp(orbitPitch_ + in.lookPitch, -0.5, 1.0); }
            // Battlefield-style: close behind and slightly above, rolling with the jet
            double dist = prof.chaseDistance * 0.95 * zoom_;
            double height = dist * 0.2;
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
            cam_.target = toRl(s.pos + q.forward() * 45.0 + q.up() * 4.2);
            Vec3 up = s.onGround ? Vec3{0, 1, 0} : normalize(Vec3{0, 1, 0} * 0.05 + q.up() * 0.95);
            cam_.up = toRl(up);
            fovTarget = settings_.fovDeg + 4.0 + std::clamp((s.airspeed - 120.0) / 250.0, 0.0, 1.0) * 10.0 + s.abLevel * 5.0;
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
    f.showRunwayLights = true;
    f.bloom = settings_.bloom;
    f.quality = settings_.quality;
    f.shadowFocus = toRl(battle_.player().fm->state().pos);
    if (rain_) {
        f.rain = 1.0f;
        Vec3 v = battle_.player().fm->state().vel;
        f.rainWind = {static_cast<float>(-v.x), static_cast<float>(-v.y), static_cast<float>(-v.z)};
        // distant lightning: a brief flash of the whole sky every so often
        double ph = std::fmod(time_ * 0.043 + 0.37, 1.0);
        if (ph < 0.012) f.exposureBoost *= 1.0f + 1.6f * static_cast<float>(std::sin(ph / 0.012 * kPi));
    }
    return f;
}

void Game::renderFrame() {
    FrameParams f = replayActive_ ? makeReplayFrameParams() : makeFrameParams();
    // effects need per-jet models for wingtips
    std::vector<const JetModel*> models;
    for (const auto& j : battle_.jets()) models.push_back(&renderer_.jetModel(j->fm->profile().style));
    if (screen_ == Screen::Playing && !replayActive_) {
        // factory chimneys smoke continuously (only near the camera)
        chimneyTimer_ -= GetFrameTime() > 0 ? GetFrameTime() : 1.0 / 60.0;
        if (chimneyTimer_ <= 0) {
            chimneyTimer_ = 0.35;
            for (const Vector3& c : renderer_.dressing().chimneyTops)
                if (Vector3Distance(c, cam_.position) < 9000)
                    renderer_.effects().spawnSmokePuff(c, {2.5f, 3.5f, 0.8f}, 5.0f, 34.0f, 16.0f, {0.62f, 0.62f, 0.64f}, 0.45f);
        }
    }
    if (screen_ == Screen::Playing && !capturing_ && !replayActive_)
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
    // picture-in-picture: target / missile / hit camera
    const int pipW = 480, pipH = 300;
    bool pipOn = settings_.missileCam && !replayActive_ && !hideHud_ && pip_.mode != PipMode::Off &&
                 (screen_ == Screen::Playing || screen_ == Screen::Paused);
    if (pipOn) {
        FrameParams pf = makeFrameParams();
        pf.cam = pip_.cam;
        pf.hideJetId = -1;
        pf.bloom = false;
        pf.nearPlane = 0.5f;
        pf.quality = settings_.quality >= GraphicsQuality::High ? GraphicsQuality::Medium : GraphicsQuality::Low;
        renderer_.renderPip(pf, pipW, pipH);
        rlSetClipPlanes(nearPlane_, farPlane_);
    }

    BeginDrawing();
    ClearBackground(BLACK);
    renderer_.composite(f);
    renderer_.profMark("post (bloom/tone)");
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
        hc.simMs = simMs_;
        hc.hudMs = hudMs_;
        hc.drawCalls = renderer_.drawCalls();
        hc.particles = renderer_.effects().particleCount();
        hc.showDebug = showDebug_;
        hc.showHelp = showHelp_;
        hc.arena = mode_ == GameMode::Arena && !freeFlight_;
        hc.timeOfDay = lighting_.name;
        hc.combiner = combiner;
        hc.minimap = renderer_.world().minimap;
        hc.minimapHalf = WorldMeshes::kMinimapHalf;
        hc.gpws = gpws_;
        hc.dead = playerDead_;
        hc.deathReason = deathReason_;
        hc.respawnTimer = respawnTimer_;
        hc.missionDone = missionDone_;
        if (missionNo_ < kCampaignMissions) {
            const auto& cat = campaignCatalogue();
            std::string nextTitle = cat[missionNo_].title;
            for (char& ch : nextTitle) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            char nb[160];
            std::snprintf(nb, sizeof nb, "PRESS ENTER FOR MISSION %02d: %s", missionNo_ + 1, nextTitle.c_str());
            hc.missionNext = nb;
        } else {
            hc.missionNext = "CAMPAIGN COMPLETE  -  PRESS ENTER";
        }
        hc.runway = terrain_.runway();
        hc.deck = terrain_.deck();
        hc.campaign = campaignDef_;
        hc.showGlidePath = (mission_ && mission_->id() == "M03") ||
                           (campaignDef_ && mission_ && mission_->stepIndex() < campaignDef_->objectives.size() &&
                            campaignDef_->objectives[mission_->stepIndex()].kind == ObjectiveKind::Touchdown);
        hc.droppedSteps = stepper_.droppedSteps;
        hc.pipActive = pipOn;
        hc.pip = &pip_;
        hc.pipTex = renderer_.pipTexture();
        hc.pipW = pipW; hc.pipH = pipH;
        hc.replay = replayActive_;
        hc.replayT = replayT_; hc.replayFrom = replayFrom_; hc.replayTo = replayTo_; hc.deathTime = deathTime_;
        hc.loss = &lossReport_;
        hc.gamepad = input_.gamepadConnected();
        auto th = std::chrono::steady_clock::now();
        hud_.draw(hc);
        renderer_.profMark("hud");
        hudMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - th).count();
        if (settings_.showFps && !showDebug_) {
            Percentiles pc = stats_.compute();
            char b[64];
            if (dynScale_ < 0.999)
                std::snprintf(b, sizeof b, "%.0f FPS  %.1f ms  RES %d%%", pc.mean > 0 ? 1000.0 / pc.mean : 0.0, pc.mean, static_cast<int>(std::lround(dynScale_ * 100)));
            else
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
        case Screen::Campaign: drawCampaign(); break;
        case Screen::Hangar: drawHangar(); break;
        case Screen::Settings: drawSettings(); break;
        case Screen::Paused: drawPause(); break;
        case Screen::Briefing: drawBriefing(); break;
        case Screen::Locations: drawLocations(); break;
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
    nav_.begin(6, in.up, in.down, in.confirm, in.left, in.right);
    float x = 70 * s, y = 236 * s, bw = 430 * s, bh = 50 * s, gap = 9 * s;
    if (ui::button(nav_, 0, {x, y, bw, bh}, "CAMPAIGN", "61 missions: training, strikes, refuelling, carrier ops")) { audio_.play(Sfx::Click); screen_ = Screen::Campaign; nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 1, {x, y, bw, bh}, "DOGFIGHT ARENA", "Bandit waves over " + terrain_.info().name)) { audio_.play(Sfx::Click); startArenaBriefed(); return; }
    y += bh + gap;
    if (ui::button(nav_, 2, {x, y, bw, bh}, "WORLD LOCATIONS", "Real terrain: Himalaya, Alps, Sahara, Andes, Rio...")) { audio_.play(Sfx::Click); screen_ = Screen::Locations; locStatus_.clear(); nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 3, {x, y, bw, bh}, "HANGAR", "8 aircraft, 8 camouflage schemes")) { audio_.play(Sfx::Click); hangarSel_ = settings_.aircraft; startHangar(); nav_.focus = hangarSel_; return; }
    y += bh + gap;
    if (ui::button(nav_, 4, {x, y, bw, bh}, "SETTINGS", "Audio, graphics, controls")) { audio_.play(Sfx::Click); settingsReturn_ = Screen::Title; screen_ = Screen::Settings; nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 5, {x, y, bw, bh}, "QUIT")) { quit_ = true; return; }
    ui::text("v0.7  -  C++20 / raylib  -  fictional simulation, illustrative aircraft data", 70 * s, h - 34 * s, 13 * s, Color{255, 255, 255, 120}, Align::Left, false);
    ui::text("ENTER select   ARROWS navigate", w - 30 * s, h - 34 * s, 13 * s, Color{255, 255, 255, 120}, Align::Right, false);
}

void Game::drawCampaign() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangleGradientH(0, 0, static_cast<int>(w), static_cast<int>(h), Color{0, 0, 0, 215}, Color{0, 0, 0, 120});
    ui::title("CAMPAIGN", 50 * s, 26 * s, 34 * s);
    const auto& cat = campaignCatalogue();
    const int n = static_cast<int>(cat.size());
    int done = 0;
    for (int i = 1; i <= n; ++i) if (settings_.missionCompleted(i)) ++done;
    char prog[64];
    std::snprintf(prog, sizeof prog, "%d / %d COMPLETE", done, n);
    ui::text(prog, w - 50 * s, 40 * s, 15 * s, Color{150, 255, 205, 230}, Align::Right);
    nav_.begin(n + 1, in.up, in.down, in.confirm, in.left, in.right);
    // six chapters: 1-10, 11-20, 21-30, 31-40, 41-51, 52-61 (carrier)
    static const int starts[] = {0, 10, 20, 30, 40, 51, 61};
    const int nc = 6;
    auto colOf = [&](int i) { int c = 0; while (c + 1 < nc && i >= starts[c + 1]) ++c; return c; };
    auto rowOf = [&](int i) { return i - starts[colOf(i)]; };
    if ((in.left || in.right) && nav_.focus < n) {
        int c = (colOf(nav_.focus) + (in.right ? 1 : nc - 1)) % nc;
        int rows = std::min(starts[c + 1], n) - starts[c];
        nav_.focus = std::min(starts[c] + std::min(rowOf(nav_.focus), rows - 1), n - 1);
    }
    const float colW = (w - 100 * s) / static_cast<float>(nc) - 8 * s;
    const float rowH = 37 * s;
    static const char* chapters[] = {"TRAINING", "OPERATIONS", "OFFENSIVE", "ENDGAME", "CRITICAL STRIKE", "CARRIER OPS"};
    for (int c = 0; c < nc; ++c)
        ui::text(chapters[c], 50 * s + c * (colW + 8 * s), 78 * s, 12 * s, Color{255, 176, 60, 220});
    for (int i = 0; i < n; ++i) {
        const MissionInfo& m = cat[i];
        float col = static_cast<float>(colOf(i)), row = static_cast<float>(rowOf(i));
        Rectangle r = {50 * s + col * (colW + 8 * s), 96 * s + row * (rowH + 3 * s), colW, rowH};
        char label[96];
        std::snprintf(label, sizeof label, "%02d  %s", m.number, m.title.c_str());
        if (ui::measure(label, 14 * s, true) > r.width - 24 * s) {
            std::string t = m.title;
            while (t.size() > 4 && ui::measure(std::to_string(m.number) + "    " + t + "..", 14 * s, true) > r.width - 24 * s) t.pop_back();
            std::snprintf(label, sizeof label, "%02d  %s..", m.number, t.c_str());
        }
        if (ui::button(nav_, i, r, label, "", true)) {
            audio_.play(Sfx::Click);
            startMissionBriefed(m.number);
            return;
        }
        if (settings_.missionCompleted(m.number))
            ui::text("+", r.x + r.width - 10 * s, r.y + r.height / 2 - 9 * s, 18 * s, Color{120, 240, 160, 255}, Align::Right);
    }
    // briefing for the focused mission
    if (nav_.focus < n) {
        const MissionInfo& m = cat[nav_.focus];
        Rectangle b = {50 * s, h - 118 * s, w - 100 * s - 230 * s, 64 * s};
        ui::panel(b, Color{0, 0, 0, 150}, Color{255, 255, 255, 40});
        char head[96];
        std::snprintf(head, sizeof head, "MISSION %02d", m.number);
        ui::text(head, b.x + 14 * s, b.y + 9 * s, 12 * s, Color{255, 176, 60, 230});
        ui::text(m.summary, b.x + 14 * s, b.y + 29 * s, 17 * s, WHITE, Align::Left, false);
    }
    if (ui::button(nav_, n, {w - 50 * s - 210 * s, h - 118 * s, 210 * s, 64 * s}, "BACK") || in.back) {
        if (mode_ == GameMode::None) screen_ = Screen::Title; else startTitle();
        nav_.focus = 0;
    }
}

void Game::drawHangar() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangleGradientH(0, 0, static_cast<int>(w * 0.45f), static_cast<int>(h), Color{0, 0, 0, 190}, Color{0, 0, 0, 0});
    ui::title("HANGAR", 60 * s, 40 * s, 38 * s);
    const auto& all = allProfiles();
    nav_.begin(11, in.up, in.down, in.confirm, in.left, in.right);
    // left / right on an aircraft row changes its camouflage directly
    if (nav_.focus < 8 && (in.left || in.right)) {
        int sk = settings_.skinFor(nav_.focus);
        sk = (sk + (in.right ? 1 : kSkinCount - 1)) % kSkinCount;
        settings_.setSkin(nav_.focus, sk);
        settings_.aircraft = nav_.focus;      // painting an aircraft also makes it the one you fly
        settings_.save(settingsPath_);
        if (!battle_.jets().empty()) battle_.player().skin = sk;
        audio_.play(Sfx::Click, 0.5f);
    }
    float y = 100 * s;
    for (int i = 0; i < 8; ++i) {
        const AircraftProfile& p = all[i];
        bool chosen = settings_.aircraft == i;
        if (ui::button(nav_, i, {60 * s, y, 380 * s, 44 * s}, p.name, chosen ? "SELECTED - your aircraft" : "Press ENTER / click to select")) {
            settings_.aircraft = i;
            settings_.save(settingsPath_);
            audio_.play(Sfx::Click);
        }
        if (chosen) DrawRectangle(static_cast<int>(430 * s), static_cast<int>(y + 14 * s), static_cast<int>(8 * s), static_cast<int>(16 * s), Color{150, 255, 205, 255});
        if (nav_.focus == i && hangarSel_ != i) { hangarSel_ = i; startHangar(); }
        y += 48 * s;
    }
    if (ui::button(nav_, 8, {60 * s, y + 8 * s, 380 * s, 44 * s}, "FLY DOGFIGHT ARENA", all[settings_.aircraft].name)) { startArenaBriefed(); return; }
    if (ui::button(nav_, 9, {60 * s, y + 58 * s, 380 * s, 40 * s}, "BACK") || in.back) { startTitle(); return; }
    // stats panel
    const AircraftProfile& p = all[hangarSel_];
    Rectangle r = {w - 400 * s, 110 * s, 360 * s, 320 * s};
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
    std::snprintf(b, sizeof b, "%.0f / %.0f deg/s", p.maxRollRate * 1.28 / kDeg, p.maxPitchRate * 2.05 / kDeg); row("Arcade roll / pitch", b);
    std::snprintf(b, sizeof b, "%d ATA + 4 ATS", p.missiles); row("Missiles", b);
    ui::text("Public, rounded figures; aero data tuned & illustrative.", r.x + 16 * s, r.y + r.height - 28 * s, 12 * s, Color{170, 190, 185, 200}, Align::Left, false);
    // camouflage selector for the aircraft on display
    int sk = settings_.skinFor(hangarSel_);
    std::vector<std::string> names;
    for (int i = 0; i < kSkinCount; ++i) names.push_back(skin(i).name);
    if (ui::cycle(nav_, 10, {r.x, r.y + r.height + 14 * s, r.width, 44 * s}, "Camouflage", sk, names)) {
        settings_.setSkin(hangarSel_, sk);
        settings_.aircraft = hangarSel_;      // painting an aircraft also makes it the one you fly
        settings_.save(settingsPath_);
        if (!battle_.jets().empty()) battle_.player().skin = sk;
        audio_.play(Sfx::Click, 0.5f);
    }
    ui::text("LEFT / RIGHT on an aircraft also changes its camouflage", r.x, r.y + r.height + 66 * s, 12 * s, Color{170, 190, 185, 200}, Align::Left, false);
}

void Game::drawSettings() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangle(0, 0, static_cast<int>(w), static_cast<int>(h), Color{0, 0, 0, 170});
    ui::title("SETTINGS", 60 * s, 34 * s, 38 * s);
    nav_.begin(24, in.up, in.down, in.confirm, in.left, in.right);
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
    changed |= ui::toggle(nav_, 20, {x1, y, cw, rh}, "Voice (briefings & radio calls)", settings_.voiceBriefings); y += rh + g;
    ui::text("Music from other apps keeps playing; in-game music is off by default.", x1, y + 4 * s, 12 * s, Color{180, 190, 190, 200}, Align::Left, false);
    float y2 = 110 * s;
    ui::text("GRAPHICS & COMFORT", x2, y2 - 26 * s, 15 * s, kAccent);
    int q = static_cast<int>(settings_.quality);
    if (ui::cycle(nav_, 7, {x2, y2, cw, rh}, "Quality", q, {"LOW", "MEDIUM", "HIGH", "ULTRA HD"})) { settings_.quality = static_cast<GraphicsQuality>(q); changed = true; }
    y2 += rh + g;
    int t = static_cast<int>(tod_);
    if (ui::cycle(nav_, 8, {x2, y2, cw, rh}, "Time of day", t, {"MORNING", "GOLDEN HOUR", "NOON"})) { tod_ = static_cast<TimeOfDay>(t); lighting_ = Lighting::preset(tod_); }
    y2 += rh + g;
    changed |= ui::toggle(nav_, 9, {x2, y2, cw, rh}, "Bloom", settings_.bloom); y2 += rh + g;
    changed |= ui::slider(nav_, 10, {x2, y2, cw, rh}, "Field of view", settings_.fovDeg, 55, 95, false); y2 += rh + g;
    changed |= ui::slider(nav_, 11, {x2, y2, cw, rh}, "Camera shake", settings_.cameraShake); y2 += rh + g;
    changed |= ui::toggle(nav_, 12, {x2, y2, cw, rh}, "G blackout / redout", settings_.gEffects); y2 += rh + g;
    changed |= ui::toggle(nav_, 13, {x2, y2, cw, rh}, "Invert pitch", settings_.invertPitch); y2 += rh + g;
    if (ui::cycle(nav_, 14, {x2, y2, cw, rh}, "Handling", settings_.handling, {"ARCADE", "ASSISTED", "SIMULATION"})) changed = true;
    y2 += rh + g;
    changed |= ui::slider(nav_, 15, {x2, y2, cw, rh}, "Control sensitivity", settings_.sensitivity, 0.3, 2.0); y2 += rh + g;
    changed |= ui::toggle(nav_, 19, {x2, y2, cw, rh}, "Dynamic resolution (hold frame rate)", settings_.dynamicResolution); y2 += rh + g;
    changed |= ui::toggle(nav_, 16, {x1, y + 22 * s, cw, rh}, "Missile / target camera", settings_.missileCam);
    changed |= ui::toggle(nav_, 17, {x1, y + 22 * s + rh + g, cw, rh}, "Crash replay + analysis", settings_.crashReplay);
    {
        static const int caps[] = {60, 75, 120, 0};
        int fi = 1;
        for (int i = 0; i < 4; ++i) if (caps[i] == settings_.fpsCap) fi = i;
        if (ui::cycle(nav_, 22, {x1, y + 22 * s + 2 * (rh + g), cw, rh}, "Frame rate limit", fi, {"60 FPS", "75 FPS", "120 FPS", "UNLIMITED"})) {
            settings_.fpsCap = caps[fi];
            SetTargetFPS(settings_.fpsCap);
            changed = true;
        }
        // VSync locks the frame rate to the monitor (60 Hz screens cannot show 75 fps with it on)
        if (ui::toggle(nav_, 23, {x2, y2 + 10 * s + rh + 4 * s + g, cw, rh}, "VSync (off allows > 60 fps on 60 Hz screens)", settings_.vsync)) {
            if (settings_.vsync) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
            changed = true;
        }
    }
    {
        int have = 0, total = 0;
        for (const LocationDef& l : locations()) if (l.id != "desert") { ++total; if (locationAvailable(l.id)) ++have; }
        std::string sub = std::to_string(have) + " of " + std::to_string(total) + " real-world maps downloaded" + (download_.busy() ? " - downloading..." : "");
        if (ui::button(nav_, 21, {x2, y2 + 10 * s, cw, rh + 4 * s}, have < total ? "DOWNLOAD WORLD MAPS" : "WORLD MAPS", sub)) {
            settings_.save(settingsPath_);
            locReturn_ = Screen::Settings;
            screen_ = Screen::Locations;
            nav_.focus = 0;
            return;
        }
    }
    if (ui::button(nav_, 18, {x1, h - 64 * s, 200 * s, 46 * s}, "BACK") || in.back) {
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
    bool resume = ui::button(nav_, 0, {x, y, bw, bh}, "RESUME") || (in.pause && !pauseJustOpened_ && !in.confirm);
    pauseJustOpened_ = false;
    if (IsKeyPressed(KEY_Q) && (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER) || IsKeyDown(KEY_LEFT_CONTROL))) { quit_ = true; return; }
    y += bh + gap;
    if (resume) { screen_ = Screen::Playing; audio_.setPaused(false); stepper_.accumulator = 0; return; }
    if (ui::button(nav_, 1, {x, y, bw, bh}, "RESTART")) { if (mode_ == GameMode::Mission01) startMission(missionNo_); else startArena(false); return; }
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
                mission_->update(missionContext(), FlightModel::kDt);
                if (campaignDef_) director_.update(*mission_, battle_);
            }
        }
        time_ += chunk;
        damagePulse_ = std::max(0.0, damagePulse_ - chunk * 1.8);
        processEvents();
        if (screen_ == Screen::Playing && mode_ != GameMode::None) updatePip(chunk);
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
    if (set == "cine") {
        // lock -> missile camera -> hit cam, then a crash with its replay
        startArena(false);
        battle_.clearCombatants();
        battle_.setAiEnabled(false);
        tod_ = TimeOfDay::Morning; lighting_ = Lighting::preset(tod_);
        Jet& p = battle_.player();
        p.fm->resetAirborne({-6000, 1500, 3200}, 0.0, 230, 0.8);
        Jet& e = battle_.debugAddJet(Team::Red, AircraftId::Su35, {-3300, 1560, 3350}, 0.15, 200);
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        auto run = [&](double secs) {
            scriptedStep(secs, [&](double) { setInput(0, 0, 0, 0); });
            updatePip(secs);   // the scripted stepper does not run updatePlaying
        };
        p.lockTarget = e.id; p.lockProgress = 0.6;
        run(0.3);
        snap("cine_01_target_cam", 3);
        { Image im = LoadImageFromTexture(renderer_.pipTexture()); ImageFlipVertical(&im); ExportImage(im, (opt_.captureDir + "/pip_target.png").c_str()); UnloadImage(im);
          std::printf("pip cam pos %.0f %.0f %.0f tgt %.0f %.0f %.0f fov %.2f dist %.0f\n", pip_.cam.position.x, pip_.cam.position.y, pip_.cam.position.z, pip_.cam.target.x, pip_.cam.target.y, pip_.cam.target.z, pip_.cam.fovy, pip_.lastDist); }
        p.lockProgress = 1.0;
        battle_.fireMissile(p);
        processEvents();
        run(0.8);
        snap("cine_02_missile_cam", 3);
        for (int i = 0; i < 40 && pip_.mode == PipMode::Missile; ++i) run(0.1);
        run(0.25);
        snap("cine_03_hit_cam", 2);
        run(1.2);
        snap("cine_04_hit_cam_late", 2);
        // ground attack with the air-to-surface missile
        p.fm->resetAirborne({terrain_.townCenter().x - 3200, 700, terrain_.townCenter().z}, 0.0, 220, 0.8);
        p.fm->mutableState().q = Quat::fromEuler(0.0, -8 * kDeg, 0);
        p.weaponAG = true;
        battle_.start(BattleConfig{});
        battle_.clearCombatants();
        BattleConfig gc; gc.enemyJets = 0; gc.allyJets = 0;
        battle_.start(gc);
        for (auto& jj : battle_.jets()) if (!jj->isPlayer) jj->alive = false;
        battle_.setAiEnabled(false);
        battle_.player().weaponAG = true;
        run(1.0);
        int best = -1; double bd = 1e9;
        for (int gi = 0; gi < static_cast<int>(battle_.ground().size()); ++gi) {
            double d = length(battle_.ground()[gi].pos - battle_.player().fm->state().pos);
            if (d < bd) { bd = d; best = gi; }
        }
        battle_.player().lockTarget = -1000 - best; battle_.player().lockProgress = 1.0;
        run(0.1);
        snap("cine_05_target_pod", 2);
        battle_.fireMissile(battle_.player());
        processEvents();
        run(1.0);
        snap("cine_06_ats_cam", 2);
        for (int i = 0; i < 60 && pip_.mode == PipMode::Missile; ++i) run(0.1);
        run(0.3);
        snap("cine_07_ats_hit", 2);
        // crash: dive into a ridge and watch the replay
        startArena(false);
        battle_.clearCombatants();
        battle_.setAiEnabled(false);
        Jet& q = battle_.player();
        q.fm->resetAirborne({-4200, 950, -2600}, -0.9, 240, 0.9);
        double t0 = time_;
        for (int i = 0; i < 400 && q.alive; ++i) {
            scriptedStep(1.0 / 30.0, [&](double) { setInput(-0.35, 0.25, 0, 0); });
            gpws_ = false;
            const FlightState& st = q.fm->state();
            for (double tt = 1.0; tt <= 5.0; tt += 1.0) { Vec3 w = st.pos + st.vel * tt; if (w.y < terrain_.surfaceHeight(w.x, w.z) + 20.0) gpws_ = true; }
            recordFrame();
        }
        std::printf("crash after %.1f s, alive=%d\n", time_ - t0, q.alive);
        deathTime_ = time_; crashPos_ = q.fm->state().pos; playerDead_ = true;
        lossReport_ = analyzeLoss(recorder_, deathTime_, q.fm->state().crashReason, LossCause::Crash, "");
        screen_ = Screen::Playing;
        beginReplay();
        for (double t = 0; t < 2.0; t += 1.0 / 30.0) updateReplay(1.0 / 30.0);
        snap("cine_08_replay_side", 1);
        while (replayActive_ && replayT_ < deathTime_ - 0.3) updateReplay(1.0 / 30.0);
        snap("cine_09_replay_slowmo", 1);
        while (replayActive_ && replayT_ < deathTime_ + 0.35) updateReplay(1.0 / 30.0);
        snap("cine_10_replay_impact", 1);
        return 0;
    }
    if (set == "terrain") {
        hideHud_ = std::getenv("SKYFALL_HUD") == nullptr;
        startArena(false);
        battle_.clearCombatants();
        battle_.setAiEnabled(false);
        struct Shot { Vec3 pos; double hdg, pitch; const char* name; TimeOfDay tod; };
        const Shot shots[] = {
            {{-3500, 900, -2500}, -0.9, -6, "canyon_low_morning", TimeOfDay::Morning},
            {{-6000, 2600, -7000}, 0.6, -14, "mountains_high", TimeOfDay::Morning},
            {{-2000, 520, 3200}, 0.05, -8, "valley_town", TimeOfDay::Morning},
            {{1500, 1400, 9500}, -0.6, -10, "south_range_noon", TimeOfDay::Noon},
            {{-4000, 1100, -1500}, 0.4, -5, "golden_ridges", TimeOfDay::GoldenHour},
            {{4500, 700, -4500}, 0.3, -4, "coast_cliffs", TimeOfDay::Morning},
        };
        for (const Shot& sh : shots) {
            tod_ = sh.tod; lighting_ = Lighting::preset(tod_);
            Jet& p = battle_.player();
            p.fm->resetAirborne(sh.pos, sh.hdg, 230, 0.8);
            p.fm->mutableState().q = Quat::fromEuler(sh.hdg, sh.pitch * kDeg, 0);
            p.fm->mutableState().vel = p.fm->state().q.forward() * 230.0;
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(0.1, [&](double) { setInput(0, 0, 0, 0); });
            snap(sh.name, 3);
        }
        return 0;
    }
    if (set == "skins") {
        hideHud_ = true;
        for (int ac : {0, 4}) {
            hangarSel_ = ac;
            for (int k = 0; k < kSkinCount; ++k) {
                settings_.setSkin(ac, k);
                startHangar();
                screen_ = Screen::Playing;
                orbitYaw_ = 2.5; orbitPitch_ = 0.35; zoom_ = 0.85;
                scriptedStep(0.1, [&](double) {});
                char nm[64];
                std::snprintf(nm, sizeof nm, "skin_%d_%s", ac, skin(k).name);
                for (char* q = nm; *q; ++q) if (*q == ' ') *q = '_';
                snap(nm, 2);
            }
            settings_.setSkin(ac, 0);
        }
        return 0;
    }
    if (set == "download") {
        // downloader self-test: SKYFALL_TILES=<url template> SKYFALL_DLOUT=<dir>
        const char* url = std::getenv("SKYFALL_TILES");
        const char* out = std::getenv("SKYFALL_DLOUT");
        if (url) download_.tileUrl = url;
        std::string dir = out ? out : "/tmp/skyfall_dl";
        download_.start({"fuji"}, dir, dir + "/.cache");
        while (download_.busy()) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
        std::printf("download: %s\n", download_.status().c_str());
        return 0;
    }
    if (set == "carrier") {
        hideHud_ = false;
        startMission(52);
        Jet& p = battle_.player();
        camMode_ = CameraMode::External; orbitYaw_ = 2.3; orbitPitch_ = 0.22; zoom_ = 2.4; chaseInit_ = false;
        scriptedStep(0.2, [&](double) { setInput(0, 0, 0, 0); });
        snap("carrier_deck", 3);
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        scriptedStep(2.0, [&](double) { setInput(0, 0, 0, 1.0); });
        snap("carrier_full_ab", 3);
        scriptedStep(2.5, [&](double) { setInput(0, 0, 0, 1.0); });
        snap("carrier_ab_lit", 2);
        p.fm->setParkingBrake(false);
        scriptedStep(1.6, [&](double) { setInput(0, 0, 0, 1.0); });
        snap("carrier_cat_shot", 2);
        scriptedStep(5.0, [&](double) { setInput(0, 0, 0, 0); });
        snap("carrier_climb_out", 2);
        camMode_ = CameraMode::External; orbitYaw_ = 3.6; orbitPitch_ = 0.1; zoom_ = 1.4;
        scriptedStep(0.1, [&](double) { setInput(0, 0, 0, 0); });
        snap("carrier_ab_side", 2);
        camMode_ = CameraMode::Cockpit;
        scriptedStep(0.3, [&](double) { setInput(0, 0, 0, 0); });
        snap("carrier_cockpit_rain", 2);
        return 0;
    }
    if (set == "menus") {
        hideHud_ = true;
        startTitle(); screen_ = Screen::Campaign; nav_.focus = 54;
        snap("menu_campaign", 2);
        startTitle(); settingsReturn_ = Screen::Title; screen_ = Screen::Settings; nav_.focus = 21;
        snap("menu_settings", 2);
        startTitle(); locReturn_ = Screen::Settings; screen_ = Screen::Locations; nav_.focus = 3;
        snap("menu_locations", 2);
        startMission(55); beginBriefing(55);
        for (int i = 0; i < 300; ++i) briefT_ += 1.0 / 30.0;
        snap("menu_briefing_carrier", 2);
        return 0;
    }
    if (set == "skinplay") {
        // the hangar camouflage must follow the jet into every mode
        settings_.aircraft = 0;
        settings_.setSkin(0, 7);
        startArena(false);
        hideHud_ = false;
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        scriptedStep(0.3, [&](double) { setInput(0, 0, 0, 0); });
        snap("skinplay_arena", 3);
        startMission(10);
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        scriptedStep(0.3, [&](double) { setInput(0, 0, 0, 0); });
        snap("skinplay_campaign", 3);
        settings_.setSkin(0, 0);
        return 0;
    }
    if (set == "locations") {
        // every location whose data is present: title flyover + a free-flight view
        const char* only = std::getenv("SKYFALL_LOC");
        for (const LocationDef& l : locations()) {
            if (!locationAvailable(l.id)) continue;
            if (only && std::string(only).find(l.id) == std::string::npos) continue;
            loadLocation(l.id, false);
            startTitle();
            scriptedStep(0.2, [&](double) {});
            snap((std::string("loc_") + l.id + "_title").c_str(), 3);
            freeFlight_ = true;
            startArena(false);
            hideHud_ = false;
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(0.3, [&](double) { setInput(0, 0, 0, 0); });
            snap((std::string("loc_") + l.id + "_flight").c_str(), 4);
            freeFlight_ = false;
        }
        loadLocation("desert", false);
        return 0;
    }
    if (set == "world") {
        hideHud_ = true;
        auto look = [&](Vec3 target, double dist, double agl, double ang, const char* name) {
            startArena(false);
            battle_.clearCombatants();
            battle_.setAiEnabled(false);
            tod_ = TimeOfDay::Morning; lighting_ = Lighting::preset(tod_);
            Vec3 from{target.x - std::cos(ang) * dist, 0, target.z - std::sin(ang) * dist};
            from.y = std::max(terrain_.surfaceHeight(from.x, from.z), terrain_.surfaceHeight(target.x, target.z)) + agl;
            double pitch = -std::atan2(from.y - terrain_.surfaceHeight(target.x, target.z), dist) * 0.85;
            double hdg = ang + 0.32;   // keep the subject to the left of the jet in the chase view
            battle_.player().fm->resetAirborne(from, hdg, 200, 0.6);
            battle_.player().fm->mutableState().q = Quat::fromEuler(hdg, pitch, 0.0);
            battle_.player().fm->mutableState().vel = Quat::fromEuler(hdg, pitch, 0.0).forward() * 200.0;
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(0.2, [&](double) { setInput(0, 0, 0, 0); });
            snap(name, 4);
        };
        const WorldLayout& L = layout_;
        if (L.towns.size() > 1) look(L.towns[1].center, 900, 220, 0.4, "w_town");
        if (L.towns.size() > 3) look(L.towns[3].center, 700, 160, 1.2, "w_village");
        if (!L.factories.empty()) look(L.factories[0], 700, 180, 0.9, "w_industry");
        int rb = L.largestBridge(true);
        if (rb >= 0) { Vec3 m = (L.bridges[rb].a + L.bridges[rb].b) * 0.5; look(m, 500, 60, std::atan2(L.bridges[rb].b.z - L.bridges[rb].a.z, L.bridges[rb].b.x - L.bridges[rb].a.x) + 1.4, "w_rail_viaduct"); }
        int best = -1; double bl = 0;
        for (size_t i = 0; i < L.bridges.size(); ++i) if (!L.bridges[i].rail && L.bridges[i].maxClearance > bl) { bl = L.bridges[i].maxClearance; best = static_cast<int>(i); }
        if (best >= 0) { Vec3 m = (L.bridges[best].a + L.bridges[best].b) * 0.5; look(m, 450, 50, std::atan2(L.bridges[best].b.z - L.bridges[best].a.z, L.bridges[best].b.x - L.bridges[best].a.x) + 1.5, "w_road_bridge"); }
        if (!L.turbines.empty()) look(L.turbines[0].pos, 700, 120, 0.3, "w_wind_farm");
        if (L.railway.length() > 0) {
            double lead = std::fmod(time_ * 17.0, L.railway.length());
            look(L.railway.at(lead), 300, 70, 2.2, "w_train");
        }
        look(Vec3{-6000, 0, 3600}, 2500, 500, 0.2, "w_overview");
        hideHud_ = false;
        return 0;
    }
    if (set == "infra") {
        auto approachGroup = [&](int mission, int group, double dist, double agl, double ang, const char* name, bool hud) {
            startMission(mission);
            battle_.setAiEnabled(false);
            hideHud_ = !hud;
            Vec3 c; int n = 0;
            for (const GroundTarget& g : battle_.ground()) if (g.group == group) { c += g.pos; ++n; }
            if (n) c = c * (1.0 / n);
            Vec3 from{c.x - std::cos(ang) * dist, 0, c.z - std::sin(ang) * dist};
            from.y = std::max(terrain_.surfaceHeight(from.x, from.z), c.y) + agl;
            double pitch = -std::atan2(from.y - c.y, dist) * 0.8;
            battle_.player().fm->resetAirborne(from, ang, 200, 0.6);
            battle_.player().fm->mutableState().q = Quat::fromEuler(ang, pitch, 0.0);
            battle_.player().fm->mutableState().vel = Quat::fromEuler(ang, pitch, 0.0).forward() * 200.0;
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(0.25, [&](double) { setInput(0, 0, 0, 0); });
            snap(name, 4);
        };
        approachGroup(42, 1, 900, 160, 0.3, "i42_power_plant", false);
        approachGroup(43, 1, 800, 150, 0.8, "i43_refinery", false);
        approachGroup(44, 1, 900, 120, 1.9, "i44_viaduct", true);
        approachGroup(45, 1, 600, 120, 0.5, "i45_comms_tower", false);
        approachGroup(46, 1, 500, 90, 1.2, "i46_train", false);
        approachGroup(47, 1, 900, 180, 0.6, "i47_factories", false);
        approachGroup(48, 1, 600, 110, 0.4, "i48_parked_jets", false);
        approachGroup(50, 1, 600, 120, 0.4, "i50_ammo", false);
        approachGroup(42, 1, 2500, 300, 0.3, "i42_hud", true);
        // tanker: rendezvous view and the refuelling position
        startMission(41);
        battle_.setAiEnabled(false);
        hideHud_ = false;
        {
            const Tanker& tk = battle_.tanker();
            Vec3 fwd{std::cos(tk.heading), 0, std::sin(tk.heading)};
            battle_.player().fm->resetAirborne(tk.pos - fwd * 260.0 - Vec3{0, 25, 0}, tk.heading, tk.speed, 0.6);
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(0.1, [&](double) { setInput(0, 0, 0, 0); });
            snap("i41_tanker_approach", 3);
            Quat q = Quat::fromEuler(battle_.tanker().heading, 0.0, 0.0);
            FlightState& st = battle_.player().fm->mutableState();
            st.q = q;
            st.pos = battle_.tanker().basket() - q.forward() * 8.0 - q.up() * 1.0;
            st.vel = battle_.tanker().vel;
            scriptedStep(0.5, [&](double) {
                const Tanker& t2 = battle_.tanker();
                Quat q2 = Quat::fromEuler(t2.heading, 0.0, 0.0);
                FlightState& s2 = battle_.player().fm->mutableState();
                s2.q = q2; s2.vel = t2.vel;
                s2.pos = t2.basket() - q2.forward() * 8.0 - q2.up() * 1.0;
                setInput(0, 0, 0, 0);
            });
            snap("i41_refuelling", 3);
        }
        hideHud_ = false;
        return 0;
    }
    if (set == "campaign") {
        // new ground targets and campaign HUD, framed from a jet approaching each site
        auto approach = [&](int mission, double tx, double tz, double dist, double agl, const char* name, double side = 0.0) {
            startMission(mission);
            battle_.setAiEnabled(false);
            double ang = std::atan2(tz - (tz - 1), 0.0) + side;   // approach from the west by default
            ang = side;
            Vec3 from{tx - std::cos(ang) * dist, 0, tz - std::sin(ang) * dist};
            from.y = std::max(terrain_.surfaceHeight(from.x, from.z), terrain_.surfaceHeight(tx, tz)) + agl;
            double pitch = -std::atan2(agl * 0.8, dist);
            battle_.player().fm->resetAirborne(from, ang, 200, 0.6);
            battle_.player().fm->mutableState().q = Quat::fromEuler(ang, pitch, 0.0);
            battle_.player().fm->mutableState().vel = Quat::fromEuler(ang, pitch, 0.0).forward() * 200.0;
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(0.25, [&](double) { setInput(0, 0, 0, 0); });
            snap(name, 4);
        };
        startTitle(); screen_ = Screen::Campaign; nav_.focus = 12;
        snap("campaign_menu", 1);
        startMissionBriefed(13);
        for (int i = 0; i < 90; ++i) { briefT_ += 1.0 / 30.0; }
        snap("briefing_m13", 2);
        startArenaBriefed();
        briefT_ = 30;
        snap("briefing_arena", 2);
        approach(12, 3900, -5673, 1500, 220, "c12_gun_line");
        approach(13, 11500, -6000, 1400, 260, "c13_oil_rig", 0.5);
        approach(17, -5500, -6500, 1300, 260, "c17_fuel_depot");
        approach(25, 5500, -12000, 1300, 250, "c25_hangars", 1.2);
        approach(21, -3200, -9000, 1200, 230, "c21_command_post");
        approach(19, 11000, -1500, 1500, 250, "c19_patrol_boats", 0.3);
        approach(22, 500, 3200, 1100, 200, "c22_artillery");
        startMission(4);
        battle_.setAiEnabled(false);
        scriptedStep(1.0, [&](double) { setInput(0, 0, 0, 0); });
        snap("c04_waypoint_hud", 3);
        startMission(3);
        scriptedStep(0.5, [&](double) { setInput(0, 0, 0, 0); });
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        snap("m03_start_minimap", 4);
        // close-up beauty shots with the HUD hidden
        hideHud_ = true;
        approach(13, 11500, -6000, 420, 70, "c13_oil_rig_close", 0.5);
        approach(12, 3900, -5673, 380, 60, "c12_guns_close", 0.4);
        approach(25, 5500, -12000, 420, 70, "c25_hangars_close", 1.6);
        approach(19, 11000, -1500, 900, 80, "c19_boats_close", 0.3);
        return 0;
    }
    if (set == "missions") {
        startMission(3);
        scriptedStep(0.5, [&](double) { setInput(0, 0, 0, 0); });
        snap("m03_cockpit_approach", 3);
        camMode_ = CameraMode::Chase; chaseInit_ = false;
        snap("m03_chase_approach", 4);
        startMission(2);
        scriptedStep(1.0, [&](double) { setInput(0, -0.6, 0, 0); });
        snap("m02_chase_bank", 4);
        screen_ = Screen::Campaign; mode_ = GameMode::None;
        startTitle(); screen_ = Screen::Campaign;
        snap("campaign_menu", 1);
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
    bool arena = opt_.startMode.empty() || opt_.startMode == "arena";
    if (arena) {
        startArena(false);
        battle_.player().fm->resetAirborne({-4000, 2000, 2000}, 0.0, 230, 0.85);
    } else if (!startModeFromString(opt_.startMode)) {
        std::printf("unknown --mode %s\n", opt_.startMode.c_str());
        return 1;
    }
    benchSim_.clear(); benchHud_.clear();
    camMode_ = CameraMode::Chase;
    renderer_.setProfiling(std::getenv("SKYFALL_PROFILE") != nullptr);
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
        if (elapsed > 1.0) { stats_.push(dt * 1000.0); benchSim_.push_back(simMs_); benchHud_.push_back(hudMs_); }  // skip warm-up
        elapsed += dt;
    }
    auto meanMax = [](const std::vector<double>& v, double& mean, double& mx) {
        mean = 0; mx = 0;
        for (double x : v) { mean += x; mx = std::max(mx, x); }
        if (!v.empty()) mean /= v.size();
    };
    double sm, sx, hm, hx;
    meanMax(benchSim_, sm, sx);
    meanMax(benchHud_, hm, hx);
    std::printf("cpu: simulation mean %.3f ms max %.3f ms | hud mean %.3f ms max %.3f ms\n", sm, sx, hm, hx);
    if (renderer_.profiling()) std::printf("gpu passes (per frame, serialised):\n%s", renderer_.profReport().c_str());
    std::printf("scene %dx%d, draw calls %d, triangles %.0fk\n", renderer_.width(), renderer_.height(), renderer_.drawCalls(), renderer_.triangles() / 1000.0);
    std::fflush(stdout);
    std::filesystem::create_directories("benchmarks");
    std::string renderer = std::string(reinterpret_cast<const char*>(rlGetVersion() == RL_OPENGL_33 ? "OpenGL 3.3" : "OpenGL"));
    stats_.writeCsv("benchmarks/frame_times.csv");
    stats_.writeJson("benchmarks/benchmark.json", arena ? "arena_chase" : opt_.startMode.c_str(), GetRenderWidth(), GetRenderHeight(), renderer);
    Percentiles p = FrameStats::computeFrom(stats_.all());
    std::printf("benchmark: %zu frames, mean %.2f ms (%.0f fps), p50 %.2f, p95 %.2f, p99 %.2f ms\n", p.samples, p.mean,
                p.mean > 0 ? 1000.0 / p.mean : 0, p.p50, p.p95, p.p99);
    return 0;
}

}  // namespace sky
