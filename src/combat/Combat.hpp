// SKYFALL combat sandbox — fictional game-system mechanics only (no real-world tactics).
// Independent of raylib so it can be unit-tested and stepped deterministically.
#pragma once

#include "../sim/FlightModel.hpp"
#include "../sim/WorldLayout.hpp"

#include <memory>
#include <string>
#include <vector>

namespace sky {

enum class Team { Blue, Red };
enum class AiSkill { Rookie, Veteran, Ace };
enum class AiState { Patrol, Engage, Evade, Recover, Extend };

struct Jet {
    int id = 0;
    Team team = Team::Blue;
    bool isPlayer = false;
    int skin = 0;           // camouflage scheme (0 = factory), see render/Skins.hpp
    std::string callsign;
    // transform at the start of the latest physics step (render interpolation)
    Vec3 prevPos;
    Quat prevQ;
    bool prevValid = false;
    std::unique_ptr<FlightModel> fm;
    double health = 100.0;
    bool alive = true;
    double deadTimer = 0;
    int missiles = 6;       // ATA (air-to-air)
    int agms = 4;           // ATS (air-to-surface)
    bool weaponAG = false;  // selected missile type: false = ATA, true = ATS
    double agReload = 0;
    int rounds = 480;
    int flareCharges = 3;
    double flareCooldown = 0;     // recharge timer for one charge
    double flareBurstTimer = 0;   // remaining time of an active burst
    double missileCooldown = 0;
    double gunHeat = 0;
    bool gunFiring = false;
    double gunTimer = 0;
    int lockTarget = -1;          // jet id or -1000-groundIndex
    double lockProgress = 0;      // 0..1, >= 1 locked
    // AI
    AiSkill skill = AiSkill::Veteran;
    AiState ai = AiState::Patrol;
    int aiTarget = -1;
    Vec3 waypoint;
    double aiTimer = 0;
    double reaction = 0;
    // squadron tactics (campaign tactics level >= 1; unused at level 0)
    int squad = -1;               // wave the jet belongs to (-1 = none)
    int squadRole = 0;            // position in the squadron: 0 lead, 1/3 flankers, 2 high cover
    double jinkTimer = 0;         // time to the next break reversal
    double jinkSign = 1;          // current break direction (+1 right / -1 left)
    int evadeMissile = -1;        // missile the current beam manoeuvre is against
    double evadeSign = 1;
    double extendTimer = 0;       // > 0: unloading to regain speed
    double crankTimer = 0;        // > 0: offsetting after a missile launch
    double crankSign = 1;
    double hitFlash = 0;
    // scripted route (Training Academy formation lead): no weapons, no threat reactions
    std::vector<Vec3> route;
    size_t routeIdx = 0;
    double routeSpeed = 200;
    bool routeLoop = true;
    bool formationLead = false;   // training lead: smooth loop, tight speed hold (a wingman formates on it)
    int kills = 0;
    double ramp = 0;              // cargo ramp 0 closed .. 1 open (transport)
    // ---- mission roles (Training Academy operations) ----
    int aiPrefer = -1;            // jet id this aircraft hunts first (escort missions: the transport)
    bool strike = false;          // attacks the other side's ground units (air-to-surface missiles, gun)
    int strikeTarget = -1;        // ground index of the current strike
    double strikeTimer = 0;       // > 0: egressing after a pass
    int followId = -1;            // wingman: holds a loose formation on this jet when nothing to fight
    double damageScale = 1.0;     // heavy aircraft take less damage per hit
    bool kinematic = false;       // the flight state is driven by a script (transport paths) until it is hit hard
    double breakTimer = 0;        // > 0: damaged, breaking off to regroup
    ControlInputs lastInput{};
    // Trail bookkeeping for renderer
    double contrailTimer = 0;
};

struct Missile {
    int id = 0;
    int owner = -1;
    Team team = Team::Blue;
    int targetJet = -1;     // jet id being tracked, -1 = none
    int targetGround = -1;  // ground target index
    int decoyFlare = -1;    // flare index if decoyed
    Vec3 pos, vel;
    double life = 0;
    double motor = 4.0;     // remaining boost/sustain time
    bool active = true;
    bool surfaceLaunched = false;
    bool airToSurface = false;  // ATS / air-to-ground missile (bigger warhead, ground lock)
    Vec3 prevPos;               // position at the start of the latest physics step
    bool prevValid = false;
};

struct Bullet {
    Vec3 pos, vel;
    double life = 0;
    int owner = -1;
    Team team = Team::Blue;
    bool tracer = false;
};

struct Flare {
    Vec3 pos, vel;
    double life = 0;
    int owner = -1;
    bool active = true;
};

enum class GroundKind { Tank, Truck, AAA, SAM, Bunker, Radar,
                        Gun,        // anti-aircraft gun emplacement (sandbags + twin cannon), fires flak
                        Artillery,  // field gun battery (long barrel), no air threat
                        OilRig,     // offshore oil platform (stands in the sea)
                        FuelTank,   // storage tank at a fuel depot
                        Hangar,     // aircraft shelter
                        Command,    // command post building with masts
                        Ship,       // patrol boat with a deck gun (moves on the sea)
                        // critical infrastructure (campaign 42-51)
                        PowerPlant, // turbine hall with two cooling towers and a stack
                        Refinery,   // distillation columns, pipe racks, flare stack
                        Bridge,     // a road / rail bridge span (placed on a real bridge of the layout)
                        CommTower,  // 90 m lattice communications mast
                        Factory,    // saw-tooth roof production hall with chimney
                        Locomotive, // diesel locomotive (runs on the railway)
                        Wagon,      // freight / armoured wagon (runs on the railway)
                        Launcher,   // mobile missile launcher vehicle
                        ParkedJet,  // aircraft parked on an airfield
                        Ammo,       // earth-covered ammunition bunker
                        Substation, // electrical transformer yard
                        Helicopter, // flying: transport or (armed) attack helicopter, targetable by ATA and ATS
                        Cargo,      // supply pallet (airdropped under a parachute, or unloaded from a transport)
                        Count };

/// Display name, missile aim height above the base, hit radius and default health per kind.
const char* groundKindName(GroundKind k);
double groundAimHeight(GroundKind k);
double groundHitRadius(GroundKind k);
double groundMaxHealth(GroundKind k);
inline bool groundIsNaval(GroundKind k) { return k == GroundKind::OilRig || k == GroundKind::Ship; }
/// Kinds that shoot at aircraft when armed.
inline bool groundShootsAir(GroundKind k) { return k == GroundKind::AAA || k == GroundKind::Gun || k == GroundKind::Ship || k == GroundKind::SAM || k == GroundKind::Helicopter; }
inline bool groundOnRail(GroundKind k) { return k == GroundKind::Locomotive || k == GroundKind::Wagon; }

struct GroundTarget {
    GroundKind kind = GroundKind::Tank;
    Vec3 pos;
    double heading = 0;
    double health = 100;
    double maxHealth = 100;
    bool alive = true;
    double fireCooldown = 0;
    double pathAngle = 0;     // vehicles (and boats) drive along a circle
    double pathRadius = 0;
    Vec3 pathCenter;
    Team team = Team::Red;
    std::string label;        // e.g. "GUN 2", "OIL RIG A" (shown on the HUD and in the kill feed)
    double railS = -1;        // >= 0: position along the railway (trains), metres
    double railSpeed = 0;     // m/s along the railway (stops at the end of the line)
    double length = 0;        // bridges: span length (m); heading gives the span direction
    int group = 0;            // mission objective group (0 = not an objective)
    bool armed = true;        // false for range targets that never shoot back
    // helicopters: fly a circle (pathCenter / pathRadius) at flyAgl above the ground
    double flyAgl = 0;        // > 0: airborne
    double flySpeed = 38;     // m/s along the circle
    bool attack = false;      // attack helicopter (fires its cannon at aircraft when armed)
    double fallV = 0;         // shot down: vertical speed of the falling wreck
    // a route (convoys, rescue helicopters): driven / flown along the polyline at routeSpeed,
    // stopping at the end; routeS = distance travelled
    std::vector<Vec3> route;
    double routeS = 0, routeSpeed = 0;
    double hold = 0;          // > 0: waits (seconds) before moving on (a rescue helicopter on the pick-up)
    bool huntGround = false;  // attack helicopter: closes on and fires at the other side's ground units
    int aircraft = -1;        // ParkedJet: AircraftId shown (-1 = a Flanker)
    double chuteV = 0;        // Cargo: descending under a parachute at this rate (m/s), 0 = on the ground
    bool airborne() const { return kind == GroundKind::Helicopter && alive && flyAgl > 0; }
    bool arrived() const { return !route.empty() && routeS >= routeLength() - 1.0; }
    double routeLength() const {
        double L = 0;
        for (size_t i = 1; i < route.size(); ++i) L += std::hypot(route[i].x - route[i - 1].x, route[i].z - route[i - 1].z);
        return L;
    }
};

enum class EventType { Explosion, SmallExplosion, Hit, JetKilled, GroundKilled, MissileLaunch, FlaresOut,
                       PlayerHit, PlayerKilled, MissileWarningStart, LockAcquired, Splash, MissileMissed, Flak,
                       JetDamaged };   // any jet damaged (b = victim, size = damage, text = weapon): visual effects

struct CombatEvent {
    EventType type;
    Vec3 pos;
    double size = 1.0;
    int a = -1, b = -1;       // actor ids (killer, victim) when meaningful
    std::string text;
    int c = -1;               // missile id for launch events
};

struct KillFeedEntry {
    std::string killer, victim, weapon;
    bool killerBlue = true;
    double age = 0;
};

/// Air-to-air refuelling tanker: flies a racetrack circle at constant speed and altitude, trailing a
/// hose with a drogue basket behind and below the tail.
struct Tanker {
    bool active = false;
    Vec3 center;              // racetrack centre (x, z) and altitude (y)
    double radius = 7000;
    double speed = 175;       // m/s
    double angle = 0;         // position on the circle
    Vec3 pos, vel;
    double heading = 0;
    bool contact = false;     // player probe in the basket
    double contactTime = 0;
    Vec3 basket() const;      // world position of the drogue
};

struct BattleConfig {
    int enemyJets = 4;
    int allyJets = 1;
    bool groundTargets = true;   // the arena's town garrison
    bool endlessWaves = true;    // arena: a new wave after each cleared one; campaign: scripted waves only
    AiSkill skill = AiSkill::Veteran;
    unsigned seed = 1234;
    bool playerInvulnerable = false;
    double agReloadSeconds = 14.0;
    /// Enemy tactics (campaign difficulty): 0 = classic pursuit AI (missions 1-46 and the arena),
    /// 1 = tactical (beam/notch missile defence, guns defence with break reversals, energy
    /// management), 2 = squadron (adds bracketing flankers, high cover, cranking after a shot,
    /// unpredictable reversals, tighter air defences), 3 = final battle (as 2, faster tempo).
    int tactics = 0;
    /// Academy operations: damaged AI aircraft break off and regroup, wingmen hold formation on
    /// their leader between fights, strike aircraft attack ground units.
    bool smartAi = false;
};

struct PlayerCommands {
    bool fireGun = false;
    bool fireMissile = false;   // edge-triggered
    bool flares = false;        // edge-triggered
    bool cycleTarget = false;   // edge-triggered
};

class Battle {
public:
    explicit Battle(const Terrain& terrain);
    const Terrain& terrain() const { return *terrain_; }

