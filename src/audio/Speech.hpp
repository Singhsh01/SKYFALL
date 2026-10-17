// Spoken mission briefings using the platform's own text-to-speech (no voice files shipped):
//   macOS   - the built-in `say` command; prefers an English (Canada) voice, then a North-American
//             female voice (Samantha, Allison, Ava, ...).
//   browser - the Web Speech API (speechSynthesis), preferring en-CA, then a female English voice.
//   Linux   - espeak-ng / espeak if installed (female variant), otherwise captions only.
#pragma once

#include <string>

namespace sky {

class Speech {
public:
    Speech();
    ~Speech();
    void speak(const std::string& text);
    void stop();
    bool speaking();
    bool available() const { return available_; }
    const std::string& voiceName() const { return voice_; }
    void setEnabled(bool on) { enabled_ = on; if (!on) stop(); }

private:
    void detectVoice();
    bool available_ = false;
    bool enabled_ = true;
    bool detected_ = false;
    std::string voice_;
    std::string engine_;   // "say", "espeak-ng", "espeak", "web"
    long pid_ = -1;
};

}  // namespace sky
