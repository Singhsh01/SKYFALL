#include "Voice.hpp"

#include "../sim/Locations.hpp"
#include "Campaign.hpp"
#include "Mission.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <set>

namespace sky {

const char* speakerName(Speaker s) {
    switch (s) {
        case Speaker::Captain: return "NORTHSTAR";
        case Speaker::Awacs: return "AWACS";
        case Speaker::Tower: return "TOWER";
        case Speaker::Instructor: return "INSTRUCTOR";
        case Speaker::Wingman: return "WARDOG 2";
    }
    return "";
}

const char* speakerVoice(Speaker s) {
    switch (s) {
        case Speaker::Captain: return "bf_isabella";
        case Speaker::Awacs: return "bf_isabella";
        case Speaker::Tower: return "bf_emma";
        case Speaker::Instructor: return "bf_isabella";
        case Speaker::Wingman: return "af_bella";
    }
    return "bf_isabella";
}

bool speakerOnRadio(Speaker s) { return s != Speaker::Captain; }

std::string voiceKey(Speaker who, const std::string& text) {
    // FNV-1a 64 over "<voice>|<radio>|<text>" (identical in tools/make_voice.py)
    std::string s = std::string(speakerVoice(who)) + "|" + (speakerOnRadio(who) ? "1" : "0") + "|" + text;
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char b[24];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(h));
    return b;
}

