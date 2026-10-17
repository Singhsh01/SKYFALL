// Spoken voice for the whole game: the pre-mission briefings and every radio call.
//
// Each line is a recording made with a neural female voice (assets/voice/<key>.mp3, see
// mission/Voice.hpp and tools/make_voice.py), played through the game's audio device (native)
// or an HTML audio element (browser). A line without a recording falls back to the platform's
// text-to-speech, preferring a high-quality female English voice:
//   macOS   - `say` with Ava / Zoe / Serena / Samantha (Premium or Enhanced variants first)
//   browser - speechSynthesis, female English voice
//   Linux   - espeak-ng female variant
// Radio calls queue behind each other; a briefing interrupts everything.
#pragma once

#include "../mission/Voice.hpp"

#include <deque>
#include <string>

namespace sky {

class Speech {
public:
    Speech();
    ~Speech();
    /// Where the recordings live (native: a folder; browser: a URL prefix).
    void setVoiceDir(const std::string& dir) { dir_ = dir; }
    /// Speak a line. interrupt = stop whatever is playing and clear the queue first.
    void say(Speaker who, const std::string& text, bool interrupt = false);
    /// Call once per frame: advances the queue.
    void update();
    void stop();
    bool speaking();
    bool available() const { return true; }
    bool hasRecording(Speaker who, const std::string& text) const;
    void setEnabled(bool on) { enabled_ = on; if (!on) stop(); }
    void setVolume(float v) { volume_ = v; }
    const std::string& systemVoice() { detectVoice(); return voice_; }

private:
    struct Item { Speaker who; std::string text; };
    void start(const Item& it);
    bool playRecording(const Item& it);
    void systemSpeak(const std::string& text);
    bool systemSpeaking();
    void stopCurrent();
    void detectVoice();

    std::deque<Item> queue_;
    std::string dir_ = "assets/voice";
    bool enabled_ = true;
    float volume_ = 1.0f;
    bool detected_ = false;
    bool ttsAvailable_ = false;
    std::string voice_;
    std::string engine_;
    long pid_ = -1;
    void* sound_ = nullptr;   // raylib Sound (native)
    bool active_ = false;     // something is (or was just) playing
};

}  // namespace sky
