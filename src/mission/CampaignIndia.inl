// Campaign missions 62-100: the India theatres (v1.1). Included by Campaign.cpp inside its
// anonymous namespace, so the mission-building helpers are shared.
//
//   62-75  Urban operations      Mumbai, Chennai, Bengaluru, Kochi, Visakhapatnam, Guwahati
//   76-85  Himalayan operations  Ladakh, Nubra, Kashmir, Himachal, Uttarakhand, Gangotri, Tawang, Sikkim
//   86-95  Coastal and naval     the Arabian Sea and the Bay of Bengal, carrier operations
//   96-100 The final campaign    ending with mission 100, "SKYFALL - Final Air Superiority"
//
// Every target that may be destroyed is a military one: armour, air defences, artillery,
// launchers, command posts, helicopters, warships, airbases. Hospitals, schools, homes, stations
// and the monastery are PROTECTED SITES (blue): each one lost costs score, and in the urban
// missions losing one fails the mission. Air-to-surface missiles damage anything within 45 m,
// so the protected sites are never closer than ~80 m to a target, but a miss can still hit one.
//
// Coordinates are written for the Skyfall Desert like the rest of the campaign (airfield in the
// west, the sea to the east) and adapted to the location; city targets are anchored to the
// location's main city (offsets in city radii, snapped onto its streets).

constexpr int kCiv = 8;   // the protected-site group of every mission

TargetDef cityT(K k, double ox, double oz, std::string label, int group, bool armed = true) {
    TargetDef t = tgt(k, ox, oz, std::move(label), group, armed);
    t.anchor = TargetDef::Anchor::City;
    return t;
}

TargetDef civ(double ox, double oz, std::string label) {
    TargetDef t = cityT(K::Civilian, ox, oz, std::move(label), kCiv, false);
    t.team = Team::Blue;
    return t;
}

TargetDef damT(K k, double along, double across, std::string label, int group, bool armed = true) {
    TargetDef t = tgt(k, along, across, std::move(label), group, armed);
    t.anchor = TargetDef::Anchor::Dam;
    return t;
}

/// A helicopter flying a circle of `r` metres around (x, z) at `agl` metres above the ground.
TargetDef heli(double x, double z, double r, double agl, std::string label, int group, double phase = 0, bool city = false) {
    TargetDef t = tgt(K::Helicopter, x, z, std::move(label), group);
    t.pathRadius = r;
    t.pathAngle = phase;
    t.flyAgl = agl;
    if (city) t.anchor = TargetDef::Anchor::City;
    return t;
}

ObjectiveDef cityPoint(const std::string& title, double ox, double oz, double radius = 900, double maxAgl = 0, const std::string& instr = "") {
    ObjectiveDef o = waypoint(title, ox, oz, radius, maxAgl, instr);
    o.cityAnchor = true;
    return o;
}

void protectCivilians(CampaignMissionDef& m, int limit) {
    m.civilianGroup = kCiv;
    m.civilianLimit = limit;
}

