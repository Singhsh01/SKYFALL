#include "Speech.hpp"

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
EM_JS(void, sky_web_speak, (const char* text), {
    if (!('speechSynthesis' in window)) return;
    var t = UTF8ToString(text);
    var synth = window.speechSynthesis;
    synth.cancel();
    var pick = function () {
        var vs = synth.getVoices() || [];
        var female = /(female|samantha|allison|ava|susan|zoe|clara|linda|heather|jenny|aria|emma|karen|catherine|moira|tessa|google us english|google uk english female)/i;
        var ca = vs.filter(function (v) { return /en[-_]CA/i.test(v.lang); });
        var caF = ca.filter(function (v) { return female.test(v.name); });
        if (caF.length) return caF[0];
        if (ca.length) return ca[0];
        var us = vs.filter(function (v) { return /en[-_]US/i.test(v.lang) && female.test(v.name); });
        if (us.length) return us[0];
        var en = vs.filter(function (v) { return /^en/i.test(v.lang) && female.test(v.name); });
        if (en.length) return en[0];
        var any = vs.filter(function (v) { return /^en/i.test(v.lang); });
        return any.length ? any[0] : null;
    };
    var u = new SpeechSynthesisUtterance(t);
    var v = pick();
    if (v) { u.voice = v; u.lang = v.lang; } else { u.lang = 'en-CA'; }
    u.rate = 1.02; u.pitch = 1.08;
    synth.speak(u);
});
EM_JS(void, sky_web_stop, (), { if ('speechSynthesis' in window) window.speechSynthesis.cancel(); });
EM_JS(int, sky_web_speaking, (), { return ('speechSynthesis' in window) && window.speechSynthesis.speaking ? 1 : 0; });
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

void Speech::detectVoice() {
    if (detected_) return;
    detected_ = true;
#if defined(__EMSCRIPTEN__)
    engine_ = "web";
    voice_ = "browser voice (en-CA preferred)";
    available_ = true;
#elif defined(__APPLE__)
    engine_ = "say";
    available_ = access("/usr/bin/say", X_OK) == 0;
    if (!available_) return;
    // Lines look like "Samantha (Enhanced)   en_US    # Hello! My name is Samantha."
    std::vector<std::pair<std::string, std::string>> voices;   // name, locale
    if (FILE* p = popen("/usr/bin/say -v '?' 2>/dev/null", "r")) {
        char line[512];
        while (std::fgets(line, sizeof line, p)) {
            std::string head = trim(std::string(line).substr(0, std::string(line).find('#')));
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
    // 1) any Canadian English voice
    for (auto& v : voices) if (v.second == "en_CA") { voice_ = v.first; return; }
    // 2) a North-American female voice (best quality variant first)
    const char* female[] = {"Samantha", "Allison", "Ava", "Susan", "Zoe", "Nicky", "Joelle", "Noelle", "Kathy", "Vicki", "Victoria"};
    for (const char* f : female)
        for (const char* q : {" (Premium)", " (Enhanced)", ""})
            if (has(std::string(f) + q)) { voice_ = std::string(f) + q; return; }
    voice_.clear();   // system default
#else
    if (access("/usr/bin/espeak-ng", X_OK) == 0) { engine_ = "/usr/bin/espeak-ng"; available_ = true; voice_ = "en-us+f3"; }
    else if (access("/usr/bin/espeak", X_OK) == 0) { engine_ = "/usr/bin/espeak"; available_ = true; voice_ = "en-us+f3"; }
#endif
}

void Speech::speak(const std::string& text) {
    if (!enabled_) return;
    detectVoice();
    if (!available_) return;
    stop();
#if defined(__EMSCRIPTEN__)
    sky_web_speak(text.c_str());
#elif defined(__APPLE__) || defined(__unix__)
    std::vector<std::string> args;
#if defined(__APPLE__)
    args = {"say"};
    if (!voice_.empty()) { args.push_back("-v"); args.push_back(voice_); }
    args.push_back("-r"); args.push_back("182");
    args.push_back(text);
    const char* exe = "/usr/bin/say";
#else
    args = {engine_, "-v", voice_, "-s", "165", text};
    const char* exe = engine_.c_str();
#endif
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    if (posix_spawn(&pid, exe, &fa, nullptr, argv.data(), environ) == 0) pid_ = pid;
    posix_spawn_file_actions_destroy(&fa);
#endif
}

void Speech::stop() {
#if defined(__EMSCRIPTEN__)
    if (detected_) sky_web_stop();
#elif defined(__APPLE__) || defined(__unix__)
    if (pid_ > 0) {
        kill(static_cast<pid_t>(pid_), SIGTERM);
        int st = 0;
        waitpid(static_cast<pid_t>(pid_), &st, 0);
        pid_ = -1;
    }
#endif
}

bool Speech::speaking() {
#if defined(__EMSCRIPTEN__)
    return detected_ && sky_web_speaking() != 0;
#elif defined(__APPLE__) || defined(__unix__)
    if (pid_ <= 0) return false;
    int st = 0;
    if (waitpid(static_cast<pid_t>(pid_), &st, WNOHANG) == static_cast<pid_t>(pid_)) { pid_ = -1; return false; }
    return true;
#else
    return false;
#endif
}

}  // namespace sky
