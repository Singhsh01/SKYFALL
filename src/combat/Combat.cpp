#include "Combat.hpp"

#include <algorithm>
#include <memory>
#include <cmath>

namespace sky {

namespace {
constexpr double kJetHitRadius = 9.0;
constexpr double kGroundHitRadius = 10.0;
constexpr double kLockRange = 5200.0;
constexpr double kLockCone = 26.0 * kDeg;
// Air-to-surface: generous lock so ground targets are practical in arcade play
constexpr double kAgLockRange = 7500.0;
constexpr double kAgLockCone = 34.0 * kDeg;
constexpr double kAgLockTime = 0.8;
constexpr double kGunAssistCone = 4.0 * kDeg;     // cannon rounds bend towards a target inside this cone
constexpr double kGunAssistRange = 2600.0;
constexpr double kGunGroundDamage = 14.0;
constexpr double kMissileMaxLife = 16.0;
constexpr double kGunRate = 30.0;       // rounds per second (visual rounds, each one a tracer-capable slug)
constexpr double kMuzzleSpeed = 1050.0;

const char* kRedNames[] = {"BANDIT", "VULTURE", "COBRA", "RAVEN", "JACKAL", "HYENA", "MANTIS", "WRAITH"};
const char* kBlueNames[] = {"WARDOG", "GARUDA", "SABER", "LANCER"};

/// Closest distance between point c and segment a-b.
double segmentPointDistance(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 ab = b - a;
    double t = clampd(dot(c - a, ab) / std::max(dot(ab, ab), 1e-9), 0.0, 1.0);
    return length(a + ab * t - c);
}
}  // namespace

const char* groundKindName(GroundKind k) {
    switch (k) {
        case GroundKind::Tank: return "TANK";
        case GroundKind::Truck: return "TRUCK";
        case GroundKind::AAA: return "AAA";
        case GroundKind::SAM: return "SAM SITE";
        case GroundKind::Bunker: return "BUNKER";
        case GroundKind::Radar: return "RADAR";
        case GroundKind::Gun: return "GUN";
        case GroundKind::Artillery: return "ARTILLERY";
        case GroundKind::OilRig: return "OIL RIG";
        case GroundKind::FuelTank: return "FUEL TANK";
        case GroundKind::Hangar: return "HANGAR";
        case GroundKind::Command: return "COMMAND POST";
        case GroundKind::Ship: return "PATROL BOAT";
        case GroundKind::PowerPlant: return "POWER PLANT";
        case GroundKind::Refinery: return "REFINERY";
        case GroundKind::Bridge: return "BRIDGE";
        case GroundKind::CommTower: return "COMMS TOWER";
        case GroundKind::Factory: return "FACTORY";
        case GroundKind::Locomotive: return "LOCOMOTIVE";
        case GroundKind::Wagon: return "WAGON";
        case GroundKind::Launcher: return "MISSILE LAUNCHER";
        case GroundKind::ParkedJet: return "PARKED JET";
        case GroundKind::Ammo: return "AMMO BUNKER";
        case GroundKind::Substation: return "SUBSTATION";
        default: return "TARGET";
    }
}

double groundAimHeight(GroundKind k) {
    switch (k) {
        case GroundKind::OilRig: return 24.0;
        case GroundKind::FuelTank: return 6.0;
        case GroundKind::Hangar: return 7.0;
        case GroundKind::Command: return 5.0;
        case GroundKind::Radar: return 3.0;
        case GroundKind::Ship: return 3.0;
        case GroundKind::Bunker: return 2.0;
        case GroundKind::PowerPlant: return 14.0;
        case GroundKind::Refinery: return 12.0;
        case GroundKind::Bridge: return 0.0;      // bridge pos is the deck
        case GroundKind::CommTower: return 25.0;
        case GroundKind::Factory: return 7.0;
        case GroundKind::Locomotive: case GroundKind::Wagon: return 2.5;
        case GroundKind::ParkedJet: return 2.0;
        case GroundKind::Ammo: return 2.5;
        case GroundKind::Substation: return 3.0;
        default: return 1.5;
    }
}

double groundHitRadius(GroundKind k) {
    switch (k) {
        case GroundKind::OilRig: return 34.0;
        case GroundKind::Hangar: return 24.0;
        case GroundKind::Ship: return 18.0;
        case GroundKind::Command: case GroundKind::FuelTank: case GroundKind::Bunker: return 15.0;
        case GroundKind::PowerPlant: return 45.0;
        case GroundKind::Refinery: return 35.0;
        case GroundKind::Bridge: return 30.0;
        case GroundKind::CommTower: return 28.0;
        case GroundKind::Factory: return 30.0;
        case GroundKind::Substation: return 20.0;
        case GroundKind::Ammo: return 16.0;
        default: return 14.0;
    }
}

double groundMaxHealth(GroundKind k) {
    switch (k) {
        case GroundKind::Truck: return 60;
        case GroundKind::Bunker: case GroundKind::Radar: return 160;
        case GroundKind::Gun: case GroundKind::Artillery: return 120;
        case GroundKind::OilRig: return 380;      // two ATS hits
        case GroundKind::FuelTank: return 90;
        case GroundKind::Hangar: return 300;
        case GroundKind::Command: return 260;
        case GroundKind::Ship: return 220;
        case GroundKind::PowerPlant: return 560;   // three ATS hits
        case GroundKind::Refinery: return 380;
        case GroundKind::Bridge: return 380;
        case GroundKind::CommTower: return 180;
        case GroundKind::Factory: return 360;
        case GroundKind::Locomotive: return 180;
        case GroundKind::Wagon: return 110;
        case GroundKind::Launcher: return 90;
        case GroundKind::ParkedJet: return 70;
        case GroundKind::Ammo: return 180;
        case GroundKind::Substation: return 160;
        default: return 100;
    }
}

Battle::Battle(const Terrain& terrain) : terrain_(&terrain), rng_(1234) {}

int Battle::addGround(GroundTarget g) {
    if (g.pathRadius > 0 && g.pathCenter.x == 0 && g.pathCenter.z == 0) g.pathCenter = g.pos;
    if (groundOnRail(g.kind) && g.railS >= 0 && layout_ && !layout_->railway.pts.empty()) {
        Vec3 tan;
        g.pos = layout_->railway.at(g.railS, &tan);
        g.pos.y = std::max(g.pos.y, terrain_->surfaceHeight(g.pos.x, g.pos.z));   // cuttings: the track bed is on the ground
        g.heading = std::atan2(tan.z, tan.x);
    } else if (g.kind == GroundKind::Bridge && g.pos.y > 0) {
        // bridge deck height comes from the layout (already in pos.y)
    } else if (groundIsNaval(g.kind)) g.pos.y = std::max(Terrain::kSeaLevel, terrain_->height(g.pos.x, g.pos.z));
    else g.pos.y = terrain_->height(g.pos.x, g.pos.z);
    g.maxHealth = groundMaxHealth(g.kind);
    g.health = g.maxHealth;
    g.alive = true;
    if (g.fireCooldown <= 0) g.fireCooldown = rng_.range(1.0, 6.0);
    ground_.push_back(g);
    return static_cast<int>(ground_.size()) - 1;
}

void Battle::spawnEnemies(int count, AiSkill skill, const Vec3& center, double radius, double altitude, bool unarmed,
                          const std::string& leadCallsign) {
    static const AircraftId types[] = {AircraftId::Su35, AircraftId::MiG29, AircraftId::Su57, AircraftId::Su30};
    const Vec3 pp = player().fm->state().pos;
    for (int i = 0; i < count; ++i) {
        double a = 2.0 * kPi * i / std::max(1, count) + rng_.range(-0.4, 0.4);
        Vec3 pos{center.x + std::cos(a) * radius, 0, center.z + std::sin(a) * radius};
        pos.x = clampd(pos.x, -15000, 15000);
        pos.z = clampd(pos.z, -15000, 15000);
        pos.y = std::max(altitude + rng_.range(-150, 150), terrain_->surfaceHeight(pos.x, pos.z) + 600.0);
        double hdg = std::atan2(pp.z - pos.z, pp.x - pos.x) + rng_.range(-0.3, 0.3);
        Jet& j = debugAddJet(Team::Red, types[(i + wave_) % 4], pos, hdg, 220);
        j.ai = AiState::Engage;
        j.skill = skill;
        j.squad = wave_;
        j.squadRole = i;
        if (i == 0 && !leadCallsign.empty()) j.callsign = leadCallsign;
        if (unarmed) { j.missiles = 0; j.rounds = 0; j.flareCharges = 0; j.ai = AiState::Patrol; }
    }
    ++wave_;
    waveBanner_ = 4.0;
}

int Battle::groundAlive(int group) const {
    int n = 0;
    for (const GroundTarget& g : ground_) if (g.group == group && g.alive) ++n;
    return n;
}

Vec3 Battle::groundVelocity(int idx) const {
    const GroundTarget& g = ground_[idx];
    if (g.railS >= 0 && g.railSpeed != 0 && layout_ && g.alive) {
        if (g.railS <= 0 || g.railS >= layout_->railway.length()) return {};
        Vec3 tan;
        layout_->railway.at(g.railS, &tan);
        return tan * g.railSpeed;
    }
    if (g.pathRadius <= 0 || !g.alive) return {};
    double speed = g.kind == GroundKind::Truck ? 9.0 : (g.kind == GroundKind::Ship ? 8.0 : 4.5);
    return Vec3{-std::sin(g.pathAngle), 0, std::cos(g.pathAngle)} * speed;
}

int Battle::bestGroundTarget(const Vec3& from, const Vec3& dir, double coneRad, double range, Team shooter) const {
    int best = -1;
    double bestScore = 1e18;
    for (int gi = 0; gi < static_cast<int>(ground_.size()); ++gi) {
        const GroundTarget& g = ground_[gi];
        if (!g.alive || g.team == shooter) continue;
        Vec3 d = groundAimPoint(gi) - from;
        double dist = length(d);
        if (dist > range || dist < 1.0) continue;
        double ang = std::acos(clampd(dot(d * (1.0 / dist), dir), -1.0, 1.0));
        if (ang > coneRad) continue;
        // prefer objective targets, then the one closest to the boresight
        double score = ang + dist / 30000.0 - (g.group > 0 ? 0.15 : 0.0);
        if (score < bestScore && lineOfSight(from, groundAimPoint(gi))) { bestScore = score; best = gi; }
    }
    return best;
}

Vec3 Tanker::basket() const {
    // drogue 46 m behind the tail and 9 m below, on the centreline
    Vec3 f{std::cos(heading), 0, std::sin(heading)};
    return pos - f * 46.0 - Vec3{0, 9.0, 0};
}

void Battle::startTanker(const Vec3& center, double radius, double speed, double startAngle) {
    tanker_ = Tanker{};
    tanker_.active = true;
    tanker_.center = center;
    tanker_.radius = radius;
    tanker_.speed = speed;
    tanker_.angle = startAngle;
    updateTanker(0.0);
}

void Battle::updateTanker(double dt) {
    Tanker& tk = tanker_;
    if (!tk.active) return;
    // counter-clockwise circle seen from above: heading is the tangent
    tk.angle += tk.speed / tk.radius * dt;
    tk.pos = {tk.center.x + std::cos(tk.angle) * tk.radius, tk.center.y, tk.center.z + std::sin(tk.angle) * tk.radius};
    tk.heading = tk.angle + kPi / 2;
    tk.vel = Vec3{std::cos(tk.heading), 0, std::sin(tk.heading)} * tk.speed;
    Jet& p = player();
    if (!p.alive) { tk.contact = false; return; }
    FlightState& ps = p.fm->mutableState();
    Vec3 probe = ps.pos + ps.q.forward() * 8.0 + ps.q.up() * 1.0;
    Vec3 rel = probe - tk.basket();
    double relSpeed = length(ps.vel - tk.vel);
    // body collision with the tanker (fuselage + wings, generous box)
    Vec3 d = ps.pos - tk.pos;
    Vec3 fwd{std::cos(tk.heading), 0, std::sin(tk.heading)}, right{-fwd.z, 0, fwd.x};
    double along = dot(d, fwd), side = dot(d, right);
    if (std::fabs(along) < 26.0 && std::fabs(side) < 22.0 && std::fabs(d.y) < 7.0) {
        damageJet(p, 1000.0, -1, "COLLISION");
        tk.contact = false;
        return;
    }
    bool inBasket = length(rel) < (tk.contact ? 12.0 : 9.0) && relSpeed < (tk.contact ? 18.0 : 13.0);
    if (inBasket) {
        if (!tk.contact) events_.push_back({EventType::LockAcquired, tk.basket(), 1.0, p.id, -2, "CONTACT"});
        tk.contact = true;
        tk.contactTime += dt;
        // arcade station-keeping: the hose drags the jet onto the tanker's speed and the basket
        double k = std::min(1.0, 2.5 * dt);
        ps.vel = ps.vel + (tk.vel - ps.vel) * k;
        ps.pos = ps.pos - rel * std::min(1.0, 1.2 * dt);
        double maxFuel = p.fm->profile().fuelMassKg;
        ps.fuelKg = std::min(maxFuel, ps.fuelKg + maxFuel * 0.045 * dt);   // ~20 s from empty to full
    } else {
        tk.contact = false;
    }
}

int Battle::groundTotal(int group) const {
    int n = 0;
    for (const GroundTarget& g : ground_) if (g.group == group) ++n;
    return n;
}

Jet& Battle::createPlayer(AircraftId id) {
    jets_.clear();
    missiles_.clear(); bullets_.clear(); flares_.clear(); ground_.clear(); events_.clear(); feed_.clear();
    nextJetId_ = 0;
    auto j = std::make_unique<Jet>();
    j->id = nextJetId_++;
    j->team = Team::Blue;
    j->isPlayer = true;
    j->callsign = "YOU";
    j->fm = std::make_unique<FlightModel>(profile(id), *terrain_);
    const AircraftProfile& p = profile(id);
    j->missiles = p.missiles;
    j->rounds = p.cannonRounds;
    j->flareCharges = p.flareCharges;
    jets_.push_back(std::move(j));
    active_ = false;
    score_ = 0;
    groundKills_ = 0;
    stats_ = PlayerStats{};
    tanker_ = Tanker{};
    wave_ = 0;
    return *jets_[0];
}

Jet& Battle::debugAddJet(Team team, AircraftId id, const Vec3& pos, double heading, double speed) {
    auto j = std::make_unique<Jet>();
    j->id = nextJetId_++;
    j->team = team;
    j->fm = std::make_unique<FlightModel>(profile(id), *terrain_);
    j->fm->resetAirborne(pos, heading, speed, 0.8);
    const AircraftProfile& p = profile(id);
    j->missiles = std::min(p.missiles, 4);
    j->rounds = p.cannonRounds;
    j->flareCharges = 2;
    j->skill = cfg_.skill;
    j->waypoint = pos;
    j->reaction = rng_.range(0.0, 1.0);
    if (team == Team::Red) j->callsign = std::string(kRedNames[j->id % 8]) + " " + std::to_string(1 + j->id % 4);
    else j->callsign = std::string(kBlueNames[j->id % 4]) + " " + std::to_string(2 + j->id % 3);
    jets_.push_back(std::move(j));
    return *jets_.back();
}

void Battle::clearCombatants() {
    if (jets_.size() > 1) jets_.erase(jets_.begin() + 1, jets_.end());
    missiles_.clear(); bullets_.clear(); flares_.clear(); ground_.clear();
    active_ = false;
}

void Battle::start(const BattleConfig& cfg) {
    cfg_ = cfg;
    rng_ = Rng(cfg.seed);
    clearCombatants();
    active_ = true;
    wave_ = 0;
    const FlightState& ps = player().fm->state();
    Euler e = toEuler(ps.q);
    // Allies in loose formation behind the player.
    for (int i = 0; i < cfg.allyJets; ++i) {
        Vec3 off = ps.q.rotate(Vec3{-120.0 - 80.0 * i, 10.0 + 15.0 * i, (i % 2 ? -1 : 1) * (90.0 + 40.0 * i)});
        Vec3 pos = ps.pos + off;
        pos.y = std::max(pos.y, terrain_->surfaceHeight(pos.x, pos.z) + 400.0);
        Jet& a = debugAddJet(Team::Blue, AircraftId::F22, pos, e.heading, std::max(ps.airspeed, 180.0));
        a.ai = AiState::Patrol;
    }
    if (cfg.groundTargets) {
        Vec3 c = terrain_->townCenter();
        auto place = [&](GroundKind k, double ang, double r, double pathR) {
            GroundTarget g;
            g.kind = k;
            g.pathCenter = {c.x + std::cos(ang) * r, 0, c.z + std::sin(ang) * r};
            g.pathRadius = pathR;
            g.pathAngle = rng_.range(0, 2 * kPi);
            g.pos = g.pathCenter;
            g.pos.y = terrain_->height(g.pos.x, g.pos.z);
            g.heading = rng_.range(0, 2 * kPi);
            g.health = g.maxHealth = (k == GroundKind::Bunker || k == GroundKind::Radar) ? 160 : 100;
            g.fireCooldown = rng_.range(1.0, 6.0);
            ground_.push_back(g);
        };
        for (int i = 0; i < 6; ++i) place(GroundKind::Tank, i * 1.05, 300 + 90 * i, 120 + 20 * i);
        for (int i = 0; i < 3; ++i) place(GroundKind::Truck, 0.5 + i * 2.1, 650, 260);
        for (int i = 0; i < 3; ++i) place(GroundKind::AAA, 1.0 + i * 2.09, 820, 0);
        place(GroundKind::SAM, 2.6, 1000, 0);
        place(GroundKind::SAM, 5.4, 950, 0);
        place(GroundKind::Radar, 4.0, 700, 0);
        for (int i = 0; i < 4; ++i) place(GroundKind::Bunker, 0.8 + i * 1.6, 450, 0);
    }
    if (cfg.enemyJets > 0) spawnWave();
}

void Battle::spawnWave() {
    ++wave_;
    waveBanner_ = 4.0;
    const FlightState& ps = player().fm->state();
    Euler e = toEuler(ps.q);
    int count = cfg_.enemyJets + (wave_ - 1);
    count = std::min(count, 8);
    static const AircraftId types[] = {AircraftId::Su35, AircraftId::MiG29, AircraftId::Su57, AircraftId::Su30};
    for (int i = 0; i < count; ++i) {
        double ang = e.heading + rng_.range(-0.7, 0.7);
        double dist = rng_.range(6500, 9000);
        Vec3 pos{ps.pos.x + std::cos(ang) * dist, 0, ps.pos.z + std::sin(ang) * dist};
        // keep spawns inside the map
        pos.x = clampd(pos.x, -12500, 12500);
        pos.z = clampd(pos.z, -12500, 12500);
        double ground = terrain_->surfaceHeight(pos.x, pos.z);
        pos.y = std::max(ground + 900.0, rng_.range(1400, 2800));
        double hdg = std::atan2(ps.pos.z - pos.z, ps.pos.x - pos.x) + rng_.range(-0.3, 0.3);
        Jet& j = debugAddJet(Team::Red, types[i % 4], pos, hdg, 210);
        j.ai = AiState::Engage;
        j.skill = (wave_ >= 3 && i == 0) ? AiSkill::Ace : cfg_.skill;
    }
}

int Battle::enemiesAlive() const {
    int n = 0;
    for (auto& j : jets_) if (j->team == Team::Red && j->alive) ++n;
    return n;
}

Jet* Battle::jetById(int id) {
    for (auto& j : jets_) if (j->id == id) return j.get();
    return nullptr;
}
const Jet* Battle::jetById(int id) const {
    for (auto& j : jets_) if (j->id == id) return j.get();
    return nullptr;
}

bool Battle::lineOfSight(const Vec3& a, const Vec3& b) const {
    Vec3 d = b - a;
    double len = length(d);
    int n = std::clamp(static_cast<int>(len / 400.0), 2, 24);
    for (int i = 1; i < n; ++i) {
        Vec3 p = a + d * (double(i) / n);
        if (p.y < terrain_->surfaceHeight(p.x, p.z) + 5.0) return false;
    }
    return true;
}

Vec3 Battle::proportionalNav(const Vec3& mPos, const Vec3& mVel, const Vec3& tPos, const Vec3& tVel, double N) {
    Vec3 r = tPos - mPos;
    Vec3 v = tVel - mVel;
    double r2 = std::max(dot(r, r), 1.0);
    Vec3 omega = cross(r, v) / r2;          // LOS angular rate
    Vec3 rHat = normalize(r);
    double vc = -dot(rHat, v);              // closing speed
    return cross(omega, rHat) * (N * std::max(vc, 50.0));
}

void Battle::pushFeed(const std::string& k, const std::string& v, const std::string& w, bool blue) {
    feed_.push_back({k, v, w, blue, 0.0});
    if (feed_.size() > 6) feed_.erase(feed_.begin());
}

void Battle::fireMissile(Jet& j) {
    bool ag = j.isPlayer && j.weaponAG;
    if ((ag ? j.agms : j.missiles) <= 0 || j.missileCooldown > 0) return;
    const FlightState& s = j.fm->state();
    Missile m;
    m.id = nextMissileId_++;
    m.owner = j.id;
    m.team = j.team;
    m.pos = s.pos + s.q.rotate(Vec3{2.0, -1.6, (j.missiles % 2 ? 1.6 : -1.6)});
    m.vel = s.vel + s.q.forward() * 15.0 - s.q.up() * 6.0;
    m.life = 0;
    m.motor = 5.0;
    if (j.lockProgress >= 1.0) {
        if (j.lockTarget >= 0) m.targetJet = j.lockTarget;
        else if (j.lockTarget <= -1000) m.targetGround = -1000 - j.lockTarget;
    }
    m.airToSurface = ag;
    if (ag) {
        m.targetJet = -1;
        m.motor = 6.0;
        if (m.targetGround < 0)   // not locked yet: the seeker takes the best target near the nose
            m.targetGround = bestGroundTarget(s.pos, s.q.forward(), 22.0 * kDeg, kAgLockRange, j.team);
    }
    missiles_.push_back(m);
    if (ag) j.agms--; else j.missiles--;
    if (j.isPlayer) ++stats_.missilesFired;
    j.missileCooldown = j.isPlayer ? 0.7 : (j.skill == AiSkill::Ace ? 5.0 : 8.0);
    events_.push_back({EventType::MissileLaunch, m.pos, 1.0, j.id, ag ? -1000 - m.targetGround : m.targetJet, ag ? "ATS" : "ATA", m.id});
    if (j.isPlayer) {
        // missiles slowly re-arm in the arena (Battlefield-style reload) — handled in update
    }
}

void Battle::launchSurfaceMissile(const Vec3& from, int targetJet) {
    Missile m;
    m.id = nextMissileId_++;
    m.owner = -1;
    m.team = Team::Red;
    m.pos = from + Vec3{0, 6, 0};
    const Jet* t = jetById(targetJet);
    Vec3 dir = t ? normalize(t->fm->state().pos - m.pos) : Vec3{0, 1, 0};
    m.vel = dir * 120.0 + Vec3{0, 60, 0};
    m.targetJet = targetJet;
    m.motor = 6.0;
    m.surfaceLaunched = true;
    missiles_.push_back(m);
    events_.push_back({EventType::MissileLaunch, m.pos, 1.0, -1, targetJet, "SAM"});
}

void Battle::dropFlares(Jet& j) {
    if (j.flareCharges <= 0 || j.flareBurstTimer > 0) return;
    j.flareCharges--;
    j.flareBurstTimer = 1.25;
    if (j.flareCooldown <= 0) j.flareCooldown = 9.0;
    events_.push_back({EventType::FlaresOut, j.fm->state().pos, 1.0, j.id, -1, ""});
    // Decoy chance against every missile currently tracking this jet.
    for (Missile& m : missiles_) {
        if (!m.active || m.targetJet != j.id || m.decoyFlare >= 0) continue;
        double d = length(m.pos - j.fm->state().pos);
        double p = d < 300 ? 0.35 : (j.isPlayer ? 0.92 : (j.skill == AiSkill::Ace ? 0.8 : 0.6));
        if (rng_.uniform() < p) m.decoyFlare = -2;  // resolved to an actual flare when spawned
    }
}

void Battle::fireGunRound(Jet& j) {
    const FlightState& s = j.fm->state();
    Bullet b;
    double spread = j.isPlayer ? 0.0025 : (j.skill == AiSkill::Ace ? 0.004 : 0.008);
    Vec3 aimDir = s.q.forward();
    if (j.isPlayer) {
        // Arcade gun assist: if a target sits within a few degrees of the nose, bend the stream
        // onto its lead point (time of flight, target motion and gravity drop).
        Vec3 muzzle = s.pos + s.q.rotate(Vec3{6.0, 0.6, 1.0});
        Vec3 tp, tv;
        bool have = false;
        double bestAng = kGunAssistCone;
        for (auto& jp : jets_) {
            if (!jp->alive || jp->team == j.team) continue;
            Vec3 d = jp->fm->state().pos - muzzle;
            double dist = length(d);
            if (dist > kGunAssistRange) continue;
            double a = std::acos(clampd(dot(d * (1.0 / dist), aimDir), -1.0, 1.0));
            if (a < bestAng) { bestAng = a; tp = jp->fm->state().pos; tv = jp->fm->state().vel; have = true; }
        }
        int gi = bestGroundTarget(muzzle, aimDir, kGunAssistCone, kGunAssistRange, j.team);
        if (!have && gi >= 0) { tp = groundAimPoint(gi); tv = groundVelocity(gi); have = true; }
        if (have) {
            Vec3 rel = tp - muzzle;
            double tof = length(rel) / kMuzzleSpeed;
            for (int it = 0; it < 2; ++it) {
                Vec3 lead = tp + tv * tof - s.vel * tof + Vec3{0, 0.5 * kG * tof * tof, 0};
                tof = length(lead - muzzle) / kMuzzleSpeed;
                rel = lead - muzzle;
            }
            aimDir = normalize(rel);
        }
        ++stats_.roundsFired;
    }
    Vec3 dir = normalize(aimDir + s.q.up() * rng_.range(-spread, spread) + s.q.right() * rng_.range(-spread, spread));
    b.pos = s.pos + s.q.rotate(Vec3{6.0, 0.6, 1.0});
    b.vel = s.vel + dir * kMuzzleSpeed;
    b.owner = j.id;
    b.team = j.team;
    b.life = 2.6;
    b.tracer = true;
    bullets_.push_back(b);
    j.rounds--;
}

void Battle::damageJet(Jet& victim, double dmg, int attacker, const char* weapon) {
    if (!victim.alive) return;
    if (victim.isPlayer && cfg_.playerInvulnerable) dmg = 0;
    victim.health -= dmg;
    victim.hitFlash = 0.35;
    if (victim.isPlayer) stats_.damageTaken += static_cast<int>(std::lround(dmg));
    const Jet* att = jetById(attacker);
    if (victim.isPlayer) events_.push_back({EventType::PlayerHit, victim.fm->state().pos, dmg, attacker, victim.id, ""});
    else if (att && att->isPlayer) events_.push_back({EventType::Hit, victim.fm->state().pos, dmg, attacker, victim.id, ""});
    if (victim.health <= 0) {
        victim.health = 0;
        victim.alive = false;
        victim.deadTimer = 0;
        victim.fm->forceCrash(CrashReason::Destroyed);
        events_.push_back({EventType::JetKilled, victim.fm->state().pos, 3.0, attacker, victim.id, weapon});
        std::string killer = att ? att->callsign : "SAM SITE";
        pushFeed(killer, victim.callsign, weapon, att ? att->team == Team::Blue : false);
        if (att) const_cast<Jet*>(att)->kills++;
        if (att && att->isPlayer) score_ += 100;
        else if (att && att->team == Team::Blue && victim.team == Team::Red) score_ += 25;  // assist
        if (victim.isPlayer) events_.push_back({EventType::PlayerKilled, victim.fm->state().pos, 1.0, attacker, victim.id, weapon});
    }
}

void Battle::damageGround(int idx, double dmg, int attacker) {
    GroundTarget& g = ground_[idx];
    if (!g.alive) return;
    g.health -= dmg;
    const Jet* att = jetById(attacker);
    if (att && att->isPlayer) events_.push_back({EventType::Hit, g.pos, dmg, attacker, -1, ""});
    if (g.health <= 0) {
        g.alive = false;
        double size = 2.0;
        if (g.kind == GroundKind::Bunker) size = 2.5;
        if (g.kind == GroundKind::FuelTank || g.kind == GroundKind::Hangar || g.kind == GroundKind::Command) size = 3.4;
        if (g.kind == GroundKind::OilRig) size = 4.5;
        events_.push_back({EventType::GroundKilled, g.pos + Vec3{0, groundAimHeight(g.kind), 0}, size, attacker, idx, ""});
        pushFeed(att ? att->callsign : "?", g.label.empty() ? groundKindName(g.kind) : g.label, "", g.team == Team::Red);
        if (att && att->isPlayer) { score_ += g.group > 0 ? 100 : 50; ++groundKills_; }
    }
}

void Battle::updateLock(Jet& j, double dt, bool cycle) {
    const FlightState& s = j.fm->state();
    Vec3 fwd = s.q.forward();
    auto valid = [&](int tid, double& angleOut, double& distOut) -> bool {
        Vec3 tp;
        if (tid >= 0) {
            const Jet* t = jetById(tid);
            if (!t || !t->alive || t->team == j.team) return false;
            tp = t->fm->state().pos;
        } else if (tid <= -1000) {
            int gi = -1000 - tid;
            if (gi >= static_cast<int>(ground_.size()) || !ground_[gi].alive || ground_[gi].team == j.team) return false;
            tp = ground_[gi].pos + Vec3{0, groundAimHeight(ground_[gi].kind), 0};
        } else return false;
        Vec3 d = tp - s.pos;
        distOut = length(d);
        angleOut = std::acos(clampd(dot(normalize(d), fwd), -1.0, 1.0));
        bool ag = tid <= -1000;
        double cone = ag ? (j.isPlayer ? kAgLockCone : kLockCone * 0.8) : kLockCone;
        double range = ag && j.isPlayer ? kAgLockRange : kLockRange;
        if (j.isPlayer && ag != j.weaponAG) return false;  // lock follows the selected missile type
        return distOut < range && angleOut < cone && lineOfSight(s.pos, tp);
    };
    double ang = 0, dist = 0;
    bool currentValid = valid(j.lockTarget, ang, dist);
    if (!currentValid || cycle) {
        // choose best candidate (air first)
        std::vector<std::pair<double, int>> cands;
        if (!j.weaponAG) {
            for (auto& t : jets_) {
                double a, d;
                if (valid(t->id, a, d)) cands.push_back({a + d / 20000.0, t->id});
            }
        }
        if (j.isPlayer && j.weaponAG) {
            for (int gi = 0; gi < static_cast<int>(ground_.size()); ++gi) {
                double a, d;
                if (valid(-1000 - gi, a, d)) cands.push_back({a + d / 20000.0 - (ground_[gi].group > 0 ? 0.15 : 0.0), -1000 - gi});
            }
        }
        std::sort(cands.begin(), cands.end());
        int newTarget = -1;
        if (!cands.empty()) {
            newTarget = cands[0].second;
            if (cycle && currentValid) {
                for (size_t i = 0; i < cands.size(); ++i)
                    if (cands[i].second == j.lockTarget) { newTarget = cands[(i + 1) % cands.size()].second; break; }
            }
        }
        if (newTarget != j.lockTarget) { j.lockTarget = newTarget; j.lockProgress = 0; }
        currentValid = newTarget != -1 && valid(newTarget, ang, dist);
    }
    double lockTime = j.isPlayer ? (j.weaponAG ? kAgLockTime : 1.4) : (j.skill == AiSkill::Ace ? 1.6 : (j.skill == AiSkill::Veteran ? 2.1 : 2.8));
    double before = j.lockProgress;
    if (currentValid) j.lockProgress = std::min(1.0, j.lockProgress + dt / lockTime);
    else j.lockProgress = std::max(0.0, j.lockProgress - dt * 1.5);
    if (j.isPlayer && before < 1.0 && j.lockProgress >= 1.0)
        events_.push_back({EventType::LockAcquired, s.pos, 1.0, j.id, j.lockTarget, ""});
}

ControlInputs Battle::aiPilot(Jet& j, double dt, PlayerCommands& out) {
    ControlInputs in;
    FlightModel& fm = *j.fm;
    const FlightState& s = fm.state();
    Euler e = toEuler(s.q);
    const double V = std::max(s.airspeed, 1.0);
    j.aiTimer -= dt;

    // 1. Threat assessment: incoming missiles
    const Missile* threat = nullptr;
    double threatD = 1e9;
    for (const Missile& m : missiles_) {
        if (!m.active || m.targetJet != j.id || m.decoyFlare >= 0) continue;
        double d = length(m.pos - s.pos);
        if (d < threatD) { threatD = d; threat = &m; }
    }
    // 2. Target selection (re-evaluated periodically)
    if (j.aiTimer <= 0) {
        j.aiTimer = 1.5 + rng_.range(0.0, 1.0);
        double best = 1e18;
        j.aiTarget = -1;
        for (auto& t : jets_) {
            if (!t->alive || t->team == j.team) continue;
            double d = length(t->fm->state().pos - s.pos);
            if (t->isPlayer) d *= 0.7;
            if (d < best) { best = d; j.aiTarget = t->id; }
        }
    }
    Jet* target = jetById(j.aiTarget);
    if (target && !target->alive) target = nullptr;
    const int tac = cfg_.tactics;
    // tactics: is someone sitting on our six with the nose on us?
    bool gunThreat = false, gunThreatLocked = false;
    if (tac > 0) {
        for (auto& t : jets_) {
            if (!t->alive || t->team == j.team) continue;
            const FlightState& ts = t->fm->state();
            Vec3 rel = s.pos - ts.pos;
            double d = length(rel);
            if (d > 1300.0 || d < 1.0) continue;
            Vec3 u = rel * (1.0 / d);
            if (dot(ts.q.forward(), u) > std::cos(12.0 * kDeg) && dot(s.q.forward(), u) > std::cos(60.0 * kDeg)) {
                gunThreat = true;
                gunThreatLocked = t->lockTarget == j.id && t->lockProgress > 0.6;
                // break towards the attacker's side: the turn that makes him overshoot
                if (tac >= 2 && j.jinkTimer <= 0) j.jinkSign = dot(ts.pos - s.pos, s.q.right()) >= 0 ? 1.0 : -1.0;
            }
        }
    }
    j.crankTimer = std::max(0.0, j.crankTimer - dt);

    Vec3 desired = s.q.forward();
    double desiredSpeed = 215.0;
    bool aggressive = false;

    // ground clearance prediction
    double minClear = 1e9;
    for (double t : {1.0, 2.5, 4.5}) {
        Vec3 p = s.pos + s.vel * t;
        minClear = std::min(minClear, p.y - terrain_->surfaceHeight(p.x, p.z));
    }
    double agl = s.pos.y - terrain_->surfaceHeight(s.pos.x, s.pos.z);
    double horiz = std::sqrt(s.pos.x * s.pos.x + s.pos.z * s.pos.z);

    if (!j.route.empty() && minClear >= 120.0 && agl >= 150.0) {
        // scripted route: fly the waypoints (looping) at the commanded speed
        j.ai = AiState::Patrol;
        Vec3 tgt = j.route[std::min(j.routeIdx, j.route.size() - 1)];
        Vec3 d = tgt - s.pos;
        double hd = std::hypot(d.x, d.z);
        // the training lead: a waypoint left abeam inside the turn circle counts as passed,
        // or a slow jet in thin air could orbit it for ever
        bool abeam = j.formationLead && hd < 2600.0 && dot(normalize(Vec3{d.x, 0, d.z}), s.q.forward()) < 0.35;
        if (hd < 500.0 || (hd < 1500.0 && dot(d, s.q.forward()) < 0) || abeam) {
            if (j.routeIdx + 1 < j.route.size()) ++j.routeIdx;
            else if (j.routeLoop) j.routeIdx = 0;
            tgt = j.route[std::min(j.routeIdx, j.route.size() - 1)];
            d = tgt - s.pos;
        }
        desired = normalize(d);
        if (j.formationLead && j.route.size() > 2) {
            // fly the loop as a smooth curve: blend towards the leg after the waypoint as it nears
            Vec3 nxt = j.route[(j.routeIdx + 1) % j.route.size()];
            Vec3 leg = normalize(nxt - tgt);
            double hd2 = std::hypot(d.x, d.z);
            double wgt = clampd(1.0 - hd2 / 2600.0, 0.0, 1.0);
            desired = normalize(desired * (1.0 - 0.5 * wgt) + leg * (0.5 * wgt));
        }
        desired.y = clampd(desired.y, -0.2, 0.25);
        desired = normalize(desired);
        desiredSpeed = j.routeSpeed;
    } else if (minClear < 220.0 || agl < 260.0) {
        j.ai = AiState::Recover;
        Vec3 f = s.q.forward();
        desired = normalize(Vec3{f.x, 0, f.z} + Vec3{0, 1.3, 0});
        desiredSpeed = 260;
    } else if (threat && threatD < (tac > 0 ? 3800.0 : 3200.0)) {
        j.ai = AiState::Evade;
        Vec3 los = normalize(threat->pos - s.pos);
        Vec3 side = normalize(cross(los, Vec3{0, 1, 0}));
        if (tac == 0) {
            if (dot(side, s.q.right()) < 0) side = -side;
            desired = normalize(side + Vec3{0, -0.15, 0});
        } else {
            // beam the missile (fly across its line of sight so its closure collapses) and drag it
            // down towards the clutter; the side is chosen once per missile, at squadron level not
            // always the obvious one
            if (j.evadeMissile != threat->id) {
                j.evadeMissile = threat->id;
                j.evadeSign = dot(side, s.q.right()) >= 0 ? 1.0 : -1.0;
                if (tac >= 2 && rng_.uniform() < 0.3) j.evadeSign = -j.evadeSign;
            }
            side = side * j.evadeSign;
            double dive = agl > 900.0 ? -0.32 : (agl > 500.0 ? -0.1 : 0.12);
            desired = normalize(side + Vec3{0, dive, 0});
            if (threatD < 950.0) {
                // last-ditch break: pull hard across the missile's path, over the top or under
                double vert = (tac >= 2 && agl > 700.0 && (threat->id % 2)) ? -0.5 : 0.55;
                desired = normalize(side * 0.6 - los * 0.4 + Vec3{0, vert, 0});
            }
        }
        desiredSpeed = 400;
        double reactRange = j.skill == AiSkill::Ace ? 2200.0 : (j.skill == AiSkill::Veteran ? 1700.0 : 1100.0);
        if (tac > 0) reactRange += 400.0;
        if (threatD < reactRange && j.flareCharges > 0 && j.flareBurstTimer <= 0) out.flares = true;
    } else if (tac > 0 && gunThreat) {
        // guns defence: an enemy is saddled up behind; break hard and keep reversing the turn so
        // the attacker can never settle the pipper
        j.ai = AiState::Evade;
        if (j.jinkTimer <= 0) j.jinkTimer = tac >= 2 ? rng_.range(1.4, 2.8) : 2.4;   // first break
        j.jinkTimer -= dt;
        if (j.jinkTimer <= 0) {
            j.jinkSign = -j.jinkSign;   // reversal (scissors)
            j.jinkTimer = tac >= 2 ? rng_.range(1.1, 2.6) : 2.4;
        }
        Vec3 f = s.q.forward();
        Vec3 rr = s.q.right();
        Vec3 r = length(Vec3{rr.x, 0, rr.z}) > 0.1 ? normalize(Vec3{rr.x, 0, rr.z}) : normalize(cross(f, Vec3{0, 1, 0}));
        double vert = tac >= 2 ? (j.jinkSign > 0 ? 0.25 : -0.15) : 0.1;
        if (agl < 700.0) vert = 0.35;
        desired = normalize(r * (1.6 * j.jinkSign) + f * 0.25 + Vec3{0, vert, 0});
        desiredSpeed = 260;
        if (j.flareCharges > 0 && j.flareBurstTimer <= 0 && gunThreatLocked) out.flares = true;
    } else if (horiz > 13000.0) {
        j.ai = AiState::Patrol;
        desired = normalize(Vec3{-s.pos.x, 0, -s.pos.z} + Vec3{0, 0.1, 0});
    } else if (target) {
        j.ai = AiState::Engage;
        const FlightState& ts = target->fm->state();
        Vec3 d = ts.pos - s.pos;
        double dist = length(d);
        double tLead = clampd(dist / (kMuzzleSpeed + V * 0.3), 0.0, 2.0);
        Vec3 aim = dist < 1600.0 ? ts.pos + ts.vel * tLead : ts.pos + ts.vel * 1.2;
        desired = normalize(aim - s.pos);
        if (tac >= 2 && j.squad >= 0 && dist > 2300.0) {
            // squadron geometry: flankers bracket the target from both sides, the third jet comes
            // in high; the leader drives straight in
            Vec3 tv{ts.vel.x, 0, ts.vel.z};
            Vec3 tf = length(tv) > 1.0 ? normalize(tv) : normalize(Vec3{d.x, 0, d.z});
            Vec3 tr{-tf.z, 0, tf.x};
            int role = j.squadRole % 4;
            Vec3 offset;
            if (role == 1) offset = tr * 1800.0 + Vec3{0, 250, 0};
            else if (role == 3) offset = tr * -1800.0 + Vec3{0, 250, 0};
            else if (role == 2) offset = Vec3{0, 900, 0};
            double fade = smoothstep(2300.0, 5000.0, dist);
            desired = normalize(aim + offset * fade - s.pos);
        }
        if (tac >= 2 && j.crankTimer > 0 && dist > 3000.0) {
            // crank after a missile shot: offset 40 degrees to slow the closure while the missile flies
            Vec3 h = normalize(Vec3{desired.x, 0, desired.z});
            Vec3 hr{-h.z, 0, h.x};
            desired = normalize(h * std::cos(40.0 * kDeg) + hr * (std::sin(40.0 * kDeg) * j.crankSign) + Vec3{0, desired.y, 0});
        }
        if (dist < 260.0 && dot(normalize(ts.vel), s.q.forward()) < -0.3) {
            // head-on merge: break away to avoid collision
            desired = normalize(s.q.right() + Vec3{0, 0.3, 0});
        }
        desiredSpeed = dist > 3000 ? 300 : 230;
        if (tac > 0) {
            desiredSpeed = dist > 3000 ? 310 : (dist > 1500 ? 260 : 225);   // fast in, corner speed in the turn fight
            if (j.extendTimer <= 0 && V < 150.0 && dist > 900.0) j.extendTimer = tac >= 2 ? 3.5 : 4.5;
            if (j.extendTimer > 0) {
                // too slow to turn: unload and extend, then come back for another pass
                j.extendTimer -= dt;
                j.ai = AiState::Extend;
                Vec3 f = s.q.forward();
                desired = normalize(Vec3{f.x, 0, f.z} + Vec3{0, agl > 900.0 ? -0.2 : 0.05, 0});
                desiredSpeed = 420;
            }
        }
        aggressive = true;
        // Weapons
        Vec3 fwd = s.q.forward();
        double offBore = std::acos(clampd(dot(normalize(aim - s.pos), fwd), -1.0, 1.0));
        double gunCone = j.skill == AiSkill::Ace ? 3.5 * kDeg : 2.5 * kDeg;
        if (tac >= 2) gunCone += 0.8 * kDeg;   // squadron pilots take the snapshot
        out.fireGun = dist < 1000.0 && offBore < gunCone && j.rounds > 0;
        int missilesAtTarget = 0;
        for (const Missile& m : missiles_) if (m.active && m.targetJet == target->id && m.decoyFlare < 0) ++missilesAtTarget;
        if (tac == 0) {
            if (j.lockProgress >= 1.0 && j.lockTarget == target->id && dist < 4200.0 && dist > 600.0 &&
                missilesAtTarget < (target->isPlayer ? 1 : 2) && j.missileCooldown <= 0)
                out.fireMissile = rng_.uniform() < 0.02;
        } else {
            // never more than one missile at the player at a time, and a breather between shots
            double spacing = tac >= 3 ? 3.0 : (tac >= 2 ? 3.5 : 5.0);
            bool spaced = !target->isPlayer || time_ - lastShotAtPlayer_ > spacing;
            double maxR = tac >= 2 ? 4800.0 : 4400.0;
            if (j.lockProgress >= 1.0 && j.lockTarget == target->id && dist < maxR && dist > 700.0 && spaced &&
                missilesAtTarget < (target->isPlayer ? 1 : 2) && j.missileCooldown <= 0 && rng_.uniform() < (tac >= 2 ? 0.04 : 0.03)) {
                out.fireMissile = true;
                if (target->isPlayer) lastShotAtPlayer_ = time_;
                if (tac >= 2) { j.crankTimer = 3.0; j.crankSign = rng_.uniform() < 0.5 ? -1.0 : 1.0; }
            }
        }
    } else {
        j.ai = AiState::Patrol;
        Vec3 d = j.waypoint - s.pos;
        if (length(d) < 1500.0 || j.waypoint.y <= 0) {
            j.waypoint = {rng_.range(-8000, 8000), rng_.range(1500, 2600), rng_.range(-8000, 8000)};
        }
        desired = normalize(j.waypoint - s.pos);
    }
    // keep a floor of altitude while engaging
    if (aggressive && agl < 600.0 && desired.y < 0) desired.y *= 0.3;

    // ---- Convert desired direction into stick inputs (bank-to-turn) ----
    Vec3 db = s.q.unrotate(desired);
    double angle = std::acos(clampd(db.x, -1.0, 1.0));
    double rollErr = std::atan2(db.z, db.y);
    double fine = smoothstep(2.0 * kDeg, 14.0 * kDeg, angle);
    double skillGain = j.skill == AiSkill::Rookie ? 0.75 : (j.skill == AiSkill::Ace ? 1.15 : 1.0);
    in.roll = clampd((rollErr * 1.8 * fine - e.bank * 1.2 * (1.0 - fine)) * skillGain, -1.0, 1.0);
    double align = std::cos(rollErr);
    double pull = angle * 3.5;
    if (fine < 1.0) pull = db.y * 9.0;
    in.pitch = clampd(pull * (align > 0.2 ? 1.0 : 0.15), -0.6, 1.0) * (j.skill == AiSkill::Rookie ? 0.85 : 1.0);
    if (j.ai == AiState::Recover) { in.roll = clampd(-e.bank * 1.5, -1, 1); in.pitch = 1.0; }
    in.yaw = clampd(db.z * 3.0, -1.0, 1.0) * (1.0 - fine * 0.7);

    double leverTarget = V < desiredSpeed - 25 ? 1.0 : (V > desiredSpeed + 30 ? 0.55 : 0.85);
    in.throttleRate = clampd((leverTarget - s.throttle) * 6.0, -1.0, 1.0);
    in.airbrake = V > desiredSpeed + 120;
    if (j.formationLead && j.ai == AiState::Patrol) {
        // training lead: hold the commanded speed closely (a wingman has to be able to formate on it)
        leverTarget = clampd(0.45 + (desiredSpeed - V) * 0.04, 0.03, 1.0);
        in.throttleRate = clampd((leverTarget - s.throttle) * 6.0, -1.0, 1.0);
        in.airbrake = V > desiredSpeed + 12.0;
    }
    return in;
}

void Battle::updateJet(Jet& j, double dt, const ControlInputs* override, const PlayerCommands* cmd) {
    if (!j.alive) { j.deadTimer += dt; return; }
    PlayerCommands aiCmd;
    ControlInputs in;
    if (override) in = *override;
    else if (aiEnabled_) in = aiPilot(j, dt, aiCmd);
    else in = ControlInputs{};
    const PlayerCommands& c = cmd ? *cmd : aiCmd;
    j.lastInput = in;
    j.fm->step(in, dt);
    if (j.fm->state().crashed) {
        j.alive = false;
        j.health = 0;
        bool water = j.fm->state().crashReason == CrashReason::Water;
        events_.push_back({water ? EventType::Splash : EventType::JetKilled, j.fm->state().pos, 3.0, -1, j.id, "CRASH"});
        if (j.fm->state().crashReason != CrashReason::Destroyed) pushFeed(j.callsign, "", "CRASHED", j.team == Team::Blue);
        if (j.isPlayer) events_.push_back({EventType::PlayerKilled, j.fm->state().pos, 1.0, -1, j.id, "CRASH"});
        return;
    }
    j.hitFlash = std::max(0.0, j.hitFlash - dt);
    j.missileCooldown = std::max(0.0, j.missileCooldown - dt);
    // flare recharge (one charge per cooldown period)
    if (j.flareCharges < 3) {
        j.flareCooldown -= dt;
        if (j.flareCooldown <= 0) { j.flareCharges++; j.flareCooldown = j.flareCharges < 3 ? 9.0 : 0.0; }
    }
    // Player missile re-arm (arcade): one missile every 10 s up to the loadout
    if (j.isPlayer && active_ && j.missiles < j.fm->profile().missiles) {
        j.gunHeat += dt;
        if (j.gunHeat > 10.0) { j.missiles++; j.gunHeat = 0; }
    }
    if (j.isPlayer && active_ && j.agms < 4) {
        j.agReload += dt;
        if (j.agReload > cfg_.agReloadSeconds) { j.agms++; j.agReload = 0; }
    }
    // flare burst
    if (j.flareBurstTimer > 0) {
        double before = j.flareBurstTimer;
        j.flareBurstTimer -= dt;
        // release a pair every 0.15 s
        int nb = static_cast<int>(before / 0.15), na = static_cast<int>(std::max(j.flareBurstTimer, 0.0) / 0.15);
        if (nb != na) {
            const FlightState& s = j.fm->state();
            for (int side = -1; side <= 1; side += 2) {
                Flare f;
                f.pos = s.pos - s.q.forward() * 4.0 - s.q.up() * 1.0;
                f.vel = s.vel * 0.82 + s.q.right() * (side * rng_.range(35, 65)) - s.q.up() * rng_.range(20, 40) +
                        s.q.forward() * -10.0;
                f.life = 0;
                f.owner = j.id;
                flares_.push_back(f);
                int fi = static_cast<int>(flares_.size()) - 1;
                for (Missile& m : missiles_)
                    if (m.active && m.targetJet == j.id && m.decoyFlare == -2) m.decoyFlare = fi;
            }
        }
    }
    // Gun
    j.gunFiring = c.fireGun && j.rounds > 0;
    if (j.gunFiring) {
        j.gunTimer -= dt;
        while (j.gunTimer <= 0 && j.rounds > 0) { fireGunRound(j); j.gunTimer += 1.0 / kGunRate; }
    } else {
        j.gunTimer = 0;
    }
    if (j.isPlayer && j.rounds <= 0 && active_) j.rounds = 480;  // arcade ammo refill in the arena
    updateLock(j, dt, c.cycleTarget);
    if (c.fireMissile) fireMissile(j);
    if (c.flares) dropFlares(j);
}

void Battle::updateMissiles(double dt) {
    for (Missile& m : missiles_) {
        if (!m.active) continue;
        m.life += dt;
        m.motor -= dt;
        Vec3 tPos, tVel;
        bool hasTarget = false;
        Jet* tj = nullptr;
        if (m.decoyFlare >= 0 && m.decoyFlare < static_cast<int>(flares_.size()) && flares_[m.decoyFlare].active) {
            tPos = flares_[m.decoyFlare].pos; tVel = flares_[m.decoyFlare].vel; hasTarget = true;
        } else if (m.decoyFlare >= 0) {
            hasTarget = false;  // flare burnt out: missile is lost
        } else if (m.targetJet >= 0) {
            tj = jetById(m.targetJet);
            if (tj && tj->alive) { tPos = tj->fm->state().pos; tVel = tj->fm->state().vel; hasTarget = true; }
        } else if (m.targetGround >= 0 && m.targetGround < static_cast<int>(ground_.size()) && ground_[m.targetGround].alive) {
            tPos = groundAimPoint(m.targetGround); tVel = groundVelocity(m.targetGround); hasTarget = true;
        }
        double speed = length(m.vel);
        Vec3 dir = normalize(m.vel);
        Vec3 acc{0, -kG * 0.25, 0};
        if (hasTarget && m.life > 0.25) {
            Vec3 los = normalize(tPos - m.pos);
            if (dot(los, dir) < std::cos((m.airToSurface ? 85.0 : 65.0) * kDeg) && m.life > 1.0) {
                // seeker gimbal limit exceeded: lose the target
                if (tj && tj->isPlayer) events_.push_back({EventType::MissileMissed, m.pos, 1.0, m.owner, tj->id, ""});
                m.targetJet = -1; m.targetGround = -1; m.decoyFlare = -3;
            } else {
                Vec3 a = proportionalNav(m.pos, m.vel, tPos, tVel, 4.0);
                // blend in pure pursuit while slow to get off the rail cleanly
                a += (los - dir * dot(los, dir)) * (speed * 1.5);
                double aMax = (m.surfaceLaunched ? 25.0 : 32.0) * kG;
                double al = length(a);
                if (al > aMax) a = a * (aMax / al);
                acc += a;
            }
        }
        if (m.motor > 0) acc += dir * (m.life < 0.4 ? 60.0 : 210.0);
        acc -= dir * (2.6e-4 * speed * speed);
        Vec3 prev = m.pos;
        m.vel += acc * dt;
        m.pos += m.vel * dt;

        // Proximity fuse against the target and any other hostile jet
        for (auto& jp : jets_) {
            Jet& j = *jp;
            if (!j.alive || j.team == m.team || j.id == m.owner) continue;
            double d = segmentPointDistance(prev, m.pos, j.fm->state().pos);
            if (d < 16.0) {
                double dmg = 100.0;
                if (j.isPlayer) dmg = d < 8.0 ? 60.0 : 40.0;
                m.active = false;
                events_.push_back({EventType::Explosion, m.pos, 1.6, m.owner, j.id, ""});
                if (m.owner == player().id) ++stats_.missileHits;
                damageJet(j, dmg, m.owner, "MISSILE");
                break;
            }
        }
        if (!m.active) continue;
        if (m.targetGround >= 0) {
            for (int gi = 0; gi < static_cast<int>(ground_.size()); ++gi) {
                if (!ground_[gi].alive) continue;
                const GroundTarget& gt = ground_[gi];
                if (segmentPointDistance(prev, m.pos, gt.pos + Vec3{0, groundAimHeight(gt.kind), 0}) < groundHitRadius(gt.kind)) {
                    m.active = false;
                    events_.push_back({EventType::Explosion, m.pos, m.airToSurface ? 2.6 : 1.8, m.owner, -1, ""});
                    if (m.owner == player().id) ++stats_.missileHits;
                    damageGround(gi, 200.0, m.owner);
                    if (m.airToSurface)  // blast radius against nearby vehicles
                        for (int gk = 0; gk < static_cast<int>(ground_.size()); ++gk)
                            if (gk != gi && ground_[gk].alive && length(ground_[gk].pos - m.pos) < 45.0) damageGround(gk, 120.0, m.owner);
                    break;
                }
            }
            if (!m.active) continue;
        }
        if (m.decoyFlare >= 0 && m.decoyFlare < static_cast<int>(flares_.size()) &&
            length(m.pos - flares_[m.decoyFlare].pos) < 12.0) {
            m.active = false;
            events_.push_back({EventType::SmallExplosion, m.pos, 1.0, -1, -1, ""});
            continue;
        }
        double gy = terrain_->surfaceHeight(m.pos.x, m.pos.z);
        if (m.pos.y < gy) {
            m.active = false;
            m.pos.y = gy + 1;
            events_.push_back({terrain_->isWater(m.pos.x, m.pos.z) ? EventType::Splash : EventType::Explosion, m.pos, 1.2, -1, -1, ""});
            // splash damage to ground targets
            for (int gi = 0; gi < static_cast<int>(ground_.size()); ++gi)
                if (ground_[gi].alive && length(ground_[gi].pos - m.pos) < (m.airToSurface ? 45.0 : 30.0)) damageGround(gi, 120.0, m.owner);
            continue;
        }
        if (m.life > kMissileMaxLife) {
            m.active = false;
            events_.push_back({EventType::SmallExplosion, m.pos, 0.8, -1, -1, ""});
        }
    }
    missiles_.erase(std::remove_if(missiles_.begin(), missiles_.end(), [](const Missile& m) { return !m.active; }), missiles_.end());
}

void Battle::updateBullets(double dt) {
    for (Bullet& b : bullets_) {
        if (b.life <= 0) continue;
        b.life -= dt;
        Vec3 prev = b.pos;
        b.vel.y -= kG * dt;
        b.pos += b.vel * dt;
        bool hit = false;
        for (auto& jp : jets_) {
            Jet& j = *jp;
            if (!j.alive || j.team == b.team || j.id == b.owner) continue;
            if (segmentPointDistance(prev, b.pos, j.fm->state().pos) < kJetHitRadius) {
                damageJet(j, b.owner < 0 ? 2.5 : 5.0, b.owner, "CANNON");
                if (!jets_.empty() && b.owner == jets_[0]->id) ++stats_.roundHits;
                hit = true;
                break;
            }
        }
        if (!hit && b.team == Team::Blue) {
            for (int gi = 0; gi < static_cast<int>(ground_.size()); ++gi) {
                if (!ground_[gi].alive) continue;
                const GroundTarget& gt = ground_[gi];
                if (gt.team == b.team) continue;
                double hr = std::max(kGroundHitRadius, groundHitRadius(gt.kind) * 0.7);
                if (segmentPointDistance(prev, b.pos, gt.pos + Vec3{0, groundAimHeight(gt.kind), 0}) < hr) {
                    damageGround(gi, kGunGroundDamage, b.owner);
                    if (b.owner == player().id) ++stats_.roundHits;
                    hit = true;
                    break;
                }
            }
        }
        if (!hit && b.pos.y < terrain_->surfaceHeight(b.pos.x, b.pos.z)) {
            if (b.owner >= 0) events_.push_back({EventType::SmallExplosion, b.pos, 0.25, b.owner, -1, "impact"});
            hit = true;
        }
        if (hit) b.life = 0;
        else if (b.life <= 0 && b.owner < 0 && rng_.uniform() < 0.35) {
            // AAA shell self-destructs: flak burst near the closest hostile aircraft
            for (auto& jp : jets_) {
                if (!jp->alive || jp->team == b.team) continue;
                double d = length(jp->fm->state().pos - b.pos);
                if (d < 900.0) {
                    events_.push_back({EventType::Flak, b.pos, 1.0, -1, jp->id, ""});
                    if (d < 25.0) damageJet(*jp, 6.0, -1, "FLAK");
                    break;
                }
            }
        }
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [](const Bullet& b) { return b.life <= 0; }), bullets_.end());
}

