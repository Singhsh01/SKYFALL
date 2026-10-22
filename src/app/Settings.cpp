#include "Settings.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace sky {

namespace {
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool toBool(const std::string& v) { return v == "1" || v == "true" || v == "on" || v == "yes"; }
}  // namespace

std::string defaultSettingsPath() {
    namespace fs = std::filesystem;
    fs::path dir;
#if defined(_WIN32)
    if (const char* a = std::getenv("APPDATA")) dir = fs::path(a) / "SKYFALL";
#elif defined(__APPLE__)
    if (const char* h = std::getenv("HOME")) dir = fs::path(h) / "Library" / "Application Support" / "SKYFALL";
#else
    if (const char* x = std::getenv("XDG_CONFIG_HOME")) dir = fs::path(x) / "skyfall";
    else if (const char* h = std::getenv("HOME")) dir = fs::path(h) / ".config" / "skyfall";
#endif
    if (dir.empty()) dir = ".";
    return (dir / "settings.ini").string();
}

QualityProfile qualityProfile(GraphicsQuality q) {
    QualityProfile p;
    switch (q) {
        case GraphicsQuality::Low:
            p.sceneScale = 0.7; p.terrainHiDist = 2500; p.terrainL0Dist = 0; p.treeDist = 3000; p.sceneryDist = 9000; p.rainDrops = 1800; p.shadows = false;
            break;
        case GraphicsQuality::Medium:
            p.sceneScale = 0.85; p.terrainHiDist = 4500; p.terrainL0Dist = 900; p.treeDist = 6000; p.sceneryDist = 14000;
            break;
        case GraphicsQuality::High: break;
        case GraphicsQuality::Ultra: p.nativePixels = true; p.maxWidth = 3840; break;
        case GraphicsQuality::Cinematic4K:
            p.nativePixels = true; p.maxWidth = 3840; p.terrainHiDist = 9000; p.terrainL0Dist = 3000; p.treeDist = 12000; p.sceneryDist = 26000;
            p.rainDrops = 6000;
            break;
        case GraphicsQuality::Performance120:
            p.maxWidth = 1600; p.terrainHiDist = 4500; p.terrainL0Dist = 1200; p.treeDist = 5000; p.sceneryDist = 13000; p.rainDrops = 2200;
            p.frameBudgetMs = 1000.0 / 120.0;
            break;
    }
    return p;
}

const char* qualityName(GraphicsQuality q) {
    static const char* n[kQualityCount] = {"LOW", "MEDIUM", "HIGH", "ULTRA HD", "CINEMATIC 4K", "PERFORMANCE 120"};
    int i = static_cast<int>(q);
    return i >= 0 && i < kQualityCount ? n[i] : "HIGH";
}

void Settings::migrate() {
    if (settingsVersion >= 1) return;
    // v0.8 and older defaulted to a 75 fps cap with VSync off: on the 60 Hz MacBook Air display
    // that tears and judders. Untouched, it now follows the display (VSync, no extra cap).
    if (fpsCap == 75 && !vsync) { vsync = true; fpsCap = 0; }
    settingsVersion = 1;
}