    /// Creates the player jet (id 0) — caller then resets its flight model as needed.
    Jet& createPlayer(AircraftId id);
    void start(const BattleConfig& cfg);   // spawn allies, enemies and ground forces
    void clearCombatants();                // remove everything except the player
    /// Removes one aircraft (not the player); missiles aimed at it go ballistic.
    void removeJet(int id);
    /// Takes a ground target out of play without an explosion (training retries).
    void retireGround(int index);
    GroundTarget& groundMut(int index) { return ground_[static_cast<size_t>(index)]; }

    // ---- scripted scenarios (campaign) ----
    /// Adds a ground target; y is set from the terrain (sea surface for naval kinds), health from the kind.
    int addGround(GroundTarget g);
    /// Spawns `count` hostile jets around `center` at `altitude` (MSL), heading towards the player.
    void spawnEnemies(int count, AiSkill skill, const Vec3& center, double radius, double altitude, bool unarmed = false,
                      const std::string& leadCallsign = "");
    int tactics() const { return cfg_.tactics; }
    int groundAlive(int group) const;      // live targets in an objective group
    Vec3 groundVelocity(int idx) const;    // m/s, from the vehicle's circular route (0 when static)
    Vec3 groundAimPoint(int idx) const { return ground_[idx].pos + Vec3{0, groundAimHeight(ground_[idx].kind), 0}; }
    /// Best ground target for the player's ATS / cannon assist inside `coneRad` of `dir` and `range`.
    int bestGroundTarget(const Vec3& from, const Vec3& dir, double coneRad, double range, Team shooter) const;
    int groundTotal(int group) const;
    int groundKilledByPlayer() const { return groundKills_; }
    struct PlayerStats { int missilesFired = 0, missileHits = 0, roundsFired = 0, roundHits = 0, damageTaken = 0; };
    const PlayerStats& playerStats() const { return stats_; }
    // ---- infrastructure / refuelling ----
    void setLayout(const WorldLayout* layout) { layout_ = layout; }
    const WorldLayout* layout() const { return layout_; }
    void startTanker(const Vec3& center, double radius, double speed, double startAngle);
    const Tanker& tanker() const { return tanker_; }
    // test helpers (as if destroyed by the player)
    void debugDestroyGround(int idx) { damageGround(idx, 1e6, player().id); }
    void debugKillJet(int id) { if (Jet* j = jetById(id)) damageJet(*j, 1e6, player().id, "TEST"); }

