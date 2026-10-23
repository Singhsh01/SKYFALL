// TRAINING ACADEMY — twenty flight- and combat-training levels flown on SKYFALL's world locations.
//
// A level is turned into a Course by reading the terrain: valley-following routes (A* over a
// height grid), gates whose height follows the ground with altitude windows, natural or
// generated obstacles (rock pillars, arches, lattice masts, bridge decks) placed on flat valley
// floors, a military compound to overfly, a tanker orbit above the highest ground, and a
// landing on the runway (or, for the final qualification, the aircraft carrier). Every route is
// checked for clearance, climb gradients and turn radius so that all eight aircraft can fly it
// in all three handling modes; the tests fly every level with a bot to prove it.
//
// A TrainingSession runs a Course: it advances gates, runs manoeuvre tasks, the formation lead,
// the tanker and the landing, keeps the scoring (accuracy, manoeuvres, altitude/speed
// discipline, terrain clearance, stability, refuelling, landing), stores checkpoints for
// retries and drives the virtual instructor, which only uses lines from a finite vocabulary so
// every call can be pre-recorded by tools/make_voice.py.
#pragma once

#include "../combat/Combat.hpp"
#include "../sim/TerrainAnalysis.hpp"
#include "../sim/WorldLayout.hpp"
#include "Mission.hpp"
#include "Voice.hpp"

#include <array>
#include <string>
#include <vector>