bool Settings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    settingsVersion = 0;   // until the file says otherwise
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        try {
            if (k == "quality") quality = static_cast<GraphicsQuality>(std::clamp(std::stoi(v), 0, kQualityCount - 1));
            else if (k == "smooth_motion") smoothMotion = toBool(v);
            else if (k == "arcade_boost") arcadeBoost = toBool(v);
            else if (k == "settings_version") settingsVersion = std::max(0, std::stoi(v));
            else if (k == "fps_cap") fpsCap = std::clamp(std::stoi(v), 0, 240);
            else if (k == "width") width = std::max(640, std::stoi(v));
            else if (k == "height") height = std::max(360, std::stoi(v));
            else if (k == "fullscreen") fullscreen = toBool(v);
            else if (k == "vsync") vsync = toBool(v);
            else if (k == "fov") fovDeg = std::clamp(std::stod(v), 50.0, 100.0);
            else if (k == "bloom") bloom = toBool(v);
            else if (k == "camera_shake") cameraShake = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "g_effects") gEffects = toBool(v);
            else if (k == "ui_scale") uiScale = std::clamp(std::stod(v), 0.75, 1.5);
            else if (k == "show_fps") showFps = toBool(v);
            else if (k == "high_contrast_hud") highContrastHud = toBool(v);
            else if (k == "missile_cam") missileCam = toBool(v);
            else if (k == "crash_replay") crashReplay = toBool(v);
            else if (k == "dynamic_resolution") dynamicResolution = toBool(v);
            else if (k == "campaign_done") { for (size_t i = 0; i < v.size() && i < campaignDone.size(); ++i) campaignDone[i] = v[i] == '1' ? '1' : '0'; }
            else if (k == "hud_style") hudStyle = std::clamp(std::stoi(v), 0, 1);
            else if (k == "invert_pitch") invertPitch = toBool(v);
            else if (k == "sensitivity") sensitivity = std::clamp(std::stod(v), 0.3, 2.0);
            else if (k == "deadzone") deadzone = std::clamp(std::stod(v), 0.0, 0.5);
            else if (k == "handling") handling = std::clamp(std::stoi(v), 0, 2);
            else if (k == "aircraft") aircraft = std::clamp(std::stoi(v), 0, 7);
            else if (k == "location") location = v;
            else if (k == "skins") { for (size_t i = 0; i < v.size() && i < skins.size(); ++i) skins[i] = ((v[i] >= '0' && v[i] <= '9') || (v[i] >= 'a' && v[i] <= 'z')) ? v[i] : '0'; }
            else if (k == "master") master = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "music") music = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "engine") engine = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "radio") radio = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "effects") effects = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "music_enabled") musicEnabled = toBool(v);
            else if (k == "captions") captions = toBool(v);
            else if (k == "voice_briefings") voiceBriefings = toBool(v);
            else extra[k] = v;
        } catch (...) {
            // malformed value: keep default
        }
    }
    return true;
}

bool Settings::save(const std::string& path) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "# SKYFALL settings - safe to edit by hand\n[graphics]\n"
      << "quality=" << static_cast<int>(quality) << "\nfps_cap=" << fpsCap << "\nwidth=" << width << "\nheight=" << height
      << "\nfullscreen=" << fullscreen << "\nvsync=" << vsync << "\nfov=" << fovDeg << "\nbloom=" << bloom
      << "\ncamera_shake=" << cameraShake << "\ng_effects=" << gEffects << "\nui_scale=" << uiScale
      << "\nshow_fps=" << showFps << "\nhigh_contrast_hud=" << highContrastHud << "\nhud_style=" << hudStyle << "\nmissile_cam=" << missileCam << "\ncrash_replay=" << crashReplay << "\ndynamic_resolution=" << dynamicResolution << "\nsmooth_motion=" << smoothMotion << "\nsettings_version=" << settingsVersion << "\n[input]\n"
      << "invert_pitch=" << invertPitch << "\nsensitivity=" << sensitivity << "\ndeadzone=" << deadzone
      << "\nhandling=" << handling << "\narcade_boost=" << arcadeBoost << "\naircraft=" << aircraft << "\nskins=" << skins << "\nlocation=" << location << "\n[audio]\n"
      << "master=" << master << "\nmusic=" << music << "\nengine=" << engine << "\nradio=" << radio
      << "\neffects=" << effects << "\nmusic_enabled=" << musicEnabled << "\ncaptions=" << captions << "\nvoice_briefings=" << voiceBriefings << "\n[progress]\ncampaign_done=" << campaignDone << "\n";
    if (!extra.empty()) {
        f << "[extra]\n";
        for (auto& [k, v] : extra) f << k << '=' << v << '\n';
    }
    return static_cast<bool>(f);
}

}  // namespace sky