void addIndiaMissions(std::vector<CampaignMissionDef>& v) {
    auto add = [&](int n, const std::string& home, const std::string& title, const std::string& summary) -> CampaignMissionDef& {
        v.emplace_back();
        CampaignMissionDef& m = v.back();
        m.number = n; m.title = title; m.summary = summary; m.homeLocation = home;
        return m;
    };
    using S = AiSkill;
    const char* kNoStrike = "Protected sites are marked blue: hospitals, schools, homes. ATS damages anything within 45 m, so attack along the street, not across it.";

    // =============================== Urban operations (62-75) ===============================
    {
        auto& m = add(62, "mumbai", "Harbour Lights", "Mobile SAMs have moved into the docks beside a hospital. Precision only.");
        m.startX = -6500; m.startZ = -1500; m.startAlt = 1600;
        m.briefing = "Skyfall One, the enemy has parked two mobile SAM launchers and their radar in the dock district, right beside the city hospital. They think we will not strike. Prove them wrong, without a scratch on that hospital.";
        m.debrief = "SAMs down, hospital untouched. The harbour is open again.";
        m.debriefClean = "Three clean hits and not a window broken at the hospital. That is how it is done, Skyfall One.";
        m.debriefCostly = "The SAMs are down, but we hit a protected site. The press will have that by morning.";
        m.targets.push_back(cityT(K::SAM, 0.42, -0.30, "SAM 1", 1));
        m.targets.push_back(cityT(K::SAM, 0.58, -0.18, "SAM 2", 1));
        m.targets.push_back(cityT(K::Radar, 0.50, -0.42, "RADAR", 1));
        m.targets.push_back(cityT(K::AAA, 0.30, -0.10, "AAA", 0));
        m.targets.push_back(civ(0.50, -0.06, "HOSPITAL"));
        m.targets.push_back(civ(0.66, -0.40, "SCHOOL"));
        m.objectives = {cityPoint("CITY INITIAL POINT", -0.9, 0.2, 1100, 0, "Fly to the initial point west of the city, then run in on the docks."),
                        destroy(1, "DESTROY SAM 1-2 AND RADAR", kNoStrike)};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(63, "mumbai", "Marine Drive", "Attack helicopters are hunting along the sea front. Splash them over open ground.");
        m.startX = -5000; m.startZ = 2500; m.startAlt = 1300;
        m.briefing = "Four enemy attack helicopters are working low along the sea front. Get down to their level, use the cannon or ATA missiles, and keep your shots away from the apartment blocks.";
        m.debrief = "All four helicopters down. The sea front is quiet.";
        m.debriefClean = "Four helicopters and no damage on the ground. Superb shooting.";
        m.debriefCostly = "The helicopters are down, but a protected building was hit. Check your backstops.";
        for (int i = 0; i < 4; ++i) m.targets.push_back(heli(0.35 + 0.15 * (i % 2), -0.1 + 0.25 * (i / 2), 300, 170 + 20 * i, "ATTACK HELO " + std::to_string(i + 1), 1, i * 1.6, true));
        m.targets.push_back(civ(0.2, 0.25, "APARTMENTS"));
        m.targets.push_back(civ(0.7, 0.05, "APARTMENTS"));
        m.objectives = {cityPoint("SEA FRONT", 0.3, 0.1, 1300, 0, "Drop to helicopter height over the sea front."),
                        destroy(1, "SPLASH 4 ATTACK HELICOPTERS", "Helicopters in the air lock with ATA (1); the cannon works too. Mind what is behind them.", "1 = ATA   SPACE = GUN")};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(64, "mumbai", "Skyline Hunt", "An armoured column is moving through the streets between the towers.");
        m.startX = -7000; m.startZ = -3000; m.startAlt = 1700;
        m.briefing = "Enemy armour is pushing through the business district. Five tanks and their supply trucks, strung along the avenues between the towers. The towers will mask your approach: pick your street, line up, and fire along it.";
        m.debrief = "The column is stopped. The district is ours.";
        m.debriefClean = "Every tank, no collateral. The ground commander sends his thanks.";
        m.debriefCostly = "Column destroyed, but we hit a protected building. We cannot afford that again.";
        const double tanks[5][2] = {{-0.10, -0.25}, {-0.10, -0.05}, {-0.10, 0.15}, {0.12, 0.30}, {0.32, 0.30}};
        for (int i = 0; i < 5; ++i) m.targets.push_back(cityT(K::Tank, tanks[i][0], tanks[i][1], "TANK " + std::to_string(i + 1), 1));
        m.targets.push_back(cityT(K::Truck, -0.30, -0.40, "SUPPLY TRUCK 1", 2));
        m.targets.push_back(cityT(K::Truck, -0.30, -0.55, "SUPPLY TRUCK 2", 2));
        m.targets.push_back(cityT(K::AAA, 0.0, 0.45, "AAA", 0));
        m.targets.push_back(civ(0.12, 0.0, "SCHOOL"));
        m.targets.push_back(civ(-0.30, 0.10, "APARTMENTS"));
        m.targets.push_back(civ(0.30, 0.55, "HOSPITAL"));
        m.waves = {wave(2, S::Veteran, 4000, -3000, 2300)};
        m.objectives = {destroy(1, "DESTROY 5 TANKS", kNoStrike), destroy(2, "DESTROY THE SUPPLY TRUCKS")};
        m.objectives[1].wavesLate = {{0, 90.0}};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(65, "mumbai", "Sea Link", "Bombers are running in on the city from the sea. Stop them before the coast.");
        m.startX = -2000; m.startZ = 1500; m.startAlt = 2600; m.allies = 1; m.timeLimit = 300;
        m.briefing = "A raid is inbound from the west over the sea: two strike pairs with escorts. Nothing crosses the coast. You have five minutes before they reach the city.";
        m.debrief = "The raid never made landfall. The city sleeps tonight because of you.";
        m.waves = {wave(2, S::Veteran, 9000, -3000, 2200), wave(2, S::Veteran, 9500, 3500, 2000), wave(2, S::Ace, 10500, 0, 2800, 600)};
        m.objectives = {air("STOP THE FIRST STRIKE PAIR", {0}), air("STOP THE SECOND STRIKE PAIR", {1}),
                        air("DEFEAT THE ESCORT", {2}, "The escort is two aces: they came for you, not the city.")};
    }
    {
        auto& m = add(66, "chennai", "Metro Shield", "Guns on the expressway are shelling the metro line. Silence them.");
        m.startX = -6000; m.startZ = 1000; m.startAlt = 1500;
        m.briefing = "Enemy anti-aircraft guns have set up on the expressway approaches and are firing on anything that flies. The metro stations below them are full of people sheltering. Take the guns one by one, along the road.";
        m.debrief = "Guns silenced. The metro is running again.";
        m.debriefClean = "Not one station touched. The city owes you, Skyfall One.";
        m.debriefCostly = "The guns are gone, but a station was hit. That will be remembered.";
        for (int i = 0; i < 4; ++i) m.targets.push_back(cityT(K::Gun, -0.45 + 0.3 * i, 0.18, "GUN " + std::to_string(i + 1), 1));
        m.targets.push_back(cityT(K::Radar, 0.2, 0.55, "FIRE CONTROL RADAR", 1));
        m.targets.push_back(civ(-0.30, 0.45, "METRO STATION"));
        m.targets.push_back(civ(0.30, -0.15, "METRO STATION"));
        m.targets.push_back(civ(-0.65, -0.15, "HOSPITAL"));
        m.objectives = {cityPoint("RUN-IN POINT", -1.1, 0.18, 1000), destroy(1, "DESTROY GUN 1-4 AND RADAR", kNoStrike)};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(67, "chennai", "Marina Beach", "Landing craft off the beach, armour on the sand.");
        m.startX = 3000; m.startZ = -6000; m.startAlt = 1500;
        m.briefing = "An enemy landing is under way: four landing craft off the beach and armour already ashore. Sink the boats first, then the tanks, before they reach the city streets.";
        m.debrief = "The landing is broken. Not one tank reached the city.";
        for (int i = 0; i < 4; ++i) {
            TargetDef b = tgt(K::Ship, 9900, -2500 + 900 * i, "LANDING CRAFT " + std::to_string(i + 1), 1);
            b.pathRadius = 350; b.pathAngle = i * 1.3; m.targets.push_back(b);
        }
        line(m.targets, K::Tank, "BEACH TANK", 4, 7200, 3000, 180, kPi / 2, 2);
        m.targets.push_back(tgt(K::AAA, 7000, 4300, "AAA", 0));
        m.waves = {wave(2, S::Veteran, 6000, -9000, 2200)};
        m.objectives = {destroy(1, "SINK 4 LANDING CRAFT"), destroy(2, "DESTROY 4 BEACH TANKS")};
        m.objectives[1].wavesOnStart = {0};
    }
    {
        auto& m = add(68, "chennai", "Container Port", "Missile launchers are hiding among the containers.");
        m.startX = 2000; m.startZ = 8000; m.startAlt = 1600; m.timeLimit = 360;
        m.briefing = "Three mobile launchers are hiding in the container yards by the port, setting up to fire on our airbase. Six minutes. The port workers' housing is right next door.";
        m.debrief = "Launchers destroyed before they could fire.";
        m.targets.push_back(tgt(K::Launcher, 7100, 2600, "LAUNCHER 1", 1));
        m.targets.push_back(tgt(K::Launcher, 7300, 3200, "LAUNCHER 2", 1));
        m.targets.push_back(tgt(K::Launcher, 6900, 3700, "LAUNCHER 3", 1));
        m.targets.push_back(tgt(K::Radar, 6600, 3100, "RADAR", 1));
        m.targets.push_back(tgt(K::AAA, 6500, 2700, "AAA 1", 0));
        m.targets.push_back(tgt(K::AAA, 6600, 3900, "AAA 2", 0));
        TargetDef h = tgt(K::Civilian, 7200, 4400, "WORKERS' HOUSING", kCiv, false);
        h.team = Team::Blue;
        m.targets.push_back(h);
        m.waves = {wave(2, S::Veteran, 3000, 9000, 2200)};
        m.objectives = {destroy(1, "DESTROY 3 LAUNCHERS AND RADAR (6:00)", "The launchers are 400-600 m apart; the housing is just north of the yard.")};
        m.objectives[0].wavesLate = {{0, 120.0}};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(69, "bengaluru", "Garden City", "An enemy command post has taken over a business park.");
        m.startX = -7000; m.startZ = 6000; m.startAlt = 1700;
        m.briefing = "The enemy's forward headquarters is in a business park in the middle of the city, with a SAM on the roof line and guns in the car parks. The hospital and two schools are within a kilometre. Precision, Skyfall One.";
        m.debrief = "Headquarters destroyed. Their command net has gone silent.";
        m.debriefClean = "A surgical strike. Not one protected site touched.";
        m.debriefCostly = "Headquarters destroyed, but at a cost we will have to answer for.";
        m.targets.push_back(cityT(K::SAM, -0.20, -0.30, "SAM", 1));
        m.targets.push_back(cityT(K::Command, 0.05, -0.05, "COMMAND POST", 2));
        m.targets.push_back(cityT(K::Bunker, 0.25, -0.05, "SIGNALS BUNKER", 2));
        m.targets.push_back(cityT(K::Gun, -0.15, 0.20, "GUN 1", 0));
        m.targets.push_back(cityT(K::Gun, 0.30, 0.25, "GUN 2", 0));
        m.targets.push_back(civ(0.05, 0.30, "HOSPITAL"));
        m.targets.push_back(civ(-0.40, -0.05, "SCHOOL"));
        m.targets.push_back(civ(0.45, -0.35, "SCHOOL"));
        m.waves = {wave(2, S::Veteran, 3000, -4000, 2400)};
        m.objectives = {destroy(1, "DESTROY THE SAM", kNoStrike), destroy(2, "DESTROY COMMAND POST AND BUNKER", kNoStrike),
                        air("DEFEAT THE RESPONSE", {0})};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(70, "bengaluru", "Silicon Plateau", "Four launchers are moving through the suburbs. Five minutes.");
        m.startX = -8000; m.startZ = -2000; m.startAlt = 1700; m.timeLimit = 300;
        m.briefing = "Four mobile missile launchers are driving through the eastern suburbs towards their firing points. They will be ready in five minutes. They are moving, so lead them, and watch the housing estates.";
        m.debrief = "All four launchers destroyed on the move.";
        for (int i = 0; i < 4; ++i) {
            TargetDef l = tgt(K::Launcher, 4700 + 700 * (i % 2), 3000 + 900 * (i / 2), "LAUNCHER " + std::to_string(i + 1), 1);
            l.pathRadius = 260; l.pathAngle = i * 1.5; m.targets.push_back(l);
        }
        m.targets.push_back(tgt(K::AAA, 5050, 3450, "AAA", 0));
        TargetDef c1 = tgt(K::Civilian, 5900, 2500, "HOUSING ESTATE", kCiv, false); c1.team = Team::Blue; m.targets.push_back(c1);
        TargetDef c2 = tgt(K::Civilian, 4300, 4700, "SCHOOL", kCiv, false); c2.team = Team::Blue; m.targets.push_back(c2);
        m.waves = {wave(2, S::Veteran, 9000, 2000, 2200)};
        m.objectives = {destroy(1, "DESTROY 4 LAUNCHERS (5:00)", "Fire a little ahead of the moving launchers. The estate and the school are marked blue.")};
        m.objectives[0].wavesLate = {{0, 100.0}};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(71, "bengaluru", "Lake District", "Artillery hidden by the lakes is shelling the city. Find it.");
        m.startX = -6000; m.startZ = 8000; m.startAlt = 1600;
        m.briefing = "Enemy artillery is dug in along the lake shores north of the city, shelling the centre. Six guns in two batteries, and attack helicopters covering them. Batteries first.";
        m.debrief = "The shelling has stopped.";
        line(m.targets, K::Artillery, "BATTERY A GUN", 3, -1500, 5000, 90, 0.4, 1);
        line(m.targets, K::Artillery, "BATTERY B GUN", 3, 1300, 5100, 90, -0.3, 2);
        m.targets.push_back(heli(0, 5200, 900, 110, "ATTACK HELO 1", 3, 0.0));
        m.targets.push_back(heli(0, 5200, 900, 130, "ATTACK HELO 2", 3, 3.1));
        m.targets.push_back(tgt(K::AAA, -200, 4700, "AAA", 0));
        m.waves = {wave(2, S::Veteran, 6000, 4000, 2400)};
        m.objectives = {destroy(1, "DESTROY BATTERY A"), destroy(2, "DESTROY BATTERY B"), destroy(3, "SPLASH 2 ATTACK HELICOPTERS", "", "1 = ATA   SPACE = GUN")};
        m.objectives[1].wavesLate = {{0, 80.0}};
    }
    {
        auto& m = add(72, "kochi", "Backwaters", "Fast attack boats are using the coast to hide. Sink them.");
        m.startX = 4000; m.startZ = 7000; m.startAlt = 1300;
        m.briefing = "Six enemy fast attack boats are hiding along the coast, darting out to raid shipping. Sink them all. The fishing harbour is protected.";
        m.debrief = "Six boats sunk. The fishermen can go out again.";
        for (int i = 0; i < 6; ++i) {
            TargetDef b = tgt(K::Ship, 11700 + 500 * (i % 2), -4000 + 1600 * (i / 2), "FAST BOAT " + std::to_string(i + 1), 1 + i / 3);
            b.pathRadius = 420 + 60 * (i % 3); b.pathAngle = i * 1.1; m.targets.push_back(b);
        }
        TargetDef h = tgt(K::Civilian, 7000, 2500, "FISHING HARBOUR", kCiv, false); h.team = Team::Blue; m.targets.push_back(h);
        m.waves = {wave(2, S::Veteran, 6000, -8000, 2000)};
        m.objectives = {destroy(1, "SINK FAST BOATS 1-3"), destroy(2, "SINK FAST BOATS 4-6")};
        m.objectives[1].wavesOnStart = {0};
        protectCivilians(m, -1);
    }
    {
        auto& m = add(73, "kochi", "Queen of the Arabian Sea", "A strike package is heading for the shipyard. Turn it back.");
        m.startX = 0; m.startZ = 0; m.startAlt = 2500; m.allies = 2; m.timeLimit = 330;
        m.briefing = "Radar has a strike package heading for the Kochi shipyard: three groups, the last with fighter cover. You and two wingmen are all there is. Five and a half minutes.";
        m.debrief = "The shipyard is untouched. Every bomber turned back or went into the sea.";
        m.waves = {wave(2, S::Veteran, 9500, -6000, 2000), wave(3, S::Veteran, 10000, 5000, 2300), wave(2, S::Ace, 11000, 0, 3000, 500)};
        m.objectives = {air("INTERCEPT THE SOUTHERN GROUP", {0}), air("INTERCEPT THE NORTHERN GROUP", {1}), air("DEFEAT THE FIGHTER COVER", {2})};
    }
    {
        auto& m = add(74, "vizag", "Steel City", "Artillery on the hills above the city. A SAM covers it.");
        m.startX = -8000; m.startZ = -6000; m.startAlt = 1800;
        m.briefing = "The enemy holds the ridge above Visakhapatnam with a SAM and four guns, shelling the port. Kill the SAM first, then work the guns along the ridge.";
        m.debrief = "The ridge is clear. The port is safe.";
        m.targets.push_back(tgt(K::SAM, -2200, -7600, "SAM", 1));
        m.targets.push_back(tgt(K::Radar, -2648, -7287, "RADAR", 1));
        line(m.targets, K::Artillery, "GUN", 4, -800, -8200, 160, 0.3, 2);
        m.targets.push_back(tgt(K::AAA, -1200, -7700, "AAA", 0));
        m.waves = {wave(2, S::Veteran, 3000, -10000, 2400)};
        m.objectives = {destroy(1, "DESTROY SAM AND RADAR"), destroy(2, "DESTROY 4 GUNS")};
        m.objectives[1].wavesOnStart = {0};
    }
    {
        auto& m = add(75, "assam", "Brahmaputra Crossing", "Armour is crossing the river into Guwahati. Stop it at the bridgehead.");
        m.startX = -7000; m.startZ = 4000; m.startAlt = 1700;
        m.briefing = "Enemy armour is massing at a bridgehead on the near bank, ready to push into the city. Hit the tanks in the open, before they reach the streets. Helicopters are covering the crossing.";
        m.debrief = "The bridgehead is gone. Guwahati holds.";
        m.debriefClean = "Bridgehead destroyed and the city untouched. Outstanding.";
        line(m.targets, K::Tank, "TANK", 5, -0.0, 0.0, 120, 0.2, 1);
        for (TargetDef& t : m.targets) { t.anchor = TargetDef::Anchor::City; t.x = -0.55 + t.x / 1000.0; t.z = 0.55 + t.z / 1000.0; }
        m.targets.push_back(heli(-2000, 6000, 700, 120, "ATTACK HELO 1", 2, 0.0));
        m.targets.push_back(heli(-2000, 6000, 700, 140, "ATTACK HELO 2", 2, 2.0));
        m.targets.push_back(cityT(K::SAM, -0.75, 0.80, "SAM", 0));
        m.targets.push_back(civ(-0.20, 0.30, "RIVERSIDE HOMES"));
        m.targets.push_back(civ(-0.35, -0.05, "HOSPITAL"));
        m.waves = {wave(2, S::Veteran, 3000, 8000, 2300)};
        m.objectives = {destroy(1, "DESTROY 5 TANKS AT THE BRIDGEHEAD", kNoStrike), destroy(2, "SPLASH THE HELICOPTERS", "", "1 = ATA   SPACE = GUN")};
        m.objectives[1].wavesLate = {{0, 60.0}};
        protectCivilians(m, 0);
    }

    // ============================= Himalayan operations (76-85) =============================
    {
        auto& m = add(76, "ladakh", "Thin Air", "Intercept reconnaissance jets over the Indus at altitude.");
        m.startX = -6000; m.startZ = 0; m.startAlt = 2600; m.startSpeed = 240;
        m.briefing = "At this altitude the air is thin: the engines give less, the wings bite less, and you turn wider. Two reconnaissance jets are photographing the valley. Climb and catch them.";
        m.debrief = "Both recce jets down. They saw nothing they will ever report.";
        m.waves = {wave(2, S::Veteran, 6000, -4000, 3400, 1200)};
        m.objectives = {simple(ObjectiveKind::Climb, "CLIMB TO 4500 M", "Climb above the peaks: the air is thin up here, so climb fast and turn gently.", "W = THROTTLE UP", 4500),
                        air("SPLASH 2 RECONNAISSANCE JETS", {0})};
    }
    {
        auto& m = add(77, "ladakh", "Indus Valley", "Artillery and flak on a ridge above the valley.");
        m.startX = -7000; m.startZ = -3000; m.startAlt = 2200;
        m.briefing = "An enemy battery on the ridge is shelling the road up the valley. Come in low along the river, pop over the ridge and hit the guns before the flak finds you.";
        m.debrief = "The battery is destroyed. The road is open.";
        m.objectives = {waypoint("RIVER ENTRY", -3500, -2000, 900, 350, "Drop into the valley: stay below 350 m above the ground."),
                        waypoint("RIVER BEND", 0, -3500, 900, 350, "Follow the valley floor, below the ridges."),
                        destroy(1, "DESTROY 4 GUNS ON THE RIDGE")};
        for (const auto& gp : {std::pair<double, double>{4042, -5586}, {4123, -5578}, {4278, -5515}, {4408, -5500}})
            m.targets.push_back(tgt(K::Artillery, gp.first, gp.second, "GUN " + std::to_string(m.targets.size() + 1), 1));
        m.targets.push_back(tgt(K::Gun, 4700, -5100, "FLAK 1", 0));
        m.targets.push_back(tgt(K::Gun, 3700, -6000, "FLAK 2", 0));
    }
    {
        auto& m = add(78, "nubra", "Dunes of Nubra", "A low-level run up the valley, then the launchers by the dunes.");
        m.startX = -8000; m.startZ = 3000; m.startAlt = 1600;
        m.briefing = "Three launchers are hidden among the dunes of the Nubra valley, guarded by a radar on the ridge. Stay under its coverage: below three hundred metres all the way in.";
        m.debrief = "Launchers destroyed. The radar never saw you coming.";
        m.objectives = {waypoint("LOW GATE 1", -5000, 2000, 700, 300), waypoint("LOW GATE 2", -2000, 500, 700, 300),
                        waypoint("LOW GATE 3", 1000, 1200, 700, 300), destroy(1, "DESTROY 3 LAUNCHERS"), destroy(2, "DESTROY THE RADAR")};
        line(m.targets, K::Launcher, "LAUNCHER", 3, 3600, 1500, 260, 1.0, 1);
        m.targets.push_back(tgt(K::Radar, 4600, 3200, "RADAR", 2));
        m.targets.push_back(tgt(K::AAA, 3900, 2400, "AAA", 0));
    }
    {
        auto& m = add(79, "kashmir", "Dal Lake", "Helicopters raid the lakeside. The houseboats and homes are protected.");
        m.startX = -6000; m.startZ = -2000; m.startAlt = 1500;
        m.briefing = "Enemy helicopters are raiding the lakeside around Srinagar, with a SAM covering them from the hills. Kill the SAM, then the helicopters, and watch where your rounds land.";
        m.debrief = "The raid is broken. Srinagar is safe tonight.";
        m.debriefClean = "Not a single protected site damaged. Beautifully flown.";
        m.debriefCostly = "Helicopters down, but we hit a protected site. Not good enough.";
        m.targets.push_back(cityT(K::SAM, 0.9, -0.7, "SAM", 1));
        for (int i = 0; i < 3; ++i) m.targets.push_back(heli(0.2 * i - 0.1, 0.35, 380, 100 + 20 * i, "HELICOPTER " + std::to_string(i + 1), 2, i * 2.0, true));
        m.targets.push_back(civ(-0.25, 0.10, "LAKESIDE HOMES"));
        m.targets.push_back(civ(0.30, 0.05, "HOSPITAL"));
        m.objectives = {destroy(1, "DESTROY THE SAM"), destroy(2, "SPLASH 3 HELICOPTERS", kNoStrike, "1 = ATA   SPACE = GUN")};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(80, "kashmir", "Pir Panjal", "A radar and SAMs hold the pass. Use the mountains as cover.");
        m.startX = -8000; m.startZ = 6000; m.startAlt = 2400;
        m.briefing = "The pass is watched by a radar and two SAMs. They cannot see through rock: keep a ridge between you and the radar until the last moment.";
        m.debrief = "The pass is open. Our transports can come through.";
        m.targets.push_back(tgt(K::Radar, 0, 8000, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 600, 7400, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, -665, 8565, "SAM 2", 1));
        m.targets.push_back(tgt(K::Gun, 400, 8800, "FLAK", 0));
        m.waves = {wave(2, S::Veteran, 6000, 9000, 3200)};
        m.objectives = {waypoint("RIDGE SHADOW", -4000, 7000, 800, 400, "Stay below 400 m above the ground: the ridges hide you from the radar."),
                        destroy(1, "DESTROY RADAR AND SAM 1-2"), air("DEFEAT THE PATROL", {0})};
    }
    {
        auto& m = add(81, "himachal", "Valley of the Gods", "A supply convoy is climbing the valley road to the front.");
        m.startX = -7000; m.startZ = -6000; m.startAlt = 2000;
        m.briefing = "A supply convoy is grinding up the valley towards the front with ammunition for their guns. Six trucks and an escort. The valley is narrow: watch the walls.";
        m.debrief = "The convoy is burning. Their guns will be quiet tomorrow.";
        convoy(m.targets, K::Truck, "SUPPLY TRUCK", 6, -1424, -3576, 600, 1);
        m.targets.push_back(tgt(K::AAA, -1500, -4300, "ESCORT AAA", 0));
        m.waves = {wave(2, S::Veteran, 4000, -9000, 2800)};
        m.objectives = {destroy(1, "DESTROY 6 SUPPLY TRUCKS")};
        m.objectives[0].wavesLate = {{0, 75.0}};
    }
    {
        auto& m = add(82, "uttarakhand", "Tehri Dam", "A strike package is heading for the dam. It must not get through.");
        m.startX = -6000; m.startZ = 0; m.startAlt = 2600; m.allies = 1; m.timeLimit = 420;
        m.briefing = "A strike package is heading for the Tehri dam. If it is breached, the valley below floods. Three groups, seven minutes. And there are commandos with a SAM on the hillside above the dam: take that out first.";
        m.debrief = "The dam stands. The valley will never know how close it came.";
        m.targets.push_back(damT(K::SAM, -600, 500, "COMMANDO SAM", 1));
        m.targets.push_back(damT(K::Truck, -700, 620, "COMMANDO TRUCK", 1));
        m.targets.push_back(damT(K::Civilian, 90, 70, "DAM CONTROL ROOM", kCiv, false));
        m.targets.back().team = Team::Blue;
        m.waves = {wave(2, S::Veteran, 7000, -6000, 2600), wave(2, S::Veteran, 7500, 5500, 2400), wave(2, S::Ace, 9000, 0, 3200, 600)};
        m.objectives = {destroy(1, "DESTROY THE COMMANDO SAM", "The dam itself is protected: attack the SAM from the hillside side."),
                        air("INTERCEPT THE FIRST GROUP", {0}), air("INTERCEPT THE SECOND GROUP", {1}), air("DEFEAT THE ESCORT", {2})};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(83, "gangotri", "Gangotri Glacier", "Two aces among the peaks above six thousand metres.");
        m.startX = -5000; m.startZ = 0; m.startAlt = 4200; m.startSpeed = 250;
        m.briefing = "Two enemy aces are flying top cover over the glacier. The air is thin and the peaks are high: every turn costs energy you cannot get back quickly. Fight smart.";
        m.debrief = "Two aces down above the glacier. That is a story for the mess.";
        WaveDef a = wave(1, S::Ace, 4000, -2000, 4600, 300); a.callsign = "GLACIER ONE";
        WaveDef b = wave(1, S::Ace, 5000, 2000, 4800, 300); b.callsign = "GLACIER TWO";
        m.waves = {a, b};
        m.objectives = {air("DEFEAT GLACIER FLIGHT", {0, 1}, "Thin air: keep your speed up, fight in the vertical, use the peaks to break their locks.")};
    }
    {
        auto& m = add(84, "tawang", "Tawang Ridge", "A radar site above the monastery town. The monastery is protected.");
        m.startX = -7000; m.startZ = 3000; m.startAlt = 2600;
        m.briefing = "The enemy has put a radar and two SAMs on the ridge above Tawang. The monastery and the town below are protected sites. Come in from the north side of the ridge so a miss goes into the mountainside.";
        m.debrief = "The ridge is clear and the monastery untouched.";
        m.debriefCostly = "The ridge is clear, but we damaged a protected site. That cannot happen.";
        m.targets.push_back(cityT(K::Radar, 0.0, -1.6, "RADAR", 1));
        m.targets.push_back(cityT(K::SAM, 0.5, -1.7, "SAM 1", 1));
        m.targets.push_back(cityT(K::SAM, -0.5, -1.5, "SAM 2", 1));
        m.targets.push_back(civ(0.0, 0.0, "MONASTERY"));
        m.targets.push_back(civ(0.4, 0.3, "TOWN SCHOOL"));
        m.waves = {wave(2, S::Veteran, 5000, -6000, 3400)};
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-2"), air("DEFEAT THE PATROL", {0})};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(85, "sikkim", "Teesta Gorge", "A low run down the gorge to an armoured bridgehead.");
        m.startX = -8000; m.startZ = -2000; m.startAlt = 1800;
        m.briefing = "Armour has crossed the Teesta and is digging in on the near bank. The gorge is too steep for their radar to see into: fly down it, below three hundred and fifty metres, and hit them from the river.";
        m.debrief = "Bridgehead destroyed. Gangtok is safe.";
        m.objectives = {waypoint("GORGE ENTRY", -5000, -1000, 800, 350), waypoint("GORGE BEND", -2000, 0, 800, 350),
                        destroy(1, "DESTROY 4 TANKS AT THE BRIDGEHEAD"), destroy(2, "DESTROY THE COMMAND VEHICLE")};
        line(m.targets, K::Tank, "TANK", 4, 1200, 600, 110, 0.8, 1);
        m.targets.push_back(tgt(K::Truck, 1500, 1100, "COMMAND VEHICLE", 2));
        m.targets.push_back(tgt(K::AAA, 900, 1200, "AAA", 0));
        m.waves = {wave(2, S::Veteran, 6000, 4000, 2600)};
        m.objectives[3].wavesOnStart = {0};
    }

    // ============================ Coastal and naval (86-95) ============================
    {
        auto& m = add(86, "mumbai", "Western Fleet", "Launch from the carrier and break up a raid on the fleet.");
        m.briefing = "The Western Fleet is at sea and an anti-ship raid is inbound: two pairs, low over the water. Launch, climb, and splash them before they get in range.";
        m.debrief = "The fleet is safe. Welcome back aboard.";
        m.waves = {wave(2, S::Veteran, 11000, -8500, 1200, 1200), wave(2, S::Veteran, 8000, -4000, 1400, 900)};
        m.objectives = {simple(ObjectiveKind::Climb, "CLIMB TO 1200 M", "Keep climbing straight ahead to 1200 metres.", "PULL BACK GENTLY", 1200),
                        air("SPLASH THE FIRST RAIDERS", {0}), air("SPLASH THE SECOND RAIDERS", {1})};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(87, "mumbai", "Gateway", "Fast attack boats inside the harbour. The ferry terminal is protected.");
        m.startX = 4000; m.startZ = -6000; m.startAlt = 1400;
        m.briefing = "Enemy fast attack boats have slipped into the harbour and are shooting up shipping. The ferry terminal is full of people. Sink the boats, away from the quays.";
        m.debrief = "Harbour clear. The ferries are running.";
        for (int i = 0; i < 5; ++i) {
            TargetDef b = tgt(K::Ship, 11800, -3000 + 1000 * i, "FAST BOAT " + std::to_string(i + 1), 1);
            b.pathRadius = 450; b.pathAngle = i * 1.2; m.targets.push_back(b);
        }
        TargetDef f = tgt(K::Civilian, 8400, 400, "FERRY TERMINAL", kCiv, false); f.team = Team::Blue; m.targets.push_back(f);
        m.waves = {wave(2, S::Veteran, 6000, 6000, 2200)};
        m.objectives = {destroy(1, "SINK 5 FAST BOATS", "The boats weave: fire ATS early and let the seeker lead them.")};
        m.objectives[0].wavesLate = {{0, 90.0}};
        protectCivilians(m, 0);
    }
    {
        auto& m = add(88, "mumbai", "Offshore Field", "Warships are holding our offshore platforms. Sink them, not the rigs.");
        m.startX = 4000; m.startZ = -9500; m.startAlt = 1500;
        m.briefing = "Four enemy warships are holding the offshore field. The platforms are ours and full of crew: sink the ships, do not touch the rigs.";
        m.debrief = "Field recaptured, platforms intact.";
        m.protectGroup = 9;
        for (const auto& r : {std::pair<double, double>{11300, -8800}, {12700, -6800}, {11100, -5000}}) {
            TargetDef t = tgt(K::OilRig, r.first, r.second, "FRIENDLY RIG", 9);
            t.team = Team::Blue; m.targets.push_back(t);
        }
        for (int i = 0; i < 4; ++i) {
            TargetDef b = tgt(K::Ship, 11800, -7000, "WARSHIP " + std::to_string(i + 1), 1);
            b.pathRadius = 350 + 130 * i; b.pathAngle = i * 1.6; m.targets.push_back(b);
        }
        m.waves = {wave(2, S::Veteran, 7000, -11000, 2200)};
        m.objectives = {destroy(1, "SINK 4 WARSHIPS", "Friendly rigs are marked blue. ATS splash damages anything within 45 m.")};
        m.objectives[0].wavesLate = {{0, 100.0}};
    }
    {
        auto& m = add(89, "chennai", "Bay of Bengal", "A carrier strike on an enemy surface group.");
        m.briefing = "An enemy surface group is closing the coast: three warships and a fighter screen. Launch, sweep the fighters, then sink the ships.";
        m.debrief = "The surface group is on the bottom of the Bay. Bring it home.";
        for (int i = 0; i < 3; ++i) {
            TargetDef b = tgt(K::Ship, 12200, -9500 + 700 * i, "WARSHIP " + std::to_string(i + 1), 1);
            b.pathRadius = 600 + 200 * i; b.pathAngle = i * 2.0; m.targets.push_back(b);
        }
        m.waves = {wave(2, S::Veteran, 10500, -8000, 2000, 1200)};
        m.objectives = {air("SWEEP THE FIGHTER SCREEN", {0}), destroy(1, "SINK 3 WARSHIPS")};
        carrierStart(m);
        recoverOverCarrier(m);
    }
    {
        auto& m = add(90, "vizag", "Eastern Seaboard", "Missile boats and a coastal SAM threaten the naval base.");
        m.startX = -3000; m.startZ = -7000; m.startAlt = 1600;
        m.briefing = "Missile boats are lining up on the naval base, covered by a SAM on the headland. SAM first, then the boats, then their air cover.";
        m.debrief = "The seaboard is clear.";
        m.targets.push_back(tgt(K::SAM, 9494, -5650, "HEADLAND SAM", 1));
        m.targets.push_back(tgt(K::Radar, 9300, -6400, "RADAR", 1));
        for (int i = 0; i < 4; ++i) {
            TargetDef b = tgt(K::Ship, 12000, -4000 + 900 * i, "MISSILE BOAT " + std::to_string(i + 1), 2);
            b.pathRadius = 500; b.pathAngle = i * 1.4; m.targets.push_back(b);
        }
        m.waves = {wave(2, S::Veteran, 8000, -1000, 2400), wave(1, S::Ace, 9000, 1000, 2800, 300)};
        m.objectives = {destroy(1, "DESTROY THE HEADLAND SAM AND RADAR"), destroy(2, "SINK 4 MISSILE BOATS"), air("DEFEAT THE AIR COVER", {0, 1})};
    }
    {
        auto& m = add(91, "kochi", "Lakshadweep Run", "A long-range strike: refuel from the tanker, then the enemy picket ships.");
        m.startX = -7000; m.startZ = -1500; m.startAlt = 2600; m.startSpeed = 200; m.startFuel = 0.22;
        m.tanker = true; m.tankerX = -1500; m.tankerZ = -2500; m.tankerAlt = 3000; m.tankerRadius = 6500; m.tankerSpeed = 175;
        m.briefing = "The enemy picket ships are far out to sea. You will not get there and back on internal fuel: tank first, then the pickets.";
        m.debrief = "Pickets sunk, and you made it home on the tanker's gas.";
        ObjectiveDef join; join.kind = ObjectiveKind::Rendezvous; join.title = "JOIN THE TANKER"; join.radius = 1200;
        join.instruction = "Fly to the tanker (TANKER marker). Approach from behind and slightly below."; join.hint = "FOLLOW THE MARKER";
        ObjectiveDef fill; fill.kind = ObjectiveKind::Refuel; fill.title = "REFUEL TO 80 %"; fill.value = 0.8;
        fill.instruction = "Probe in the basket: match the tanker's speed and hold contact while fuel flows."; fill.hint = "W / S = MATCH SPEED";
        for (int i = 0; i < 3; ++i) {
            TargetDef b = tgt(K::Ship, 12800, 4000 + 1100 * i, "PICKET " + std::to_string(i + 1), 1);
            b.pathRadius = 500; b.pathAngle = i * 2.1; m.targets.push_back(b);
        }
        m.waves = {wave(2, S::Veteran, 10000, 6000, 2400)};
        m.objectives = {join, fill, destroy(1, "SINK 3 PICKET SHIPS")};
        m.objectives[2].wavesLate = {{0, 60.0}};
    }
    {
        auto& m = add(92, "kochi", "Monsoon Raid", "Bombers under the monsoon cloud. Find them in the rain.");
        m.startX = -4000; m.startZ = 0; m.startAlt = 1800; m.rain = true; m.timeOfDay = 3; m.allies = 1;
        m.briefing = "The monsoon is in, and the enemy is using it: bombers low under the cloud base, heading for the port. Radar will get you close; the last mile is your eyes.";
        m.debrief = "Every bomber accounted for. Fine flying in that weather.";
        m.waves = {wave(2, S::Veteran, 9000, -3000, 1100, 1000), wave(2, S::Veteran, 9000, 3500, 1200, 1000), wave(1, S::Ace, 10000, 0, 1500, 300)};
        m.objectives = {air("SPLASH THE SOUTHERN BOMBERS", {0}), air("SPLASH THE NORTHERN BOMBERS", {1}), air("DEFEAT THE ESCORT LEADER", {2})};
    }
    {
        auto& m = add(93, "vizag", "Coastal Battery", "Coastal guns and their radar on the headland.");
        m.startX = 2000; m.startZ = -9000; m.startAlt = 1600;
        m.briefing = "A coastal battery on the headland is firing on our ships: four heavy guns, a radar and a command bunker, with flak all round.";
        m.debrief = "The battery is silent. The ships can come in.";
        {
            const double cg[4][2] = {{9435, -5768}, {9521, -5592}, {9522, -5520}, {9535, -5340}};
            for (int i = 0; i < 4; ++i) m.targets.push_back(tgt(K::Artillery, cg[i][0], cg[i][1], "COASTAL GUN " + std::to_string(i + 1), 1));
        }
        m.targets.push_back(tgt(K::Radar, 9200, -5100, "RADAR", 1));
        m.targets.push_back(tgt(K::Bunker, 9300, -6000, "COMMAND BUNKER", 2));
        m.targets.push_back(tgt(K::Gun, 9528, -5300, "FLAK 1", 0));
        m.targets.push_back(tgt(K::Gun, 9100, -5700, "FLAK 2", 0));
        m.targets.push_back(tgt(K::Gun, 9374, -5976, "FLAK 3", 0));
        m.objectives = {destroy(1, "DESTROY 4 COASTAL GUNS AND RADAR"), destroy(2, "DESTROY THE COMMAND BUNKER")};
    }
    {
        auto& m = add(94, "chennai", "Amphibious Assault", "Repel a landing: ships, armour on the beach and helicopters.");
        m.startX = 2000; m.startZ = 7000; m.startAlt = 1600; m.allies = 1;
        m.briefing = "A full landing is under way: assault ships offshore, armour on the beach, helicopters overhead. Ships first, then the beach, then the helicopters.";
        m.debrief = "The landing is thrown back into the sea.";
        for (int i = 0; i < 3; ++i) {
            TargetDef b = tgt(K::Ship, 12000, 1500 + 1200 * i, "ASSAULT SHIP " + std::to_string(i + 1), 1);
            b.pathRadius = 380; b.pathAngle = i * 1.7; m.targets.push_back(b);
        }
        line(m.targets, K::Tank, "BEACH TANK", 4, 7250, 3000, 200, kPi / 2, 2);
        m.targets.push_back(heli(9500, 3200, 800, 120, "HELICOPTER 1", 3, 0.0));
        m.targets.push_back(heli(9500, 3200, 800, 140, "HELICOPTER 2", 3, 3.0));
        m.waves = {wave(2, S::Veteran, 6000, 9000, 2200)};
        m.objectives = {destroy(1, "SINK 3 ASSAULT SHIPS"), destroy(2, "DESTROY 4 BEACH TANKS"), destroy(3, "SPLASH 2 HELICOPTERS", "", "1 = ATA   SPACE = GUN")};
        m.objectives[1].wavesOnStart = {0};
    }
    {
        auto& m = add(95, "mumbai", "Trident", "Three coordinated raids on the carrier group.");
        m.briefing = "They are throwing everything at the carrier: three raids from three directions, the last one led by their best. Launch and hold the line.";
        m.debrief = "Three raids, and the fleet did not take a scratch. Bravo Zulu, Skyfall One.";
        m.allies = 1;
        WaveDef leader = wave(1, S::Ace, 10000, -12000, 2600, 300); leader.callsign = "STRIKE LEADER";
        m.waves = {wave(2, S::Veteran, 11500, -9500, 1600, 1000), wave(2, S::Veteran, 7000, -5000, 2000, 1200), leader,
                   wave(1, S::Veteran, 10500, -12500, 2400, 300)};
        m.objectives = {simple(ObjectiveKind::Climb, "CLIMB TO 1200 M", "Keep climbing straight ahead to 1200 metres.", "PULL BACK GENTLY", 1200),
                        air("BREAK THE NORTHERN RAID", {0}), air("BREAK THE WESTERN RAID", {1}), air("DEFEAT THE STRIKE LEADER", {2, 3})};
        carrierStart(m);
        recoverOverCarrier(m);
    }

    // =============================== The final campaign (96-100) ===============================
    {
        auto& m = add(96, "kashmir", "Iron Monsoon", "The enemy's air-defence belt in the valley. Roll it back.");
        m.startX = -9000; m.startZ = -4000; m.startAlt = 2000; m.allies = 2;
        m.briefing = "The final offensive starts here. Their air-defence belt runs across the valley: three SAMs, a radar and the guns. Take it apart, and expect their fighters to come up when it starts to go.";
        m.debrief = "The belt is broken. The road north is open.";
        m.targets.push_back(tgt(K::Radar, -1500, -6000, "RADAR", 1));
        m.targets.push_back(tgt(K::SAM, -2200, -5600, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, -700, -6400, "SAM 2", 1));
        m.targets.push_back(tgt(K::SAM, 600, -5400, "SAM 3", 2));
        m.targets.push_back(tgt(K::Gun, -1278, -5148, "GUN 1", 0));
        m.targets.push_back(tgt(K::Gun, -703, -4974, "GUN 2", 0));
        m.targets.push_back(tgt(K::Gun, -272, -4852, "GUN 3", 0));
        m.waves = {wave(2, S::Veteran, 4000, -9000, 2800), wave(1, S::Ace, 5000, -3000, 3000, 300)};
        m.objectives = {destroy(1, "DESTROY RADAR AND SAM 1-2"), destroy(2, "DESTROY SAM 3"), air("DEFEAT THE COUNTERATTACK", {0, 1})};
        m.objectives[1].wavesLate = {{0, 70.0}};
    }
    {
        auto& m = add(97, "ladakh", "Eagle's Nest", "Their high-altitude airbase: jets in the open, hangars and a SAM ring.");
        m.startX = -9000; m.startZ = 5000; m.startAlt = 2600;
        m.briefing = "The enemy's high-altitude airbase is the last place their fighters can operate from in the north. Catch the jets on the ground, hit the hangars, and kill the scramble.";
        m.debrief = "Eagle's Nest is finished. They have nowhere left to fly from in the north.";
        m.targets.push_back(tgt(K::SAM, -2600, 4100, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, -400, 3100, "SAM 2", 1));
        for (int i = 0; i < 6; ++i) m.targets.push_back(tgt(K::ParkedJet, -2000 + 130 * i, 3600, "PARKED JET " + std::to_string(i + 1), 2, false, kPi / 2));
        line(m.targets, K::Hangar, "HANGAR", 2, -1600, 3250, 160, 0.0, 2);
        m.targets.push_back(tgt(K::Gun, -1300, 4000, "GUN", 0));
        m.allies = 1;
        m.waves = {wave(2, S::Veteran, 5000, 9000, 3600), wave(1, S::Ace, 7000, 5000, 3800, 300)};
        m.objectives = {destroy(1, "DESTROY SAM 1-2"), destroy(2, "DESTROY 6 PARKED JETS AND 2 HANGARS"), air("SHOOT DOWN THE SCRAMBLE", {0})};
        m.objectives[2].wavesLate = {{1, 45.0}};
    }
    {
        auto& m = add(98, "tawang", "Northern Lights", "Their command and control in the mountains, at golden hour.");
        m.startX = -8000; m.startZ = -5000; m.startAlt = 2800; m.timeOfDay = 1; m.allies = 1;
        m.briefing = "Their northern command post is dug into a mountainside with communications masts on the ridge above. Masts first, so they cannot call for help, then the command post itself.";
        m.debrief = "Northern command is gone. They are fighting blind.";
        m.targets.push_back(tgt(K::CommTower, -1000, -8000, "COMMS TOWER 1", 1));
        m.targets.push_back(tgt(K::CommTower, 1200, -7800, "COMMS TOWER 2", 1));
        m.targets.push_back(tgt(K::Command, 0, -6500, "COMMAND POST", 2));
        ring(m.targets, K::Bunker, "BUNKER", 3, 0, -6500, 170, 2, true, 0.5);
        ring(m.targets, K::Gun, "FLAK", 3, 0, -6500, 480, 0, true, 1.1);
        m.targets.push_back(tgt(K::SAM, 665, -6935, "SAM", 0));
        m.waves = {wave(2, S::Ace, 4000, -9000, 3600, 600)};
        m.objectives = {destroy(1, "DESTROY COMMS TOWERS 1-2"), destroy(2, "DESTROY COMMAND POST AND BUNKERS"), air("DEFEAT THE ACES", {0})};
        m.objectives[1].wavesLate = {{0, 90.0}};
    }
    {
        auto& m = add(99, "bengaluru", "Storm Over the Deccan", "The main strike on the city, in a thunderstorm. Hold the sky.");
        m.startX = -3000; m.startZ = 0; m.startAlt = 2400; m.rain = true; m.timeOfDay = 3; m.allies = 2; m.timeLimit = 420;
        m.briefing = "This is their last big throw: the main strike on the city, under a thunderstorm, and attack helicopters already over the suburbs. Seven minutes. Helicopters first, they are over the homes. Then the bombers.";
        m.debrief = "The city stands. Their last strike force is gone.";
        m.debriefClean = "The city stands, and not one protected site was touched in that storm. Remarkable.";
        m.debriefCostly = "The city stands, but we lost a protected site in the fight. We will carry that.";
        for (int i = 0; i < 3; ++i) m.targets.push_back(heli(0.3 * i - 0.3, -0.4, 330, 110 + 15 * i, "ATTACK HELO " + std::to_string(i + 1), 1, i * 2.1, true));
        m.targets.push_back(civ(-0.25, -0.25, "HOMES"));
        m.targets.push_back(civ(0.25, -0.65, "SCHOOL"));
        m.targets.push_back(civ(0.0, 0.2, "HOSPITAL"));
        m.waves = {wave(3, S::Veteran, 9000, -4000, 2200, 1200), wave(3, S::Veteran, 9500, 4000, 2400, 1200), wave(2, S::Ace, 11000, 0, 2800, 500)};
        m.objectives = {destroy(1, "SPLASH 3 ATTACK HELICOPTERS", kNoStrike, "1 = ATA   SPACE = GUN"), air("STOP THE SOUTHERN BOMBERS", {0}),
                        air("STOP THE NORTHERN BOMBERS", {1}), air("DEFEAT THE STRIKE LEADERS", {2})};
        protectCivilians(m, 1);
    }
    {
        auto& m = add(100, "mumbai", "SKYFALL - Final Air Superiority", "The last battle: the SAM belt, the fleet, their elite squadron and their headquarters.");
        m.allies = 3;
        m.briefing = "This is it, Skyfall One. Everything they have left is over Mumbai: a SAM belt on the coast, a surface group, their headquarters in the city and their elite squadron. Win the sky, and the war is over. Launch.";
        m.debrief = "SKYFALL. Air superiority is ours. Every pilot in the fleet heard that, Captain. The war is over.";
        m.debriefClean = "SKYFALL. Air superiority is ours, the city untouched, the fleet unscratched. A flawless finish to the war. There is no one left who can fly against you.";
        m.debriefCostly = "SKYFALL. Air superiority is ours, but we hit a protected site getting there. We won the war, Captain. Remember what it cost.";
        m.targets.push_back(tgt(K::Radar, 9376, -6006, "COAST RADAR", 1));
        m.targets.push_back(tgt(K::SAM, 9324, -6325, "SAM 1", 1));
        m.targets.push_back(tgt(K::SAM, 9527, -5393, "SAM 2", 1));
        for (int i = 0; i < 3; ++i) {
            TargetDef b = tgt(K::Ship, 12600, -9000 + 900 * i, "WARSHIP " + std::to_string(i + 1), 2);
            b.pathRadius = 700 + 150 * i; b.pathAngle = i * 2.0; m.targets.push_back(b);
        }
        m.targets.push_back(cityT(K::Command, 0.15, -0.10, "ENEMY HEADQUARTERS", 3));
        m.targets.push_back(cityT(K::SAM, -0.20, -0.30, "CITY SAM", 3));
        m.targets.push_back(cityT(K::Gun, 0.35, 0.10, "CITY GUN", 0));
        m.targets.push_back(civ(0.15, 0.25, "HOSPITAL"));
        m.targets.push_back(civ(-0.25, 0.05, "SCHOOL"));
        m.targets.push_back(civ(0.45, -0.30, "APARTMENTS"));
        WaveDef lead = wave(1, S::Ace, 6000, -2000, 3200, 300); lead.callsign = "KING COBRA";
        WaveDef two = wave(1, S::Veteran, 7000, -1000, 3000, 300); two.callsign = "COBRA TWO";
        WaveDef three = wave(1, S::Veteran, 7000, -3000, 3000, 300); three.callsign = "COBRA THREE";
        m.waves = {wave(2, S::Veteran, 11500, -9500, 1800, 1000), wave(2, S::Veteran, 6000, -9000, 2200), lead, two, three,
                   wave(2, S::Veteran, 9000, 2000, 2400)};
        m.objectives = {destroy(1, "DESTROY COAST RADAR AND SAM 1-2"), air("BREAK THE FIRST COUNTERATTACK", {0}),
                        destroy(2, "SINK 3 WARSHIPS"), destroy(3, "DESTROY ENEMY HEADQUARTERS", kNoStrike),
                        air("DEFEAT COBRA SQUADRON", {2, 3, 4}, "Their elite: King Cobra and his two wingmen fight as one. Stay fast, stay together, and use your wingmen.")};
        m.objectives[2].wavesLate = {{1, 80.0}};
        m.objectives[3].wavesLate = {{5, 90.0}};
        carrierStart(m);
        recoverOverCarrier(m);
        protectCivilians(m, 1);
    }
}
