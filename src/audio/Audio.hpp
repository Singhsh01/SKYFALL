// Procedural audio (no sample files): engine, wind, cannon, missiles, explosions, lock tones,
// missile warning, radio crackle and optional ambient music. Independent buses for
// master / music / engine / radio / effects. Background music is OFF by default.
//
// The stream is opened in shared mode through raylib/miniaudio, so music from other apps
// keeps playing on macOS. (iOS would additionally need an AVAudioSession "mix with others"
// category — see docs/ROADMAP.md.)
#pragma once

#include "../app/Settings.hpp"

#include <atomic>
#include <mutex>
#include <vector>

namespace sky {

enum class Sfx { Explosion, BigExplosion, MissileLaunch, Flares, Hit, Click, Radio, Gear, Impact, Flyby, Rush };

class Audio {
public:
    bool init();
    void shutdown();
    bool ready() const { return ready_; }
    void applySettings(const Settings& s);
    void setEngine(float rpm, float ab, float airspeed, bool cockpit, bool alive);
    void setGun(bool firing);
    void setLockState(int s);         // 0 none, 1 locking, 2 locked
    void setMissileWarning(bool on);
    void setStallWarning(bool on);
    /// Sense of speed: 0..1 low-altitude rush (wind roar near the ground).
    void setSpeedFeel(float lowAlt) { lowAlt_ = lowAlt; }
    void play(Sfx s, float gain = 1.0f, float pan = 0.0f);
    void setPaused(bool p) { paused_ = p; }

    // called from the audio thread
    void render(float* out, unsigned frames);

private:
    struct Voice { Sfx type; float t = 0, dur = 1, gain = 1, pan = 0; float lp = 0, lp2 = 0; unsigned seed = 1; };
    float noise();

    bool ready_ = false;
    void* stream_ = nullptr;
    std::mutex mtx_;
    std::vector<Voice> voices_;
    std::atomic<float> rpm_{0}, ab_{0}, speed_{0}, lowAlt_{0};
    float smoothLow_ = 0, lpRoar_ = 0;
    std::atomic<bool> cockpit_{true}, alive_{true}, gun_{false}, warn_{false}, stall_{false}, paused_{false};
    std::atomic<int> lock_{0};
    std::atomic<float> master_{0.8f}, music_{0.5f}, engine_{0.85f}, radio_{0.65f}, effects_{0.9f};
    std::atomic<bool> musicOn_{false};
    // synth state (audio thread only)
    unsigned rng_ = 22222;
    double t_ = 0;
    float lpRumble_ = 0, lpAb_ = 0, lpWind_ = 0, hpWindPrev_ = 0, hpWind_ = 0, lpGun_ = 0;
    double whinePhase_ = 0, whine2Phase_ = 0, lockPhase_ = 0, gunTimer_ = 0, gunEnv_ = 0;
    double musicPhase_[6] = {0, 0, 0, 0, 0, 0};
    float smoothRpm_ = 0, smoothAb_ = 0, smoothSpeed_ = 0, smoothCockpit_ = 1;
};

}  // namespace sky
