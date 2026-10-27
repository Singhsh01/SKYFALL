#include "Audio.hpp"

#include "raylib.h"

#include <algorithm>
#include <cmath>

namespace sky {

namespace {
Audio* g_audio = nullptr;
constexpr int kRate = 44100;
constexpr double kTau = 6.283185307179586;
void callback(void* buffer, unsigned int frames) {
    if (g_audio) g_audio->render(static_cast<float*>(buffer), frames);
}
inline float lp1(float& state, float x, float cutoffHz) {
    float a = 1.0f - std::exp(-static_cast<float>(kTau) * cutoffHz / kRate);
    state += a * (x - state);
    return state;
}
}  // namespace

bool Audio::init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TraceLog(LOG_WARNING, "SKYFALL: no audio device - continuing silently");
        return false;
    }
    SetAudioStreamBufferSizeDefault(1024);
    auto* s = new AudioStream(LoadAudioStream(kRate, 32, 2));
    if (!IsAudioStreamValid(*s)) {
        delete s;
        return false;
    }
    stream_ = s;
    g_audio = this;
    SetAudioStreamCallback(*s, callback);
    PlayAudioStream(*s);
    ready_ = true;
    return true;
}

void Audio::shutdown() {
    if (stream_) {
        auto* s = static_cast<AudioStream*>(stream_);
        StopAudioStream(*s);
        UnloadAudioStream(*s);
        delete s;
        stream_ = nullptr;
    }
    g_audio = nullptr;
    if (IsAudioDeviceReady()) CloseAudioDevice();
    ready_ = false;
}

void Audio::applySettings(const Settings& s) {
    master_ = static_cast<float>(s.master);
    music_ = static_cast<float>(s.music);
    engine_ = static_cast<float>(s.engine);
    radio_ = static_cast<float>(s.radio);
    effects_ = static_cast<float>(s.effects);
    musicOn_ = s.musicEnabled;
}

void Audio::setEngine(float rpm, float ab, float airspeed, bool cockpit, bool alive) {
    rpm_ = rpm; ab_ = ab; speed_ = airspeed; cockpit_ = cockpit; alive_ = alive;
}
void Audio::setGun(bool f) { gun_ = f; }
void Audio::setLockState(int s) { lock_ = s; }
void Audio::setMissileWarning(bool on) { warn_ = on; }
void Audio::setStallWarning(bool on) { stall_ = on; }

void Audio::setNearbyJet(float distance, float closing, float pan, float throttle) {
    jetDist_ = distance; jetClosing_ = closing; jetPan_ = std::clamp(pan, -1.0f, 1.0f); jetThrottle_ = throttle;
}
void Audio::setNearbyMissile(float distance, float closing, float pan) {
    mslDist_ = distance; mslClosing_ = closing; mslPan_ = std::clamp(pan, -1.0f, 1.0f);
}

void Audio::play(Sfx s, float gain, float pan, float delay, float distance) {
    if (!ready_) return;
    Voice v;
    v.type = s;
    v.gain = gain;
    v.pan = std::clamp(pan, -1.0f, 1.0f);
    v.delay = std::clamp(delay, 0.0f, 6.0f);
    v.muffle = std::clamp(distance / 6000.0f, 0.0f, 0.9f);
    switch (s) {
        case Sfx::Explosion: v.dur = 3.0f; break;
        case Sfx::BigExplosion: v.dur = 5.0f; break;
        case Sfx::MissileLaunch: v.dur = 1.8f; break;
        case Sfx::Flares: v.dur = 1.2f; break;
        case Sfx::Hit: v.dur = 0.25f; break;
        case Sfx::Click: v.dur = 0.06f; break;
        case Sfx::Radio: v.dur = 0.35f; break;
        case Sfx::Gear: v.dur = 1.2f; break;
        case Sfx::Impact: v.dur = 0.15f; break;
        case Sfx::Flyby: v.dur = 1.7f; break;
        case Sfx::Rush: v.dur = 1.1f; break;
    }
    v.seed = static_cast<unsigned>(voices_.size() * 7919 + 17);
    std::lock_guard<std::mutex> lk(mtx_);
    if (voices_.size() < 48) voices_.push_back(v);
}

float Audio::noise() {
    rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFF) / 32767.5f - 1.0f;
}

