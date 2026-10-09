// User settings persisted as a small human-readable INI file in the user's config directory.
// Only SKYFALL's own file is ever written; unknown keys are preserved on save.
#pragma once

#include <map>
#include <string>

namespace sky {

enum class GraphicsQuality { Low = 0, Medium = 1, High = 2 };

struct Settings {
    // Graphics
    GraphicsQuality quality = GraphicsQuality::High;
    int width = 1280, height = 720;
    bool fullscreen = false;
    bool vsync = true;
    double fovDeg = 70.0;
    bool bloom = true;
    double cameraShake = 0.6;   // 0..1 motion comfort
    bool gEffects = true;       // blackout / redout vignette
    double uiScale = 1.0;
    bool showFps = true;
    bool highContrastHud = false;
    int hudStyle = 0;           // 0 Minimal (Battlefield-like), 1 Full flight symbology
    // Input
    bool invertPitch = false;
    double sensitivity = 1.0;
    double deadzone = 0.12;
    int handling = 0;  // 0 Arcade (default), 1 Assisted, 2 Simulation
    // Audio (0..1)
    double master = 0.8, music = 0.5, engine = 0.85, radio = 0.65, effects = 0.9;
    bool musicEnabled = false;  // background music OFF by default (listen to your own)
    bool captions = true;

    /// Returns false if the file did not exist or could not be parsed (defaults are kept).
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::map<std::string, std::string> extra;  // unknown keys round-trip unchanged
};

/// Platform config path: macOS ~/Library/Application Support/SKYFALL/settings.ini,
/// Linux $XDG_CONFIG_HOME/skyfall/settings.ini, Windows %APPDATA%\SKYFALL\settings.ini.
std::string defaultSettingsPath();

}  // namespace sky
