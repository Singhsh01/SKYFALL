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

// Home runway landing (approach gate, touchdown, stop) appended to a mission.
void landAtHome(CampaignMissionDef& m) {
    m.objectives.push_back(waypoint("APPROACH GATE", -13500, 3000, 900, 0,
                                    "Fly to the approach gate 3 km west of runway 09, then line up on heading 090."));
    m.objectives.push_back(simple(ObjectiveKind::Touchdown, "TOUCHDOWN", "Gear down (G), fly the glide path and touch down on runway 09.", "G = GEAR"));
    m.objectives.push_back(simple(ObjectiveKind::Stop, "BRAKE TO A STOP", "Throttle idle and hold the brakes until stopped.", "S + LEFT CTRL"));
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
        m.targets.push_back(tgt(K::Radar, -3500, -11500, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, -4000, -11000, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, -2900, -11200, "SAM 2", 1));
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
    for (auto& m : v) if (m.atsReload <= 0) m.atsReload = 10.0;
    return v;
}

double horizDist(const Vec3& a, double x, double z) { return std::hypot(a.x - x, a.z - z); }
}  // namespace

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

void CampaignDirector::begin(const CampaignMissionDef& def, Battle& battle) {
    def_ = &def;
    lastStep_ = static_cast<size_t>(-1);
    BattleConfig cfg;
    cfg.enemyJets = 0;
    cfg.allyJets = def.allies;
    cfg.groundTargets = false;
    cfg.endlessWaves = false;
    cfg.seed = 1000u + static_cast<unsigned>(def.number);
    cfg.agReloadSeconds = def.atsReload;
    battle.start(cfg);
    for (const TargetDef& t : def.targets) {
        GroundTarget g;
        g.kind = t.kind;
        g.pos = {t.x, 0, t.z};
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
        battle.spawnEnemies(w.count, w.skill, {w.x, 0, w.z}, w.radius, w.altitude, w.unarmed);
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
