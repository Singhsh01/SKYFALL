#include "Speech.hpp"

#include "raylib.h"

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
// One shared <audio> element; when the recording is missing, speechSynthesis reads the text.
EM_JS(void, sky_voice_play, (const char* url, const char* text, float volume), {
    var u = UTF8ToString(url), t = UTF8ToString(text);
    var st = window.__skyVoice || (window.__skyVoice = { audio: null, tts: false });
    var tts = function () {
        if (!('speechSynthesis' in window)) return;
        var synth = window.speechSynthesis;
        var vs = synth.getVoices() || [];
        var female = /(female|samantha|allison|ava|susan|zoe|serena|kate|karen|moira|tessa|fiona|clara|linda|heather|jenny|aria|emma|libby|sonia|natasha|catherine|google us english|google uk english female)/i;
        var pick = vs.filter(function (v) { return /^en/i.test(v.lang) && female.test(v.name) && /(natural|neural|premium|enhanced|online)/i.test(v.name); })[0]
            || vs.filter(function (v) { return /^en/i.test(v.lang) && female.test(v.name); })[0]
            || vs.filter(function (v) { return /^en/i.test(v.lang); })[0];
        var m = new SpeechSynthesisUtterance(t);
        if (pick) { m.voice = pick; m.lang = pick.lang; } else { m.lang = 'en-US'; }
        m.rate = 1.0; m.pitch = 1.05; m.volume = volume;
        st.tts = true;
        m.onend = m.onerror = function () { st.tts = false; };
        synth.speak(m);
    };
    if (st.audio) { try { st.audio.pause(); } catch (e) {} }
    var a = new Audio(u);
    a.volume = Math.max(0, Math.min(1, volume));
    st.audio = a;
    a.onerror = function () { if (st.audio === a) { st.audio = null; tts(); } };
    a.onended = function () { if (st.audio === a) st.audio = null; };
    var p = a.play();
    if (p && p.catch) p.catch(function () { if (st.audio === a) { st.audio = null; tts(); } });
});
EM_JS(void, sky_voice_stop, (), {
    var st = window.__skyVoice;
    if (!st) return;
    if (st.audio) { try { st.audio.pause(); } catch (e) {} st.audio = null; }
    if ('speechSynthesis' in window) window.speechSynthesis.cancel();
    st.tts = false;
});
EM_JS(int, sky_voice_busy, (), {
    var st = window.__skyVoice;
    if (!st) return 0;
    return (st.audio && !st.audio.ended) || st.tts || (('speechSynthesis' in window) && window.speechSynthesis.speaking) ? 1 : 0;
});
#elif defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace sky {