    void update(double dt, const ControlInputs& playerInput, const PlayerCommands& cmd);

    Jet& player() { return *jets_[0]; }
    const Jet& player() const { return *jets_[0]; }
    std::vector<std::unique_ptr<Jet>>& jets() { return jets_; }
    const std::vector<std::unique_ptr<Jet>>& jets() const { return jets_; }
    // changes whenever a jet is created or destroyed (unique across Battle objects), so a
    // Jet* kept across a frame can be checked before it is used again
    unsigned long long rosterVersion() const { return roster_; }
    const std::vector<Missile>& missiles() const { return missiles_; }
    std::vector<Missile>& missilesMut() { return missiles_; }   // render interpolation only
    const std::vector<Bullet>& bullets() const { return bullets_; }
    const std::vector<Flare>& flares() const { return flares_; }
    const std::vector<GroundTarget>& ground() const { return ground_; }
    std::vector<CombatEvent> drainEvents() { auto e = std::move(events_); events_.clear(); return e; }
    const std::vector<KillFeedEntry>& killFeed() const { return feed_; }

    bool active() const { return active_; }
    int score() const { return score_; }
    int wave() const { return wave_; }
    int enemiesAlive() const;
    bool playerMissileWarning() const { return missileWarning_; }
    double nearestThreatDistance() const { return threatDist_; }
    Vec3 nearestThreatDir() const { return threatDir_; }
    Jet* jetById(int id);
    const Jet* jetById(int id) const;
    double waveBannerTimer() const { return waveBanner_; }