void Audio::render(float* out, unsigned frames) {
    const float dt = 1.0f / kRate;
    std::vector<Voice> local;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        local.swap(voices_);
    }
    float master = master_, eng = engine_ * master, fx = effects_ * master, mus = music_ * master, rad = radio_ * master;
    bool paused = paused_;
    for (unsigned i = 0; i < frames; ++i) {
        t_ += dt;
        // smooth parameters (avoid zipper noise)
        smoothRpm_ += (rpm_.load() - smoothRpm_) * 0.0005f;
        smoothAb_ += (ab_.load() - smoothAb_) * 0.0005f;
        smoothSpeed_ += (speed_.load() - smoothSpeed_) * 0.0005f;
        smoothCockpit_ += ((cockpit_.load() ? 1.0f : 0.0f) - smoothCockpit_) * 0.0005f;
        float L = 0, R = 0;
        if (!paused && alive_) {
            float rpm = smoothRpm_, ab = smoothAb_, spd = smoothSpeed_, ck = smoothCockpit_;
            float n = noise();
            float rumble = lp1(lpRumble_, n, 90.0f + 420.0f * rpm) * (rpm * rpm) * 2.2f;
            float abRoar = lp1(lpAb_, n, 700.0f - 350.0f * ck) * ab * 2.4f;
            whinePhase_ += kTau * (600.0 + 2400.0 * rpm) * dt;
            whine2Phase_ += kTau * (1210.0 + 4810.0 * rpm) * dt;
            float whine = (std::sin(whinePhase_) * 0.05f + std::sin(whine2Phase_) * 0.02f) * rpm * (0.4f + 0.6f * ck);
            float windIn = n;
            hpWind_ = 0.97f * (hpWind_ + windIn - hpWindPrev_);
            hpWindPrev_ = windIn;
            smoothLow_ += (lowAlt_.load() - smoothLow_) * 0.0003f;
            // wind keeps rising past the speed of sound (arcade top speeds reach Mach 2), and
            // low over the ground a deep roar of displaced air joins it
            float sp2 = std::min(1.6f, (spd / 320.0f) * (spd / 320.0f));
            float wind = lp1(lpWind_, hpWind_, 1800.0f - 900.0f * ck + 500.0f * std::min(1.0f, spd / 600.0f)) * sp2 * 0.75f;
            wind += lp1(lpRoar_, n, 220.0f) * smoothLow_ * std::min(1.0f, spd / 400.0f) * 1.6f * (1.0f - 0.4f * ck);
            // gusts: a slow swell in the wind (stronger in a storm)
            gustPhase_ += static_cast<float>(dt * (0.13 + 0.07 * std::sin(t_ * 0.05)));
            float gust = 1.0f + gust_.load() * 0.5f * static_cast<float>(std::sin(gustPhase_ * kTau) * std::sin(gustPhase_ * 2.7 * kTau + 1.3));
            wind *= gust;
            float engine = (rumble * (1.0f - 0.35f * ck) + abRoar + whine) * eng + wind * eng * 0.8f;
            L += engine; R += engine;
            // rain drumming on the canopy (inside) or hissing past (outside)
            float rain = rain_.load();
            if (rain > 0.01f) {
                hpRain_ = 0.9f * (hpRain_ + n - hpRainPrev_);
                hpRainPrev_ = n;
                float drops = lp1(lpRain_, hpRain_, 3500.0f + 2500.0f * (1.0f - ck)) * rain * (0.18f + 0.22f * ck);
                if (ck > 0.5f && (rng_ & 2047u) < static_cast<unsigned>(rain * 14.0f)) drops += noise() * 0.25f * rain;   // single drops on the glass
                L += drops * eng; R += drops * eng;
            }
            // the nearest other jet: engine roar, Doppler-shifted (c / (c - v)), louder as it closes
            float jd = jetDist_.load();
            float jetTarget = jd > 0 && jd < 2500.0f ? 1.0f / (1.0f + jd / 120.0f) : 0.0f;
            smoothJet_ += (jetTarget - smoothJet_) * 0.0008f;
            if (smoothJet_ > 0.002f) {
                float dop = std::clamp(343.0f / std::max(60.0f, 343.0f - jetClosing_.load()), 0.4f, 2.6f);
                float jn = noise();
                float roar = lp1(lpJet_, jn, (380.0f + 900.0f * jetThrottle_.load()) * dop) - lp1(lpJet2_, jn, 70.0f * dop);
                float js = roar * smoothJet_ * (1.2f + 1.4f * jetThrottle_.load()) * fx * (1.0f - 0.5f * ck);
                float jp = jetPan_.load();
                L += js * (1.0f - std::max(0.0f, jp)); R += js * (1.0f + std::min(0.0f, jp));
            }
            // a missile going past: a tearing rocket roar with the same Doppler drop
            float md = mslDist_.load();
            float mslTarget = md > 0 && md < 900.0f ? 1.0f / (1.0f + md / 60.0f) : 0.0f;
            smoothMsl_ += (mslTarget - smoothMsl_) * 0.002f;
            if (smoothMsl_ > 0.002f) {
                float dop = std::clamp(343.0f / std::max(60.0f, 343.0f - mslClosing_.load()), 0.4f, 2.6f);
                float ms = lp1(lpMsl_, noise(), 1600.0f * dop) * smoothMsl_ * 1.6f * fx;
                float mp = mslPan_.load();
                L += ms * (1.0f - std::max(0.0f, mp)); R += ms * (1.0f + std::min(0.0f, mp));
            }
            // cannon: 30 Hz bursts of filtered noise
            if (gun_) {
                gunTimer_ += dt;
                if (gunTimer_ > 1.0 / 30.0) { gunTimer_ = 0; gunEnv_ = 1.0; }
            }
            gunEnv_ *= 0.9985;
            float g = lp1(lpGun_, n, 1400.0f) * static_cast<float>(gunEnv_) * 2.2f * fx;
            L += g; R += g;
            // lock / warning tones
            int lock = lock_;
            if (lock > 0) {
                lockPhase_ += kTau * (lock == 2 ? 1250.0 : 900.0) * dt;
                bool on = lock == 2 ? true : std::fmod(t_, 0.25) < 0.08;
                float tone = on ? static_cast<float>(std::sin(lockPhase_)) * 0.07f * fx : 0.0f;
                L += tone; R += tone;
            }
            if (warn_) {
                double f = std::fmod(t_, 0.24) < 0.12 ? 1050.0 : 760.0;
                float tone = static_cast<float>(std::sin(kTau * f * t_)) * 0.09f * fx;
                L += tone; R += tone;
            }
            if (stall_) {
                float tone = std::fmod(t_, 0.5) < 0.25 ? static_cast<float>(std::sin(kTau * 420.0 * t_)) * 0.05f * fx : 0.0f;
                L += tone; R += tone;
            }
        }
        // one-shots
        for (Voice& v : local) {
            if (v.delay > 0) { v.delay -= dt; continue; }   // still travelling at the speed of sound
            if (v.t >= v.dur) continue;
            float tt = v.t, e = 0, s = 0;
            float n = noise();
            const float clear = 1.0f - v.muffle;   // distant sounds lose their highs
            switch (v.type) {
                case Sfx::Explosion:
                case Sfx::BigExplosion: {
                    const bool big = v.type == Sfx::BigExplosion;
                    // layered: the crack of the shock front, the boom of the fireball, a long
                    // low rumble rolling off the terrain, and debris crackling down afterwards
                    float crack = tt < 0.05f ? n * (1.0f - tt / 0.05f) * 1.6f * clear * clear : 0.0f;
                    e = std::exp(-tt * (big ? 1.6f : 2.6f)) * std::min(1.0f, tt * 200.0f);
                    float boom = lp1(v.lp, n, (160.0f + 900.0f * std::exp(-tt * 6.0f)) * (0.35f + 0.65f * clear)) * 3.2f * e;
                    boom += static_cast<float>(std::sin(kTau * (big ? 34.0 : 42.0) * tt)) * e * 0.5f;
                    float rumbleEnv = std::min(1.0f, tt * 6.0f) * std::exp(-tt * (big ? 0.7f : 1.1f));
                    float rumble = lp1(v.lp2, n, 55.0f) * rumbleEnv * (big ? 3.4f : 2.2f);
                    float crackle = 0;
                    if (tt > 0.25f && tt < (big ? 3.2f : 1.8f)) {
                        if ((static_cast<unsigned>(n * 1e6f) & 1023u) < (big ? 9u : 5u)) v.crackle = 1.0f;
                        v.crackle *= 0.995f;
                        crackle = lp1(v.lp3, n, 2600.0f * clear + 300.0f) * v.crackle * 0.55f * std::exp(-tt * 0.9f);
                    }
                    s = crack + boom + rumble + crackle;
                    break;
                }
                case Sfx::MissileLaunch:
                    e = std::exp(-tt * 1.4f) * std::min(1.0f, tt * 30.0f);
                    s = lp1(v.lp, n, 900.0f + 2600.0f * std::exp(-tt * 2.0f)) * 1.6f * e;
                    break;
                case Sfx::Flares:
                    e = std::fmod(tt, 0.15f) < 0.03f ? 1.0f : 0.0f;
                    s = lp1(v.lp, n, 2500.0f) * e * 0.8f;
                    break;
                case Sfx::Hit:
                    e = std::exp(-tt * 18.0f);
                    s = (n * 0.6f + static_cast<float>(std::sin(kTau * 1800.0 * tt)) * 0.4f) * e * 0.9f;
                    break;
                case Sfx::Impact:
                    e = std::exp(-tt * 30.0f);
                    s = lp1(v.lp, n, 1200.0f) * e * 0.6f;
                    break;
                case Sfx::Click:
                    e = 1.0f - tt / v.dur;
                    s = static_cast<float>(std::sin(kTau * 1800.0 * tt)) * e * 0.12f;
                    break;
                case Sfx::Radio: {
                    e = (tt < 0.05f || tt > v.dur - 0.08f) ? 1.0f : 0.25f;
                    float bp = lp1(v.lp, n, 3000.0f) - lp1(v.lp2, n, 900.0f);
                    s = bp * e * 0.6f * (rad / std::max(fx, 0.01f));
                    break;
                }
                case Sfx::Flyby: {
                    // a jet passing close: rising then falling rush, pitch dropping as it goes by
                    float pk = 0.45f;
                    e = tt < pk ? (tt / pk) * (tt / pk) : std::exp(-(tt - pk) * 3.2f);
                    s = lp1(v.lp, n, 2800.0f * std::exp(-tt * 1.4f) + 260.0f) * e * 1.5f;
                    s += lp1(v.lp2, n, 140.0f) * e * 0.9f;
                    break;
                }
                case Sfx::Rush:
                    e = std::sin(3.14159f * std::min(1.0f, tt / v.dur)) * std::exp(-tt * 1.5f);
                    s = lp1(v.lp, n, 900.0f + 2200.0f * std::exp(-tt * 2.5f)) * e * 1.1f;
                    break;
                case Sfx::Gear:
                    e = std::sin(3.14159f * tt / v.dur);
                    s = lp1(v.lp, n, 250.0f) * e * 0.9f;
                    break;
            }
            s *= v.gain * fx;
            L += s * (1.0f - std::max(0.0f, v.pan));
            R += s * (1.0f + std::min(0.0f, v.pan));
            v.t += dt;
        }
        // optional ambient music (simple evolving pad)
        if (musicOn_ && !paused) {
            static const double chords[4][3] = {{110.0, 164.81, 220.0}, {98.0, 146.83, 196.0}, {87.31, 130.81, 174.61}, {98.0, 155.56, 196.0}};
            int c = static_cast<int>(t_ / 8.0) % 4;
            float env = static_cast<float>(0.5 - 0.5 * std::cos(kTau * std::fmod(t_, 8.0) / 8.0));
            float m = 0;
            for (int k = 0; k < 3; ++k) {
                musicPhase_[k] += kTau * chords[c][k] * dt;
                musicPhase_[k + 3] += kTau * chords[c][k] * 1.004 * dt;
                m += static_cast<float>(std::sin(musicPhase_[k]) + std::sin(musicPhase_[k + 3]) * 0.7);
            }
            m *= 0.025f * (0.6f + 0.4f * env) * mus;
            L += m; R += m;
        }
        out[i * 2] = std::tanh(L);
        out[i * 2 + 1] = std::tanh(R);
    }
    // keep unfinished voices
    std::lock_guard<std::mutex> lk(mtx_);
    for (Voice& v : local)
        if (v.t < v.dur) voices_.push_back(v);
}

}  // namespace sky