namespace {
[[maybe_unused]] std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

Speech::Speech() = default;

Speech::~Speech() { stop(); }

bool Speech::hasRecording(Speaker who, const std::string& text) const {
#if defined(__EMSCRIPTEN__)
    (void)who; (void)text;
    return true;
#else
    return FileExists((dir_ + "/" + voiceKey(who, text) + ".mp3").c_str());
#endif
}

void Speech::say(Speaker who, const std::string& text, bool interrupt) {
    if (!enabled_ || text.empty()) return;
    if (interrupt) { queue_.clear(); stopCurrent(); }
    if (queue_.size() >= 3) queue_.pop_front();   // radio chatter: never fall far behind the action
    queue_.push_back({who, text});
    update();
}

void Speech::update() {
    if (speaking()) return;
    if (active_) stopCurrent();
    if (queue_.empty() || !enabled_) return;
    Item it = std::move(queue_.front());
    queue_.pop_front();
    start(it);
}

void Speech::start(const Item& it) {
    active_ = true;
#if defined(__EMSCRIPTEN__)
    std::string url = dir_ + "/" + voiceKey(it.who, it.text) + ".mp3";
    sky_voice_play(url.c_str(), it.text.c_str(), volume_);
#else
    if (!playRecording(it)) systemSpeak(it.text);
#endif
}

bool Speech::playRecording(const Item& it) {
#if defined(__EMSCRIPTEN__)
    (void)it;
    return false;
#else
    if (!IsAudioDeviceReady()) return false;
    std::string path = dir_ + "/" + voiceKey(it.who, it.text) + ".mp3";
    if (!FileExists(path.c_str())) return false;
    Sound s = LoadSound(path.c_str());
    if (s.frameCount == 0) return false;
    SetSoundVolume(s, volume_);
    PlaySound(s);
    TraceLog(LOG_INFO, "SKYFALL: voice %s", path.c_str());
    sound_ = new Sound(s);
    return true;
#endif
}

void Speech::stopCurrent() {
#if defined(__EMSCRIPTEN__)
    sky_voice_stop();
#else
    if (sound_) {
        Sound* s = static_cast<Sound*>(sound_);
        StopSound(*s);
        UnloadSound(*s);
        delete s;
        sound_ = nullptr;
    }
#if defined(__APPLE__) || defined(__unix__)
    if (pid_ > 0) {
        kill(static_cast<pid_t>(pid_), SIGTERM);
        int st = 0;
        waitpid(static_cast<pid_t>(pid_), &st, 0);
        pid_ = -1;
    }
#endif
#endif
    active_ = false;
}

void Speech::stop() {
    queue_.clear();
    stopCurrent();
}

bool Speech::speaking() {
#if defined(__EMSCRIPTEN__)
    return active_ && sky_voice_busy() != 0;
#else
    if (sound_ && IsSoundPlaying(*static_cast<Sound*>(sound_))) return true;
    return systemSpeaking();
#endif
}

// ------------------------------------------------------------------------------------------
// System text-to-speech fallback
// ------------------------------------------------------------------------------------------
void Speech::detectVoice() {
    if (detected_) return;
    detected_ = true;
#if defined(__EMSCRIPTEN__)
    ttsAvailable_ = true;
    voice_ = "browser voice";
#elif defined(__APPLE__)
    engine_ = "/usr/bin/say";
    ttsAvailable_ = access("/usr/bin/say", X_OK) == 0;
    if (!ttsAvailable_) return;
    // Lines look like "Samantha (Enhanced)   en_US    # Hello! My name is Samantha."
    std::vector<std::pair<std::string, std::string>> voices;   // name, locale
    if (FILE* p = popen("/usr/bin/say -v '?' 2>/dev/null </dev/null", "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, p)) {
            std::string l(line);
            std::string head = trim(l.substr(0, l.find('#')));
            size_t sp = head.find_last_of(" \t");
            if (sp == std::string::npos) continue;
            std::string loc = head.substr(sp + 1), name = trim(head.substr(0, sp));
            if (loc.size() >= 5 && loc[2] == '_') voices.push_back({name, loc});
        }
        pclose(p);
    }
    auto has = [&](const std::string& n) {
        for (auto& v : voices) if (v.first == n) return true;
        return false;
    };
    // natural-sounding female English voices, best quality variant first
    const char* female[] = {"Ava", "Zoe", "Serena", "Allison", "Samantha", "Susan", "Kate", "Karen", "Moira", "Tessa", "Fiona", "Nicky", "Victoria"};
    for (const char* q : {" (Premium)", " (Enhanced)"})
        for (const char* f : female)
            if (has(std::string(f) + q)) { voice_ = std::string(f) + q; return; }
    for (const char* f : female)
        if (has(f)) { voice_ = f; return; }
    voice_ = "Samantha";
#else
    if (access("/usr/bin/espeak-ng", X_OK) == 0) { engine_ = "/usr/bin/espeak-ng"; ttsAvailable_ = true; voice_ = "en-us+f3"; }
    else if (access("/usr/bin/espeak", X_OK) == 0) { engine_ = "/usr/bin/espeak"; ttsAvailable_ = true; voice_ = "en-us+f3"; }
#endif
}

void Speech::systemSpeak(const std::string& text) {
    detectVoice();
    if (!ttsAvailable_) return;
#if defined(__APPLE__) || (defined(__unix__) && !defined(__EMSCRIPTEN__))
    std::vector<std::string> args;
#if defined(__APPLE__)
    args = {"say", "-v", voice_, "-r", "178", text};
#else
    args = {engine_, "-v", voice_, "-s", "165", text};
#endif
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    // own process group: the voice process never takes part in the terminal's job control
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&at, 0);
    if (posix_spawn(&pid, engine_.c_str(), &fa, &at, argv.data(), environ) == 0) pid_ = pid;
    posix_spawnattr_destroy(&at);
    TraceLog(LOG_INFO, "SKYFALL: system voice (%s) for: %.60s", voice_.c_str(), text.c_str());
    posix_spawn_file_actions_destroy(&fa);
#else
    (void)text;
#endif
}

bool Speech::systemSpeaking() {
#if (defined(__APPLE__) || defined(__unix__)) && !defined(__EMSCRIPTEN__)
    if (pid_ <= 0) return false;
    int st = 0;
    if (waitpid(static_cast<pid_t>(pid_), &st, WNOHANG) == static_cast<pid_t>(pid_)) { pid_ = -1; return false; }
    return true;
#else
    return false;
#endif
}

}  // namespace sky
