#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include "Game.hpp"
#include "../sim/Locations.hpp"
#include "../mission/Progress.hpp"
#include "../render/Skins.hpp"
#include "../sim/TerrainAnalysis.hpp"

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
    settings_.migrate();
    if (opt.quality >= 0) settings_.quality = static_cast<GraphicsQuality>(std::clamp(opt.quality, 0, kQualityCount - 1));
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

double Game::displayFrameBudgetMs() const {
    double pb = qualityProfile(settings_.quality).frameBudgetMs;
    if (pb > 0) return pb;
    if (settings_.fpsCap > 0) return 1000.0 / settings_.fpsCap;
#ifdef __EMSCRIPTEN__
    return 1000.0 / 60.0;
#else
    int hz = IsWindowReady() ? GetMonitorRefreshRate(GetCurrentMonitor()) : 60;
    return 1000.0 / (hz >= 30 && hz <= 360 ? hz : 60);
#endif
}

void Game::applyFramePacing() {
#ifndef __EMSCRIPTEN__
    // VSync shows every frame for a whole number of display refreshes (no tearing); the cap
    // (30 / 60 / 90 / 120) limits below the display rate, or above it with VSync off
    if (settings_.vsync && !capturing_) SetWindowState(FLAG_VSYNC_HINT); else ClearWindowState(FLAG_VSYNC_HINT);
    SetTargetFPS(capturing_ ? 0 : settings_.fpsCap);
#endif
}

void Game::updateDynamicResolution(double dt) {
    // Holds ~60 fps by lowering the 3D scene resolution in 10 % steps (HUD stays native).
    // Down quickly when frames are slow; up only after sustained headroom, and not again soon
    // after an up-step that had to be undone (prevents oscillation and its stutter).
    // screenshots and --native benchmarks render at full resolution; a normal --bench plays like the game
    if (!settings_.dynamicResolution || (capturing_ && opt_.benchSeconds <= 0) || opt_.native) { dynScale_ = 1.0; return; }
    if (screen_ != Screen::Playing || replayActive_) { dynTimer_ = dynAccum_ = 0; dynFrames_ = 0; return; }
    dynAccum_ += dt; ++dynFrames_; dynTimer_ += dt;
    if (dynTimer_ < 1.0) return;
    double meanMs = dynAccum_ / std::max(1, dynFrames_) * 1000.0;
    dynTimer_ = dynAccum_ = 0; dynFrames_ = 0;
    // budget: the preset's (Performance 120: 8.3 ms), else the frame-rate cap, else the display
    double budget = displayFrameBudgetMs();
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
    Briefing br = makeBriefing(missionNo, missionNo > 0 ? &campVariant_ : nullptr);
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
    if (voice && (!capturing_ || std::getenv("SKYFALL_VOICE"))) speech_.say(who, text);
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
    // into /maps, kept in the browser's IndexedDB (mounted before main, see web/shell.html):
    // a map is downloaded once and is there on every later visit, offline too
    var name = UTF8ToString(id);
    fetch('terrain/' + name + '.skt').then(function (r) {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.arrayBuffer();
    }).then(function (buf) {
        try { FS.mkdir('/maps'); } catch (e) {}
        FS.writeFile('/maps/' + name + '.incoming', new Uint8Array(buf));
    }).catch(function (e) {
        try { FS.mkdir('/terrain'); } catch (e2) {}
        FS.writeFile('/terrain/' + name + '.missing', 'x');
    });
});

EM_JS(void, sky_sync_maps, (), {
    FS.syncfs(false, function (e) { if (e) console.warn('map store sync', e); });
});
#endif

std::string Game::terrainPath(const std::string& id) {
    // the persistent map store first (verified copies), then the maps bundled with the game
    if (maps_.installed(id)) return maps_.pathFor(id);
    return bundledMapPath(id);
}

std::string Game::bundledMapPath(const std::string& id) {
#ifdef __EMSCRIPTEN__
    return "/terrain/" + id + ".skt";
#else
    std::string app = GetApplicationDirectory();
    for (const std::string& p : {std::string("assets/terrain/") + id + ".skt", app + "assets/terrain/" + id + ".skt",
                                 app + "../Resources/assets/terrain/" + id + ".skt", app + "../assets/terrain/" + id + ".skt",
                                 std::string("../assets/terrain/") + id + ".skt"})
        if (FileExists(p.c_str())) return p;
    return "assets/terrain/" + id + ".skt";
#endif
}

std::string Game::terrainDir() {
#ifdef __EMSCRIPTEN__
    return "/terrain";
#else
    std::string app = GetApplicationDirectory();
    for (const std::string& p : {std::string("assets/terrain"), app + "assets/terrain", app + "../Resources/assets/terrain", app + "../assets/terrain", std::string("../assets/terrain")})
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
    return maps_.installed(id) || FileExists(bundledMapPath(id).c_str());
}

void Game::pumpMapDownloads() {
    // finished downloads land in maps/incoming: validate, checksum and move into the store
#ifdef __EMSCRIPTEN__
    for (const LocationDef& l : locations()) {
        std::string in = maps_.dir() + "/" + l.id + ".incoming";
        if (!FileExists(in.c_str())) continue;
        std::string err;
        bool ok = maps_.install(l.id, in, "download", err);
        std::remove(in.c_str());
        if (!ok) locStatus_ = l.name + ": the downloaded map failed verification (" + err + "). Try again.";
        sky_sync_maps();
    }
#else
    for (const std::string& id : download_.takeFinished()) {
        std::string in = maps_.dir() + "/incoming/" + id + ".skt";
        std::string err;
        bool verified = false;
        if (!maps_.install(id, in, "download", err, &verified)) locStatus_ = id + ": the downloaded map failed verification (" + err + ").";
        std::error_code ec;
        std::filesystem::remove(in, ec);
    }
#endif
}

void Game::installAllMaps() {
    // every location exactly once: maps already in the store are kept, bundled copies are
    // installed straight away (offline), the rest is downloaded
    int installed = 0, kept = 0, failed = 0;
    std::vector<std::string> fetch;
    for (const LocationDef& l : locations()) {
        if (l.id == "desert") continue;
        if (maps_.check(l.id) == MapStore::State::Valid) { ++kept; continue; }
        std::string b = bundledMapPath(l.id), err;
        if (FileExists(b.c_str()) && maps_.install(l.id, b, "bundled", err)) { ++installed; continue; }
        if (FileExists(b.c_str())) ++failed;
        fetch.push_back(l.id);
    }
#ifdef __EMSCRIPTEN__
    for (const std::string& id : fetch) sky_fetch_terrain(id.c_str());
    sky_sync_maps();
#else
    if (!fetch.empty()) download_.start(fetch, maps_.dir() + "/incoming", maps_.dir() + "/.tiles");
#endif
    char b[200];
    std::snprintf(b, sizeof b, "Maps: %d already installed, %d installed now%s%s", kept, installed,
                  fetch.empty() ? "" : (", " + std::to_string(fetch.size()) + " downloading").c_str(), failed ? " (a bundled copy was damaged)" : "");
    locStatus_ = b;
}