namespace sky {

constexpr int kTrainingLevels = 20;

enum class GateCue { None, TurnLeft, TurnRight, Pass, Climb, Descend, Canyon, Pillars, Arch, Bridge, Masts, Compound, Glide, Formation };

struct Gate {
    Vec3 pos;                    // centre
    Vec3 dir{1, 0, 0};           // direction of travel through the gate (horizontal unit vector)
    double radius = 200;         // a pass inside this radius is a hit
    double minAgl = 0, maxAgl = 0;   // altitude window above the surface at the gate (0 = none)
    double ground = 0;           // surface height under the gate
    GateCue cue = GateCue::None; // what the instructor announces for it
    int section = 0;
};

enum class SectionKind {
    ColdStart, Lineup, Takeoff, GearUp, Climb,
    Route,                       // gates
    LevelTurn, ClimbTurn, DescendTurn, HighG, Loop, Roll, Recovery,
    Formation, Rendezvous, Refuel,
    Landing,                     // runway: approach gates, touchdown, stop (go-around pattern on a miss)
    CarrierLanding,              // approach gates, trap (bolter pattern)
    Engage                       // combat: destroy the section's targets (or survive), see Engagement
};

/// Weapons the player may use in an engagement.
enum class WeaponRule { Any, GunsOnly, MissilesOnly };

/// A combat exercise: aircraft, helicopters, ground targets or ships to destroy. Jets are spawned
/// ahead of the player when the section begins; helicopters and ground targets stand at `area`.
struct Engagement {
    std::vector<int> jets;                 // AircraftId of each bandit
    int skill = 1;                         // AiSkill: 0 rookie, 1 veteran, 2 ace
    bool jetsArmed = true;
    bool jetsOnRoute = false;              // fly a fixed racetrack (gunnery / missile drones)
    int transportHelis = 0, attackHelis = 0;
    int friendlyHelis = 0;                 // own helicopters among them (IFF: green, never targets)
    bool helisArmed = false;
    std::vector<int> ground;               // GroundKind of each ground target / ship
    bool groundArmed = false;
    Vec3 area;                             // y = ground height at the area
    double radius = 600;                   // spread of the helicopters / ground targets
    double jetAlt = 0;                     // bandit altitude (0 = the player's)
    WeaponRule rule = WeaponRule::Any;
    double surviveSeconds = 0;             // > 0: defensive exercise, survive this long
    int continueFrom = -1;                 // >= 0: fight the survivors of that engagement (no new spawns)
    bool fromBase = false;                 // bandits launch in formation from an enemy base and fly at you
};

enum class RouteStyle { Navigation, Valley, LowLevel, Precision, HighSpeed, Obstacles, Transit, Approach };

struct Section {
    SectionKind kind = SectionKind::Route;
    RouteStyle style = RouteStyle::Transit;
    std::string title, instruction, hint;
    std::string intro;                    // spoken by the instructor when the section begins
    int gateBegin = 0, gateEnd = 0;       // gates [begin, end) belonging to the section
    int gateStart = 0;                    // first active gate (landing: after the go-around pattern)
    double speedMin = 0, speedMax = 0;    // m/s advisory band (scored), 0 = none
    double ceilingAgl = 0;                // hard ceiling of low-level sections (scored), 0 = none
    double value = 0, value2 = 0;         // task parameters (bank, g, height change, seconds...)
    double estSeconds = 0;                // estimated duration
};

struct Prop { GroundKind kind; Vec3 pos; double heading; std::string label; };

struct Course {
    int level = 1;
    std::string locationId;
    std::vector<Gate> gates;
    std::vector<Section> sections;
    std::vector<Obstacle> obstacles;
    std::vector<Prop> props;               // military compound (static, unarmed)
    bool coldStart = false;
    bool carrierStart = false;             // catapult launch from the carrier (deck)
    Vec3 startPos;                         // takeoff position on the runway (CG height excluded)
    double startHeading = 0;               // takeoff heading (rad, 0 = +X)
    // tanker
    bool tanker = false;
    Vec3 tankerCenter;                     // y = altitude
    double tankerRadius = 9000, tankerSpeed = 175;
    // formation lead
    std::vector<Vec3> formationRoute;
    double formationSpeed = 200;
    // landing
    bool carrier = false;
    DeckInfo deck;
    Vec3 landAim;                          // touchdown aim point (runway or carrier wires)
    Vec3 landDir{1, 0, 0};                 // landing direction
    double glideDeg = 3.0;                 // inner glide path (the last glideBreak metres)
    double glideOuterDeg = 3.0;            // outer segment: steeper where terrain demands it
    double glideBreak = 1e9;
    int finalGate = -1;                    // circling approach: first gate of the straight final (-1 = straight in)
    double estimatedSeconds = 0;
    double routeLength = 0;                // metres of gated route
    std::string error;                     // non-empty if the course could not be built
    double minRequiredTurnRadius = 0;      // tightest turn the route asks for (validation)
    std::vector<Engagement> engagements;   // Engage sections (Section::value = index)
};

/// Glide-path height above the touchdown aim point at a distance `d` before it (two segments).
double approachHeight(const Course& c, double d);
double approachSlope(const Course& c, double d);

struct TrainingLevelDef {
    int number = 1;
    std::string title, summary;
    int difficulty = 1;                    // 1..5
    std::string location;                  // recommended world location
    bool needsSea = false;                 // carrier landing
    std::vector<std::string> briefing;     // radio briefing parts (static text)
    std::vector<std::string> skills;       // shown in the menu
};
const std::vector<TrainingLevelDef>& trainingLevels();
const TrainingLevelDef& trainingLevel(int number);

/// Builds a course for a level on the given terrain (deterministic). Check Course::error.
Course buildCourse(int level, const Terrain& t, const WorldLayout& layout);
/// Can the level be flown on this terrain (the final level needs open sea for the carrier)?
bool levelSupportsTerrain(int level, const Terrain& t, std::string* why = nullptr);

// ---------------------------------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------------------------------
struct ScoreCard {
    // category scores 0..100 (-1 = not part of this level)
    double accuracy = -1, manoeuvre = -1, discipline = -1, clearance = -1, stability = -1, refuel = -1, landing = -1, combat = -1;
    double total = 0;
    int kills = 0, targets = 0;           // combat exercises: destroyed / total
    int gatesHit = 0, gatesMissed = 0, gatesTotal = 0;
    int crashes = 0, bolters = 0, wire = 0;
    double touchdownSink = 0;
    double flightSeconds = 0;
};
/// Category weights: accuracy, manoeuvre, discipline, clearance, stability, refuel, landing, combat.
ScoreCard finalizeScore(ScoreCard c);

// ---------------------------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------------------------
struct RadioCall {
    Speaker who = Speaker::Instructor;
    std::string text;
    int priority = 1;          // 0 chatter, 1 normal, 2 urgent (interrupts)
};

enum class TrainingEventKind { GateHit, GateMissed, SectionDone, Trap, Bolter, Touchdown, GoAround, RefuelContact, Crash, TargetDestroyed };
struct TrainingEvent { TrainingEventKind kind; int index = 0; double value = 0; };

class TrainingSession {
public:
    void begin(Course course, Battle& battle, int aircraftId, int handlingMode = 1);
    /// Once per fixed step, BEFORE the mission update. `step` = current mission step (section).
    void update(const MissionContext& ctx, Battle& battle, size_t step, double dt);
    /// The mission FSM for the course: one step per section, predicates read this session.
    Mission buildMission();

