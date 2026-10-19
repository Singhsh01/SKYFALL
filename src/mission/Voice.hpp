// Everything the game says out loud. Every spoken line is known in advance, so each one is
// recorded once with a neural female voice (tools/make_voice.py, Kokoro-82M) and shipped as
// assets/voice/<key>.mp3, where <key> is a hash of the speaker and the exact text. At run time
// the game plays that recording; a line with no recording falls back to the system's
// text-to-speech (female voice preferred).
#pragma once

#include <string>
#include <vector>

namespace sky {

/// Who is talking. Every speaker is a woman, heard over the radio (radio-band filter).
/// Captain = the operations officer who briefs the pilot ("Captain") from the ground.
enum class Speaker { Captain = 0, Awacs = 1, Tower = 2, Instructor = 3, Wingman = 4 };

struct VoiceLine {
    Speaker who;
    std::string text;
};

const char* speakerName(Speaker s);      // shown in the HUD radio box
const char* speakerVoice(Speaker s);     // recording voice id (Kokoro)
bool speakerOnRadio(Speaker s);          // radio-band filtered recording
/// File name stem of the recording for a line (16 hex digits).
std::string voiceKey(Speaker who, const std::string& text);

/// Objective titles are written in capitals for the HUD; this reads them in sentence case.
std::string spokenCaps(const std::string& caps);

/// The pre-mission briefing for a campaign mission (1..N) or the Dogfight Arena (0).
struct Briefing {
    std::string header, title;
    std::string display;                  // typed-out text on screen
    std::string speech;                   // the whole briefing as one text
    std::vector<std::string> parts;       // the radio transmissions it is spoken in
    std::vector<std::string> objectives;  // HUD titles
};
struct CampaignVariant;
/// `v`: the world location's variant of the campaign (nullptr = the Skyfall Desert original).
Briefing makeBriefing(int missionNo, const CampaignVariant* v = nullptr);

/// Fixed radio calls.
namespace lines {
extern const char* const kTowerStartup;
extern const char* const kInstructorStart;
extern const char* const kTowerLanding;
extern const char* const kTowerLanding27;
extern const char* const kArenaStart;
extern const char* const kRespawn;
extern const char* const kTowerEngine;
extern const char* const kTowerTakeoff;
extern const char* const kTowerClimb;
extern const char* const kTowerDone1;
extern const char* const kInstructorDone2;
extern const char* const kTowerDone3;
extern const char* const kSamLaunch;
extern const char* const kMissileSix;
extern const char* const kKills[4];
extern const char* const kLost;
extern const char* const kCarrierCat;
extern const char* const kCarrierAirborne;
}  // namespace lines

/// "Next: <objective>" call when a campaign objective completes.
std::string nextObjectiveLine(const std::string& stepTitle);
/// Free-flight greeting for a location name.
std::string freeFlightLine(const std::string& locationName);

/// Every line the game can say (used to record the voice pack and by the tests).
std::vector<VoiceLine> allVoiceLines();

}  // namespace sky
