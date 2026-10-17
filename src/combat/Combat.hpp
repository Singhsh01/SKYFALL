// SKYFALL combat sandbox — fictional game-system mechanics only (no real-world tactics).
// Independent of raylib so it can be unit-tested and stepped deterministically.
#pragma once

#include "../sim/FlightModel.hpp"

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
    std::string callsign;
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
    double hitFlash = 0;
    int kills = 0;
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
                        Count };

/// Display name, missile aim height above the base, hit radius and default health per kind.
const char* groundKindName(GroundKind k);
double groundAimHeight(GroundKind k);
double groundHitRadius(GroundKind k);
double groundMaxHealth(GroundKind k);
inline bool groundIsNaval(GroundKind k) { return k == GroundKind::OilRig || k == GroundKind::Ship; }
/// Kinds that shoot at aircraft when armed.
inline bool groundShootsAir(GroundKind k) { return k == GroundKind::AAA || k == GroundKind::Gun || k == GroundKind::Ship || k == GroundKind::SAM; }

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
    int group = 0;            // mission objective group (0 = not an objective)
    bool armed = true;        // false for range targets that never shoot back
};

enum class EventType { Explosion, SmallExplosion, Hit, JetKilled, GroundKilled, MissileLaunch, FlaresOut,
                       PlayerHit, PlayerKilled, MissileWarningStart, LockAcquired, Splash, MissileMissed, Flak };

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

struct BattleConfig {
    int enemyJets = 4;
    int allyJets = 1;
    bool groundTargets = true;   // the arena's town garrison
    bool endlessWaves = true;    // arena: a new wave after each cleared one; campaign: scripted waves only
    AiSkill skill = AiSkill::Veteran;
    unsigned seed = 1234;
    bool playerInvulnerable = false;
    double agReloadSeconds = 14.0;
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

    /// Creates the player jet (id 0) — caller then resets its flight model as needed.
    Jet& createPlayer(AircraftId id);
    void start(const BattleConfig& cfg);   // spawn allies, enemies and ground forces
    void clearCombatants();                // remove everything except the player

    // ---- scripted scenarios (campaign) ----
    /// Adds a ground target; y is set from the terrain (sea surface for naval kinds), health from the kind.
    int addGround(GroundTarget g);
    /// Spawns `count` hostile jets around `center` at `altitude` (MSL), heading towards the player.
    void spawnEnemies(int count, AiSkill skill, const Vec3& center, double radius, double altitude, bool unarmed = false);
    int groundAlive(int group) const;      // live targets in an objective group
    int groundTotal(int group) const;
    int groundKilledByPlayer() const { return groundKills_; }
    // test helpers (as if destroyed by the player)
    void debugDestroyGround(int idx) { damageGround(idx, 1e6, player().id); }
    void debugKillJet(int id) { if (Jet* j = jetById(id)) damageJet(*j, 1e6, player().id, "TEST"); }

    void update(double dt, const ControlInputs& playerInput, const PlayerCommands& cmd);

    Jet& player() { return *jets_[0]; }
    const Jet& player() const { return *jets_[0]; }
    std::vector<std::unique_ptr<Jet>>& jets() { return jets_; }
    const std::vector<std::unique_ptr<Jet>>& jets() const { return jets_; }
    const std::vector<Missile>& missiles() const { return missiles_; }
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
    void launchSurfaceMissile(const Vec3& from, int targetJet);
    void dropFlares(Jet& j);
    Missile& debugAddMissile(const Missile& m) { missiles_.push_back(m); return missiles_.back(); }
    Jet& debugAddJet(Team team, AircraftId id, const Vec3& pos, double heading, double speed);
    void setAiEnabled(bool on) { aiEnabled_ = on; }

private:
    void spawnWave();
    void updateJet(Jet& j, double dt, const ControlInputs* override, const PlayerCommands* cmd);
    ControlInputs aiPilot(Jet& j, double dt, PlayerCommands& out);
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
    int wave_ = 0;
    double waveTimer_ = 0;
    double waveBanner_ = 0;
    bool missileWarning_ = false;
    bool prevWarning_ = false;
    double threatDist_ = 1e9;
    Vec3 threatDir_;
};

}  // namespace sky