void Game::verifyAllMaps() {
    int ok = 0, bad = 0, missing = 0;
    std::string badNames;
    for (const LocationDef& l : locations()) {
        if (l.id == "desert") continue;
        MapStore::State st = maps_.check(l.id, true);
        if (st == MapStore::State::Valid) ++ok;
        else if (st == MapStore::State::Missing) ++missing;
        else { ++bad; badNames += (badNames.empty() ? "" : ", ") + l.name; }
    }
    locStatus_ = "Verified (SHA-256): " + std::to_string(ok) + " intact, " + std::to_string(bad) + " damaged, " + std::to_string(missing) +
                 " not in the store" + (bad ? " - repair: " + badNames : "") + ".";
}

void Game::repairMap(const std::string& id) {
    const LocationDef* l = findLocation(id);
    if (!l || id == "desert") return;
    maps_.remove(id);
    std::string b = bundledMapPath(id), err;
    if (FileExists(b.c_str()) && maps_.install(id, b, "bundled", err)) { locStatus_ = l->name + ": repaired from the copy bundled with the game."; return; }
#ifdef __EMSCRIPTEN__
    sky_fetch_terrain(id.c_str());
    locStatus_ = l->name + ": downloading a fresh copy...";
#else
    if (download_.start({id}, maps_.dir() + "/incoming", maps_.dir() + "/.tiles")) locStatus_ = l->name + ": downloading a fresh copy...";
#endif
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
    nav_.begin(n + 6, in.up, in.down, in.confirm, in.left, in.right);
    if (nav_.focus < n) locSel_ = nav_.focus;
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
                else if (download_.start({l.id}, maps_.dir() + "/incoming", maps_.dir() + "/.tiles")) locStatus_.clear();
            } else if (l.id != terrain_.info().id) {
                switchTo_ = l.id;
            }
            return;
        }
        bool loading = download_.busy() && download_.current() == l.name;
        bool stored = l.id == "desert" || maps_.installed(l.id);
        ui::text(avail ? (stored ? l.region : l.region + "  (BUNDLED)") : loading ? "DOWNLOADING..." : "NOT DOWNLOADED - CLICK TO GET", x + bw - 10 * s, y + 10 * s, 12 * s,
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
        startTitle(); nav_.focus = 3; return;
    }
    // the map store: download every map once, verify, repair
    int inStore = 0, total = 0;
    for (const LocationDef& l : L) if (l.id != "desert") { ++total; if (maps_.installed(l.id)) ++inStore; }
    float dy = ay + 158 * s;
    if (download_.busy()) {
        if (ui::button(nav_, n + 3, {ax, dy, aw, 40 * s}, "CANCEL DOWNLOAD", download_.current())) download_.cancel();
        Rectangle pb = {ax, dy + 48 * s, aw, 10 * s};
        DrawRectangleRec(pb, Color{255, 255, 255, 40});
        DrawRectangleRec({pb.x, pb.y, pb.width * download_.progress(), pb.height}, Color{150, 255, 205, 230});
        ui::text(download_.status(), ax, dy + 64 * s, 13 * s, Color{220, 230, 230, 230}, Align::Left, false);
    } else {
        std::string sub = std::to_string(inStore) + " of " + std::to_string(total) + " in the map store - kept across updates, offline";
        if (ui::button(nav_, n + 3, {ax, dy, aw, 40 * s}, inStore < total ? "DOWNLOAD ALL MAPS" : "ALL MAPS INSTALLED", sub)) { audio_.play(Sfx::Click); installAllMaps(); }
    }
    dy += 84 * s;
    float hw2 = (aw - 8 * s) / 2;
    if (ui::button(nav_, n + 4, {ax, dy, hw2, 36 * s}, "VERIFY ALL MAPS")) { audio_.play(Sfx::Click); verifyAllMaps(); }
    const LocationDef& sel = L[std::clamp(locSel_, 0, n - 1)];
    if (ui::button(nav_, n + 5, {ax + hw2 + 8 * s, dy, hw2, 36 * s}, "REPAIR " + sel.name, "", sel.id != "desert") && sel.id != "desert") {
        audio_.play(Sfx::Click);
        repairMap(sel.id);
    }
    dy += 46 * s;
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
    if (!battle_.jets().empty())
        battle_.player().skin = settings_.skinFor(static_cast<int>(battle_.player().fm->profile().id));
    if (!battle_.jets().empty())
        battle_.player().fm->setHandling(static_cast<HandlingMode>(settings_.handling));
    if (!battle_.jets().empty()) battle_.player().fm->setArcadeBoost(settings_.arcadeBoost);
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
    terrain_.clearObstacles();
    gateMarkers_.clear();
    corridor_.clear();
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
    TraceLog(LOG_INFO, "SKYFALL: start mission %d", n);
    terrain_.clearObstacles();
    gateMarkers_.clear();
    corridor_.clear();
    const std::string loc = campaignLocation();
    if (n <= kCampaignMissions && terrain_.info().id != loc) {
        // the campaign is flown over the location chosen on the campaign screen: switch first
        // (next frame), then start
        if (!locationAvailable(loc)) { locStatus_ = "This location is not downloaded yet: get it in WORLD LOCATIONS."; return; }
        switchTo_ = loc;
        switchMission_ = n;
        return;
    }
    freeFlight_ = false;
    screen_ = Screen::Playing;
    mode_ = GameMode::Mission01;   // "guided mission" mode
    missionNo_ = n;
    campaignDef_ = nullptr;
    campVariant_ = campaignVariantFor(terrain_, layout_);
    if (n >= 4 && campaignMission(n)) {
        // the mission laid out on the loaded location (the desert keeps the original)
        campDef_ = adaptCampaignMission(*campaignMission(n), terrain_, layout_);
        campaignDef_ = &campDef_;
    }
    director_ = CampaignDirector{};
    tod_ = n == 3 ? TimeOfDay::Noon : TimeOfDay::Morning;
    if (campaignDef_) tod_ = campaignDef_->timeOfDay == 1 ? TimeOfDay::GoldenHour : campaignDef_->timeOfDay == 2 ? TimeOfDay::Noon
                           : campaignDef_->timeOfDay == 3 ? TimeOfDay::Storm : TimeOfDay::Morning;
    lighting_ = Lighting::preset(tod_);
    rain_ = campaignDef_ && campaignDef_->rain;
    if (campaignDef_ && campaignDef_->carrier) terrain_.setDeck(campaignDef_->deck);
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
        double off = campVariant_.climbOffset;
        p.fm->resetAirborne({rw.center.x + 2000.0, 1250.0 + off, rw.center.z}, 0.0, 200.0, 0.62);
        mission_ = makeMission02(off);
        camMode_ = CameraMode::Chase;
    } else if (n == 3) {
        Mission03Approach ap = mission03Approach(terrain_);
        Vec3 th = landingThreshold(rw, ap.dir);
        double d = ap.dist, hdg = ap.dir > 0 ? 0.0 : kPi;
        p.fm->resetAirborne({th.x - ap.dir * d, rw.center.y + glidePathHeight(d), th.z}, hdg, 95.0, 0.45);
        p.fm->mutableState().q = Quat::fromEuler(hdg, 2.0 * kDeg, 0.0);
        p.fm->mutableState().vel = Quat::fromEuler(hdg, -3.0 * kDeg, 0.0).forward() * 95.0;
        mission_ = makeMission03(ap.dir);
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
    // the briefing comes first over the radio (queueBriefing), then these calls
    radioQueue_.clear();
    if (campaignDef_ && campaignDef_->carrier) queueRadio(Speaker::Tower, lines::kCarrierCat);
    else if (n == 1) queueRadio(Speaker::Tower, lines::kTowerStartup);
    else if (n == 2) queueRadio(Speaker::Instructor, lines::kInstructorStart);
    else if (n == 3) queueRadio(Speaker::Tower, campVariant_.runway27 ? lines::kTowerLanding27 : lines::kTowerLanding);
    audio_.play(Sfx::Radio);
}