    const Course& course() const { return course_; }
    int gateIndex() const { return gate_; }
    size_t section() const { return section_; }
    const Gate* currentGate() const { return gate_ >= 0 && gate_ < static_cast<int>(course_.gates.size()) && sectionHasGates() ? &course_.gates[gate_] : nullptr; }
    bool sectionHasGates() const;
    double formationSlotTime() const { return slotTime_; }
    bool inSlot() const { return inSlot_; }
    Vec3 slotPosition(const Battle& battle) const;
    int leadId() const { return leadId_; }
    double taskProgress() const { return taskProgress_; }      // manoeuvre progress 0..1
    std::string taskStatus() const { return taskStatus_; }     // HUD line for manoeuvres
    double refuelContact() const { return refuelContact_; }

    ScoreCard liveScore() const;
    ScoreCard finalScore() const;

    // checkpoints and retries
    bool canRetry() const { return checkpointValid_; }
    void retry(Battle& battle);
    int crashes() const { return crashes_; }
    void notifyCrash();

    /// Combat: targets destroyed / spawned in the running engagement, and the totals.
    int engagementAlive(const Battle& battle) const;
    int kills() const { return kills_; }
    int targets() const { return targets_; }
    double surviveRemaining() const;
    /// Jet ids and ground indices of the running engagement (empty outside one).
    void engagementTargets(std::vector<int>& jets, std::vector<int>& ground) const;
    std::vector<RadioCall> drainRadio();
    std::vector<TrainingEvent> drainEvents();

    // instructor helpers (also used to enumerate the voice pack)
    static std::string headingWords(int deg10);
    static std::string numberWords(int n);

private:
    void beginSection(size_t idx, const MissionContext& ctx, Battle& battle);
    void updateGates(const MissionContext& ctx, double dt);
    void updateTask(const MissionContext& ctx, double dt);
    void updateFormation(const MissionContext& ctx, Battle& battle, double dt);
    void updateLanding(const MissionContext& ctx, double dt);
    void updateScoring(const MissionContext& ctx, double dt);
    void beginEngagement(const Section& s, const MissionContext& ctx, Battle& battle);
    void updateEngagement(const Section& s, const MissionContext& ctx, Battle& battle, double dt);
    void endEngagement(Battle& battle, bool removeSurvivors);
    void instructor(const MissionContext& ctx, const Battle& battle, double dt);
    void say(Speaker who, const std::string& text, int priority, const std::string& key, double cooldown);
    void sayVariant(Speaker who, const std::vector<std::string>& variants, int priority, const std::string& key, double cooldown);
    bool sectionDone(size_t idx, const MissionContext& c) const;
    double sectionProgress(size_t idx, const MissionContext& c) const;

