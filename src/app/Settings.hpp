// User settings persisted as a small human-readable INI file in the user's config directory.
// Only SKYFALL's own file is ever written; unknown keys are preserved on save.
#pragma once

#include <map>
#include <string>

namespace sky {

enum class GraphicsQuality { Low = 0, Medium = 1, High = 2, Ultra = 3,   // Ultra: native (Retina / 4K) resolution
                             Cinematic4K = 4,      // native pixels up to 3840 x 2160, longest view distances
                             Performance120 = 5 }; // window-point resolution, 120 fps budget, lighter scenery
constexpr int kQualityCount = 6;

/// What each graphics preset renders: scene resolution, view distances and effect budgets.
struct QualityProfile {
    double sceneScale = 1.0;      // x the window size in points
    bool nativePixels = false;    // render at the display's real pixels (Retina / 4K) instead of points
    double maxWidth = 1920.0;     // cap on the 3D scene width (pixels)
    float terrainHiDist = 7000, terrainL0Dist = 2200;   // terrain LOD distances
    float treeDist = 9000, sceneryDist = 20000;         // vegetation / buildings view distances
    int rainDrops = 4200;
    bool shadows = true;
    double frameBudgetMs = 0;     // > 0: the dynamic resolution aims at this frame time
};
QualityProfile qualityProfile(GraphicsQuality q);
const char* qualityName(GraphicsQuality q);

struct Settings {
    // Graphics
    GraphicsQuality quality = GraphicsQuality::Ultra;
    int fpsCap = 0;             // frame-rate limit (0 = none: the display paces with VSync)
    int width = 1280, height = 720;
    bool fullscreen = false;
    bool vsync = true;         // frames in step with the display (no tearing, even motion)
    double fovDeg = 70.0;
    bool bloom = true;
    double cameraShake = 0.6;   // 0..1 motion comfort
    bool gEffects = true;       // blackout / redout vignette
    double uiScale = 1.0;
    double hudPanelScale = 0.55;    // size of the non-critical HUD panels (1.0 = the earlier, larger panels)
    bool showFps = true;
    bool highContrastHud = false;
    int hudStyle = 0;
    bool missileCam = true;     // picture-in-picture target / missile / hit camera
    bool crashReplay = true;    // replay + analysis after the aircraft is lost
    bool killReplay = true;     // second window: an enemy's view of its own shoot-down
    bool dynamicResolution = true;  // lower the 3D resolution automatically to hold the frame rate
    bool smoothMotion = true;       // draw between the last two physics steps (even motion at any fps)
    int settingsVersion = 1;        // a file without the key was written by v0.8 or older (see migrate)
    // Input
    bool invertPitch = false;
    double sensitivity = 1.0;
    double deadzone = 0.12;
    int handling = 0;
    bool arcadeBoost = true;        // Arcade handling: fast acceleration / braking performance profile
    int aircraft = 0;
    std::string location = "desert";   // world location (see sim/Locations.hpp)
    std::string skins = std::string(8, '0');   // camouflage per aircraft (digit = skin index)
    // one character per aircraft: '0'-'9' then 'a'-'z' (skin index 0-35)
    int skinFor(int aircraftId) const {
        if (aircraftId < 0 || aircraftId >= static_cast<int>(skins.size())) return 0;
        char c = skins[aircraftId];
        return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'z' ? 10 + (c - 'a') : 0);
    }
    void setSkin(int aircraftId, int skin) {
        if (aircraftId >= 0 && aircraftId < static_cast<int>(skins.size()) && skin >= 0 && skin < 36)
            skins[aircraftId] = static_cast<char>(skin < 10 ? '0' + skin : 'a' + (skin - 10));
    }
    // Audio (0..1)
    double master = 0.8, music = 0.5, engine = 0.85, radio = 0.65, effects = 0.9;
    bool musicEnabled = false;  // background music OFF by default (listen to your own)
    bool captions = true;
    bool voiceBriefings = true;   // spoken mission briefings (system text-to-speech)
    // Progress: one character per campaign mission ('1' = completed)
    std::string campaignDone = std::string(61, '0');
    bool missionCompleted(int n) const { return n >= 1 && n <= static_cast<int>(campaignDone.size()) && campaignDone[n - 1] == '1'; }
    void markCompleted(int n) { if (n >= 1 && n <= static_cast<int>(campaignDone.size())) campaignDone[n - 1] = '1'; }

    /// Upgrades a file from an older version: only values that were never changed from the old
    /// defaults are moved to the new defaults (75 fps cap without VSync -> paced by the display).
    void migrate();
    /// Returns false if the file did not exist or could not be parsed (defaults are kept).
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::map<std::string, std::string> extra;  // unknown keys round-trip unchanged
};

/// Platform config path: macOS ~/Library/Application Support/SKYFALL/settings.ini,
/// Linux $XDG_CONFIG_HOME/skyfall/settings.ini, Windows %APPDATA%\SKYFALL\settings.ini.
std::string defaultSettingsPath();

}  // namespace sky
