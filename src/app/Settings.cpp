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

bool Settings::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        try {
            if (k == "quality") quality = static_cast<GraphicsQuality>(std::clamp(std::stoi(v), 0, 2));
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
            else if (k == "invert_pitch") invertPitch = toBool(v);
            else if (k == "sensitivity") sensitivity = std::clamp(std::stod(v), 0.3, 2.0);
            else if (k == "deadzone") deadzone = std::clamp(std::stod(v), 0.0, 0.5);
            else if (k == "simulation_handling") simulationHandling = toBool(v);
            else if (k == "master") master = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "music") music = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "engine") engine = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "radio") radio = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "effects") effects = std::clamp(std::stod(v), 0.0, 1.0);
            else if (k == "music_enabled") musicEnabled = toBool(v);
            else if (k == "captions") captions = toBool(v);
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
      << "quality=" << static_cast<int>(quality) << "\nwidth=" << width << "\nheight=" << height
      << "\nfullscreen=" << fullscreen << "\nvsync=" << vsync << "\nfov=" << fovDeg << "\nbloom=" << bloom
      << "\ncamera_shake=" << cameraShake << "\ng_effects=" << gEffects << "\nui_scale=" << uiScale
      << "\nshow_fps=" << showFps << "\nhigh_contrast_hud=" << highContrastHud << "\n[input]\n"
      << "invert_pitch=" << invertPitch << "\nsensitivity=" << sensitivity << "\ndeadzone=" << deadzone
      << "\nsimulation_handling=" << simulationHandling << "\n[audio]\n"
      << "master=" << master << "\nmusic=" << music << "\nengine=" << engine << "\nradio=" << radio
      << "\neffects=" << effects << "\nmusic_enabled=" << musicEnabled << "\ncaptions=" << captions << "\n";
    if (!extra.empty()) {
        f << "[extra]\n";
        for (auto& [k, v] : extra) f << k << '=' << v << '\n';
    }
    return static_cast<bool>(f);
}

}  // namespace sky
