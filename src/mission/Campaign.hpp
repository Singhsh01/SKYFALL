// Campaign missions 04-40, described as data: where the player starts, which ground targets and
// enemy waves exist, and an ordered list of objectives. buildCampaignMission() turns a definition
// into the same telemetry-predicate Mission FSM used by the hand-written training missions.
#pragma once

#include "Mission.hpp"

#include <string>
#include <vector>

namespace sky {

struct TargetDef {
    GroundKind kind = GroundKind::Tank;
    double x = 0, z = 0;
    double heading = 0;
    std::string label;
    int group = 0;            // objective group (0 = defences, not required)
    bool armed = true;
    double pathRadius = 0;    // > 0: drives (or sails) a circle of this radius around (x, z)
    double pathAngle = 0;
    Team team = Team::Red;    // Team::Blue = friendly asset to protect
    // Anchoring to generated infrastructure (the layout differs per terrain):
    // City: x / z are offsets from the centre of the map's main city in units of its radius,
    // snapped onto the street grid (resolveCampaignAnchors turns them into world coordinates).
    // Dam: x / z are metres from the middle of the layout's hydro dam (x downstream, z along the
    // crest); without a dam they fall back to the city.
    enum class Anchor { None, Railway, Bridge, Industrial, City, Dam } anchor = Anchor::None;
    double flyAgl = 0;        // helicopters: > 0 flies the path circle at this height above the ground
    double railFraction = 0;  // Railway: position along the line (0..1); x/z ignored
    double railSpeed = 0;     // Railway: m/s (trains move; wagons follow the locomotive)
    int index = 0;            // Industrial: which factory slot
    int bridge = -1;          // Bridge: which bridge of the layout (-1 = the longest, rail first)
};

struct WaveDef {
    int count = 2;
    AiSkill skill = AiSkill::Veteran;
    double x = 0, z = 0;      // centre of the spawn ring
    double radius = 1500;
    double altitude = 2000;   // metres above sea level
    bool unarmed = false;     // target drones
    std::string callsign;     // name of the wave leader (empty = generated)
};

enum class ObjectiveKind {
    Waypoint,       // fly within `radius` of (x, z) (optionally below `maxAgl`)
    DestroyGroup,   // destroy every ground target in `group`
    DestroyAir,     // destroy every hostile aircraft (including waves spawned by this objective)
    PullG,          // hold at least `value` g for `hold` seconds
    Loop,           // pitch through a full loop
    AileronRoll,    // roll through 360 degrees
    Touchdown,      // land on the home runway
    Stop,           // stop on the runway
    Rendezvous,     // fly within `radius` of the tanker
    Refuel,         // take on fuel until the tank is at least `value` (0..1) full
    FullPower,      // on the deck: throttle into afterburner
    Launch,         // catapult shot: off the bow and climbing
    GearUp,         // airborne with the gear retracted
    Climb,          // reach `value` metres altitude
};

struct ObjectiveDef {
    ObjectiveKind kind = ObjectiveKind::Waypoint;
    std::string title;
    std::string instruction;
    std::string hint;
    double x = 0, z = 0, radius = 700, maxAgl = 0;
    int group = 0;
    double value = 0, hold = 0;
    std::vector<int> wavesOnStart;   // indices into CampaignMissionDef::waves spawned when this objective begins
    // reinforcements: waves that arrive if this objective is still running `seconds` after it began
    std::vector<std::pair<int, double>> wavesLate;
    bool cityAnchor = false;         // Waypoint: x / z are offsets from the main city (see TargetDef::Anchor::City)
};

struct CampaignMissionDef {
    int number = 0;
    std::string title;
    std::string summary;      // one line for the menu
    std::string briefing;     // radio call at the start
    std::string debrief;      // radio call on success
    int timeOfDay = 0;        // 0 morning, 1 golden hour, 2 noon
    double startX = 0, startZ = 0, startAlt = 1800, startSpeed = 230;
    double startHeading = 1e9;   // radians, 0 = east; 1e9 = towards the first objective
    int allies = 0;
    double atsReload = 10.0;     // seconds per ATS missile re-arm
    std::vector<TargetDef> targets;
    std::vector<WaveDef> waves;
    std::vector<ObjectiveDef> objectives;
    int protectGroup = 0;        // fail if any friendly target in this group is destroyed
    double startFuel = -1;       // fraction of internal fuel at the start (-1 = full)
    bool tanker = false;         // refuelling tanker on a racetrack
    double tankerX = 0, tankerZ = 0, tankerAlt = 3000, tankerRadius = 6500, tankerSpeed = 175;
    double timeLimit = 0;        // seconds, 0 = none
    bool carrier = false;        // start on the aircraft carrier's catapult
    bool rain = false;           // rain and low overcast
    int tactics = -1;            // enemy tactics level (see BattleConfig::tactics); -1 = by mission number
    DeckInfo deck;               // the aircraft carrier (carrier missions)
    double homeX = -10500, homeZ = 3000;   // the runway threshold the landing objectives point at
    std::string location = "desert";       // world location the definition is laid out for
    // v1.1 theatres: the Indian location the mission is written for (flown there when it is
    // downloaded, otherwise over the campaign location like the rest)
    std::string homeLocation;
    // protected civilian sites (GroundKind::Civilian, Team::Blue): every loss costs score; more
    // than `civilianLimit` losses fail the mission (-1: never fails, the penalty only)
    int civilianGroup = 0;
    int civilianLimit = 0;
    // outcome-dependent debriefs: a clean run (no civilian loss, little damage) and a costly one
    std::string debriefClean, debriefCostly;
};

/// How a campaign mission went, for the debrief card and the outcome-dependent radio call.
struct CampaignOutcome {
    bool success = false;
    double seconds = 0;
    int airKills = 0, groundKills = 0;
    int missilesFired = 0, missileHits = 0;
    int civiliansLost = 0, civiliansTotal = 0;
    int friendliesLost = 0;
    double damageTaken = 0;
    int score() const;            // 0..1000
    const char* grade() const;    // S A B C D
};
/// The debrief radio call for an outcome (clean / costly variants when the mission has them).
const std::string& campaignDebrief(const CampaignMissionDef& def, const CampaignOutcome& o);

/// What a world location offers the campaign: open sea (boats, oil rigs), room for the carrier,
/// a railway, and how much higher the airfield's training heights are (mission 2).
struct CampaignVariant {
    bool sea = true, carrier = true, rail = true;
    double climbOffset = 0;
    bool runway27 = false;   // mission 3 lands on runway 27 (terrain blocks the approach from the west)
};
/// The variant of a location (the Skyfall Desert is the original: everything, no offset).
CampaignVariant campaignVariantFor(const Terrain& t, const WorldLayout& L);
/// Height added to mission 2's altitudes on this location (0 in the desert).
double mission02Offset(const Terrain& t);
/// Titles, labels and target kinds of a mission as flown in a variant (no geometry): boats become
/// vehicles without sea, trains truck convoys without a railway, the catapult start an airborne
/// start without a carrier.
CampaignMissionDef retitleCampaignMission(const CampaignMissionDef& def, const CampaignVariant& v);
/// The mission laid out on a location's terrain (see CampaignWorld.cpp). The desert returns `def`.
CampaignMissionDef adaptCampaignMission(const CampaignMissionDef& def, const Terrain& t, const WorldLayout& L);

/// The aircraft carrier used by the carrier missions: off the east coast, bow to the north.
DeckInfo campaignCarrierDeck();

/// City-anchored targets and waypoints in world coordinates for this layout (the main city's
/// street grid); everything else is unchanged. adaptCampaignMission does this itself.
CampaignMissionDef resolveCampaignAnchors(const CampaignMissionDef& def, const Terrain& t, const WorldLayout& L);

/// Definitions for missions 4..100 (nullptr outside that range).
const CampaignMissionDef* campaignMission(int number);
const std::vector<CampaignMissionDef>& campaignMissions();
/// Builds the objective FSM for a definition.
Mission buildCampaignMission(const CampaignMissionDef& def);
/// Heading the player starts with (resolves the "towards the first objective" default).
double campaignStartHeading(const CampaignMissionDef& def);
/// World-space point an objective refers to (waypoint, or the centroid of its live target group).
bool objectivePoint(const CampaignMissionDef& def, const ObjectiveDef& o, const Battle* battle, Vec3& out);

/// Runs a campaign definition on a Battle: places targets and allies at the start and releases
/// enemy waves when the objective that owns them begins. Shared by the game and the tests.
class CampaignDirector {
public:
    /// `seedSalt` varies the random draws (tests replay a mission with different luck).
    void begin(const CampaignMissionDef& def, Battle& battle, unsigned seedSalt = 0);
    /// Call after every Mission::update; spawns waves for newly started objectives.
    void update(const Mission& mission, Battle& battle);
    const CampaignMissionDef* def() const { return def_; }
    /// Late reinforcement waves released so far (the game calls them on the radio).
    int lateReleased() const { return lateReleased_; }

private:
    void spawnFor(size_t objective, Battle& battle);
    const CampaignMissionDef* def_ = nullptr;
    size_t lastStep_ = static_cast<size_t>(-1);
    double stepStart_ = 0;                 // battle clock when the current objective began
    std::vector<char> lateSpawned_;        // per objective late wave: already released
    int lateReleased_ = 0;
};

}  // namespace sky