void Game::queueRadio(Speaker who, const std::string& text, bool front) {
    if (front) radioQueue_.push_front({who, text});
    else radioQueue_.push_back({who, text});
}

void Game::queueBriefing(int missionNo) {
    // the pilot is already in the cockpit; operations briefs over the radio, in short calls
    Briefing br = makeBriefing(missionNo, missionNo > 0 ? &campVariant_ : nullptr);
    for (auto it = br.parts.rbegin(); it != br.parts.rend(); ++it) queueRadio(Speaker::Captain, *it, true);
    camMode_ = CameraMode::Cockpit;
    chaseInit_ = false;
    radioNextAt_ = time_ + 1.2;   // a moment to settle in before the first call
}

void Game::updateRadioQueue() {
    if (radioQueue_.empty() || time_ < radioNextAt_) return;
    if (speech_.speaking()) { radioNextAt_ = time_ + 0.2; return; }
    auto item = radioQueue_.front();
    radioQueue_.pop_front();
    double dur = std::max(4.0, item.second.size() / 13.0 + 1.0);
    hud_.radio(speakerName(item.first), item.second, dur);
    bool voiced = settings_.voiceBriefings && (!capturing_ || std::getenv("SKYFALL_VOICE"));
    if (voiced) speech_.say(item.first, item.second);
    audio_.play(Sfx::Radio, 0.5f);
    // with a voice, the next call follows the end of this one; captions-only paces by length
    radioNextAt_ = time_ + (voiced && speech_.hasRecording(item.first, item.second) ? 0.6 : dur * 0.9);
}

void Game::startArena(bool keepPlayerState) {
    screen_ = Screen::Playing;
    mode_ = GameMode::Arena;
    terrain_.clearObstacles();
    gateMarkers_.clear();
    corridor_.clear();
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
    radioQueue_.clear();
    if (freeFlight_) queueRadio(Speaker::Tower, freeFlightLine(terrain_.info().name));
    else queueRadio(Speaker::Awacs, lines::kArenaStart);
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
    p.fm->setArcadeBoost(settings_.arcadeBoost);
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
    pumpMapDownloads();
    if (screen_ == Screen::Playing) updateRadioQueue();
    if (!switchTo_.empty()) {
        // menus request location changes; the world is rebuilt here, outside any drawing
        std::string id = switchTo_;
        int mission = switchMission_;
        switchTo_.clear();
        switchMission_ = 0;
        bool ok = loadLocation(id, true);
        int training = pendingTraining_;
        bool toAcademy = academyAfterSwitch_;
        pendingTraining_ = 0;
        academyAfterSwitch_ = false;
        if (mission > 0) { if (ok) startMissionBriefed(mission); else { startTitle(); screen_ = Screen::Campaign; } }
        else if (training > 0) { if (ok) startTraining(training); else openAcademy(); }
        else if (toAcademy) openAcademy();
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
        const QualityProfile qp = qualityProfile(settings_.quality);
        double scale = qp.sceneScale * dynScale_;
        // Ultra / Cinematic 4K render at the display's real pixel count (Retina / 4K), up to
        // 3840 wide; the others render at the window size in points and the composite pass
        // upscales with contrast-adaptive sharpening (reconstructed 4K on a 4K display)
        bool native = qp.nativePixels;
        double baseW = native ? GetRenderWidth() : GetScreenWidth(), baseH = native ? GetRenderHeight() : GetScreenHeight();
        double maxW = qp.maxWidth;
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
        case Screen::Academy: titleTime_ += dt; updateTitleScene(dt); break;
        default: break;
    }
    beginRenderInterpolation();
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
    endRenderInterpolation();
}

void Game::beginRenderInterpolation() {
    interpActive_ = false;
    if (!settings_.smoothMotion || replayActive_ || battle_.jets().empty()) return;
    if (screen_ == Screen::Paused || screen_ == Screen::Briefing) return;
    const double a = std::clamp(stepper_.alpha(), 0.0, 1.0);
    interpJets_.clear();
    interpMissiles_.clear();
    for (auto& jp : battle_.jets()) {
        Jet& j = *jp;
        if (!j.prevValid || !j.fm) continue;
        FlightState& s = j.fm->mutableState();
        // a reset or teleport since the last step: draw where it is now
        if (dot(s.pos - j.prevPos, s.pos - j.prevPos) > 250.0 * 250.0) continue;
        interpJets_.push_back({&j, s.pos, s.q});
        s.pos = j.prevPos + (s.pos - j.prevPos) * a;
        Quat q0 = j.prevQ, q1 = s.q;
        double d = q0.w * q1.w + q0.x * q1.x + q0.y * q1.y + q0.z * q1.z;
        if (d < 0) { q1 = Quat{-q1.w, -q1.x, -q1.y, -q1.z}; }
        s.q = Quat{q0.w + (q1.w - q0.w) * a, q0.x + (q1.x - q0.x) * a, q0.y + (q1.y - q0.y) * a, q0.z + (q1.z - q0.z) * a}.normalized();
    }
    for (Missile& m : battle_.missilesMut()) {
        interpMissiles_.push_back(m.pos);
        if (m.prevValid && dot(m.pos - m.prevPos, m.pos - m.prevPos) < 300.0 * 300.0) m.pos = m.prevPos + (m.pos - m.prevPos) * a;
    }
    interpActive_ = true;
}

void Game::endRenderInterpolation() {
    if (!interpActive_) return;
    for (const InterpSaved& sv : interpJets_) {
        FlightState& s = sv.jet->fm->mutableState();
        s.pos = sv.pos;
        s.q = sv.q;
    }
    auto& ms = battle_.missilesMut();
    for (size_t i = 0; i < ms.size() && i < interpMissiles_.size(); ++i) ms[i].pos = interpMissiles_[i];
    interpActive_ = false;
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
        std::error_code ec;
        hud_.radio("SYSTEM", std::string("Screenshot saved: ") + std::filesystem::absolute(name, ec).string(), 4);
    }
}