    // Exposed for tests
    static Vec3 proportionalNav(const Vec3& mPos, const Vec3& mVel, const Vec3& tPos, const Vec3& tVel, double N);
    bool lineOfSight(const Vec3& a, const Vec3& b) const;
    void fireMissile(Jet& shooter);
    /// An air-to-surface missile from an AI aircraft at a ground index (strike roles).
    void fireStrikeMissile(Jet& shooter, int groundIdx);
    /// Air-to-surface missiles and gun runs of every aircraft against one side's ground units.
    int groundAliveOfTeam(Team t) const;
    void launchSurfaceMissile(const Vec3& from, int targetJet);
    void dropFlares(Jet& j);
    Missile& debugAddMissile(const Missile& m) { missiles_.push_back(m); return missiles_.back(); }
    Jet& debugAddJet(Team team, AircraftId id, const Vec3& pos, double heading, double speed);
    /// Capture / test hooks: apply damage as a hit would (events and effects included).
    void debugDamageJet(Jet& victim, double dmg, int attacker, const char* weapon) { damageJet(victim, dmg, attacker, weapon); }
    void debugDamageGround(int idx, double dmg, int attacker) { damageGround(idx, dmg, attacker); }
    void setAiEnabled(bool on) { aiEnabled_ = on; }
    void setSmartAi(bool on) { cfg_.smartAi = on; }
    /// A scripted battle (Academy operations): no arena spawns or waves, ground units move and
    /// shoot, the AI uses the given tactics level and the smart behaviours.
    void startScripted(int tactics);
    void setTactics(int t) { cfg_.tactics = t; }
    void setActive(bool on) { active_ = on; }

private:
    void spawnWave();
    void updateJet(Jet& j, double dt, const ControlInputs* override, const PlayerCommands* cmd);
    ControlInputs aiPilot(Jet& j, double dt, PlayerCommands& out);
    int strikeTargetFor(Jet& j);
    void updateLock(Jet& j, double dt, bool cycle);
    void updateMissiles(double dt);
    void updateBullets(double dt);
    void updateFlares(double dt);
    void updateGround(double dt);
    void damageJet(Jet& victim, double dmg, int attacker, const char* weapon);
    void damageGround(int idx, double dmg, int attacker);
    void fireGunRound(Jet& j);
    void pushFeed(const std::string& k, const std::string& v, const std::string& w, bool blue);

    const Terrain* terrain_;
    std::vector<std::unique_ptr<Jet>> jets_;
    unsigned long long roster_ = 0;
    void rosterChanged();
    std::vector<Missile> missiles_;
    std::vector<Bullet> bullets_;
    std::vector<Flare> flares_;
    std::vector<GroundTarget> ground_;
    std::vector<CombatEvent> events_;
    std::vector<KillFeedEntry> feed_;
    BattleConfig cfg_{};
    Rng rng_;
    bool active_ = false;
    bool aiEnabled_ = true;
    int nextJetId_ = 0;
    int nextMissileId_ = 0;
    int score_ = 0;
    int groundKills_ = 0;
    PlayerStats stats_;
    const WorldLayout* layout_ = nullptr;
    Tanker tanker_;
    void updateTanker(double dt);
    int wave_ = 0;
    double waveTimer_ = 0;
    double waveBanner_ = 0;
    bool missileWarning_ = false;
    bool prevWarning_ = false;
    double threatDist_ = 1e9;
    double time_ = 0;                 // battle clock (s)
    double lastShotAtPlayer_ = -1e9;  // last enemy missile launch at the player (tactics >= 1 spacing)
    Vec3 threatDir_;
};

}  // namespace sky
