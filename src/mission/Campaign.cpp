#include "Campaign.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace sky {

namespace {
using K = GroundKind;

TargetDef tgt(K k, double x, double z, std::string label, int group, bool armed = true, double heading = 0) {
    TargetDef t;
    t.kind = k; t.x = x; t.z = z; t.label = std::move(label); t.group = group; t.armed = armed; t.heading = heading;
    return t;
}

/// n targets in a line, labelled "<prefix> 1..n".
void line(std::vector<TargetDef>& v, K k, const std::string& prefix, int n, double x, double z, double spacing,
          double angle, int group, bool armed = true, int first = 1) {
    for (int i = 0; i < n; ++i) {
        double o = (i - (n - 1) * 0.5) * spacing;
        v.push_back(tgt(k, x + std::cos(angle) * o, z + std::sin(angle) * o, prefix.empty() ? "" : prefix + " " + std::to_string(first + i),
                        group, armed, angle + kPi / 2));
    }
}

/// n targets on a ring (a compound), labelled "<prefix> 1..n".
void ring(std::vector<TargetDef>& v, K k, const std::string& prefix, int n, double x, double z, double radius, int group,
          bool armed = true, double phase = 0) {
    for (int i = 0; i < n; ++i) {
        double a = phase + 2 * kPi * i / n;
        v.push_back(tgt(k, x + std::cos(a) * radius, z + std::sin(a) * radius, prefix.empty() ? "" : prefix + " " + std::to_string(i + 1),
                        group, armed, a));
    }
}

/// A convoy of vehicles following each other around a circular route.
void convoy(std::vector<TargetDef>& v, K k, const std::string& prefix, int n, double cx, double cz, double radius, int group,
            double startAngle = 0, int first = 1) {
    for (int i = 0; i < n; ++i) {
        TargetDef t = tgt(k, cx, cz, prefix + " " + std::to_string(first + i), group);
        t.pathRadius = radius;
        t.pathAngle = startAngle - i * (45.0 / radius);   // ~45 m apart
        v.push_back(t);
    }
}

ObjectiveDef waypoint(const std::string& title, double x, double z, double radius = 700, double maxAgl = 0,
                      const std::string& instr = "") {
    ObjectiveDef o;
    o.kind = ObjectiveKind::Waypoint; o.title = title; o.x = x; o.z = z; o.radius = radius; o.maxAgl = maxAgl;
    o.instruction = instr.empty() ? (maxAgl > 0 ? "Fly to the marker staying below " + std::to_string(static_cast<int>(maxAgl)) + " m above ground."
                                                : "Fly to the waypoint marker (diamond on the HUD and minimap).")
                                  : instr;
    o.hint = "FOLLOW THE MARKER";
    return o;
}

ObjectiveDef destroy(int group, const std::string& title, const std::string& instr = "",
                     const std::string& hint = "2 = ATS   F = FIRE") {
    ObjectiveDef o;
    o.kind = ObjectiveKind::DestroyGroup; o.group = group; o.title = title;
    o.instruction = instr.empty() ? "Select ATS missiles (2), hold the target in the lock cone until LOCK, then fire (F)." : instr;
    o.hint = hint;
    return o;
}

ObjectiveDef air(const std::string& title, std::vector<int> waves, const std::string& instr = "") {
    ObjectiveDef o;
    o.kind = ObjectiveKind::DestroyAir; o.title = title; o.wavesOnStart = std::move(waves);
    o.instruction = instr.empty() ? "Select ATA missiles (1), lock the bandits and splash them all. Flares: X." : instr;
    o.hint = "1 = ATA   F = FIRE   X = FLARES";
    return o;
}

WaveDef wave(int count, AiSkill skill, double x, double z, double alt = 2000, double radius = 1500, bool unarmed = false) {
    WaveDef w;
    w.count = count; w.skill = skill; w.x = x; w.z = z; w.altitude = alt; w.radius = radius; w.unarmed = unarmed;
    return w;
}

ObjectiveDef simple(ObjectiveKind k, const std::string& title, const std::string& instr, const std::string& hint,
                    double value = 0, double hold = 0) {
    ObjectiveDef o;
    o.kind = k; o.title = title; o.instruction = instr; o.hint = hint; o.value = value; o.hold = hold;
    return o;
}

TargetDef onRail(K k, double fraction, double speed, std::string label, int group) {
    TargetDef t = tgt(k, 0, 0, std::move(label), group);
    t.anchor = TargetDef::Anchor::Railway; t.railFraction = fraction; t.railSpeed = speed;
    return t;
}

/// A train: locomotive first, wagons 18 m apart behind it (same direction of travel).
void train(std::vector<TargetDef>& v, double fraction, double speed, int wagons, int group, double railLength = 24000) {
    v.push_back(onRail(K::Locomotive, fraction, speed, "LOCOMOTIVE", group));
    double dir = speed >= 0 ? -1.0 : 1.0;
    for (int i = 0; i < wagons; ++i)
        v.push_back(onRail(K::Wagon, fraction + dir * (21.0 + 18.0 * i) / railLength, speed, "WAGON " + std::to_string(i + 1), group));
}

// Home runway landing (approach gate, touchdown, stop) appended to a mission.
void landAtHome(CampaignMissionDef& m) {
    m.objectives.push_back(waypoint("APPROACH GATE", -13500, 3000, 900, 0,
                                    "Fly to the approach gate 3 km west of runway 09, then line up on heading 090."));
    m.objectives.push_back(simple(ObjectiveKind::Touchdown, "TOUCHDOWN", "Gear down (G), fly the glide path and touch down on runway 09.", "G = GEAR"));
    m.objectives.push_back(simple(ObjectiveKind::Stop, "BRAKE TO A STOP", "Throttle idle and hold the brakes until stopped.", "S + LEFT CTRL"));
}

// Aircraft-carrier start in a storm: afterburner, catapult, gear up (simple and hands-off).
void carrierStart(CampaignMissionDef& m) {
    DeckInfo d = campaignCarrierDeck();
    Vec3 c = d.catapultStart();
    m.carrier = true;
    m.rain = true;
    m.timeOfDay = 3;
    m.startX = c.x; m.startZ = c.z; m.startAlt = d.center.y; m.startSpeed = 0; m.startHeading = d.heading;
    std::vector<ObjectiveDef> pre = {
        simple(ObjectiveKind::FullPower, "FULL AFTERBURNER", "You're on the catapult. Push the throttle all the way into afterburner (hold W).",
               "W = THROTTLE UP", FlightModel::kAbDetent, 0.6),
        simple(ObjectiveKind::Launch, "CATAPULT LAUNCH", "When the afterburner lights, release the brake (B). The catapult throws you off the bow - the jet climbs by itself.",
               "B = RELEASE BRAKE"),
        simple(ObjectiveKind::GearUp, "GEAR UP", "Positive climb: raise the landing gear (G).", "G = GEAR"),
    };
    m.objectives.insert(m.objectives.begin(), pre.begin(), pre.end());
}

// Low pass over the carrier to finish.
void recoverOverCarrier(CampaignMissionDef& m) {
    DeckInfo d = campaignCarrierDeck();
    m.objectives.push_back(waypoint("RETURN TO THE CARRIER", d.center.x, d.center.z, 900, 700,
                                    "Head back to the carrier and make a low pass overhead, below 700 m."));
}

std::vector<CampaignMissionDef> buildAll() {
    std::vector<CampaignMissionDef> v;
    auto add = [&](int n, const std::string& title, const std::string& summary) -> CampaignMissionDef& {
        v.emplace_back();
        CampaignMissionDef& m = v.back();
        m.number = n; m.title = title; m.summary = summary;
        return m;
    };
    using S = AiSkill;

    // ---------------- Training ----------------
    {
        auto& m = add(4, "Navigation Training", "Fly a four-leg route over the desert using the HUD markers.");
        m.startX = -7000; m.startZ = 1500; m.startAlt = 1600;
        m.briefing = "Skyfall One, four checkpoints today. Follow the diamond on your HUD and minimap.";
        m.debrief = "Route complete. Clean navigation, Skyfall One.";
        m.objectives = {waypoint("CHECKPOINT ALPHA", -4000, -2000), waypoint("CHECKPOINT BRAVO", 1000, -5000),
                        waypoint("CHECKPOINT CHARLIE", 5000, -1500), waypoint("CHECKPOINT DELTA", 2500, 2200, 700, 0,
                                                                              "Overfly the town marker.")};
    }
    {
        auto& m = add(5, "High-G Maneuvers", "Sustained 6 g pull, a full loop and an aileron roll.");
        m.startX = -5000; m.startZ = -1000; m.startAlt = 2600; m.startSpeed = 260; m.startHeading = 0;
        m.briefing = "Plenty of sky up here. Let's see how you handle the g.";
        m.debrief = "Smooth energy management. You're ready for the range.";
        m.objectives = {
            simple(ObjectiveKind::PullG, "PULL 6 G", "Roll into a turn and pull until the G meter reads 6 or more; hold it for 2 seconds.", "ARROWS", 6.0, 2.0),
            simple(ObjectiveKind::Loop, "LOOP", "Full afterburner, wings level, pull through a complete loop.", "W + DOWN ARROW"),
            simple(ObjectiveKind::AileronRoll, "AILERON ROLL", "Double-tap LEFT or RIGHT for a full aileron roll.", "DOUBLE-TAP ARROW"),
        };
    }
    {
        auto& m = add(6, "Below the Radar", "Low-level route: stay under 250 m above the ground.");
        m.startX = -8000; m.startZ = -500; m.startAlt = 900; m.startSpeed = 220;
        m.briefing = "Radar can't see you in the ground clutter. Stay low and fast.";
        m.debrief = "Nice and low. The radar never saw you.";
        m.objectives = {waypoint("LOW GATE 1", -5000, -1500, 600, 250), waypoint("LOW GATE 2", -1500, 500, 600, 250),
                        waypoint("LOW GATE 3", 2000, -1500, 600, 250), waypoint("LOW GATE 4", 5000, 500, 600, 250)};
    }
    {
        auto& m = add(7, "Gunnery Range", "Strafe four range trucks with the cannon.");
        m.startX = -6500; m.startZ = -3000; m.startAlt = 1300;
        m.briefing = "Range is hot. Four trucks, cannon only. Shallow dive, short bursts.";
        m.debrief = "All targets hit. Good gunnery.";
        line(m.targets, K::Truck, "TARGET", 4, -2000, -4000, 90, 0.3, 1, false);
        m.objectives = {destroy(1, "STRAFE TARGETS 1-4", "Dive at the trucks and fire the cannon (SPACE) in short bursts.", "SPACE = CANNON")};
    }
    {
        auto& m = add(8, "ATS Missile Range", "Lock and destroy GUN 1, GUN 2 and GUN 3 with air-to-surface missiles.");
        m.startX = -4500; m.startZ = -1000; m.startAlt = 1500;
        m.briefing = "Three inert gun emplacements on the range. ATS missiles, one per target.";
        m.debrief = "Three for three. That's how ATS is done.";
        line(m.targets, K::Gun, "GUN", 3, 1600, -3500, 140, 0.0, 1, false);
        m.objectives = {destroy(1, "DESTROY GUN 1-3")};
    }
    {
        auto& m = add(9, "ATA Missile Range", "Two target drones: lock and shoot them down with ATA missiles.");
        m.startX = -6000; m.startZ = -2000; m.startAlt = 2100;
        m.briefing = "Two target drones on station. They won't shoot back.";
        m.debrief = "Drones splashed. Welcome to the fight.";
        m.waves = {wave(2, S::Rookie, 0, -3000, 2100, 1200, true)};
        m.objectives = {air("SPLASH TWO DRONES", {0})};
    }
    // ---------------- Operations ----------------
    {
        auto& m = add(10, "First Intercept", "Two rookie bandits probing the border.");
        m.startX = -6000; m.startZ = 0; m.startAlt = 2200;
        m.briefing = "Two bandits crossing the line from the east. Intercept.";
        m.debrief = "Both bandits down. Good first kill.";
        m.waves = {wave(2, S::Rookie, 1500, -1000, 2300)};
        m.objectives = {air("INTERCEPT 2 BANDITS", {0})};
    }
    {
        auto& m = add(11, "Dogfight Initiation", "Three veterans, one wingman on your side.");
        m.startX = -6000; m.startZ = 1000; m.startAlt = 2300; m.allies = 1;
        m.briefing = "Three bandits, real pilots this time. Garuda is on your wing.";
        m.debrief = "Sky's clear. You and Garuda make a good team.";
        m.waves = {wave(3, S::Veteran, 1500, 0, 2400)};
        m.objectives = {air("DESTROY 3 BANDITS", {0})};
    }
    {
        auto& m = add(12, "The Gun Line", "Four live anti-aircraft guns on the ridge: GUN 1-4.");
        m.startX = -2500; m.startZ = -6000; m.startAlt = 1600;
        m.briefing = "Four flak guns are protecting the northern pass. Take them out. Watch the tracers.";
        m.debrief = "Gun line silenced. The pass is open.";
        line(m.targets, K::Gun, "GUN", 4, 3900, -5673, 160, 0.4, 1);
        m.objectives = {destroy(1, "DESTROY GUN 1-4", "The guns shoot back: approach fast, lock from range (2 + F), break away after firing.")};
    }
    {
        auto& m = add(13, "Oil Rig Strike", "Two offshore oil platforms fuel the enemy war machine.");
        m.startX = 3500; m.startZ = -5000; m.startAlt = 1500;
        m.briefing = "Two oil rigs off the coast. They take two ATS hits each. A patrol boat is guarding them.";
        m.debrief = "Both rigs burning. That will hurt their fuel supply.";
        m.targets.push_back(tgt(K::OilRig, 11500, -6000, "OIL RIG A", 1));
        m.targets.push_back(tgt(K::OilRig, 12500, -3200, "OIL RIG B", 1));
        TargetDef boat = tgt(K::Ship, 11800, -4600, "PATROL BOAT", 0);
        boat.pathRadius = 500; m.targets.push_back(boat);
        m.objectives = {destroy(1, "DESTROY OIL RIG A AND B")};
    }
    {
        auto& m = add(14, "Convoy Ambush", "A supply convoy is moving through the desert.");
        m.startX = -7000; m.startZ = -2500; m.startAlt = 1300;
        m.briefing = "Six trucks and two tanks on the move. Stop that convoy.";
        m.debrief = "Convoy destroyed. Those supplies won't reach the front.";
        convoy(m.targets, K::Truck, "TRUCK", 6, -1759, -534, 700, 1, 0.0);
        convoy(m.targets, K::Tank, "TANK", 2, -1759, -534, 700, 1, 2.6);
        m.targets.push_back(tgt(K::AAA, -1759, -534, "AAA", 0));
        m.objectives = {destroy(1, "DESTROY THE CONVOY", "ATS blast hits nearby vehicles too: fire into the middle of the column.")};
    }
    {
        auto& m = add(15, "SEAD", "Suppress the radar and two SAM sites in the north.");
        m.startX = -4000; m.startZ = -4000; m.startAlt = 1200;
        m.briefing = "SAM site up north. Kill the radar first, then the launchers. Flares on the missile warning.";
        m.debrief = "SAMs suppressed. The strike package can go in.";
        m.targets.push_back(tgt(K::Radar, 3087, -8150, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 2587, -8650, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 3687, -8550, "SAM 2", 1));
        line(m.targets, K::Gun, "GUN", 2, 3087, -7550, 300, 0.0, 0);
        m.objectives = {destroy(1, "DESTROY RADAR, SAM 1 AND SAM 2", "Stay low until close, lock from range, X for flares when the missile warning sounds.")};
    }
    {
        auto& m = add(16, "Two-on-Two", "You and your wingman against two veterans.");
        m.startX = -5000; m.startZ = -2000; m.startAlt = 2500; m.allies = 1;
        m.briefing = "Two-ship against two-ship. Stay together.";
        m.debrief = "Fair fight, clean win.";
        m.waves = {wave(2, S::Veteran, 2000, -2500, 2600)};
        m.objectives = {air("WIN THE TWO-ON-TWO", {0})};
    }
    {
        auto& m = add(17, "Fuel Depot", "Destroy six fuel tanks guarded by two guns.");
        m.startX = -9000; m.startZ = -2500; m.startAlt = 1400;
        m.briefing = "Fuel depot, six storage tanks. GUN 1 and GUN 2 cover it.";
        m.debrief = "The depot is a fireball. Good work.";
        ring(m.targets, K::FuelTank, "FUEL TANK", 6, -5500, -6500, 70, 1);
        line(m.targets, K::Gun, "GUN", 2, -5500, -6500, 420, 1.2, 0);
        m.objectives = {destroy(1, "DESTROY 6 FUEL TANKS")};
    }
    {
        auto& m = add(18, "Mountain Pursuit", "Chase down an ace hiding in the northern mountains.");
        m.startX = -7000; m.startZ = -5000; m.startAlt = 2200;
        m.briefing = "An enemy ace is using the mountains for cover. Find him and bring him down.";
        m.debrief = "The ace is down. Impressive flying in those peaks.";
        m.waves = {wave(1, S::Ace, -12000, -10500, 2400, 600)};
        m.objectives = {air("DESTROY THE ACE", {0}, "He is fast and uses flares: get close, lock and fire twice.")};
    }
    {
        auto& m = add(19, "Coastal Patrol", "Patrol the coast, then sink three patrol boats.");
        m.startX = 4000; m.startZ = -12000; m.startAlt = 1300;
        m.briefing = "Patrol the coastline southwards. Hostile boats reported near the bay.";
        m.debrief = "Boats sunk. The coast is ours.";
        for (int i = 0; i < 3; ++i) {
            TargetDef b = tgt(K::Ship, 11000, -1500, "BOAT " + std::to_string(i + 1), 1);
            b.pathRadius = 450; b.pathAngle = i * 2.1; m.targets.push_back(b);
        }
        m.objectives = {waypoint("COAST NORTH", 8000, -9000), waypoint("COAST CENTRE", 8200, -4500),
                        destroy(1, "SINK 3 PATROL BOATS", "The boats move and shoot: lead them with ATS or the cannon.")};
    }
    {
        auto& m = add(20, "Airbase Defence", "Two waves of attackers are heading for home.");
        m.startX = -9000; m.startZ = 1500; m.startAlt = 1500; m.startHeading = 0; m.allies = 1;
        m.briefing = "Bandits inbound to the airbase from the east! Scramble, scramble!";
        m.debrief = "Base defended. Everyone's buying you drinks tonight.";
        m.waves = {wave(3, S::Veteran, -3000, 2500, 1800), wave(3, S::Veteran, -2000, 500, 2200)};
        m.objectives = {air("STOP THE FIRST WAVE", {0}), air("STOP THE SECOND WAVE", {1})};
    }
    {
        auto& m = add(21, "Dusk Strike", "Destroy a command post and its bunkers at last light.");
        m.timeOfDay = 1;
        m.startX = -8000; m.startZ = -6000; m.startAlt = 1500;
        m.briefing = "Command post in the north, three bunkers around it. Hit it before dark.";
        m.debrief = "Command post destroyed. Their commanders are blind.";
        m.targets.push_back(tgt(K::Command, -3200, -9000, "COMMAND POST", 1));
        ring(m.targets, K::Bunker, "BUNKER", 3, -3200, -9000, 160, 1, true, 0.5);
        line(m.targets, K::Gun, "GUN", 2, -3200, -9000, 520, 2.0, 0);
        m.objectives = {destroy(1, "DESTROY COMMAND POST AND BUNKERS")};
    }
    {
        auto& m = add(22, "Artillery Battery", "GUN 1-4 artillery is shelling the town.");
        m.startX = -4000; m.startZ = -1500; m.startAlt = 1500;
        m.briefing = "Artillery south-west of the town is shelling civilians. GUN 1 through 4. Take them out.";
        m.debrief = "The guns are silent. The town is safe.";
        line(m.targets, K::Artillery, "GUN", 4, 500, 3200, 110, 0.2, 1);
        line(m.targets, K::AAA, "", 2, 500, 3200, 500, 1.7, 0);
        m.objectives = {destroy(1, "DESTROY ARTILLERY GUN 1-4")};
    }
    {
        auto& m = add(23, "Radar Network", "Three early-warning radars spread across the north.");
        m.startX = -9000; m.startZ = -6000; m.startAlt = 1600;
        m.briefing = "Three radars watch the northern border. Blind them all.";
        m.debrief = "All three radars down. They can't see us coming.";
        m.targets.push_back(tgt(K::Radar, -6000, -10000, "RADAR WEST", 1));
        m.targets.push_back(tgt(K::Radar, 0, -12000, "RADAR NORTH", 1));
        m.targets.push_back(tgt(K::Radar, 5000, -9500, "RADAR EAST", 1));
        m.targets.push_back(tgt(K::AAA, 0, -11600, "", 0));
        m.objectives = {destroy(1, "DESTROY 3 RADARS")};
    }
    {
        auto& m = add(24, "Offshore Complex", "Boats, three oil rigs and an air patrol.");
        m.startX = 4000; m.startZ = 2000; m.startAlt = 1500;
        m.briefing = "Clear the escort boats first, then hit the three rigs. Expect fighters.";
        m.debrief = "The offshore complex is burning. Outstanding.";
        for (int i = 0; i < 2; ++i) {
            TargetDef b = tgt(K::Ship, 10800, 3500, "BOAT " + std::to_string(i + 1), 1);
            b.pathRadius = 600; b.pathAngle = i * 3.1; m.targets.push_back(b);
        }
        m.targets.push_back(tgt(K::OilRig, 12000, 2000, "OIL RIG A", 2));
        m.targets.push_back(tgt(K::OilRig, 13000, 4500, "OIL RIG B", 2));
        m.targets.push_back(tgt(K::OilRig, 11500, 6500, "OIL RIG C", 2));
        m.waves = {wave(2, S::Veteran, 13000, 1000, 2200)};
        m.objectives = {destroy(1, "SINK THE ESCORT BOATS"), destroy(2, "DESTROY OIL RIGS A, B, C"), air("CLEAR THE AIR PATROL", {0})};
        m.objectives[1].wavesOnStart = {0};
    }
    {
        auto& m = add(25, "Hangar Strike", "Destroy a forward airstrip's hangars and command post.");
        m.startX = -2000; m.startZ = -12000; m.startAlt = 1500;
        m.briefing = "Forward airstrip near the north-east coast. Three hangars and a command post. Three guns.";
        m.debrief = "No more sorties from that strip.";
        line(m.targets, K::Hangar, "HANGAR", 3, 5500, -12000, 90, 0.0, 1);
        m.targets.push_back(tgt(K::Command, 5500, -11700, "COMMAND POST", 1));
        ring(m.targets, K::Gun, "GUN", 3, 5500, -11950, 420, 0, true, 0.6);
        m.objectives = {destroy(1, "DESTROY 3 HANGARS AND COMMAND POST")};
    }
    {
        auto& m = add(26, "Tank Column", "Eight tanks advancing, covered by a SAM.");
        m.startX = -10000; m.startZ = 1000; m.startAlt = 1300;
        m.briefing = "Armoured column west of the town, eight tanks. A SAM is covering them.";
        m.debrief = "The armour is stopped cold.";
        convoy(m.targets, K::Tank, "TANK", 8, -7702, -2202, 550, 1);
        m.targets.push_back(tgt(K::SAM, -7702, -2202, "SAM", 0));
        m.objectives = {destroy(1, "DESTROY 8 TANKS")};
    }
    {
        auto& m = add(27, "Combined Arms", "SAMs first, then the tanks, while fighters respond.");
        m.startX = -4000; m.startZ = 5500; m.startAlt = 1400;
        m.briefing = "Kill the SAM site, then the armour. Fighters will scramble once you're spotted.";
        m.debrief = "Air and ground, all handled. That's combined arms.";
        m.targets.push_back(tgt(K::Radar, 2000, 4800, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 1500, 5200, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 2600, 5100, "SAM 2", 1));
        convoy(m.targets, K::Tank, "TANK", 6, 4000, 3000, 450, 2);
        m.waves = {wave(3, S::Veteran, 6000, 1000, 2200)};
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-2"), destroy(2, "DESTROY 6 TANKS"), air("DESTROY THE FIGHTERS", {})};
        m.objectives[1].wavesOnStart = {0};
    }
    {
        auto& m = add(28, "Ace Duel", "One-on-one with the enemy's best pilot.");
        m.startX = -5000; m.startZ = 0; m.startAlt = 3000;
        m.briefing = "Their top ace has challenged you. One-on-one, high altitude.";
        m.debrief = "The ace is down. You're the best in the sky now.";
        m.waves = {wave(1, S::Ace, 1500, 0, 3200, 300)};
        m.objectives = {air("WIN THE DUEL", {0})};
    }
    {
        auto& m = add(29, "Twilight Hunt", "Four veterans and an ace at sunset.");
        m.timeOfDay = 1; m.allies = 1;
        m.startX = -6000; m.startZ = 3000; m.startAlt = 2400;
        m.briefing = "Five bandits silhouetted against the sunset. Use the light.";
        m.debrief = "All five down at twilight. Legendary.";
        m.waves = {wave(4, S::Veteran, 1000, 2000, 2400), wave(1, S::Ace, 2000, 4000, 2800, 300)};
        m.objectives = {air("DESTROY 5 BANDITS", {0, 1})};
    }
    {
        auto& m = add(30, "Desert Fortress", "Command post, four bunkers, gun line and a SAM.");
        m.startX = -6000; m.startZ = -12000; m.startAlt = 1500;
        m.briefing = "The fortress: command post ringed by bunkers, GUN 1-4 and a SAM. Hit the command post and bunkers.";
        m.debrief = "The fortress has fallen.";
        m.targets.push_back(tgt(K::Command, 1000, -9000, "COMMAND POST", 1));
        ring(m.targets, K::Bunker, "BUNKER", 4, 1000, -9000, 180, 1, true, 0.4);
        ring(m.targets, K::Gun, "GUN", 4, 1000, -9000, 480, 0, true, 0.0);
        m.targets.push_back(tgt(K::SAM, 1500, -8300, "SAM", 0));
        m.objectives = {destroy(1, "DESTROY COMMAND POST AND 4 BUNKERS")};
    }
    {
        auto& m = add(31, "Supply Lines", "Two convoys and the depot that feeds them.");
        m.startX = -9000; m.startZ = -4000; m.startAlt = 1300;
        m.briefing = "Two convoys on the move and the depot at the end of the line.";
        m.debrief = "Supply lines cut in three places.";
        convoy(m.targets, K::Truck, "TRUCK", 5, -4500, -4500, 600, 1);
        convoy(m.targets, K::Truck, "TRUCK", 5, 1413, -6550, 600, 1, 1.0, 6);
        ring(m.targets, K::FuelTank, "FUEL TANK", 4, 4807, -3948, 60, 2);
        m.targets.push_back(tgt(K::Gun, 4807, -4248, "GUN 1", 0));
        m.objectives = {destroy(1, "DESTROY 10 TRUCKS"), destroy(2, "DESTROY THE FUEL DEPOT")};
    }
    {
        auto& m = add(32, "Mountain SAM Belt", "Three SAM sites and a radar hidden in the southern peaks.");
        m.startX = -6000; m.startZ = 7000; m.startAlt = 2300;
        m.briefing = "SAM belt in the southern mountains. Use the terrain to mask your approach.";
        m.debrief = "The SAM belt is gone. The mountains are safe to fly.";
        m.targets.push_back(tgt(K::Radar, -100, 8600, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, -1500, 9800, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 1713, 9350, "SAM 2", 1));
        m.targets.push_back(tgt(K::SAM, 3800, 11200, "SAM 3", 1));
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-3")};
    }
    {
        auto& m = add(33, "Harbour Raid", "Sink four boats and burn the dockside fuel tanks.");
        m.startX = 0; m.startZ = 4000; m.startAlt = 1500;
        m.briefing = "Harbour on the south-east coast. Boats in the bay, fuel on the quay.";
        m.debrief = "The harbour is closed for business.";
        for (int i = 0; i < 4; ++i) {
            TargetDef b = tgt(K::Ship, 10200, 9800, "BOAT " + std::to_string(i + 1), 1);
            b.pathRadius = 650; b.pathAngle = i * 1.57; m.targets.push_back(b);
        }
        ring(m.targets, K::FuelTank, "FUEL TANK", 4, 5850, 9760, 60, 2);
        m.targets.push_back(tgt(K::Gun, 6150, 9460, "GUN 1", 0));
        m.objectives = {destroy(1, "SINK 4 BOATS"), destroy(2, "DESTROY 4 FUEL TANKS")};
    }
    {
        auto& m = add(34, "Interceptor Rush", "Three waves of interceptors, back to back.");
        m.startX = -6000; m.startZ = -1000; m.startAlt = 2300; m.allies = 1;
        m.briefing = "They're throwing everything at us. Three waves. Conserve missiles.";
        m.debrief = "Three waves, zero leakers.";
        m.waves = {wave(3, S::Rookie, 1500, -1000), wave(3, S::Veteran, 2000, 1500), wave(3, S::Veteran, 1500, -3500, 2600)};
        m.objectives = {air("WAVE 1", {0}), air("WAVE 2", {1}), air("WAVE 3", {2})};
    }
    {
        auto& m = add(35, "Hunter-Killer", "Gun line on the ground while fighters hunt you.");
        m.startX = -2000; m.startZ = -2500; m.startAlt = 1700;
        m.briefing = "GUN 1-4 near the coast, and four fighters on patrol. Choose your moment.";
        m.debrief = "Hunter became the hunted. Well flown.";
        line(m.targets, K::Gun, "GUN", 4, 6087, -2450, 170, 1.4, 1);
        m.waves = {wave(4, S::Veteran, 4000, -6000, 2400)};
        m.objectives = {destroy(1, "DESTROY GUN 1-4"), air("DESTROY THE FIGHTERS", {})};
        m.objectives[0].wavesOnStart = {0};
    }
    {
        auto& m = add(36, "Oil Field Recapture", "Sink the boats holding our oil rigs. Don't hit the rigs!");
        m.startX = 4000; m.startZ = -9000; m.startAlt = 1500;
        m.protectGroup = 9;
        m.briefing = "Enemy boats are holding OUR oil rigs. Sink the boats, do not damage the rigs.";
        m.debrief = "Rigs recaptured intact. Precise work.";
        for (const auto& r : {std::pair<double, double>{11200, -9000}, {12600, -7000}, {11000, -5200}}) {
            TargetDef t = tgt(K::OilRig, r.first, r.second, "FRIENDLY RIG", 9);
            t.team = Team::Blue; m.targets.push_back(t);
        }
        for (int i = 0; i < 3; ++i) {
            TargetDef b = tgt(K::Ship, 11600, -7100, "BOAT " + std::to_string(i + 1), 1);
            b.pathRadius = 900 + 150 * i; b.pathAngle = i * 2.0; m.targets.push_back(b);
        }
        m.objectives = {destroy(1, "SINK 3 BOATS", "Friendly rigs are marked blue. ATS splash damages anything within 45 m.")};
    }
    {
        auto& m = add(37, "Strategic Strike", "Air defences, then the command centre, then the fighters.");
        m.startX = -9000; m.startZ = -8000; m.startAlt = 1500;
        m.briefing = "Three phases: air defences, command centre and fuel, then whatever comes up to meet you.";
        m.debrief = "Strategic targets destroyed. The war just got shorter.";
        m.targets.push_back(tgt(K::Radar, -4700, -11500, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, -5200, -11000, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, -4100, -11200, "SAM 2", 1));
        m.targets.push_back(tgt(K::Command, -1000, -12500, "COMMAND POST", 2));
        ring(m.targets, K::FuelTank, "FUEL TANK", 3, -600, -12900, 55, 2);
        m.waves = {wave(3, S::Veteran, 3000, -10000, 2400), wave(1, S::Ace, 3000, -12000, 2800, 300)};
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-2"), destroy(2, "DESTROY COMMAND POST AND FUEL"), air("DESTROY THE FIGHTERS", {0, 1})};
    }
    {
        auto& m = add(38, "Last Line", "Four escalating waves. Nothing gets through.");
        m.startX = -7000; m.startZ = 2000; m.startAlt = 2200; m.allies = 2;
        m.briefing = "This is the last line. Four waves, each bigger than the last.";
        m.debrief = "The line held. Because of you.";
        m.waves = {wave(3, S::Veteran, 0, 1000), wave(4, S::Veteran, 1000, -2000), wave(4, S::Veteran, 1000, 3000, 2600),
                   wave(5, S::Ace, 2000, 0, 2800, 1800)};
        m.objectives = {air("WAVE 1", {0}), air("WAVE 2", {1}), air("WAVE 3", {2}), air("FINAL WAVE", {3})};
    }
    {
        auto& m = add(39, "Shadow Squadron", "Four aces. The enemy's elite squadron.");
        m.startX = -6000; m.startZ = -2000; m.startAlt = 2800; m.allies = 1;
        m.briefing = "Shadow Squadron: four aces. Stay with your wingman and use every flare.";
        m.debrief = "Shadow Squadron is no more.";
        m.waves = {wave(4, S::Ace, 2000, -2000, 2900, 1200)};
        m.objectives = {air("DESTROY SHADOW SQUADRON", {0})};
    }
    {
        auto& m = add(40, "Final Assault", "SEAD, oil rigs, the fortress, the last fighters, then land at home.");
        m.startX = -2000; m.startZ = -6000; m.startAlt = 1600; m.allies = 2;
        m.briefing = "Everything we've trained for. Air defences, the rigs, the command post, their last fighters. Then bring it home.";
        m.debrief = "Mission accomplished. Welcome home, Skyfall One. The war is over.";
        m.targets.push_back(tgt(K::Radar, 3500, -8200, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 3000, -8700, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 4100, -8600, "SAM 2", 1));
        m.targets.push_back(tgt(K::OilRig, 11500, -6000, "OIL RIG A", 2));
        m.targets.push_back(tgt(K::OilRig, 12500, -3200, "OIL RIG B", 2));
        m.targets.push_back(tgt(K::Command, 1050, -1913, "COMMAND POST", 3));
        ring(m.targets, K::Bunker, "BUNKER", 3, 1050, -1913, 170, 3, true, 0.3);
        ring(m.targets, K::Gun, "GUN", 3, 1050, -1913, 450, 0, true, 1.0);
        m.waves = {wave(4, S::Veteran, 4000, 0, 2400), wave(1, S::Ace, 5000, 1000, 2800, 300)};
        m.objectives = {destroy(1, "SEAD: RADAR, SAM 1-2"), destroy(2, "DESTROY OIL RIGS A AND B"),
                        destroy(3, "DESTROY COMMAND POST AND BUNKERS"), air("DESTROY THE LAST FIGHTERS", {0, 1})};
        landAtHome(m);
    }
    // ---------------- Refuelling and critical infrastructure ----------------
    {
        auto& m = add(41, "Tanker Rendezvous", "Low on fuel: join the tanker and refuel in the air.");
        m.startX = -7000; m.startZ = -1500; m.startAlt = 2600; m.startSpeed = 200; m.startFuel = 0.12;
        m.tanker = true; m.tankerX = -1500; m.tankerZ = -2500; m.tankerAlt = 3000; m.tankerRadius = 6500; m.tankerSpeed = 175;
        m.briefing = "You're running on fumes after the night patrol. Texaco Two One is orbiting at three thousand metres. Join up behind the basket and take on fuel.";
        m.debrief = "Tanks full. Thanks for the gas, Texaco. Nicely done, Captain.";
        ObjectiveDef join; join.kind = ObjectiveKind::Rendezvous; join.title = "JOIN THE TANKER"; join.radius = 1200;
        join.instruction = "Fly to the tanker (TANKER marker). Approach from behind and slightly below."; join.hint = "FOLLOW THE MARKER";
        ObjectiveDef fill; fill.kind = ObjectiveKind::Refuel; fill.title = "REFUEL TO 90 %"; fill.value = 0.9;
        fill.instruction = "Put your nose probe in the basket behind the tanker: match its speed (throttle), follow the arrows. Hold contact while fuel flows.";
        fill.hint = "W / S = MATCH SPEED";
        m.objectives = {join, fill, waypoint("RESUME PATROL", 6000, -6000, 900)};
    }
    {
        auto& m = add(42, "Lights Out", "Knock out the power station and its substations.");
        m.startX = -9500; m.startZ = -9500; m.startAlt = 1600;
        m.briefing = "This power station feeds the enemy's air-defence network. Three hits will drop the main plant; then the substations.";
        m.debrief = "The grid is down. Their radars are running on batteries now.";
        m.targets.push_back(tgt(K::PowerPlant, -5000, -9200, "POWER PLANT", 1));
        m.targets.push_back(tgt(K::Substation, -4600, -8600, "SUBSTATION 1", 1));
        m.targets.push_back(tgt(K::Substation, -5500, -8700, "SUBSTATION 2", 1));
        line(m.targets, K::Gun, "GUN", 2, -5000, -9200, 600, 0.6, 0);
        m.targets.push_back(tgt(K::SAM, -4300, -9700, "SAM", 0));
        m.objectives = {destroy(1, "DESTROY POWER PLANT AND SUBSTATIONS", "The plant needs three ATS hits: it re-arms one missile every 10 s, so line up passes.")};
    }
    {
        auto& m = add(43, "Black Gold", "A coastal refinery fuels their jets. Burn it.");
        m.startX = 0; m.startZ = -12000; m.startAlt = 1500;
        m.briefing = "The coastal refinery produces all of their jet fuel. Columns first, then the storage tanks.";
        m.debrief = "The refinery is burning. Their air force is grounded for weeks.";
        m.targets.push_back(tgt(K::Refinery, 5393, -9748, "REFINERY", 1));
        ring(m.targets, K::FuelTank, "FUEL TANK", 4, 5393, -9298, 70, 1, true, 0.4);
        line(m.targets, K::Gun, "GUN", 3, 5393, -9548, 500, 1.4, 0);
        m.targets.push_back(tgt(K::SAM, 4693, -10148, "SAM", 0));
        m.objectives = {destroy(1, "DESTROY REFINERY AND 4 FUEL TANKS")};
    }
    {
        auto& m = add(44, "Cut the Line", "Drop the railway viaduct, then stop the supply train.");
        m.startX = 1000; m.startZ = 7000; m.startAlt = 2000; m.startHeading = -0.68;
        m.briefing = "Their supply line crosses the coastal viaduct. Drop the span, then catch the train before it reaches the town.";
        m.debrief = "Viaduct down and the train is scrap. That line is closed.";
        TargetDef br = tgt(K::Bridge, 0, 0, "RAIL VIADUCT", 1);
        br.anchor = TargetDef::Anchor::Bridge; m.targets.push_back(br);
        train(m.targets, 0.55, 14.0, 4, 2);
        m.objectives = {destroy(1, "DESTROY THE RAIL VIADUCT", "Bridges are tough: two ATS hits on the middle of the span."),
                        destroy(2, "STOP THE SUPPLY TRAIN", "Hit the locomotive first; the cannon works well on wagons.")};
    }
    {
        auto& m = add(45, "Silent Airwaves", "Three communications masts and a radar.");
        m.startX = -11000; m.startZ = 0; m.startAlt = 1600;
        m.briefing = "Three relay masts carry their command traffic. Drop all three and the radar, and they'll be deaf and blind.";
        m.debrief = "Comms are dark. Nobody over there knows where we are.";
        m.targets.push_back(tgt(K::CommTower, -7500, -6500, "COMMS TOWER 1", 1));
        m.targets.push_back(tgt(K::CommTower, -2000, -8000, "COMMS TOWER 2", 1));
        m.targets.push_back(tgt(K::CommTower, 3587, -6950, "COMMS TOWER 3", 1));
        m.targets.push_back(tgt(K::Radar, 3987, -7250, "RADAR", 1));
        m.targets.push_back(tgt(K::AAA, -2300, -8200, "", 0));
        m.objectives = {destroy(1, "DESTROY 3 COMMS TOWERS AND RADAR")};
    }
    {
        auto& m = add(46, "Iron Horse", "An armoured train is running ammunition to the front.");
        m.startX = -13000; m.startZ = -2000; m.startAlt = 1500; m.startHeading = 1.4;
        m.briefing = "An armoured train is heading east with ammunition. Locomotive and five wagons. Stop it before it reaches the city.";
        m.debrief = "The ammunition train went up like fireworks. Outstanding.";
        train(m.targets, 0.12, 16.0, 5, 1);
        m.targets.push_back(tgt(K::AAA, -6000, 2600, "", 0));
        m.objectives = {destroy(1, "DESTROY THE ARMOURED TRAIN")};
    }
    // ---------------- Missions 47-51: tactical (smarter enemy, defended targets) ----------------
    {
        auto& m = add(47, "Factory Floor", "The arms factories, gun-defended, with interceptors on call.");
        m.startX = -11000; m.startZ = -1500; m.startAlt = 1500; m.startHeading = 0.68;
        m.briefing = "The industrial zone is assembling missiles. Four production halls behind a gun line. Hit the first two and their interceptors will come for you: deal with them, then finish the job.";
        m.debrief = "Production lines destroyed and the interceptors with them. That's months of output gone.";
        for (int i = 0; i < 4; ++i) {
            TargetDef f = tgt(K::Factory, 0, 0, "FACTORY " + std::to_string(i + 1), i < 2 ? 1 : 2);
            f.anchor = TargetDef::Anchor::Industrial; f.index = i; m.targets.push_back(f);
        }
        m.targets.push_back(tgt(K::Gun, -4500, 3050, "GUN 1", 0));
        m.targets.push_back(tgt(K::Gun, -4500, 4450, "GUN 2", 0));
        m.targets.push_back(tgt(K::Gun, -5250, 3750, "GUN 3", 0));
        m.waves = {wave(2, AiSkill::Veteran, -1500, 1500, 1900)};
        m.objectives = {destroy(1, "DESTROY FACTORY 1-2", "Big halls take two ATS hits each; follow up with the cannon."),
                        air("SPLASH THE INTERCEPTORS", {0}, "Two interceptors scrambled. They break hard when you get behind them: stay patient, lock, fire."),
                        destroy(2, "DESTROY FACTORY 3-4", "Finish the last two halls.")};
    }
    {
        auto& m = add(48, "Grounded", "Catch their fighters on the ground, then beat the scramble.");
        m.startX = -10000; m.startZ = -12000; m.startAlt = 1800;
        m.briefing = "Six fighters are parked at a forward strip, fuelling up, with guns around it. Hit them on the ground. Two pairs will scramble from two directions, one after the other: watch both sides.";
        m.debrief = "Six kills on the ground and the scramble failed. Clean sweep.";
        line(m.targets, K::ParkedJet, "PARKED JET", 6, -4500, -12200, 60, 0.0, 1);
        line(m.targets, K::Hangar, "HANGAR", 2, -4500, -11900, 120, 0.0, 1);
        line(m.targets, K::Gun, "GUN", 2, -4500, -12050, 700, 0.0, 0);
        m.targets.push_back(tgt(K::Gun, -3900, -12500, "GUN 3", 0));
        m.waves = {wave(2, AiSkill::Veteran, -3000, -10000, 1800), wave(2, AiSkill::Veteran, -7000, -13500, 1600, 1200)};
        m.objectives = {destroy(1, "DESTROY 6 PARKED JETS AND 2 HANGARS"), air("SHOOT DOWN THE SCRAMBLE", {0}),
                        air("SHOOT DOWN THE SECOND PAIR", {1}, "The second pair is coming in from the south-west, low.")};
    }
    {
        auto& m = add(49, "Launch Window", "Four mobile missile launchers, a fighter screen, five and a half minutes.");
        m.startX = -9000; m.startZ = 7000; m.startAlt = 1600; m.timeLimit = 330;
        m.briefing = "Four mobile launchers are setting up to fire on our airbase, under flak and a two-ship fighter screen. You have five and a half minutes. They're moving, so lead them.";
        m.debrief = "All launchers destroyed with time to spare. The base is safe.";
        const double sites[4][2] = {{-4500, 5600}, {-3300, 4700}, {-2187, 5550}, {-900, 4700}};
        for (int i = 0; i < 4; ++i) {
            TargetDef l = tgt(K::Launcher, sites[i][0], sites[i][1], "LAUNCHER " + std::to_string(i + 1), 1);
            l.pathRadius = 220; l.pathAngle = i * 1.4; m.targets.push_back(l);
        }
        m.targets.push_back(tgt(K::Radar, -2500, 5000, "RADAR", 0));
        m.targets.push_back(tgt(K::AAA, -3000, 5300, "AAA 1", 0));
        m.targets.push_back(tgt(K::AAA, -1600, 5100, "AAA 2", 0));
        m.waves = {wave(2, AiSkill::Veteran, 1500, 8000, 2000)};
        m.objectives = {destroy(1, "DESTROY 4 LAUNCHERS (5:30)", "Fire ATS a little ahead of the moving launchers; the seeker leads them. Fighters will try to drag you off.")};
        m.objectives[0].wavesOnStart = {0};
    }
    {
        auto& m = add(50, "Ammunition Dump", "A SAM guards six bunkers full of munitions.");
        m.startX = 6000; m.startZ = 12000; m.startAlt = 1600;
        m.briefing = "The main ammunition dump: six bunkers, a SAM site on the hill to the south-west and guns around the dump. Kill the SAM first, then the bunkers. Expect big secondary explosions.";
        m.debrief = "The dump is gone. Felt that one from here.";
        for (int i = 0; i < 6; ++i)
            m.targets.push_back(tgt(K::Ammo, 4733 + (i % 3) * 110, 6350 + (i / 3) * 120, "AMMO BUNKER " + std::to_string(i + 1), 1, true, 0.0));
        line(m.targets, K::Gun, "GUN", 3, 4833, 6410, 520, 1.2, 0);
        m.targets.push_back(tgt(K::SAM, 4400, 6000, "SAM SITE", 2));
        m.objectives = {destroy(2, "DESTROY THE SAM SITE", "Lock the SAM from range with ATS (2). Flares (X) on the missile warning."),
                        destroy(1, "DESTROY 6 AMMO BUNKERS")};
    }
    {
        auto& m = add(51, "Critical Mass", "Power, communications, command, fighters, then home.");
        m.startX = -11000; m.startZ = -5000; m.startAlt = 1700; m.allies = 2;
        m.briefing = "This is it, Captain. Power plant, the relay masts with their escort, the SAM-covered command post, and whatever they launch. Then bring it home.";
        m.debrief = "Critical infrastructure destroyed, and you landed it. Mission accomplished, Captain.";
        m.targets.push_back(tgt(K::PowerPlant, -5000, -9200, "POWER PLANT", 1));
        m.targets.push_back(tgt(K::CommTower, -2000, -8000, "COMMS TOWER 1", 2));
        m.targets.push_back(tgt(K::CommTower, 3500, -7000, "COMMS TOWER 2", 2));
        m.targets.push_back(tgt(K::Command, 1050, -1913, "COMMAND POST", 3));
        ring(m.targets, K::Bunker, "BUNKER", 3, 1050, -1913, 170, 3, true, 0.3);
        ring(m.targets, K::Gun, "GUN", 3, 1050, -1913, 450, 0, true, 1.0);
        m.targets.push_back(tgt(K::SAM, 600, -2300, "SAM", 0));
        m.waves = {wave(2, AiSkill::Veteran, -3000, -4000, 2200), wave(2, AiSkill::Veteran, 3000, 0, 2400), wave(1, AiSkill::Ace, 4000, 1000, 2800, 300)};
        m.objectives = {destroy(1, "DESTROY THE POWER PLANT"), destroy(2, "DESTROY COMMS TOWERS 1-2"),
                        destroy(3, "DESTROY COMMAND POST AND BUNKERS"), air("DESTROY THE LAST FIGHTERS", {1, 2})};
        m.objectives[1].wavesOnStart = {0};
        landAtHome(m);
    }
    // ---------------- Carrier operations (52-61): catapult launches in a storm, hard ----------------
    {
        auto& m = add(52, "Cat Shot", "Your first catapult launch, in the rain, and a hot patrol station.");
        m.briefing = "Welcome aboard. Weather is closing in, but the deck is open. One catapult shot, climb out and take the patrol station. A pair of bandits is working the area: expect company.";
        m.debrief = "A clean shot, two kills and home. Welcome to carrier aviation, Captain.";
        m.waves = {wave(2, AiSkill::Veteran, 10500, -9800, 1700, 1500)};
        m.objectives = {simple(ObjectiveKind::Climb, "CLIMB TO 1200 M", "Keep climbing straight ahead to 1200 metres.", "PULL BACK GENTLY", 1200),
                        waypoint("PATROL STATION", 12500, -7000, 900),
                        air("SPLASH 2 BANDITS", {0}, "Two bandits, working as a pair: when one runs, the other is setting you up.")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(53, "Storm Front", "Two pairs of bandits are probing the fleet under the storm.");
        m.briefing = "Radar has two bandits low over the water, using the storm for cover, and a second pair is coming round from the west. Launch, climb, and splash all four before they find the fleet.";
        m.debrief = "Four kills in the rain. The fleet never even saw them.";
        m.waves = {wave(2, AiSkill::Veteran, 11500, -9000, 1500, 1200), wave(1, AiSkill::Veteran, 5000, -3000, 1800, 300),
                   wave(1, AiSkill::Rookie, 4000, -1500, 1600, 300)};
        m.objectives = {air("SPLASH THE FIRST PAIR", {0}), air("SPLASH THE SECOND PAIR", {1, 2})};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(54, "Picket Boats", "Four armed attack boats and their fighter cover.");
        m.briefing = "Four fast attack boats are shadowing us from the north, and they shoot back. Sink two, and their fighter cover will come down on you. Splash the fighters, then finish the boats.";
        m.debrief = "All four boats are on the bottom and the cover is gone. Good shooting.";
        for (int i = 0; i < 4; ++i) {
            TargetDef b = tgt(K::Ship, 12600, -6500, "BOAT " + std::to_string(i + 1), i % 2 == 0 ? 1 : 2);
            b.pathRadius = 600; b.pathAngle = i * 1.57; m.targets.push_back(b);
        }
        m.waves = {wave(2, AiSkill::Veteran, 9000, -9500, 1600)};
        m.objectives = {destroy(1, "SINK BOATS 1 AND 3", "Select ATS (2), lead the moving boats and fire. Keep moving: their deck guns track you."),
                        air("SPLASH THE FIGHTER COVER", {0}), destroy(2, "SINK BOATS 2 AND 4")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(55, "Coastal Radar", "Blind the coast: radar, two SAMs, then their interceptors.");
        m.briefing = "A coastal radar is cueing two SAM sites against the fleet, with gun pits around them. Take out the radar and both launchers. Once the coast is blind, they'll scramble interceptors.";
        m.debrief = "The coast is blind and their interceptors are down. The fleet can move in.";
        m.targets.push_back(tgt(K::Radar, 3087, -8150, "COASTAL RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 2587, -8650, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 1800, -8400, "SAM 2", 1));
        line(m.targets, K::Gun, "GUN", 2, 3087, -7550, 300, 0.0, 0);
        m.targets.push_back(tgt(K::Gun, 3300, -9100, "GUN 3", 0));
        m.waves = {wave(2, AiSkill::Veteran, 6000, -12500, 2000)};
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-2", "Lock from range with ATS (2). Flares (X) on the missile warning, then turn across the missile."),
                        air("SPLASH THE INTERCEPTORS", {0})};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(56, "Harbour Strike", "Boats in the bay, a fighter response, then fuel on the quay.");
        m.briefing = "The enemy harbour on the south-east coast is refuelling their navy. Sink the boats in the bay; that will bring their fighters out. Then burn the fuel tanks under the SAM umbrella.";
        m.debrief = "Harbour closed. Nothing is sailing out of there tonight.";
        for (int i = 0; i < 4; ++i) {
            TargetDef b = tgt(K::Ship, 10200, 9800, "BOAT " + std::to_string(i + 1), 1);
            b.pathRadius = 650; b.pathAngle = i * 1.57; m.targets.push_back(b);
        }
        ring(m.targets, K::FuelTank, "FUEL TANK", 4, 5850, 9760, 60, 2);
        m.targets.push_back(tgt(K::Gun, 6150, 9460, "GUN 1", 0));
        m.targets.push_back(tgt(K::SAM, 5500, 9500, "SAM", 0));
        m.waves = {wave(2, AiSkill::Veteran, 12500, 13000, 2000)};
        m.objectives = {destroy(1, "SINK 4 BOATS"), air("SPLASH THE HARBOUR FIGHTERS", {0}), destroy(2, "DESTROY 4 FUEL TANKS")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(57, "Fleet Defence", "Two coordinated raids are coming for the carrier.");
        m.allies = 1;
        m.briefing = "Two enemy raids are inbound on the carrier. The first from the north-west; the second, an ace and his wingman, will come from the south. You and your wingman are the only fighters up. Stop both raids.";
        m.debrief = "Both raids turned back and their ace is down. The carrier is untouched, thanks to you.";
        WaveDef ace = wave(1, AiSkill::Ace, 4000, 9500, 2600, 300);
        ace.callsign = "RAIDER LEAD";
        m.waves = {wave(2, AiSkill::Veteran, 6000, -6000, 1800, 1400), wave(1, AiSkill::Rookie, 7000, -7000, 1500, 600),
                   wave(1, AiSkill::Veteran, 6500, 8500, 2200, 600), ace};
        m.objectives = {air("STOP THE FIRST RAID", {0, 1}), air("STOP THE SECOND RAID", {2, 3})};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(58, "Offshore Platforms", "Two enemy rigs, armed boats and a fighter patrol.");
        m.briefing = "Two enemy platforms north of the fleet are fuelling their navy. Two armed patrol boats guard them, and a fighter patrol will come for you after the first rig. Destroy both rigs.";
        m.debrief = "Both platforms are burning. Their fuel supply is cut.";
        m.targets.push_back(tgt(K::OilRig, 11500, -6000, "OIL RIG A", 1));
        m.targets.push_back(tgt(K::OilRig, 12500, -3200, "OIL RIG B", 2));
        TargetDef boat = tgt(K::Ship, 11800, -4600, "PATROL BOAT 1", 0);
        boat.pathRadius = 500; m.targets.push_back(boat);
        TargetDef boat2 = tgt(K::Ship, 12600, -7600, "PATROL BOAT 2", 0);
        boat2.pathRadius = 400; boat2.pathAngle = 2.0; m.targets.push_back(boat2);
        m.waves = {wave(2, AiSkill::Veteran, 6000, 1500, 1800)};
        m.objectives = {destroy(1, "DESTROY OIL RIG A", "Rigs take two ATS hits each."), air("SPLASH THE RIG PATROL", {0}),
                        destroy(2, "DESTROY OIL RIG B")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(59, "Beachhead", "Artillery under a SAM, fighters, then the refinery.");
        m.briefing = "Our marines go ashore in an hour. Artillery is dug in behind the coast under guns and a SAM, and a refinery feeds their trucks. Clear the guns, beat the fighters they send, then burn the refinery.";
        m.debrief = "The beach is clear. The landing force is on its way in.";
        line(m.targets, K::Artillery, "ARTILLERY", 4, 5586, -9246, 140, 0.0, 1);
        m.targets.push_back(tgt(K::Refinery, 5586, -9696, "REFINERY", 2));
        m.targets.push_back(tgt(K::Gun, 5300, -9000, "GUN 1", 0));
        m.targets.push_back(tgt(K::Gun, 5900, -9000, "GUN 2", 0));
        m.targets.push_back(tgt(K::SAM, 5200, -9600, "SAM", 0));
        m.waves = {wave(2, AiSkill::Veteran, 2000, -12500, 2000)};
        m.objectives = {destroy(1, "DESTROY 4 ARTILLERY PIECES"), air("SPLASH THE COUNTER-AIR", {0}), destroy(2, "DESTROY THE REFINERY")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(60, "Bridge Breaker", "Drop the defended viaduct, then outrun the pursuit.");
        m.briefing = "Their reinforcements have one way to the coast: the railway viaduct inland, under guns and a fighter patrol. Drop it. Then their ace and his wingman will chase you home: deal with them before you land.";
        m.debrief = "The viaduct is in the river and their ace with it. The coast is cut off.";
        TargetDef br = tgt(K::Bridge, 0, 0, "RAIL VIADUCT", 1);
        br.anchor = TargetDef::Anchor::Bridge; m.targets.push_back(br);
        m.targets.push_back(tgt(K::Gun, 6500, 2600, "GUN 1", 0));
        m.targets.push_back(tgt(K::Gun, 6600, 1900, "GUN 2", 0));
        m.targets.push_back(tgt(K::AAA, 7200, 2600, "AAA", 0));
        WaveDef shrike = wave(1, AiSkill::Ace, 1500, -2000, 2400, 300);
        shrike.callsign = "SHRIKE LEAD";
        m.waves = {wave(2, AiSkill::Veteran, -1500, 4500, 2000), shrike, wave(1, AiSkill::Veteran, 2500, -3000, 2200, 300)};
        m.objectives = {destroy(1, "DESTROY THE RAIL VIADUCT", "Bridges take several hits: ATS from range, then the cannon."),
                        air("SPLASH THE PURSUIT", {1, 2}, "Their ace and his wingman are on your tail. They fly as a pair and fire from range: turn across their missiles, then fight.")};
        m.objectives[0].wavesOnStart = {0};
        m.objectives[1].instruction += " Their patrol is still up: it counts too.";
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(61, "Typhoon", "The final battle: the SAM belt, two counterattacks, the command post and Typhoon flight.");
        m.allies = 2;
        m.briefing = "Final strike from the boat, Captain. Break their coastal SAM belt, beat the counterattacks and destroy the command post. Then their two best pilots, Typhoon flight, will come for you. Your two wingmen are with you. End it, and bring it home to the carrier.";
        m.debrief = "Typhoon flight is down, the command post is gone and the sky is ours. Welcome back aboard, Captain. Outstanding.";
        m.targets.push_back(tgt(K::Radar, 3087, -8150, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 2587, -8650, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 3687, -8550, "SAM 2", 1));
        m.targets.push_back(tgt(K::SAM, 1800, -8400, "SAM 3", 1));
        m.targets.push_back(tgt(K::Gun, 2800, -7800, "GUN 1", 0));
        m.targets.push_back(tgt(K::Gun, 3600, -7800, "GUN 2", 0));
        m.targets.push_back(tgt(K::Gun, 3300, -9100, "GUN 3", 0));
        m.targets.push_back(tgt(K::Command, 1050, -1913, "COMMAND POST", 2));
        ring(m.targets, K::Bunker, "BUNKER", 3, 1050, -1913, 170, 2, true, 0.3);
        ring(m.targets, K::Gun, "FLAK", 3, 1050, -1913, 450, 0, true, 1.0);
        m.targets.push_back(tgt(K::SAM, 600, -2300, "SAM 4", 0));
        WaveDef lead = wave(1, AiSkill::Ace, 4000, 1500, 2900, 300);
        lead.callsign = "TYPHOON LEAD";
        WaveDef two = wave(1, AiSkill::Ace, 5000, 2500, 2700, 300);
        two.callsign = "TYPHOON 2";
        m.waves = {wave(2, AiSkill::Veteran, 6000, -12500, 2000),
                   wave(2, AiSkill::Veteran, 9500, -2000, 1800), wave(1, AiSkill::Veteran, -7000, -6000, 2200, 300),
                   lead, two};
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-3", "Three SAMs cover each other: attack from range and turn across every missile."),
                        air("BREAK THE FIRST COUNTERATTACK", {0}, "A pair is coming off the coast. Splash them before you go inland."),
                        destroy(2, "DESTROY COMMAND POST AND BUNKERS", "Flak and a SAM ring the command post. Get in, hit it, get out."),
                        air("BREAK THE SECOND COUNTERATTACK", {1, 2}, "Three fighters from two directions. Take them one at a time."),
                        air("DEFEAT TYPHOON FLIGHT", {3, 4}, "Their best two pilots. They fight as a pair and never fly straight: stay fast, use your wingman.")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    for (auto& m : v) if (m.atsReload <= 0) m.atsReload = 10.0;
    // difficulty: 1-46 classic, 47-51 tactical, 52-60 squadron tactics, 61 the final battle
    for (auto& m : v)
        if (m.tactics < 0) m.tactics = m.number >= 61 ? 3 : (m.number >= 52 ? 2 : (m.number >= 47 ? 1 : 0));
    return v;
}

double horizDist(const Vec3& a, double x, double z) { return std::hypot(a.x - x, a.z - z); }
}  // namespace

DeckInfo campaignCarrierDeck() {
    DeckInfo d;
    d.active = true;
    d.center = {13600.0, 19.5, 1500.0};
    d.heading = -kPi / 2;   // bow to the north (-Z)
    return d;
}

const std::vector<CampaignMissionDef>& campaignMissions() {
    static const std::vector<CampaignMissionDef> k = buildAll();
    return k;
}

const std::vector<MissionInfo>& campaignCatalogue() {
    static const std::vector<MissionInfo> k = [] {
        std::vector<MissionInfo> v = {
            {1, "Cold Start & Takeoff", "Start engine, release brakes, throttle, rotate, climb, gear up.", true},
            {2, "First Solo Flight", "Pitch, roll, coordinated turns, climb and level-off.", true},
            {3, "Runway Landing", "Approach, gear, glide path, touchdown and braking.", true},
        };
        for (const auto& m : campaignMissions()) v.push_back({m.number, m.title, m.summary, true});
        return v;
    }();
    return k;
}

const CampaignMissionDef* campaignMission(int number) {
    for (const auto& m : campaignMissions()) if (m.number == number) return &m;
    return nullptr;
}

bool objectivePoint(const CampaignMissionDef& def, const ObjectiveDef& o, const Battle* battle, Vec3& out) {
    if (o.kind == ObjectiveKind::Waypoint) { out = {o.x, 0, o.z}; return true; }
    if (o.kind == ObjectiveKind::Touchdown || o.kind == ObjectiveKind::Stop) { out = {-10500, 0, 3000}; return true; }
    if ((o.kind == ObjectiveKind::Rendezvous || o.kind == ObjectiveKind::Refuel) && battle && battle->tanker().active) {
        out = battle->tanker().pos;
        return true;
    }
    if (o.kind == ObjectiveKind::DestroyGroup && battle) {
        Vec3 sum; int n = 0;
        for (const GroundTarget& g : battle->ground())
            if (g.group == o.group && g.alive) { sum += g.pos; ++n; }
        if (n == 0) return false;
        out = sum * (1.0 / n);
        return true;
    }
    (void)def;
    return false;
}

double campaignStartHeading(const CampaignMissionDef& def) {
    if (def.startHeading < 1e8) return def.startHeading;
    for (const ObjectiveDef& o : def.objectives) {
        double x = 0, z = 0;
        bool have = false;
        if (o.kind == ObjectiveKind::Waypoint) { x = o.x; z = o.z; have = true; }
        else if (o.kind == ObjectiveKind::DestroyGroup) {
            int n = 0;
            for (const TargetDef& t : def.targets) if (t.group == o.group) { x += t.x; z += t.z; ++n; }
            if (n) { x /= n; z /= n; have = true; }
        } else if (o.kind == ObjectiveKind::DestroyAir && !o.wavesOnStart.empty()) {
            const WaveDef& w = def.waves[o.wavesOnStart[0]];
            x = w.x; z = w.z; have = true;
        }
        if (have) return std::atan2(z - def.startZ, x - def.startX);
    }
    return 0.0;
}

Mission buildCampaignMission(const CampaignMissionDef& def) {
    std::vector<MissionStep> steps;
    // Waves spawned by objectives up to and including index i (each spawn bumps Battle::wave()).
    int cumulativeWaves = 0;
    for (const ObjectiveDef& o : def.objectives) {
        cumulativeWaves += static_cast<int>(o.wavesOnStart.size());
        MissionStep s;
        s.title = o.title;
        s.instruction = o.instruction;
        s.keyHint = o.hint;
        switch (o.kind) {
            case ObjectiveKind::Waypoint: {
                double x = o.x, z = o.z, r = o.radius, maxAgl = o.maxAgl;
                s.done = [x, z, r, maxAgl](const MissionContext& c) {
                    return horizDist(c.flight->pos, x, z) < r && (maxAgl <= 0 || c.flight->agl < maxAgl) && !c.flight->onGround;
                };
                s.progress = [x, z, r](const MissionContext& c) {
                    return clampd(1.0 - (horizDist(c.flight->pos, x, z) - r) / 9000.0, 0.0, 1.0);
                };
                break;
            }
            case ObjectiveKind::DestroyGroup: {
                int g = o.group;
                s.done = [g](const MissionContext& c) { return c.battle && c.battle->groundTotal(g) > 0 && c.battle->groundAlive(g) == 0; };
                s.progress = [g](const MissionContext& c) {
                    if (!c.battle) return 0.0;
                    int t = c.battle->groundTotal(g);
                    return t > 0 ? 1.0 - static_cast<double>(c.battle->groundAlive(g)) / t : 0.0;
                };
                break;
            }
            case ObjectiveKind::DestroyAir: {
                int need = cumulativeWaves;
                s.done = [need](const MissionContext& c) { return c.battle && c.battle->wave() >= need && c.battle->enemiesAlive() == 0; };
                s.progress = nullptr;
                break;
            }
            case ObjectiveKind::PullG: {
                double gv = o.value;
                s.done = [gv](const MissionContext& c) { return c.flight->gLoad >= gv && !c.flight->onGround; };
                s.holdSeconds = o.hold;
                s.progress = [gv](const MissionContext& c) { return clampd(c.flight->gLoad / gv, 0.0, 1.0); };
                break;
            }
            case ObjectiveKind::Loop: {
                auto acc = std::make_shared<double>(0.0);
                s.done = [acc](const MissionContext& c) {
                    if (c.flight->onGround) { *acc = 0; return false; }
                    *acc = std::max(0.0, *acc + c.flight->omega.z * FlightModel::kDt);
                    return *acc >= 2.0 * kPi * 0.92;
                };
                s.progress = [acc](const MissionContext&) { return clampd(*acc / (2.0 * kPi * 0.92), 0.0, 1.0); };
                break;
            }
            case ObjectiveKind::AileronRoll: {
                auto acc = std::make_shared<double>(0.0);
                s.done = [acc](const MissionContext& c) {
                    *acc += std::fabs(c.flight->omega.x) * FlightModel::kDt;
                    return *acc >= 2.0 * kPi * 0.95;
                };
                s.progress = [acc](const MissionContext&) { return clampd(*acc / (2.0 * kPi * 0.95), 0.0, 1.0); };
                break;
            }
            case ObjectiveKind::Touchdown:
                s.done = [](const MissionContext& c) { return c.flight->onGround && c.terrain->onRunway(c.flight->pos.x, c.flight->pos.z, 5.0); };
                break;
            case ObjectiveKind::Stop:
                s.done = [](const MissionContext& c) { return c.flight->onGround && c.flight->airspeed < 2.0; };
                s.progress = [](const MissionContext& c) { return clampd(1.0 - c.flight->airspeed / 80.0, 0.0, 1.0); };
                break;
            case ObjectiveKind::Rendezvous: {
                double r = o.radius;
                s.done = [r](const MissionContext& c) {
                    return c.battle && c.battle->tanker().active && length(c.flight->pos - c.battle->tanker().pos) < r;
                };
                s.progress = [r](const MissionContext& c) {
                    if (!c.battle || !c.battle->tanker().active) return 0.0;
                    return clampd(1.0 - (length(c.flight->pos - c.battle->tanker().pos) - r) / 12000.0, 0.0, 1.0);
                };
                break;
            }
            case ObjectiveKind::Refuel: {
                double f = o.value;
                s.done = [f](const MissionContext& c) { return c.flight->fuelKg >= f * c.aircraft->fuelMassKg - 1e-6; };
                s.progress = [f](const MissionContext& c) { return clampd(c.flight->fuelKg / (f * c.aircraft->fuelMassKg), 0.0, 1.0); };
                break;
            }
            case ObjectiveKind::FullPower: {
                double v = o.value;
                s.done = [v](const MissionContext& c) { return c.flight->throttle >= v - 1e-3 && c.flight->engine == EngineState::Running; };
                s.holdSeconds = o.hold;
                s.progress = [v](const MissionContext& c) { return clampd(c.flight->throttle / v, 0.0, 1.0); };
                break;
            }
            case ObjectiveKind::Launch:
                s.done = [](const MissionContext& c) { return c.flight->launched && !c.flight->onGround && c.flight->agl > 25.0; };
                s.progress = [](const MissionContext& c) {
                    if (c.flight->launched) return 1.0;
                    return c.flight->catFiring ? 0.5 + 0.5 * clampd(c.flight->catTravel / 100.0, 0.0, 1.0) : 0.0;
                };
                break;
            case ObjectiveKind::GearUp:
                s.done = [](const MissionContext& c) { return !c.flight->onGround && !c.flight->gearDown && c.flight->gearPos < 0.05; };
                s.progress = [](const MissionContext& c) { return c.flight->gearDown ? 0.0 : 1.0 - c.flight->gearPos; };
                break;
            case ObjectiveKind::Climb: {
                double alt = o.value;
                s.done = [alt](const MissionContext& c) { return c.flight->pos.y >= alt && !c.flight->onGround; };
                s.progress = [alt](const MissionContext& c) { return clampd(c.flight->pos.y / alt, 0.0, 1.0); };
                break;
            }
        }
        steps.push_back(std::move(s));
    }
    char id[8];
    std::snprintf(id, sizeof id, "C%02d", def.number);
    Mission m(id, def.title, std::move(steps));
    if (def.protectGroup > 0) {
        int g = def.protectGroup;
        m.addFailCondition([g](const MissionContext& c) -> std::string {
            if (c.battle && c.battle->groundTotal(g) > c.battle->groundAlive(g)) return "A friendly asset was destroyed";
            return "";
        });
    }
    if (def.timeLimit > 0) {
        // evaluated with the mission's own clock through a shared counter
        auto t = std::make_shared<double>(0.0);
        double limit = def.timeLimit;
        m.addFailCondition([t, limit](const MissionContext&) -> std::string {
            *t += FlightModel::kDt;
            return *t > limit ? "Out of time" : "";
        });
    }
    return m;
}

void CampaignDirector::begin(const CampaignMissionDef& def, Battle& battle, unsigned seedSalt) {
    def_ = &def;
    lastStep_ = static_cast<size_t>(-1);
    BattleConfig cfg;
    cfg.enemyJets = 0;
    cfg.allyJets = def.allies;
    cfg.groundTargets = false;
    cfg.endlessWaves = false;
    cfg.seed = 1000u + static_cast<unsigned>(def.number) + seedSalt * 7919u;
    cfg.agReloadSeconds = def.atsReload;
    cfg.tactics = std::max(0, def.tactics);
    battle.start(cfg);
    const WorldLayout* L = battle.layout();
    if (def.tanker) battle.startTanker({def.tankerX, def.tankerAlt, def.tankerZ}, def.tankerRadius, def.tankerSpeed, 3.4);
    for (const TargetDef& t0 : def.targets) {
        TargetDef t = t0;
        GroundTarget g;
        if (t.anchor == TargetDef::Anchor::Railway) {
            if (!L || L->railway.pts.empty()) continue;   // no railway on this terrain
            g.railS = clampd(t.railFraction, 0.0, 1.0) * L->railway.length();
            g.railSpeed = t.railSpeed;
            Vec3 p = L->railway.at(g.railS);
            t.x = p.x; t.z = p.z;
        } else if (t.anchor == TargetDef::Anchor::Bridge) {
            int bi = L ? L->largestBridge(true) : -1;
            if (bi < 0 && L) bi = L->largestBridge(false);
            if (bi < 0) continue;
            const Bridge& b = L->bridges[bi];
            Vec3 mid = (b.a + b.b) * 0.5;
            g.pos = mid;
            g.length = std::hypot(b.b.x - b.a.x, b.b.z - b.a.z);
            t.x = mid.x; t.z = mid.z;
            t.heading = std::atan2(b.b.z - b.a.z, b.b.x - b.a.x);
        } else if (t.anchor == TargetDef::Anchor::Industrial) {
            if (!L || t.index >= static_cast<int>(L->factories.size())) continue;
            t.x = L->factories[t.index].x; t.z = L->factories[t.index].z;
        }
        g.kind = t.kind;
        if (t.anchor != TargetDef::Anchor::Bridge) g.pos = {t.x, 0, t.z};
        g.heading = t.heading;
        g.label = t.label;
        g.group = t.group;
        g.armed = t.armed;
        g.team = t.team;
        g.pathRadius = t.pathRadius;
        g.pathAngle = t.pathAngle;
        g.pathCenter = {t.x, 0, t.z};
        if (t.pathRadius > 0) g.pos = {t.x + std::cos(t.pathAngle) * t.pathRadius, 0, t.z + std::sin(t.pathAngle) * t.pathRadius};
        battle.addGround(g);
    }
}

void CampaignDirector::spawnFor(size_t objective, Battle& battle) {
    if (!def_ || objective >= def_->objectives.size()) return;
    for (int wi : def_->objectives[objective].wavesOnStart) {
        if (wi < 0 || wi >= static_cast<int>(def_->waves.size())) continue;
        const WaveDef& w = def_->waves[wi];
        battle.spawnEnemies(w.count, w.skill, {w.x, 0, w.z}, w.radius, w.altitude, w.unarmed, w.callsign);
    }
}

void CampaignDirector::update(const Mission& mission, Battle& battle) {
    if (!def_) return;
    size_t idx = mission.stepIndex();
    if (idx == lastStep_) return;
    // objectives may complete several at once: spawn the waves of every objective that has begun
    size_t from = lastStep_ == static_cast<size_t>(-1) ? 0 : lastStep_ + 1;
    for (size_t i = from; i <= idx && i < def_->objectives.size(); ++i) spawnFor(i, battle);
    lastStep_ = idx;
}

}  // namespace sky
