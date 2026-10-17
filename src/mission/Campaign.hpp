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
    enum class Anchor { None, Railway, Bridge, Industrial } anchor = Anchor::None;
    double railFraction = 0;  // Railway: position along the line (0..1); x/z ignored
    double railSpeed = 0;     // Railway: m/s (trains move; wagons follow the locomotive)
    int index = 0;            // Industrial: which factory slot
};

struct WaveDef {
    int count = 2;
    AiSkill skill = AiSkill::Veteran;
    double x = 0, z = 0;      // centre of the spawn ring
    double radius = 1500;
    double altitude = 2000;   // metres above sea level
    bool unarmed = false;     // target drones
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
};

/// The aircraft carrier used by the carrier missions: off the east coast, bow to the north.
DeckInfo campaignCarrierDeck();

/// Definitions for missions 4..40 (nullptr outside that range).
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
    void begin(const CampaignMissionDef& def, Battle& battle);
    /// Call after every Mission::update; spawns waves for newly started objectives.
    void update(const Mission& mission, Battle& battle);
    const CampaignMissionDef* def() const { return def_; }

private:
    void spawnFor(size_t objective, Battle& battle);
    const CampaignMissionDef* def_ = nullptr;
    size_t lastStep_ = static_cast<size_t>(-1);
};

}  // namespace sky