    Course course_;
    int aircraft_ = 0;
    size_t section_ = static_cast<size_t>(-1);
    int gate_ = 0;
    double prevS_ = -1e9;           // signed distance to the current gate plane at the last step
    double gateLinger_ = 0;         // seconds spent near the current gate without passing it
    bool sectionComplete_ = false;
    // task state
    double taskAcc_ = 0, taskAcc2_ = 0, taskStartAlt_ = 0, taskPrevHeading_ = 0, taskTimer_ = 0;
    int taskPhase_ = 0;
    double taskProgress_ = 0;
    std::string taskStatus_;
    double taskErrSum_ = 0, taskErrN_ = 0;
    std::vector<double> manoeuvreScores_;
    // formation
    int leadId_ = -1;
    size_t leadRouteBase_ = 0;
    double slotTime_ = 0, formationTime_ = 0;
    bool inSlot_ = false;
    // refuel
    double refuelBase_ = 0, refuelContact_ = 0;
    bool wasContact_ = false;
    int disconnects_ = 0;
    // landing
    int baseBolters_ = 0;
    bool landed_ = false, trapped_ = false;
    bool touchdownScored_ = false;
    double landingScore_ = -1;
    bool goAroundArmed_ = false;
    // scoring accumulators
    std::vector<double> gateScores_;
    int gatesHit_ = 0, gatesMissed_ = 0;
    double disciplineTime_ = 0, disciplineGood_ = 0;
    double gpwsTime_ = 0, lowTime_ = 0, unstableTime_ = 0;
    int nearMisses_ = 0;
    double obstacleNearTimer_ = 0;
    int crashes_ = 0;
    int handling_ = 1;
    double unstableHold_ = 0;
    double flightTime_ = 0;
    bool refuelRequired_ = false, refuelDone_ = false;
    // combat
    struct Spawned { std::vector<int> jets, ground, friendly; };
    std::vector<Spawned> spawned_;          // per engagement
    int engagement_ = -1;                   // the running one
    int kills_ = 0, targets_ = 0;
    double combatTime_ = 0, combatDamage_ = 0, lastHealth_ = 100;
    std::vector<double> combatScores_;
    int playerMissiles_ = 6, playerAgms_ = 4, playerRounds_ = 480, playerFlares_ = 3;
    double surviveTimer_ = 0, resupply_ = 0, routeRefresh_ = 0;
    int lastAlive_ = -1;
    // checkpoints
    struct Checkpoint {
        bool onGround = false, engineRunning = true, gearDown = false;
        Vec3 pos, vel; Quat q; double throttle = 0.8, fuel = 0;
        size_t section = 0;
        size_t gateScores = 0, manoeuvreScores = 0, combatScores = 0; int hit = 0, missed = 0, kills = 0;
    };
    Checkpoint cp_;
    bool checkpointValid_ = false;
    // instructor
    std::vector<RadioCall> radio_;
    std::vector<TrainingEvent> events_;
    std::vector<std::pair<std::string, double>> cooldowns_;
    double clock_ = 0;
    int variantCounter_ = 0;
    int lastHeadingCalled_ = -1;
    double offCourseTimer_ = 0;
    double prevDist_ = 1e18;
    int announcedCueGate_ = -1;
    bool briefedApproach_ = false;
    double landingCallTimer_ = 0;
    double sectionStartSim_ = 0, sectionClock_ = 0;
    double refuelScore_ = 0;
    int boltersSeen_ = 0, goArounds_ = 0, wire_ = 0;
    double touchdownSink_ = 0;
    double fastTimer_ = 0, slowTimer_ = 0, ceilingTimer_ = 0, nearTimer_ = 0;
    double lastCallTime_ = -100;
    bool rotateCalled_ = false, airborneCalled_ = false, paddlesCalled_ = false, inSlotCalled_ = false, pullCalled_ = false;
public:
    /// Retries resume at this mission step.
    size_t checkpointSection() const { return cp_.section; }
    /// HUD: approach guidance (glide-path deviation in metres, + = high; lateral offset, + = right of the line;
    /// distance to the aim point). Valid while `onApproach()`.
    bool onApproach() const;
    double glideDeviation(const FlightState& f) const;
    double lineupOffset(const FlightState& f) const;
    double distanceToAim(const FlightState& f) const;
    int goArounds() const { return goArounds_; }
    bool landed() const { return landed_; }
    bool trapped() const { return trapped_; }
private:
};

/// Every line the training instructor can say (for the voice pack and its test).
std::vector<VoiceLine> trainingVoiceLines();
/// Debrief calls (instructor): end of the course, then a remark on the score (no medals).
const char* trainingCourseCompleteLine();
const char* trainingRemarkLine(double total);
/// Locations the Academy offers for a level (downloadable real terrain included). Everest is
/// left out: at 5-8 km the air is too thin for the low-level obstacle work in every aircraft.
bool academyLocationAllowed(const std::string& locationId, int level);
/// Aircraft-specific handling advice (spoken in the briefing).
std::string aircraftHandlingLine(int aircraftId);
std::string handlingModeLine(int mode);

}  // namespace sky