void Battle::updateFlares(double dt) {
    for (Flare& f : flares_) {
        if (!f.active) continue;
        f.life += dt;
        f.vel.y -= kG * 0.6 * dt;
        f.vel = f.vel * (1.0 - std::min(1.0, 0.6 * dt));
        f.pos += f.vel * dt;
        if (f.life > 4.5) f.active = false;
    }
    // compact only when no missile references a flare index
    bool referenced = false;
    for (const Missile& m : missiles_) if (m.decoyFlare >= 0) referenced = true;
    if (!referenced) {
        bool anyActive = false;
        for (const Flare& f : flares_) if (f.active) anyActive = true;
        if (!anyActive) flares_.clear();
    }
}

void Battle::updateGround(double dt) {
    for (int gi = 0; gi < static_cast<int>(ground_.size()); ++gi) {
        GroundTarget& g = ground_[gi];
        if (!g.alive) continue;
        if (g.railS >= 0 && g.railSpeed != 0 && layout_ && !layout_->railway.pts.empty()) {
            g.railS = clampd(g.railS + g.railSpeed * dt, 0.0, layout_->railway.length());   // trains stop at the terminus
            Vec3 tan;
            g.pos = layout_->railway.at(g.railS, &tan);
            g.pos.y = std::max(g.pos.y, terrain_->surfaceHeight(g.pos.x, g.pos.z));
            g.heading = std::atan2(tan.z, tan.x) + (g.railSpeed < 0 ? kPi : 0.0);
        } else if (g.pathRadius > 0) {
            double speed = g.kind == GroundKind::Truck ? 9.0 : (g.kind == GroundKind::Ship ? 8.0 : 4.5);
            g.pathAngle += speed / g.pathRadius * dt;
            g.pos = {g.pathCenter.x + std::cos(g.pathAngle) * g.pathRadius, 0,
                     g.pathCenter.z + std::sin(g.pathAngle) * g.pathRadius};
            g.pos.y = groundIsNaval(g.kind) ? std::max(Terrain::kSeaLevel, terrain_->height(g.pos.x, g.pos.z))
                                            : terrain_->height(g.pos.x, g.pos.z);
            g.heading = g.pathAngle + kPi / 2;
        }
        if (!g.armed || g.team == Team::Blue || !groundShootsAir(g.kind)) continue;
        g.fireCooldown -= dt;
        if (g.fireCooldown > 0) continue;
        // nearest blue aircraft
        Jet* best = nullptr;
        double bd = 1e9;
        for (auto& jp : jets_) {
            if (!jp->alive || jp->team == g.team) continue;
            double d = length(jp->fm->state().pos - g.pos);
            if (d < bd) { bd = d; best = jp.get(); }
        }
        if (!best) { g.fireCooldown = 1.0; continue; }
        if ((g.kind == GroundKind::AAA || g.kind == GroundKind::Gun || g.kind == GroundKind::Ship) && bd < 2600.0) {
            // short tracer burst with lead and dispersion
            const FlightState& ts = best->fm->state();
            double tof = bd / 900.0;
            Vec3 aim = ts.pos + ts.vel * tof + Vec3{0, 0.5 * kG * tof * tof, 0};
            Vec3 dir = normalize(aim - g.pos);
            for (int k = 0; k < 4; ++k) {
                Bullet b;
                b.pos = g.pos + Vec3{0, 3, 0};
                Vec3 jitter{rng_.range(-1, 1), rng_.range(-1, 1), rng_.range(-1, 1)};
                b.vel = normalize(dir + jitter * (cfg_.tactics >= 2 ? 0.024 : 0.03)) * 900.0;
                b.team = g.team;
                b.owner = -1;
                b.life = 3.2;
                b.tracer = true;
                bullets_.push_back(b);
            }
            g.fireCooldown = 0.12;
            if (rng_.uniform() < 0.03) g.fireCooldown = 2.5;  // pause between bursts
        } else if (g.kind == GroundKind::SAM && bd < (cfg_.tactics >= 2 ? 7200.0 : 6500.0) && best->isPlayer &&
                   lineOfSight(g.pos + Vec3{0, 8, 0}, best->fm->state().pos)) {
            launchSurfaceMissile(g.pos, best->id);
            g.fireCooldown = cfg_.tactics >= 2 ? 16.0 : 22.0;
        } else {
            g.fireCooldown = 1.0;
        }
    }
}