std::string spokenCaps(const std::string& caps) {
    std::string out, word;
    auto flush = [&]() {
        if (word.empty()) return;
        static const char* keep[] = {"SAM", "ATS", "ATA", "SEAD", "AAA", "CAP", "HVT"};
        bool k = false;
        for (const char* w : keep) if (word == w) k = true;
        if (!k) for (char& ch : word) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        out += word;
        word.clear();
    };
    for (char ch : caps) {
        if (std::isalnum(static_cast<unsigned char>(ch))) word += ch;
        else { flush(); out += ch; }
    }
    flush();
    if (!out.empty()) out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

Briefing makeBriefing(int missionNo) {
    Briefing b;
    std::string story, tip;
    if (missionNo == 0) {
        b.header = "DOGFIGHT ARENA";
        b.title = "Hold the line";
        story = "Waves of enemy fighters are inbound from the east, and the town below is ringed with flak guns and missile sites.";
        tip = "Splash every bandit you can. Use flares the moment you hear the missile warning, and stay with your wingmen.";
        b.objectives = {"DESTROY INCOMING BANDIT WAVES", "SUPPRESS AAA AND SAM SITES (OPTIONAL)"};
    } else {
        char h[48];
        std::snprintf(h, sizeof h, "MISSION %02d", missionNo);
        b.header = h;
        b.title = campaignCatalogue()[missionNo - 1].title;
        const CampaignMissionDef* def = missionNo >= 4 ? campaignMission(missionNo) : nullptr;
        if (def) story = def->briefing;
        else if (missionNo == 1) story = "Today is your cold start and takeoff from runway zero nine. Start the engine, release the brakes, and climb out to three hundred metres heading east.";
        else if (missionNo == 2) story = "Your first solo flight. Show me clean banks, a turn onto heading two seven zero, a climb to two thousand metres and a steady level-off.";
        else story = "You're five kilometres out on final for runway zero nine. Gear down, fly the three degree glide path, touch down and bring it to a stop.";
        Mission m = def ? buildCampaignMission(*def) : missionNo == 1 ? makeMission01() : missionNo == 2 ? makeMission02() : makeMission03();
        for (const auto& st : m.steps()) b.objectives.push_back(st.title);
        if (def) {
            bool ground = false, airObj = false;
            for (const auto& o : def->objectives) {
                if (o.kind == ObjectiveKind::DestroyGroup) ground = true;
                if (o.kind == ObjectiveKind::DestroyAir) airObj = true;
            }
            if (def->carrier) tip = "Hold the brakes, run the engine up to full afterburner, and the catapult will throw you off the deck. Climb straight ahead before you turn.";
            else if (ground) tip = "For ground targets, select ATS with key two, keep the target near your nose until the lock tone, then fire. The cannon will also walk onto targets close to the nose.";
            else if (airObj) tip = "Select ATA with key one, lock, and fire. Flares on X when they shoot back.";
        }
    }
    b.display = story + (tip.empty() ? "" : " " + tip) + " Good luck, Captain.";
    std::string say = "Captain, this is Captain Avery Langlois, Wing Operations. ";
    say += missionNo == 0 ? std::string("Dogfight Arena. ") : (std::string("Mission ") + std::to_string(missionNo) + ", " + b.title + ". ");
    say += story + " ";
    if (!b.objectives.empty()) {
        say += "Your objectives: ";
        for (size_t i = 0; i < b.objectives.size(); ++i) {
            say += spokenCaps(b.objectives[i]);
            say += i + 1 < b.objectives.size() ? "; " : ". ";
        }
    }
    if (!tip.empty()) say += tip + " ";
    say += "Good luck, Captain.";
    b.speech = say;
    return b;
}

namespace lines {
const char* const kTowerStartup = "Skyfall One, runway zero nine, wind calm. Start-up approved.";
const char* const kInstructorStart = "Your aircraft. Let's see some clean flying, Skyfall One.";
const char* const kTowerLanding = "Skyfall One, cleared to land runway zero nine, wind calm.";
const char* const kArenaStart = "Skyfall One, multiple bandits bearing zero nine zero. Weapons free.";
const char* const kRespawn = "Skyfall One, you're back in the fight.";
const char* const kTowerEngine = "Engine start confirmed. Release brakes when ready.";
const char* const kTowerTakeoff = "Skyfall One, cleared for take-off runway zero nine.";
const char* const kTowerClimb = "Positive climb. Gear up, contact AWACS.";
const char* const kTowerDone1 = "Good climb-out, Skyfall One. Press enter for your first solo flight.";
const char* const kInstructorDone2 = "Nicely flown. Press enter for the landing lesson.";
const char* const kTowerDone3 = "Skyfall One, welcome home. Press enter for navigation training.";
const char* const kSamLaunch = "SAM launch! Skyfall One, defend!";
const char* const kMissileSix = "Skyfall, missile on your six! Flares!";
const char* const kKills[4] = {"Splash one! Good kill.", "Bandit down!", "Nice shooting, Skyfall.", "Target destroyed."};
const char* const kLost = "Skyfall One is down. Mission failed.";
const char* const kCarrierCat = "Skyfall One, you're hooked up on catapult one. Full afterburner, then release the brakes.";
const char* const kCarrierAirborne = "Good shot, Skyfall One. Climb straight ahead, gear up.";
}  // namespace lines

std::string nextObjectiveLine(const std::string& stepTitle) { return "Good work. Next: " + spokenCaps(stepTitle) + "."; }

std::string freeFlightLine(const std::string& locationName) {
    return "Skyfall One, free flight over " + locationName + ". Enjoy the view.";
}

std::vector<VoiceLine> allVoiceLines() {
    std::vector<VoiceLine> v;
    std::set<std::string> seen;
    auto add = [&](Speaker s, const std::string& t) {
        if (t.empty()) return;
        if (seen.insert(voiceKey(s, t)).second) v.push_back({s, t});
    };
    add(Speaker::Captain, makeBriefing(0).speech);
    for (int n = 1; n <= kCampaignMissions; ++n) {
        add(Speaker::Captain, makeBriefing(n).speech);
        const CampaignMissionDef* def = n >= 4 ? campaignMission(n) : nullptr;
        if (def) add(Speaker::Awacs, def->debrief);
        Mission m = def ? buildCampaignMission(*def) : n == 1 ? makeMission01() : n == 2 ? makeMission02() : makeMission03();
        if (def)
            for (size_t i = 1; i < m.steps().size(); ++i)
                if (!(def->carrier && m.steps()[i].title == "GEAR UP")) add(Speaker::Awacs, nextObjectiveLine(m.steps()[i].title));
    }
    using namespace lines;
    for (const char* t : {kTowerStartup, kTowerLanding, kTowerEngine, kTowerTakeoff, kTowerClimb, kTowerDone1, kTowerDone3}) add(Speaker::Tower, t);
    for (const char* t : {kInstructorStart, kInstructorDone2}) add(Speaker::Instructor, t);
    for (const char* t : {kArenaStart, kRespawn, kSamLaunch, kLost}) add(Speaker::Awacs, t);
    for (const char* t : {kCarrierCat, kCarrierAirborne}) add(Speaker::Tower, t);
    add(Speaker::Wingman, kMissileSix);
    for (const char* t : kKills) add(Speaker::Wingman, t);
    for (const auto& l : locations()) add(Speaker::Tower, freeFlightLine(l.name));
    return v;
}

}  // namespace sky