void Game::updateTitleScene(double dt) {
    // fly the lead aircraft straight and level; wingmen hold echelon positions
    int n = stepper_.advance(dt);
    for (int i = 0; i < n; ++i) battle_.update(FlightModel::kDt, ControlInputs{}, PlayerCommands{});
    if ((screen_ == Screen::Title || screen_ == Screen::Locations || screen_ == Screen::Academy) && battle_.jets().size() >= 3) {
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
            if (mode_ == GameMode::Training) training_.update(missionContext(), battle_, mission_->stepIndex(), FlightModel::kDt);
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
        if (mode_ == GameMode::Training) {
            if (trainingDone_) startTraining(trainingLevel_);   // fly the level again
            else retryTraining();
            return;
        }
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
    if (mode_ == GameMode::Training && trainingDone_ && in.confirm) { openAcademy(); return; }
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
    if (mode_ == GameMode::Training) updateTrainingFrame(dt);

    // mission narration / completion
    if (mission_ && mode_ == GameMode::Mission01) {
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
            markCampaignCompletedAt(settings_, terrain_.info().id, missionNo_);
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
        if (mode_ == GameMode::Training && !trainingCrashNoted_) { training_.notifyCrash(); trainingCrashNoted_ = true; }
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
    // ---- sense of speed: how fast for this jet, how close to the ground, how hard it accelerates
    {
        double vmax = std::max(200.0, p.fm->arcadeTopSpeed(s.pos.y));
        double agl = s.agl;
        double target = std::clamp((s.airspeed - 0.35 * vmax) / (0.6 * vmax), 0.0, 1.0);
        lowAlt_ = (1.0 - smoothstep(40.0, 450.0, agl)) * smoothstep(0.25 * vmax, 0.55 * vmax, s.airspeed);
        speedNorm_ += (target - speedNorm_) * (1.0 - std::exp(-dt * 2.5));
        double a = dt > 1e-4 ? (s.airspeed - lastAirspeed_) / dt : 0.0;
        if (screen_ != Screen::Playing || s.onGround || std::fabs(a) > 400.0) a = 0.0;   // resets / teleports
        lastAirspeed_ = s.airspeed;
        accelSmooth_ += (a - accelSmooth_) * (1.0 - std::exp(-dt * 4.0));
        // punching into high speed: a short rush, once per surge
        bool surge = accelSmooth_ > 45.0 && speedNorm_ > 0.35;
        if (surge && !surging_) rush_ = 1.0;
        surging_ = accelSmooth_ > 25.0 && speedNorm_ > 0.3;
        rush_ *= std::exp(-dt * 2.2);
    }
    const double comfort = 0.35 + 0.65 * settings_.cameraShake;   // the comfort slider tames it all
    double shakeAmp = settings_.cameraShake * std::min(1.0, shake_ + speedNorm_ * 0.22 + lowAlt_ * speedNorm_ * 0.25 +
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
            fovTarget = settings_.fovDeg + 2 + (speedNorm_ * 5.0 + lowAlt_ * 3.0) * comfort;
            nearPlane_ = 1.0f;
            break;
        }
        case CameraMode::Chase: {
            if (!chaseInit_) { chaseQ_ = s.q; chaseInit_ = true; }
            double k = 1.0 - std::exp(-dt * 7.0);
            chaseQ_ = slerp(chaseQ_, s.q, k);
            if (in.looking) { orbitYaw_ += in.lookYaw; orbitPitch_ = std::clamp(orbitPitch_ + in.lookPitch, -0.5, 1.0); }
            // Battlefield-style: close behind and slightly above, rolling with the jet; the camera
            // drops back as the jet surges forward and closes in when it brakes
            double dist = prof.chaseDistance * 0.95 * zoom_ * (1.0 + std::clamp(accelSmooth_ / 90.0, -0.12, 0.16) * comfort);
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
            fovTarget = settings_.fovDeg + 4.0 + (speedNorm_ * 11.0 + s.abLevel * 4.0 + lowAlt_ * 5.0 +
                                                  std::clamp(accelSmooth_ / 70.0, -0.6, 1.0) * 3.0) * comfort;
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
    audio_.setSpeedFeel(playing ? static_cast<float>(lowAlt_) : 0.0f);
    if (!playing || !p.alive) return;
    // the surge into high speed gets its whoosh (same trigger as the visual rush)
    if (rush_ > 0.97 && !rushSounded_) { audio_.play(Sfx::Rush, 0.7f); rushSounded_ = true; }
    if (rush_ < 0.5) rushSounded_ = false;
    // flybys: another jet passes within ~200 m at high closing speed
    Vector3 camRight = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(cam_.target, cam_.position), cam_.up));
    for (const auto& jp : battle_.jets()) {
        const Jet& j = *jp;
        if (&j == &p || !j.alive) continue;
        const FlightState& o = j.fm->state();
        Vec3 d = o.pos - s.pos;
        double dist = length(d);
        double closing = -dot(o.vel - s.vel, d) / std::max(dist, 1.0);
        auto it = flybyNear_.find(j.id);
        bool near = dist < 200.0 && length(o.vel - s.vel) > 120.0;
        if (near && (it == flybyNear_.end() || !it->second) && closing < 60.0) {
            // just passed the closest point: play it on the side it went by
            float pan = static_cast<float>(std::clamp(Vector3DotProduct(Vector3Normalize(toRl(d)), camRight), -1.0f, 1.0f));
            audio_.play(Sfx::Flyby, static_cast<float>(std::clamp(1.4 - dist / 200.0, 0.3, 1.0)), pan);
            flybyNear_[j.id] = true;
        } else if (!near) {
            flybyNear_[j.id] = false;
        }
    }
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
    {
        const double comfort = 0.35 + 0.65 * settings_.cameraShake;
        f.speedFx = static_cast<float>(std::clamp((speedNorm_ * 0.55 + lowAlt_ * 0.45) * comfort, 0.0, 1.0));
        f.rush = static_cast<float>(rush_ * comfort);
        if (camMode_ == CameraMode::External || screen_ != Screen::Playing || replayActive_) f.speedFx = f.rush = 0.0f;
    }
    f.showRunwayLights = true;
    f.bloom = settings_.bloom;
    f.quality = settings_.quality;
    f.shadowFocus = toRl(battle_.player().fm->state().pos);
    if (mode_ == GameMode::Training && (screen_ == Screen::Playing || screen_ == Screen::Paused)) {
        f.gates = &gateMarkers_;
        if (corridorOn()) f.corridor = &corridor_;
    }
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
        hc.approachDir = campVariant_.runway27 ? -1 : 1;
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
        if (mode_ == GameMode::Training && mission_) {
            hc.training = &training_;
            hc.trainingDone = trainingDone_;
            hc.trainingResult = &trainingResult_;
            hc.trainingNewBest = trainingNewBest_;
            hc.trainingTitle = "LEVEL " + std::to_string(trainingLevel_) + "  " + trainingLevel(trainingLevel_).title;
            for (char& ch : hc.trainingTitle) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            hc.trainingNext = "ENTER  BACK TO THE ACADEMY      R  FLY IT AGAIN";
            hc.campaign = nullptr;
            hc.showGlidePath = false;
        }
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
        case Screen::Academy: drawAcademy(); break;
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
    nav_.begin(7, in.up, in.down, in.confirm, in.left, in.right);
    float x = 70 * s, y = 222 * s, bw = 430 * s, bh = 48 * s, gap = 8 * s;
    if (ui::button(nav_, 0, {x, y, bw, bh}, "CAMPAIGN", "61 missions over any world location, your aircraft and paint")) { audio_.play(Sfx::Click); screen_ = Screen::Campaign; locStatus_.clear(); nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 1, {x, y, bw, bh}, "TRAINING ACADEMY", "10 graded courses with an instructor: medals and scores")) { audio_.play(Sfx::Click); openAcademy(); return; }
    y += bh + gap;
    if (ui::button(nav_, 2, {x, y, bw, bh}, "DOGFIGHT ARENA", "Bandit waves over " + terrain_.info().name)) { audio_.play(Sfx::Click); startArenaBriefed(); return; }
    y += bh + gap;
    if (ui::button(nav_, 3, {x, y, bw, bh}, "WORLD LOCATIONS", "Real terrain: Himalaya, Alps, Sahara, Andes, Rio...")) { audio_.play(Sfx::Click); screen_ = Screen::Locations; locStatus_.clear(); nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 4, {x, y, bw, bh}, "HANGAR", "8 aircraft, 16 paint schemes")) { audio_.play(Sfx::Click); hangarSel_ = settings_.aircraft; startHangar(); nav_.focus = hangarSel_; return; }
    y += bh + gap;
    if (ui::button(nav_, 5, {x, y, bw, bh}, "SETTINGS", "Audio, graphics, controls")) { audio_.play(Sfx::Click); settingsReturn_ = Screen::Title; screen_ = Screen::Settings; nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 6, {x, y, bw, bh}, "QUIT")) { quit_ = true; return; }
    ui::text("v0.7  -  C++20 / raylib  -  fictional simulation, illustrative aircraft data", 70 * s, h - 34 * s, 13 * s, Color{255, 255, 255, 120}, Align::Left, false);
    ui::text("ENTER select   ARROWS navigate", w - 30 * s, h - 34 * s, 13 * s, Color{255, 255, 255, 120}, Align::Right, false);
}