void Battle::update(double dt, const ControlInputs& playerInput, const PlayerCommands& cmd) {
    time_ += dt;
    for (auto& jp : jets_) {
        if (jp->isPlayer) updateJet(*jp, dt, &playerInput, &cmd);
        else updateJet(*jp, dt, nullptr, nullptr);
    }
    updateMissiles(dt);
    updateBullets(dt);
    updateFlares(dt);
    if (active_) updateGround(dt);
    updateTanker(dt);

    // Missile warning for the player
    missileWarning_ = false;
    threatDist_ = 1e9;
    const Vec3 pp = player().fm->state().pos;
    for (const Missile& m : missiles_) {
        if (m.targetJet == player().id && m.decoyFlare < 0) {
            missileWarning_ = true;
            double d = length(m.pos - pp);
            if (d < threatDist_) { threatDist_ = d; threatDir_ = normalize(m.pos - pp); }
        }
    }
    if (missileWarning_ && !prevWarning_) events_.push_back({EventType::MissileWarningStart, pp, 1.0, -1, player().id, ""});
    prevWarning_ = missileWarning_;

    for (auto& f : feed_) f.age += dt;
    feed_.erase(std::remove_if(feed_.begin(), feed_.end(), [](const KillFeedEntry& f) { return f.age > 9.0; }), feed_.end());
    waveBanner_ = std::max(0.0, waveBanner_ - dt);

    // Remove long-dead AI jets, spawn next wave
    jets_.erase(std::remove_if(jets_.begin() + 1, jets_.end(), [](const std::unique_ptr<Jet>& j) { return !j->alive && j->deadTimer > 12.0; }), jets_.end());
    if (active_ && cfg_.endlessWaves && enemiesAlive() == 0) {
        waveTimer_ += dt;
        if (waveTimer_ > 5.0) { waveTimer_ = 0; spawnWave(); }
    }
}

}  // namespace sky
