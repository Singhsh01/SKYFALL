// Prints every line the game can speak as JSON lines: {"key","voice","radio","speaker","text"}.
// Used by tools/make_voice.py to record the voice pack (assets/voice).
#include "../src/mission/Voice.hpp"

#include <cstdio>
#include <string>

namespace {
std::string json(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}
}  // namespace

int main() {
    using namespace sky;
    for (const VoiceLine& l : allVoiceLines())
        std::printf("{\"key\":\"%s\",\"voice\":\"%s\",\"radio\":%d,\"speaker\":\"%s\",\"text\":%s}\n", voiceKey(l.who, l.text).c_str(),
                    speakerVoice(l.who), speakerOnRadio(l.who) ? 1 : 0, speakerName(l.who), json(l.text).c_str());
    return 0;
}