namespace {
std::string upperStr(std::string v) {
    for (char& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return v;
}
}  // namespace

std::string Game::campaignLocation() const {
    auto it = settings_.extra.find("campaign_location");
    if (it == settings_.extra.end() || !findLocation(it->second)) return "desert";
    return it->second;
}

void Game::setCampaignLocation(const std::string& id) { settings_.extra["campaign_location"] = id; }

void Game::drawCampaign() {
    const InputState& in = input_.state();
    float w = static_cast<float>(GetScreenWidth()), h = static_cast<float>(GetScreenHeight());
    float s = h / 720.0f;
    DrawRectangleGradientH(0, 0, static_cast<int>(w), static_cast<int>(h), Color{0, 0, 0, 215}, Color{0, 0, 0, 120});
    ui::title("CAMPAIGN", 50 * s, 26 * s, 34 * s);
    const auto& cat = campaignCatalogue();
    const int n = static_cast<int>(cat.size());
    const std::string loc = campaignLocation();
    const LocationDef* locDef = findLocation(loc);
    const std::string locName = locDef ? locDef->name : "Skyfall Desert";
    int done = campaignCountAt(settings_, loc);
    char prog[96];
    std::snprintf(prog, sizeof prog, "%d / %d COMPLETE OVER %s", done, n, upperStr(locName).c_str());
    ui::text(prog, w - 50 * s, 40 * s, 15 * s, Color{150, 255, 205, 230}, Align::Right);
    // missions 0..n-1, then location, aircraft, paint, back
    const int kLoc = n, kAc = n + 1, kSkin = n + 2, kBack = n + 3;
    nav_.begin(n + 4, in.up, in.down, in.confirm, in.left, in.right);
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
            locStatus_.clear();
            startMissionBriefed(m.number);
            return;
        }
        if (campaignCompletedAt(settings_, loc, m.number))
            ui::text("+", r.x + r.width - 10 * s, r.y + r.height / 2 - 9 * s, 18 * s, Color{120, 240, 160, 255}, Align::Right);
    }
    // ---- where, in what, and how it is painted ----
    const float selY = h - 172 * s, selH = 46 * s;
    const float selW = (w - 100 * s - 16 * s) / 3.0f;
    {
        const auto& L = locations();
        int li = 0;
        for (int i = 0; i < static_cast<int>(L.size()); ++i) if (L[i].id == loc) li = i;
        bool avail = locationAvailable(loc);
        int d = ui::selector(nav_, kLoc, {50 * s, selY, selW, selH}, "LOCATION", upperStr(locName), avail ? "" : "NOT DOWNLOADED");
        if (d != 0) {
            li = (li + d + static_cast<int>(L.size())) % static_cast<int>(L.size());
            setCampaignLocation(L[li].id);
            settings_.save(settingsPath_);
            locStatus_.clear();
            audio_.play(Sfx::Click, 0.5f);
        }
    }
    {
        const auto& all = allProfiles();
        int ac = settings_.aircraft;
        int d = ui::selector(nav_, kAc, {50 * s + selW + 8 * s, selY, selW, selH}, "AIRCRAFT", all[ac].name);
        if (d != 0) {
            settings_.aircraft = (ac + d + 8) % 8;
            settings_.save(settingsPath_);
            audio_.play(Sfx::Click, 0.5f);
        }
    }
    {
        int ac = settings_.aircraft;
        int sk = settings_.skinFor(ac);
        int d = ui::selector(nav_, kSkin, {50 * s + 2 * (selW + 8 * s), selY, selW, selH}, "PAINT", skin(sk).name,
                                 std::to_string(sk + 1) + " / " + std::to_string(kSkinCount));
        if (d != 0) {
            settings_.setSkin(ac, (sk + d + kSkinCount) % kSkinCount);
            settings_.save(settingsPath_);
            audio_.play(Sfx::Click, 0.5f);
        }
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
    } else {
        Rectangle b = {50 * s, h - 118 * s, w - 100 * s - 230 * s, 64 * s};
        ui::panel(b, Color{0, 0, 0, 150}, Color{255, 255, 255, 40});
        std::string txt = locStatus_.empty() ? std::string("The 61 missions are laid out on the terrain of the chosen location. Progress is kept per location.") : locStatus_;
        ui::text(locDef ? upperStr(locDef->region) : "PROCEDURAL", b.x + 14 * s, b.y + 9 * s, 12 * s, Color{255, 176, 60, 230});
        ui::text(txt, b.x + 14 * s, b.y + 29 * s, 15 * s, locStatus_.empty() ? WHITE : Color{255, 170, 120, 240}, Align::Left, false);
    }
    if (ui::button(nav_, kBack, {w - 50 * s - 210 * s, h - 118 * s, 210 * s, 64 * s}, "BACK") || in.back) {
        if (mode_ != GameMode::None) startTitle();
        screen_ = Screen::Title;
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
    nav_.begin(26, in.up, in.down, in.confirm, in.left, in.right);
    float cw = 420 * s, rh = 36 * s, g = 5 * s;
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
    std::vector<std::string> qn;
    for (int i = 0; i < kQualityCount; ++i) qn.push_back(qualityName(static_cast<GraphicsQuality>(i)));
    if (ui::cycle(nav_, 7, {x2, y2, cw, rh}, "Quality", q, qn)) {
        settings_.quality = static_cast<GraphicsQuality>(q);
        // the 120 fps preset needs a frame rate that can reach 120
        if (settings_.quality == GraphicsQuality::Performance120 && settings_.fpsCap > 0 && settings_.fpsCap < 120) settings_.fpsCap = 120;
        applyFramePacing();
        changed = true;
    }
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
        static const int caps[] = {0, 30, 60, 90, 120, 75};
        static const char* capNames[] = {"DISPLAY / NONE", "30 FPS", "60 FPS", "90 FPS", "120 FPS", "75 FPS"};
        int nCaps = settings_.fpsCap == 75 ? 6 : 5;   // 75: kept only for files that chose it
        int fi = 0;
        for (int i = 0; i < nCaps; ++i) if (caps[i] == settings_.fpsCap) fi = i;
        std::vector<std::string> cn(capNames, capNames + nCaps);
        changed |= ui::toggle(nav_, 24, {x1, y + 22 * s + 3 * (rh + g), cw, rh}, "Arcade boost (3-4 s to top speed)", settings_.arcadeBoost);
        changed |= ui::toggle(nav_, 25, {x1, y + 22 * s + 4 * (rh + g), cw, rh}, "Smooth motion (frame interpolation)", settings_.smoothMotion);
        changed |= false;
        if (ui::cycle(nav_, 22, {x1, y + 22 * s + 2 * (rh + g), cw, rh}, "Frame rate limit", fi, cn)) {
            settings_.fpsCap = caps[fi];
            applyFramePacing();
            changed = true;
        }
        // VSync keeps every frame in step with the display: no tearing, even motion
        if (ui::toggle(nav_, 23, {x2, y2 + 10 * s + rh + 4 * s + g, cw, rh}, "VSync (even motion, no tearing)", settings_.vsync)) {
            applyFramePacing();
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
    if (ui::button(nav_, 18, {x2 + cw - 200 * s, h - 60 * s, 200 * s, 42 * s}, "BACK") || in.back) {
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
    if (ui::button(nav_, 1, {x, y, bw, bh}, mode_ == GameMode::Training ? "RESTART THE COURSE" : "RESTART")) {
        if (mode_ == GameMode::Training) startTraining(trainingLevel_);
        else if (mode_ == GameMode::Mission01) startMission(missionNo_);
        else startArena(false);
        return;
    }
    y += bh + gap;
    if (ui::button(nav_, 2, {x, y, bw, bh}, "SETTINGS")) { settingsReturn_ = Screen::Paused; screen_ = Screen::Settings; nav_.focus = 0; return; }
    y += bh + gap;
    if (ui::button(nav_, 3, {x, y, bw, bh}, "CONTROLS")) { showHelp_ = true; screen_ = Screen::Playing; audio_.setPaused(false); return; }
    y += bh + gap;
    if (ui::button(nav_, 4, {x, y, bw, bh}, mode_ == GameMode::Training ? "TRAINING ACADEMY" : "MAIN MENU")) {
        if (mode_ == GameMode::Training) openAcademy(); else startTitle();
        return;
    }
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
                if (mode_ == GameMode::Training) training_.update(missionContext(), battle_, mission_->stepIndex(), FlightModel::kDt);
                mission_->update(missionContext(), FlightModel::kDt);
                if (campaignDef_) director_.update(*mission_, battle_);
            }
        }
        time_ += chunk;
        if (mode_ == GameMode::Training) updateTrainingFrame(chunk);
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
    if (set == "speed") {
        // sense of speed: the same low pass slow and in full afterburner, chase and cockpit
        hideHud_ = false;
        freeFlight_ = true;
        startArena(false);
        settings_.handling = 0;
        applySettings();
        for (int pass = 0; pass < 2; ++pass) {
            double v0 = pass == 0 ? 160.0 : 390.0;
            double gx = -6000, gz = 2500;
            double alt = terrain_.surfaceHeight(gx, gz) + 55.0;
            battle_.player().fm->resetAirborne({gx, alt, gz}, 0.0, v0, pass == 0 ? 0.45 : 1.0);
            screen_ = Screen::Playing;
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            lastAirspeed_ = v0;
            scriptedStep(1.5, [&](double) {
                const FlightState& st = battle_.player().fm->state();
                double want = terrain_.surfaceHeight(st.pos.x + 400, st.pos.z) + 55.0;
                setInput(std::clamp((want - st.pos.y) * 0.02 - st.vel.y * 0.04, -0.4, 0.4), 0, 0, pass == 0 ? 0.0 : 1.0);
            });
            for (int k = 0; k < 20; ++k) updateCamera(1.0 / 30.0);   // settle FOV / smoothing
            if (pass == 1) { rush_ = 1.0; accelSmooth_ = 80.0; }
            snap(pass == 0 ? "speed_chase_slow" : "speed_chase_fast", 0);
            camMode_ = CameraMode::Cockpit;
            for (int k = 0; k < 20; ++k) updateCamera(1.0 / 30.0);
            if (pass == 1) rush_ = 0.6;
            snap(pass == 0 ? "speed_cockpit_slow" : "speed_cockpit_fast", 0);
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
    if (set == "gear") {
        // gear retraction in flight, through the normal input path, with sound
        audio_.init();
        for (int n : {1, 2, 52, 53}) {
            startMissionBriefed(n);
            Jet& p = battle_.player();
            std::printf("mission %d: onGround %d gearDown %d\n", n, p.fm->state().onGround, p.fm->state().gearDown);
            std::fflush(stdout);
            for (int k = 0; k < 60; ++k) frame(1.0 / 30.0);
            if (p.fm->state().onGround && !p.fm->state().catArmed) {
                // runway start: engine start, brakes off, full power, rotate
                p.fm->requestEngineStart();
                for (int k = 0; k < 60 && p.fm->state().engine != EngineState::Running; ++k) { scriptedStep(0.5, [&](double) { setInput(0, 0, 0, 0); }); frame(1.0 / 30.0); }
                p.fm->setParkingBrake(false);
                for (int k = 0; k < 120 && p.fm->state().agl < 60; ++k) {
                    scriptedStep(0.5, [&](double) { setInput(p.fm->state().airspeed > 75 ? 0.45 : 0.0, 0, 0, 1.0); });
                    frame(1.0 / 30.0);
                }
            } else if (p.fm->state().onGround) {
                scriptedStep(5.0, [&](double) { setInput(0, 0, 0, 1.0); });
                p.fm->setParkingBrake(false);
                scriptedStep(9.0, [&](double t) { setInput(t > 4.0 ? 0.35 : 0.0, 0, 0, 1.0); });
            }
            std::printf("  airborne: agl %.0f gearDown %d\n", p.fm->state().agl, p.fm->state().gearDown);
            std::fflush(stdout);
            if (p.fm->toggleGear()) audio_.play(Sfx::Gear, 0.6f);
            for (int k = 0; k < 150; ++k) frame(1.0 / 30.0);
            snap("gear_mission_" + std::to_string(n));
        }
        startArena(false);
        Jet& p = battle_.player();
        p.fm->resetAirborne({-4000, 1500, 2000}, 0.0, 120, 0.7);
        if (!p.fm->state().gearDown) p.fm->toggleGear();
        scriptedStep(4.0, [&](double) { setInput(0, 0, 0, 0); });
        for (CameraMode cm : {CameraMode::Chase, CameraMode::Cockpit}) {
            camMode_ = cm; chaseInit_ = false;
            if (!p.fm->state().gearDown) { p.fm->toggleGear(); scriptedStep(4.0, [&](double) { setInput(0, 0, 0, 0); }); }
            if (p.fm->toggleGear()) audio_.play(Sfx::Gear, 0.6f);
            for (int k = 0; k < 6; ++k) {
                scriptedStep(0.5, [&](double) { setInput(0, 0, 0, 0); });
                snap(std::string("gear_") + (cm == CameraMode::Chase ? "chase_" : "cockpit_") + std::to_string(k));
            }
        }
        return 0;
    }
    if (set == "flow") {
        // end-to-end menu / mode flow through the real frame loop (no rendering): academy,
        // crash and checkpoint retry, completion and progress, the campaign over other locations
        int fails = 0;
        auto expect = [&](bool ok, const char* what) { std::printf("flow: %s %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; std::fflush(stdout); };
        auto run = [&](int frames) { for (int i = 0; i < frames; ++i) frame(1.0 / 60.0, false); };
        loadLocation("desert", false);
        openAcademy();
        expect(screen_ == Screen::Academy, "academy opens");
        acLocation_ = "desert";
        startTraining(1);
        run(60);
        expect(mode_ == GameMode::Training && screen_ == Screen::Playing && mission_.has_value(), "level 1 starts at the desert");
        expect(academyRecord(settings_, 1, settings_.aircraft).attempts >= 1, "attempt recorded");
        // fly a little (engine start), then crash and retry from the checkpoint
        battle_.player().fm->requestEngineStart();
        run(400);
        size_t before = mission_->stepIndex();
        battle_.player().fm->forceCrash(CrashReason::Terrain);
        run(240);
        expect(playerDead_, "crash detected");
        retryTraining();
        run(30);
        expect(!playerDead_ && battle_.player().alive && mission_->status() == MissionStatus::Running, "retry from the checkpoint");
        expect(mission_->stepIndex() <= before + 1, "retry resumes at the checkpoint section");
        // complete the course: debrief, score stored
        mission_->skipTo(training_.course().sections.size());
        run(30);
        expect(trainingDone_, "course completion shows the debrief");
        AcademyRecord rec = academyRecord(settings_, 1, settings_.aircraft);
        std::printf("flow: level 1 record medal %d best %.0f attempts %d\n", rec.medal, rec.best, rec.attempts);
        openAcademy();
        expect(screen_ == Screen::Academy && mode_ == GameMode::None, "back to the academy");
        // the campaign in the desert: the original mission, completion saved
        setCampaignLocation("desert");
        startMission(5);
        run(10);
        expect(terrain_.info().id == "desert" && missionNo_ == 5 && campaignDef_ == &campDef_ && campDef_.location == "desert", "desert mission 5 starts");
        mission_->skipTo(campDef_.objectives.size());
        run(10);
        expect(missionDone_ && campaignCompletedAt(settings_, "desert", 5), "desert completion is saved");
        // a level at another location: the world is switched first
        if (locationAvailable("fuji")) {
            acLocation_ = "fuji";
            startTraining(5);
            run(5);
            expect(terrain_.info().id == "fuji" && mode_ == GameMode::Training && trainingLevel_ == 5, "level 5 at Mount Fuji (location switch)");
            run(120);
            expect(!playerDead_, "level 5 runs");
        }
        // the campaign over another location: the world is switched first, progress kept apart
        if (locationAvailable("guilin")) {
            setCampaignLocation("guilin");
            startMission(14);
            run(5);
            expect(terrain_.info().id == "guilin" && missionNo_ == 14 && campDef_.location == "guilin", "mission 14 over Guilin (location switch)");
            run(120);
            expect(!playerDead_ && battle_.player().fm->state().pos.y > terrain_.surfaceHeight(battle_.player().fm->state().pos.x, battle_.player().fm->state().pos.z), "mission 14 runs");
            mission_->skipTo(campDef_.objectives.size());
            run(10);
            expect(campaignCompletedAt(settings_, "guilin", 14) && !campaignCompletedAt(settings_, "desert", 14), "Guilin progress kept apart");
        }
        // a carrier mission over a map with sea and over one without
        if (locationAvailable("musandam")) {
            setCampaignLocation("musandam");
            startMission(52);
            run(5);
            expect(terrain_.info().id == "musandam" && campDef_.carrier && terrain_.deck().active && battle_.player().fm->state().catArmed, "mission 52 from the carrier at Musandam");
        }
        if (locationAvailable("matterhorn")) {
            setCampaignLocation("matterhorn");
            startMission(52);
            run(5);
            expect(terrain_.info().id == "matterhorn" && !campDef_.carrier && !terrain_.deck().active && !battle_.player().fm->state().onGround, "mission 52 airborne over the Matterhorn");
            run(120);
            expect(!playerDead_, "mission 52 runs over the Matterhorn");
        }
        setCampaignLocation("desert");
        // carrier level at Musandam
        if (locationAvailable("musandam")) {
            acLocation_ = "musandam";
            startTraining(10);
            run(5);
            expect(terrain_.info().id == "musandam" && mode_ == GameMode::Training && training_.course().carrier && terrain_.deck().active, "level 10 with the carrier at Musandam");
        }
        // L10 refuses a location without sea
        acLocation_ = "matterhorn";
        expect(academyLocationFor(10) == "musandam", "level 10 keeps a sea location");
        startTitle();
        std::printf("flow: %d failures\n", fails);
        return fails == 0 ? 0 : 1;
    }
    if (set == "academy") {
        // menus: title, academy (route preview), the campaign screen
        hideHud_ = true;
        const char* locEnv = std::getenv("SKYFALL_LOC");
        std::string loc = locEnv ? locEnv : "desert";
        loadLocation(loc, false);
        startTitle(); nav_.focus = 1;
        snap("academy_00_title", 2);
        acLocation_ = loc;
        for (int lv : {1, 4, 9}) {
            openAcademy(); acLevel_ = lv; nav_.focus = lv - 1;
            snap("academy_01_menu_l" + std::to_string(lv), 2);
        }
        setCampaignLocation(loc);
        startTitle(); screen_ = Screen::Campaign; nav_.focus = 13;
        snap("academy_02_campaign", 2);
        // in flight: level 4 (obstacles) and level 10 (carrier) at this location
        hideHud_ = false;
        for (int lv : {4, 9, 10}) {
            if (!academyLocationAllowed(loc, lv)) continue;
            startTraining(lv);
            if (mode_ != GameMode::Training) continue;
            const Course& c = training_.course();
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            scriptedStep(2.0, [&](double) { updateRadioQueue(); });
            snap("academy_1" + std::to_string(lv % 10) + "_runway", 2);
            // jump to the obstacle section (or the approach) and fly it a few seconds
            size_t target = 0;
            for (size_t k = 0; k < c.sections.size(); ++k) {
                const Section& sec = c.sections[k];
                bool obstacle = sec.style == RouteStyle::Obstacles && sec.gateEnd > sec.gateBegin;
                bool landing = sec.kind == SectionKind::Landing || sec.kind == SectionKind::CarrierLanding;
                if ((lv == 10 && landing) || (lv != 10 && obstacle)) { target = k; break; }
            }
            if (target == 0) continue;
            const Section& sec = c.sections[target];
            int g0 = (sec.kind == SectionKind::Landing || sec.kind == SectionKind::CarrierLanding) ? sec.gateStart : sec.gateBegin;
            const Gate& g = c.gates[g0];
            Jet& p = battle_.player();
            Vec3 at = g.pos - g.dir * 1500.0;
            at.y = g.pos.y + (lv == 10 ? 1500.0 * std::tan(c.glideDeg * kDeg) : 0.0);
            if (p.fm->state().onGround || true) p.fm->resetAirborne(at, std::atan2(g.dir.z, g.dir.x), lv == 10 ? p.fm->profile().approachSpeedMs : 180.0, 0.6);
            if (lv == 10 && !p.fm->state().gearDown) p.fm->toggleGear();
            mission_->skipTo(target);
            scriptedStep(1.5, [&](double) { setInput(0, 0, 0, 0); updateRadioQueue(); });
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            snap("academy_1" + std::to_string(lv % 10) + "_section", 3);
            camMode_ = CameraMode::Cockpit;
            snap("academy_1" + std::to_string(lv % 10) + "_cockpit", 3);
            if (lv == 4) {
                settings_.extra["academy_corridor"] = "1";
                camMode_ = CameraMode::Chase; chaseInit_ = false;
                scriptedStep(0.2, [&](double) { setInput(0, 0, 0, 0); });
                snap("academy_14_corridor", 2);
                hideHud_ = true;
                std::printf("academy capture: %zu gate markers, %zu corridor beads, corridor %d\n", gateMarkers_.size(), corridor_.size(), corridorOn() ? 1 : 0);
                snap("academy_14_corridor_nohud", 2);
                hideHud_ = false;
                settings_.extra["academy_corridor"] = "0";
            }
        }
        // the debrief card
        if (mode_ == GameMode::Training && mission_) {
            mission_->skipTo(training_.course().sections.size());
            scriptedStep(0.5, [&](double) { updateRadioQueue(); });
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            snap("academy_20_debrief", 2);
        }
        startTitle();
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
        startMission(55); queueBriefing(55);
        hideHud_ = false;
        scriptedStep(1.5, [&](double) { updateRadioQueue(); });
        snap("radio_briefing_1", 2);
        scriptedStep(4.0, [&](double) { updateRadioQueue(); });
        snap("radio_briefing_2", 2);
        return 0;
    }
    if (set == "detail") {
        // close-ups: control surfaces deflected, gear, lights, LOD comparison
        hideHud_ = true;
        const int acs[] = {0, 3, 2, 5};
        for (int ac : acs) {
            hangarSel_ = ac;
            startHangar();
            screen_ = Screen::Playing;
            Jet& p = battle_.player();
            orbitYaw_ = 3.3; orbitPitch_ = 0.42; zoom_ = 1.25;
            scriptedStep(0.05, [&](double) {});
            p.lastInput.roll = 1.0; p.lastInput.pitch = 1.0; p.lastInput.yaw = 1.0;
            char nm[64];
            std::snprintf(nm, sizeof nm, "detail_%d_controls", ac);
            snap(nm, 2);
            orbitYaw_ = 0.9; orbitPitch_ = 0.12; zoom_ = 1.1;
            std::snprintf(nm, sizeof nm, "detail_%d_rear", ac);
            snap(nm, 2);
        }
        // transonic shock collar and high-G wing condensation
        {
            startArena(false);
            battle_.clearCombatants();
            battle_.setAiEnabled(false);
            Jet& p = battle_.player();
            p.fm->resetAirborne({-2000, 1200, 2000}, 0.0, 333.0, 1.0);
            camMode_ = CameraMode::External; orbitYaw_ = 1.9; orbitPitch_ = 0.12; zoom_ = 1.2;
            scriptedStep(0.6, [&](double) { setInput(0, 0, 0, 1.0); });
            snap("detail_vapour_cone", 1);
            p.fm->resetAirborne({-2000, 1200, 2000}, 0.0, 250.0, 1.0);
            scriptedStep(0.9, [&](double) { setInput(1.0, 0.3, 0, 1.0); });
            orbitYaw_ = 2.6; orbitPitch_ = 0.35;
            snap("detail_wing_vapour", 1);
            camMode_ = CameraMode::Chase; chaseInit_ = false;
            p.fm->resetAirborne({-2000, 1200, 2000}, 0.0, 250.0, 1.0);
            scriptedStep(1.5, [&](double) { setInput(0, 0, 0, 1.0); });
            snap("detail_heat_haze", 2);
        }
        // dusk flight with navigation lights; LOD distances
        startArena(false);
        battle_.clearCombatants();
        battle_.setAiEnabled(false);
        tod_ = TimeOfDay::GoldenHour; lighting_ = Lighting::preset(tod_);
        camMode_ = CameraMode::External; orbitYaw_ = 2.0; orbitPitch_ = 0.1;
        for (float z : {0.7f, 6.0f, 30.0f}) {
            zoom_ = z;
            scriptedStep(0.1, [&](double) { setInput(0, 0, 0, 0); });
            char nm[64];
            std::snprintf(nm, sizeof nm, "detail_lod_zoom_%.0f", z * 10);
            snap(nm, 2);
        }
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
    double sumCalls = 0, sumTris = 0;
    long counted = 0;
    while (elapsed < opt_.benchSeconds && !WindowShouldClose()) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        // gentle scripted turn so the view changes
        battle_.player().lastInput.roll = std::sin(elapsed * 0.3) * 0.4;
        frame(std::min(dt, 0.25));
        if (elapsed > (opt_.native ? 1.0 : 6.0)) {   // skip warm-up
            stats_.push(dt * 1000.0); benchSim_.push_back(simMs_); benchHud_.push_back(hudMs_);
            sumCalls += renderer_.drawCalls(); sumTris += renderer_.triangles(); ++counted;
        }
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
    std::printf("display %dx%d, resolution scale %d%%\n", GetRenderWidth(), GetRenderHeight(), static_cast<int>(std::lround(dynScale_ * 100)));
    std::printf("scene %dx%d, quality %s, draw calls %.0f, triangles %.0fk (per-frame means)\n", renderer_.width(), renderer_.height(), qualityName(settings_.quality),
                sumCalls / std::max(1L, counted), sumTris / std::max(1L, counted) / 1000.0);
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
