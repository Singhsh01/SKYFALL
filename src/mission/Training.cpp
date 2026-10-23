#include "Training.hpp"

#include "../sim/Locations.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace sky {

// =============================================================================================
// Levels
// =============================================================================================
const std::vector<TrainingLevelDef>& trainingLevels() {
    // Flying first (1-4), then helicopters and weapons (5-8), one-on-one air combat against the
    // F-16, Su-35, Su-57 and F-35 (9-12), then mixed missions (13-18) and the carrier finale.
    static const std::vector<TrainingLevelDef> k = {
        {1, "Carrier Scramble & Safe Takeoff", "Catapult launch from the carrier, climb-out and a navigation circuit, then a runway landing.", 1, "desert", false,
         {"Captain, welcome to the Training Academy. Today is a scramble from the carrier: catapult launch, climb-out and a navigation circuit around the range.",
          "On the catapult, push the throttle all the way into afterburner with W, then release the brake with B. The cat throws you off the bow; keep the wings level and raise the gear.",
          "Where the map has no sea, you scramble from the runway instead. Then fly the navigation gates and bring it back to the runway. Fly it smooth, Captain."},
         {"Catapult launch", "Safe climb-out", "Waypoint navigation", "Runway landing"}},
        {2, "Aircraft Handling & Waypoint Rings", "Banked turns, climbing and descending turns, high G, loop, roll and recovery, then the rings.", 2, "fuji", false,
         {"Captain, aircraft control today. Coordinated banked turns, a climbing turn, a descending turn, high G, a loop, a roll and an unusual attitude recovery.",
          "I want precise bank angles and you holding your height in the level turn. Smooth on the stick, positive on the recovery.",
          "Between the exercises you will fly short legs around the mountain."},
         {"Level turns", "Climbing / descending turns", "High G", "Loop and roll", "Recovery", "Ring gates"}},
        {3, "Mountain Navigation & Terrain Avoidance", "Waypoints through mountain valleys at controlled speed and altitude.", 2, "matterhorn", false,
         {"Captain, today you navigate the high valleys. The gates follow the valley floors between the peaks.",
          "Hold your speed in the band and keep the height I give you. Look ahead to the next gate before you reach this one.",
          "Finish with a landing back at base."},
         {"Valley navigation", "Speed discipline", "Altitude discipline", "Runway landing"}},
        {4, "Low-Altitude Tactical Flight", "Narrow valleys and passes, low and fast, then pillars and arches.", 3, "guilin", false,
         {"Captain, low level today. You stay down in the valleys, under the ceiling, through the passes and the steep ground.",
          "The ground comes up fast here. Keep the wings level when you can and use small corrections; big inputs at low level kill.",
          "Then the rock pillars and the arches. Slow down for them, line up early and look through the gap."},
         {"Low-level valleys", "Terrain clearance", "Pillar slalom", "Arches", "Runway landing"}},
        {5, "Helicopter Identification & Interception", "Four helicopters in the valleys: two hostile, two of ours. Identify them and shoot down the hostiles.", 3, "geiranger", false,
         {"Captain, four helicopters are moving low through the valleys. Two are hostile, two are ours. Find the hostiles and shoot them down.",
          "Identify before you shoot: the IFF shows ours in green and hostiles in red. Helicopters are slow and hug the ground: slow down, come in from above and behind, and use the gun or the air-to-air missile.",
          "Watch the terrain while you hunt. Then bring it home."},
         {"Valley transit", "Identification (IFF)", "Helicopter interception", "Runway landing"}},
        {6, "Helicopter Attack at a Military Outpost", "Attack helicopters and armour defend a forward outpost. Destroy them all.", 3, "wadi_rum", false,
         {"Captain, an enemy outpost in the hills. Attack helicopters patrol over it and armour is dug in inside the walls.",
          "Kill the helicopters first, they shoot back. Then switch to the air-to-surface missile with Q and take out the armour from a shallow dive.",
          "Keep moving over the outpost and do not fly low and slow over the guns."},
         {"Navigation to the outpost", "Attack helicopters", "Air-to-surface missiles", "Runway landing"}},
        {7, "Cannon Targeting & Precision Engagement", "Guns only: three target drones on a racetrack.", 3, "erg_chebbi", false,
         {"Captain, gunnery today. Three target drones fly a racetrack over the range. Guns only.",
          "Get in close, inside six hundred metres, put the pipper on the target and give it short bursts. Lead the turn.",
          "Watch your speed: do not overshoot, and do not fly through the debris."},
         {"Pursuit", "Gun tracking", "Energy management", "Runway landing"}},
        {8, "Missile Locking & Air-to-Air Combat", "Missiles only on four drones, then a fight with an armed bandit.", 3, "rio", false,
         {"Captain, missile employment. Four drones in the air first, missiles only.",
          "Get the target in front of you, wait for the lock tone, then fire. One missile per target.",
          "Then a real fight: one armed bandit. Lock him before he locks you, and break if he fires."},
         {"Lock and launch", "Target cycling", "Air-to-air combat", "Runway landing"}},
        {9, "F-16 Interception", "One-on-one against an F-16: light, quick in the roll, a hard turner.", 4, "grand_canyon", false,
         {"Captain, your first real fight. One F-16, armed, flown by an experienced pilot.",
          "The Viper turns hard and rolls fast. Do not try to out-turn it at low speed: use the vertical and keep your energy.",
          "If he launches, flares and break turn. Kill him and come home."},
         {"Basic fighter manoeuvres", "Missile defence", "Guns kill", "Runway landing"}},
        {10, "Su-35 Dogfight Training", "One-on-one against an Su-35: big, powerful and super-manoeuvrable.", 4, "aconcagua", false,
         {"Captain, today the Flanker. An Su-35, armed, with thrust vectoring.",
          "At low speed it can point its nose almost anywhere. Stay fast, make him bleed energy, and take the shot when he slows down.",
          "Flares and break turn if he launches. Then home."},
         {"Energy fighting", "Missile defence", "Missile kill", "Runway landing"}},
        {11, "Advanced Su-57 Interception", "One-on-one against an Su-57 flown by an ace.", 5, "torres_paine", false,
         {"Captain, the Felon. An Su-57 flown by an ace.",
          "He is fast, stealthy and he will not make many mistakes. Fight at your best speed, watch your six and use the terrain.",
          "Get the kill, then come home."},
         {"Ace opponent", "Defensive manoeuvres", "Weapons employment", "Runway landing"}},
        {12, "F-35 Tactical Engagement", "One-on-one against an F-35: sensors and missiles first, then a turning fight.", 5, "glencoe", false,
         {"Captain, the Lightning. An F-35 flown by an ace.",
          "He will try to shoot you from range. Deny him the shot: use the valleys, close the distance, and fight him close in.",
          "Kill him and come home."},
         {"Ace opponent", "Missile defence", "Close-in fight", "Runway landing"}},
        {13, "Multiple Helicopter Threats", "Six hostile helicopters over the hills, two of them armed, and one of ours.", 4, "musandam", false,
         {"Captain, six helicopters over the hills, two of them attack helicopters that will shoot back.",
          "Work methodically: take the attack helicopters first, then the transports. Do not fixate on one target and fly into the ground.",
          "Then come home."},
         {"Multiple targets", "Attack helicopters", "Target priority", "Runway landing"}},
        {14, "Combined Air & Ground Attack", "Two fighters launch from their base to protect an outpost with armour and guns.", 4, "fuji", false,
         {"Captain, combined operations. Two fighters will launch from their base to protect an enemy outpost with armour and anti-aircraft guns.",
          "Deal with the fighters first, then attack the outpost with the air-to-surface missile and the gun.",
          "The guns at the outpost shoot back. Keep your speed up over the target and do not loiter."},
         {"Air superiority", "Ground attack", "Threat avoidance", "Runway landing"}},
        {15, "High-Speed Mountain Combat", "A fast canyon run, then a fight with two bandits among the peaks.", 5, "grand_canyon", false,
         {"Captain, a high-speed canyon run, then two bandits waiting among the peaks.",
          "Fight around the terrain: use the ridges to break their lock, and never pull into the rock.",
          "Then come home."},
         {"High-speed canyon run", "Two-versus-one", "Terrain masking", "Runway landing"}},
        {16, "Enemy Formation Interception", "Terrain following, then a three-ship enemy formation launching from its base.", 4, "geiranger", false,
         {"Captain, terrain following through the fjords first, then an intercept.",
          "An enemy formation of three will launch from their base and fly at you. Meet them head on, then work the formation apart: take the lead first.",
          "Then come home."},
         {"Terrain following", "Enemy formation", "Multi-bandit fight", "Runway landing"}},
        {17, "Advanced Defensive Flying", "Survive ninety seconds against two attackers, then turn the fight.", 5, "rio", false,
         {"Captain, defensive flying. Two attackers, and they start behind you.",
          "For ninety seconds, survive: flares and hard turns into each missile, change altitude, use your speed.",
          "Then turn the fight around and shoot them down."},
         {"Missile defence", "Flares and break turns", "Reversal", "Runway landing"}},
        {18, "Multi-Target Combat & Refuelling", "A mountain route, a mixed strike on air and ground targets, then the tanker.", 5, "torres_paine", false,
         {"Captain, a long mission: a mountain route, a strike on an enemy outpost with its fighter cover, then the tanker.",
          "Watch your fuel and your weapons. After the strike, climb to the tanker, refuel and come home.",
          "Fly it like a real mission, Captain."},
         {"Mountain route", "Mixed targets", "Air-to-air refuelling", "Runway landing"}},
        {19, "Carrier Strike Mission & Recovery", "Catapult launch, strike the patrol boats near the fleet, then recover aboard the carrier.", 5, "rio", true,
         {"Captain, a carrier strike. You launch off the catapult; enemy patrol boats are threatening the fleet. Sink them.",
          "The boats are armed: attack from a shallow dive with the air-to-surface missile and keep moving.",
          "Then recover aboard the carrier. Fly the ball, keep your line-up and do not flare."},
         {"Catapult launch", "Maritime strike", "Carrier approach", "Carrier landing"}},
        {20, "Final Advanced Combat Qualification", "Valleys, obstacles, an intercept of bandits from their base, the tanker and a carrier landing.", 5, "musandam", true,
         {"Captain, this is your final qualification. Everything you have learned, in one flight.",
          "Valleys, mountain navigation, the obstacles, bandits launching from their base, and the tanker. Then you recover aboard the carrier.",
          "On the carrier approach, fly the ball, keep your line-up and do not flare. Good luck, Captain."},
         {"Valleys and obstacles", "Air combat", "Refuelling", "Carrier landing"}},
    };
    return k;
}

const TrainingLevelDef& trainingLevel(int number) {
    const auto& L = trainingLevels();
    return L[std::clamp(number, 1, kTrainingLevels) - 1];
}

double approachHeight(const Course& c, double d) {
    if (d <= 0) return 0;
    double g0 = std::tan(c.glideDeg * kDeg);
    if (d <= c.glideBreak) return d * g0;
    return c.glideBreak * g0 + (d - c.glideBreak) * std::tan(c.glideOuterDeg * kDeg);
}
double approachSlope(const Course& c, double d) { return std::tan((d <= c.glideBreak ? c.glideDeg : c.glideOuterDeg) * kDeg); }

ScoreCard finalizeScore(ScoreCard c) {
    const double w[8] = {25, 15, 15, 15, 10, 10, 10, 25};
    const double v[8] = {c.accuracy, c.manoeuvre, c.discipline, c.clearance, c.stability, c.refuel, c.landing, c.combat};
    double sum = 0, wsum = 0;
    for (int i = 0; i < 8; ++i)
        if (v[i] >= 0) { sum += w[i] * clampd(v[i], 0.0, 100.0); wsum += w[i]; }
    c.total = wsum > 0 ? sum / wsum : 0.0;
    return c;
}

// =============================================================================================
// Numbers in words (finite vocabulary for the voice pack)
// =============================================================================================
std::string TrainingSession::numberWords(int n) {
    static const char* ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten", "eleven",
                                 "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char* tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n < 0) return "minus " + numberWords(-n);
    if (n < 20) return ones[n];
    if (n < 100) return std::string(tens[n / 10]) + (n % 10 ? std::string("-") + ones[n % 10] : "");
    if (n < 1000) return std::string(ones[n / 100]) + " hundred" + (n % 100 ? " and " + numberWords(n % 100) : "");
    std::string s = numberWords(n / 1000) + " thousand";
    int r = n % 1000;
    if (r == 0) return s;
    if (r < 100) return s + " and " + numberWords(r);
    return s + " " + numberWords(r);
}

std::string TrainingSession::headingWords(int deg10) {
    // heading in tens of degrees (0..35); 0 is "three six zero"
    static const char* d[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    int h = ((deg10 % 36) + 36) % 36;
    int deg = h == 0 ? 360 : h * 10;
    char buf[8];
    std::snprintf(buf, sizeof buf, "%03d", deg);
    return std::string(d[buf[0] - '0']) + " " + d[buf[1] - '0'] + " " + d[buf[2] - '0'];
}

namespace {

int round50(double v) { return static_cast<int>(std::lround(v / 50.0)) * 50; }

// =============================================================================================
// Route styles
// =============================================================================================
struct StyleSpec {
    double spacing, radius, targetAgl, winLo, winHi, vMin, vMax, ceiling, lineClear, low, slope, nTurn;
    const char* title;
    const char* noun;
};
const StyleSpec& spec(RouteStyle s) {
    static const StyleSpec k[] = {
        // spacing radius agl  winLo winHi vMin vMax ceil lineClr low  slope nTurn
        {3200, 320, 650, 330, 650, 170, 290, 0, 150, 0.6, 1.0, 3.2, "NAVIGATION", "navigation leg"},             // Navigation
        {2000, 220, 260, 150, 340, 170, 260, 0, 80, 3.0, 4.0, 3.6, "VALLEY RUN", "valley run"},                   // Valley
        {1650, 160, 140, 95, 160, 160, 240, 380, 55, 5.0, 5.0, 3.6, "LOW LEVEL", "low-level run"},               // LowLevel
        {1350, 120, 110, 70, 115, 160, 230, 300, 45, 6.0, 6.0, 3.6, "TERRAIN FOLLOWING", "terrain-following run"}, // Precision
        {2500, 180, 170, 115, 210, 235, 320, 460, 60, 4.0, 4.0, 3.4, "HIGH-SPEED RUN", "high-speed run"},        // HighSpeed
        {1200, 75, 70, 40, 60, 140, 220, 0, 40, 3.0, 4.0, 3.6, "OBSTACLES", "obstacle course"},                   // Obstacles
        {3600, 400, 850, 450, 900, 180, 300, 0, 220, 0.3, 0.6, 3.0, "TRANSIT", "transit"},                        // Transit
        {1500, 120, 100, 50, 60, 70, 110, 0, 30, 0.0, 0.0, 3.0, "APPROACH", "approach"},                          // Approach
    };
    return k[static_cast<int>(s)];
}

double midSpeed(RouteStyle s) { return 0.5 * (spec(s).vMin + spec(s).vMax); }
double turnRadiusFor(RouteStyle s) {
    double v = midSpeed(s);
    double n = spec(s).nTurn;
    return v * v / (kG * std::sqrt(n * n - 1.0));
}

std::string styleIntro(RouteStyle st) {
    const StyleSpec& sp = spec(st);
    std::string speeds = "between " + TrainingSession::numberWords(round50(sp.vMin * 3.6)) + " and " +
                         TrainingSession::numberWords(round50(sp.vMax * 3.6)) + " kilometres an hour";
    switch (st) {
        case RouteStyle::Navigation:
            return "Navigation leg. Fly the gates at a comfortable height, " + speeds + ".";
        case RouteStyle::Valley:
            return "Valley run. Follow the valley floor through the gates, " + speeds + ".";
        case RouteStyle::LowLevel:
            return "Low-level run. Stay below " + TrainingSession::numberWords(static_cast<int>(sp.ceiling)) + " metres above the ground, " + speeds + ".";
        case RouteStyle::Precision:
            return "Terrain following. Every gate has a height window. Stay below " + TrainingSession::numberWords(static_cast<int>(sp.ceiling)) +
                   " metres, " + speeds + ".";
        case RouteStyle::HighSpeed:
            return "High-speed run. Push it up to " + speeds + ", and stay below " + TrainingSession::numberWords(static_cast<int>(sp.ceiling)) + " metres.";
        case RouteStyle::Transit:
            return "Transit. Climb to a safe height and follow the gates, " + speeds + ".";
        default:
            return "";
    }
}

// =============================================================================================
// Builder
// =============================================================================================
const char* combatIntroText(int i);   // the combat intro lines (voiced), defined with the vocabulary

struct Builder {
    const Terrain& t;
    const WorldLayout& L;
    HeightGrid g;
    Rng rng;
    Course c;
    Vec3 cursor, cursorDir{1, 0, 0};
    std::vector<Vec3> basins, peaks, used;
    int legCount = 0;
    bool directReturn = false;   // straight transit home instead of the valley route (shorter)

    Builder(const Terrain& t_, const WorldLayout& L_, int level, int variant = 0) : t(t_), L(L_), rng(0x5EED0000ull + level * 7919ull + variant * 104729ull) {
        g = sampleHeightGrid(t, 160.0, 15000.0);
        basins = findBasins(g, 40, 2600.0, 2500.0);
        peaks = findPeaks(g, 12, 3000.0);
        c.level = level;
        c.locationId = t.info().id;
    }

    static Vec3 hdir(Vec3 v) { v.y = 0; return normalize(v); }
    double surf(double x, double z) const { return t.surfaceHeight(x, z); }
    bool insideMap(const Vec3& p, double margin = 1200.0) const {
        return std::fabs(p.x) < Terrain::kHalfSize - margin && std::fabs(p.z) < Terrain::kHalfSize - margin;
    }

    Section& add(SectionKind k, RouteStyle st, std::string title, std::string instruction, std::string hint, std::string intro, double est) {
        Section s;
        s.kind = k;
        s.style = st;
        s.title = std::move(title);
        s.instruction = std::move(instruction);
        s.hint = std::move(hint);
        s.intro = std::move(intro);
        s.gateBegin = s.gateStart = s.gateEnd = static_cast<int>(c.gates.size());
        s.estSeconds = est;
        c.sections.push_back(s);
        return c.sections.back();
    }

    // ---- gates ---------------------------------------------------------------------------------
    /// Adds gates for a polyline of horizontal positions in the given style; heights follow the
    /// terrain. Returns the number of gates added.
    int addGates(std::vector<Vec3> pts, RouteStyle st, double radiusOverride = 0, double aglOverride = 0) {
        const StyleSpec& sp = spec(st);
        double radius = radiusOverride > 0 ? radiusOverride : sp.radius;
        double agl = aglOverride > 0 ? aglOverride : sp.targetAgl;
        int section = static_cast<int>(c.sections.size()) - 1;
        size_t first = c.gates.size();
        for (Vec3 p : pts) {
            p.x = clampd(p.x, -Terrain::kHalfSize + 900, Terrain::kHalfSize - 900);
            p.z = clampd(p.z, -Terrain::kHalfSize + 900, Terrain::kHalfSize - 900);
            Gate gt;
            gt.ground = surf(p.x, p.z);
            double high = maxSurfaceAround(t, p.x, p.z, std::min(radius * 0.9, 250.0));
            gt.pos = {p.x, high + agl, p.z};
            gt.radius = radius;
            gt.section = section;
            c.gates.push_back(gt);
        }
        if (c.gates.size() == first) return 0;
        // clearance of the straight lines between gates, then climb / descent gradients
        for (int pass = 0; pass < 4; ++pass) {
            for (size_t i = first; i < c.gates.size(); ++i) {
                Vec3 prev = i == first ? cursor : c.gates[i - 1].pos;
                double clr = segmentClearance(t, prev, c.gates[i].pos);
                if (clr < sp.lineClear) {
                    double up = sp.lineClear - clr + 5.0;
                    c.gates[i].pos.y += up;
                    if (i > first) c.gates[i - 1].pos.y += up * 0.5;
                }
            }
            const double climbMax = std::tan(9.0 * kDeg), descMax = std::tan(8.0 * kDeg);
            for (size_t i = c.gates.size(); i-- > first + 1;) {   // backwards: raise earlier gates before steep climbs
                double d = std::hypot(c.gates[i].pos.x - c.gates[i - 1].pos.x, c.gates[i].pos.z - c.gates[i - 1].pos.z);
                c.gates[i - 1].pos.y = std::max(c.gates[i - 1].pos.y, c.gates[i].pos.y - d * climbMax);
            }
            for (size_t i = first; i < c.gates.size(); ++i) {   // forwards: no dives steeper than 8 degrees
                Vec3 prev = i == first ? cursor : c.gates[i - 1].pos;
                double d = std::hypot(c.gates[i].pos.x - prev.x, c.gates[i].pos.z - prev.z);
                c.gates[i].pos.y = std::max(c.gates[i].pos.y, prev.y - d * descMax);
            }
        }
        for (size_t i = first; i < c.gates.size(); ++i) {
            Gate& gt = c.gates[i];
            double a = gt.pos.y - gt.ground;
            if (st != RouteStyle::Navigation && st != RouteStyle::Transit) {
                gt.minAgl = std::max(30.0, a - sp.winLo);
                gt.maxAgl = a + sp.winHi;
            }
        }
        cursor = c.gates.back().pos;
        if (c.gates.size() >= 2) cursorDir = hdir(c.gates.back().pos - c.gates[c.gates.size() - 2].pos);
        return static_cast<int>(c.gates.size() - first);
    }

    // ---- Dubins alignment ------------------------------------------------------------------------
    static Vec3 rnorm(Vec3 d) { return {-d.z, 0, d.x}; }
    /// Shortest circle-straight-circle path from (p0, d0) to (p1, d1) with turn radius R, sampled
    /// every ~`spacing` metres (start excluded, end included). Empty if no candidate stays on the map.
    std::vector<Vec3> dubins(Vec3 p0, Vec3 d0, Vec3 p1, Vec3 d1, double R, double spacing, double& outLen, double margin = 450.0) const {
        p0.y = p1.y = 0;
        d0 = hdir(d0);
        d1 = hdir(d1);
        std::vector<Vec3> best;
        double bestLen = 1e18;
        auto ang = [](Vec3 d) { return std::atan2(d.z, d.x); };
        auto dirOf = [](double h) { return Vec3{std::cos(h), 0, std::sin(h)}; };
        auto mod2pi = [](double a) { a = std::fmod(a, 2 * kPi); return a < 0 ? a + 2 * kPi : a; };
        for (int a : {1, -1})
            for (int b : {1, -1}) {
                Vec3 c0 = p0 + rnorm(d0) * (R * a), c1 = p1 + rnorm(d1) * (R * b);
                Vec3 V = c1 - c0;
                double D = length(V);
                Vec3 tdir;
                double Ls;
                if (a == b) {
                    if (D < 1e-6) { tdir = d0; Ls = 0; }
                    else { tdir = V / D; Ls = D; }
                } else {
                    if (D < 2 * R + 1.0) continue;
                    Ls = std::sqrt(D * D - 4 * R * R);
                    double phi = std::atan2(-2.0 * a * R, Ls);
                    tdir = dirOf(ang(V) - phi);
                }
                double h0 = ang(d0), ht = ang(tdir), h1 = ang(d1);
                double A0 = a > 0 ? mod2pi(ht - h0) : mod2pi(h0 - ht);
                double A1 = b > 0 ? mod2pi(h1 - ht) : mod2pi(ht - h1);
                double total = R * (A0 + A1) + Ls;
                if (total >= bestLen) continue;
                Vec3 p0t = c0 - rnorm(tdir) * (a * R);
                int n = std::max(1, static_cast<int>(std::ceil(total / spacing)));
                std::vector<Vec3> pts;
                bool ok = true;
                for (int k = 1; k <= n; ++k) {
                    double sd = total * k / n;
                    Vec3 q;
                    if (sd < R * A0) {
                        double h = h0 + a * sd / R;
                        q = c0 - rnorm(dirOf(h)) * (a * R);
                    } else if (sd < R * A0 + Ls) {
                        q = p0t + tdir * (sd - R * A0);
                    } else {
                        double h = ht + b * (sd - R * A0 - Ls) / R;
                        q = c1 - rnorm(dirOf(h)) * (b * R);
                    }
                    if (k == n) q = p1;
                    if (!insideMap(q, margin)) ok = false;
                    pts.push_back(q);
                }
                if (!ok) continue;
                bestLen = total;
                best = pts;
            }
        outLen = best.empty() ? 0 : bestLen;
        return best;
    }

    /// Adds gates (to the current section) that turn from the cursor onto heading `u` and end at
    /// `A` (which gets radius / agl of the caller), on a turn radius every aircraft can fly.
    void alignTo(Vec3 A, Vec3 u, RouteStyle st, double radius, double agl) {
        double R = std::max(1400.0, turnRadiusFor(st) * 1.2);
        double len = 0;
        std::vector<Vec3> pts;
        for (double k : {1.0, 0.8, 0.65, 0.5, 0.4}) {
            pts = dubins(cursor, cursorDir, A, u, R * k, std::min(spec(st).spacing, 2400.0), len);
            if (!pts.empty()) break;
        }
        for (double k : {0.5, 0.3})
            if (pts.empty()) pts = dubins(cursor, cursorDir, A, u, R * k, std::min(spec(st).spacing, 2400.0), len, -300.0);
        if (pts.empty()) {
            if (std::getenv("SKY_COURSE_DEBUG")) std::fprintf(stderr, "alignTo: no Dubins path from (%.0f %.0f dir %.2f %.2f) to (%.0f %.0f dir %.2f %.2f) style %d section %s\n", cursor.x, cursor.z, cursorDir.x, cursorDir.z, A.x, A.z, u.x, u.z, (int)st, c.sections.back().title.c_str());
            pts = {A};
            len = std::hypot(A.x - cursor.x, A.z - cursor.z);
        }
        Vec3 last = pts.back();
        pts.pop_back();
        if (!pts.empty()) addGates(pts, st);
        addGates({last}, st == RouteStyle::Transit ? RouteStyle::Transit : RouteStyle::Obstacles, radius, agl);
        cursorDir = hdir(u);
        Section& s = c.sections.back();
        s.gateEnd = static_cast<int>(c.gates.size());
        s.estSeconds += len / (0.85 * midSpeed(st));
        c.routeLength += len;
    }

    /// Lowers gates of the current section so that no descent into gate `toGate` is steeper than
    /// `deg` (keeps `clear` metres over the terrain).
    void limitDescentInto(int toGate, double deg, double clear) {
        int b = c.sections.back().gateBegin;
        for (int i = toGate - 1; i >= b; --i) {
            double d = std::hypot(c.gates[i + 1].pos.x - c.gates[i].pos.x, c.gates[i + 1].pos.z - c.gates[i].pos.z);
            double cap = c.gates[i + 1].pos.y + d * std::tan(deg * kDeg);
            double floor = maxSurfaceAround(t, c.gates[i].pos.x, c.gates[i].pos.z, 220.0) + clear;
            c.gates[i].pos.y = std::max(floor, std::min(c.gates[i].pos.y, cap));
        }
    }

    // ---- anchors -------------------------------------------------------------------------------
    Vec3 pickAnchor(double dmin, double dmax, bool lowGround) {
        std::vector<Vec3> cand;
        if (lowGround) cand = basins;
        for (int k = 0; k < 40; ++k) {   // plus random dry points (keeps big plateaus usable)
            Vec3 p{rng.range(-12500, 12500), 0, rng.range(-12500, 12500)};
            if (t.isWater(p.x, p.z) && lowGround) continue;
            p.y = surf(p.x, p.z);
            cand.push_back(p);
        }
        Vec3 best = cursor + cursorDir * dmax;
        double bestScore = 1e18;
        for (const Vec3& p : cand) {
            double d = std::hypot(p.x - cursor.x, p.z - cursor.z);
            if (d < dmin || d > dmax || !insideMap(p, 2200.0)) continue;
            Vec3 to = hdir(p - cursor);
            double ang = std::acos(clampd(dot(cursorDir, to), -1.0, 1.0));
            double reuse = 0;
            for (const Vec3& u : used)
                if (std::hypot(u.x - p.x, u.z - p.z) < 3500) reuse += 1.0;
            double hn = (p.y - g.hmin) / (g.hmax - g.hmin);
            double score = ang * 1.5 + reuse * 2.0 + (lowGround ? hn * 2.0 : 0.0) + rng.uniform() * 1.2;
            if (ang > 130 * kDeg) score += 6.0;
            if (score < bestScore) { bestScore = score; best = p; }
        }
        if (!insideMap(best, 2200.0)) best = {clampd(best.x, -12500, 12500), 0, clampd(best.z, -12500, 12500)};
        best.y = surf(best.x, best.z);
        used.push_back(best);
        return best;
    }

    PathCost costFor(RouteStyle st) const {
        PathCost pc;
        pc.low = spec(st).low;
        pc.slope = spec(st).slope;
        return pc;
    }

    /// One gated route section from the cursor to `target` following the terrain.
    void legTo(Vec3 target, RouteStyle st, const std::string& title, double radiusOverride = 0) {
        Section& s = add(SectionKind::Route, st, title, "", "", styleIntro(st), 0);
        const StyleSpec& sp = spec(st);
        s.speedMin = sp.vMin;
        s.speedMax = sp.vMax;
        s.ceilingAgl = sp.ceiling;
        s.instruction = st == RouteStyle::LowLevel || st == RouteStyle::Precision || st == RouteStyle::HighSpeed
                            ? "Fly every gate inside its height window, below the ceiling. Hold the speed band."
                            : "Fly through every gate. Hold the speed band shown on the HUD.";
        Vec3 c0 = cursor;
        std::vector<Vec3> path = st == RouteStyle::Transit ? std::vector<Vec3>{cursor, target} : valleyPath(g, cursor, target, costFor(st));
        path.front() = cursor;
        std::vector<Vec3> dense = resamplePolyline(smoothPolyline(path, 2), 150.0);
        enforceTurnRadius(dense, turnRadiusFor(st), 120);
        // join the path from the current heading: a constant-radius (Dubins) turn onto the path
        // ~2.5 km along it whenever the path starts off the nose
        std::vector<Vec3> keep;
        bool joined = false;
        if (dense.size() > 2) {
            // the join point 2.5 km along the path, or further when that needs a big loop:
            // of a few join distances and turn radii, the one with the least extra distance
            size_t k25 = 0;
            for (double acc = 0; k25 + 1 < dense.size() && acc < 2500.0; ++k25) acc += std::hypot(dense[k25 + 1].x - dense[k25].x, dense[k25 + 1].z - dense[k25].z);
            Vec3 P0 = dense[std::min(k25, dense.size() - 1)];
            double off = std::acos(clampd(dot(cursorDir, hdir(P0 - cursor)), -1.0, 1.0));
            double offT = k25 + 1 < dense.size() ? std::acos(clampd(dot(cursorDir, hdir(dense[k25 + 1] - dense[k25])), -1.0, 1.0)) : 0.0;
            if (k25 + 1 < dense.size() && (off > 30.0 * kDeg || offT > 60.0 * kDeg)) {
                double R = std::max(1400.0, turnRadiusFor(st) * 1.35);
                double bestExcess = 1e18;
                std::vector<Vec3> bestArc;
                size_t bestK = 0;
                for (double J : {2500.0, 4000.0, 6000.0}) {
                    size_t k = 0;
                    for (double acc = 0; k + 1 < dense.size() && acc < J; ++k) acc += std::hypot(dense[k + 1].x - dense[k].x, dense[k + 1].z - dense[k].z);
                    if (k + 1 >= dense.size()) break;
                    Vec3 P = dense[k];
                    Vec3 T = hdir(dense[k + 1] - dense[k]);
                    for (double f : {1.0, 0.8, 0.65, 0.5}) {
                        if (f < 0.7 && !bestArc.empty()) break;   // tight radii only when nothing else joins
                        double len = 0;
                        std::vector<Vec3> arc = dubins(cursor, cursorDir, P, T, R * f, std::min(sp.spacing, 1600.0), len);
                        if (arc.empty()) continue;
                        // extra distance over the path itself, plus a little for tighter turns
                        double pathLen = 0;
                        for (size_t i = 0; i < k; ++i) pathLen += std::hypot(dense[i + 1].x - dense[i].x, dense[i + 1].z - dense[i].z);
                        double excess = len - pathLen + (1.0 - f) * 3500.0;   // tight turns only to avoid a long loop
                        if (excess < bestExcess) { bestExcess = excess; bestArc = arc; bestK = k; }
                    }
                }
                if (!bestArc.empty()) {
                    keep = bestArc;
                    std::vector<Vec3> rest(dense.begin() + static_cast<long>(bestK), dense.end());
                    std::vector<Vec3> rp = resamplePolyline(rest, sp.spacing);
                    for (size_t i = 1; i < rp.size(); ++i) keep.push_back(rp[i]);
                    joined = true;
                }
            }
        }
        if (!joined) {
            std::vector<Vec3> pts = resamplePolyline(dense, sp.spacing);
            // drop points too close to the cursor (they would be behind or right on the nose)
            for (const Vec3& p : pts)
                if (std::hypot(p.x - cursor.x, p.z - cursor.z) > sp.spacing * 0.7 || !keep.empty()) keep.push_back(p);
        }
        if (keep.empty()) keep.push_back(target);
        addGates(keep, st, radiusOverride);
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        double len = 0;
        Vec3 prev = c0;
        for (int i = s2.gateBegin; i < s2.gateEnd; ++i) { len += std::hypot(c.gates[i].pos.x - prev.x, c.gates[i].pos.z - prev.z); prev = c.gates[i].pos; }
        s2.estSeconds = len / (0.85 * midSpeed(st)) + 6.0;
        c.routeLength += len;
        ++legCount;
    }

    void leg(RouteStyle st, const std::string& title) {
        bool low = st != RouteStyle::Navigation && st != RouteStyle::Transit;
        double dmin = st == RouteStyle::Navigation ? 9000 : (st == RouteStyle::HighSpeed ? 9000 : 6500);
        double dmax = st == RouteStyle::Navigation ? 15000 : (st == RouteStyle::HighSpeed ? 15000 : 12000);
        legTo(pickAnchor(dmin, dmax, low), st, title);
    }

    // ---- takeoff ---------------------------------------------------------------------------------
    Vec3 takeoffPos;
    double takeoffHeading = 0;
    void departure(bool cold) {
        const RunwayInfo& rw = t.runway();
        // depart in the direction with the better climb-out
        double best = -1e9;
        for (double sgn : {1.0, -1.0}) {
            Vec3 u{std::cos(rw.heading) * sgn, 0, std::sin(rw.heading) * sgn};
            Vec3 end = rw.center + u * (rw.length * 0.5);
            double worst = 1e9;
            for (double d = 1000; d <= 9000; d += 250) {
                Vec3 q = end + u * d;
                double allowed = rw.center.y + 40.0 + d * std::tan(7.0 * kDeg);
                worst = std::min(worst, allowed - surf(q.x, q.z));
                if (!insideMap(q, 300)) worst = std::min(worst, -500.0 + d * 0.01);
            }
            if (worst > best) {
                best = worst;
                takeoffHeading = std::atan2(u.z, u.x);
                takeoffPos = rw.center - u * (rw.length * 0.5 - 60.0);
                cursorDir = u;
            }
        }
        takeoffPos.y = rw.center.y;
        c.startPos = takeoffPos;
        c.startHeading = takeoffHeading;
        c.coldStart = cold;
        if (cold) {
            add(SectionKind::ColdStart, RouteStyle::Transit, "ENGINE START", "Press E to start the engine and wait for it to spool up.", "E = ENGINE START",
                "Start the engine with E.", 15);
        }
        add(SectionKind::Lineup, RouteStyle::Transit, "BRAKES OFF, FULL POWER", "Release the parking brake (B) and run the throttle to full military power (hold W).",
            "B = BRAKES   W = THROTTLE", "Brakes off, run it up to full power.", 8);
        add(SectionKind::Takeoff, RouteStyle::Transit, "TAKEOFF", "Accelerate down the runway. At rotation speed pull back gently (arrow down) and climb away.",
            "ARROW DOWN = ROTATE", "", 32);
        add(SectionKind::GearUp, RouteStyle::Transit, "GEAR UP", "Positive climb: raise the landing gear (G).", "G = GEAR", "Positive climb, gear up.", 5);
        // climb to the route height a few kilometres out
        Vec3 out = rw.center + cursorDir * (rw.length * 0.5 + 5500.0);
        double top = std::max(maxSurfaceAround(t, out.x, out.z, 1500.0), rw.center.y) + 450.0;
        Section& cl = add(SectionKind::Climb, RouteStyle::Transit, "CLIMB OUT", "Climb straight ahead to the altitude on the HUD.", "",
                          "Climb straight ahead.", 0);
        cl.value = std::max(top, rw.center.y + 500.0);
        cl.estSeconds = (cl.value - rw.center.y) / 25.0 + 18.0;
        cursor = {out.x, cl.value, out.z};
    }

    /// Catapult launch from the carrier: afterburner, brake off, the shot, gear up, climb out.
    void departureCarrier(const DeckInfo& deck) {
        c.carrierStart = true;
        c.deck = deck;
        Vec3 f = deck.forward();
        c.startPos = deck.catapultStart();
        c.startPos.y = deck.center.y;
        c.startHeading = deck.heading;
        add(SectionKind::Lineup, RouteStyle::Transit, "AFTERBURNER, THEN BRAKE OFF", "You're on the catapult. Full afterburner (hold W), then release the brake (B): the catapult fires.",
            "W = AFTERBURNER   B = BRAKE", "You're on the catapult. Full afterburner, then brakes off and the cat fires.", 12);
        add(SectionKind::Takeoff, RouteStyle::Transit, "CATAPULT LAUNCH", "The catapult throws you off the bow: keep the wings level and let the jet climb.", "", "", 10);
        add(SectionKind::GearUp, RouteStyle::Transit, "GEAR UP", "Positive climb: raise the landing gear (G).", "G = GEAR", "Positive climb, gear up.", 5);
        Vec3 out = deck.center + f * 6000.0;
        double top = std::max(maxSurfaceAround(t, out.x, out.z, 1500.0), 0.0) + 450.0;
        Section& cl = add(SectionKind::Climb, RouteStyle::Transit, "CLIMB OUT", "Climb straight ahead to the altitude on the HUD.", "", "Climb straight ahead.", 0);
        cl.value = std::max(top, 500.0);
        cl.estSeconds = cl.value / 25.0 + 18.0;
        cursor = {out.x, cl.value, out.z};
        cursorDir = f;
    }

    void climbTo(double alt, const std::string& title, const std::string& intro, double minAgl = 600.0) {
        Section& cl = add(SectionKind::Climb, RouteStyle::Transit, title, "Climb to the altitude on the HUD.", "", intro, 0);
        cl.value = alt;
        cl.value2 = minAgl;
        cl.estSeconds = std::max(15.0, (alt - cursor.y) / 30.0 + 10.0);
        cursor.y = std::max(cursor.y, alt);
    }

    double safeAltitudeHere(double radius, double margin) const {
        double top = cursor.y - 400.0;
        for (int r = 0; r <= 8; ++r)
            for (int k = 0; k < 24; ++k) {
                double a = k * kPi / 12.0, d = radius * r / 8.0;
                top = std::max(top, surf(cursor.x + std::cos(a) * d, cursor.z + std::sin(a) * d));
            }
        return top + margin;
    }

    // ---- manoeuvres ---------------------------------------------------------------------------
    void manoeuvre(SectionKind k) {
        switch (k) {
            case SectionKind::LevelTurn: {
                Section& s = add(k, RouteStyle::Transit, "LEVEL TURN, 60 DEG BANK", "Roll to 60 degrees of bank and turn through 360 degrees, holding your altitude within 100 m.",
                                 "BANK 60", "Level turn. Roll to sixty degrees of bank and turn all the way round. Hold your height.", 85);
                s.value = 60; s.value2 = 360;
                break;
            }
            case SectionKind::ClimbTurn: {
                Section& s = add(k, RouteStyle::Transit, "CLIMBING TURN", "Turn through 360 degrees while climbing at least 600 m. Keep 40 to 60 degrees of bank.",
                                 "", "Climbing turn. Full power, forty to sixty degrees of bank, and climb six hundred metres through a full turn.", 130);
                s.value = 600; s.value2 = 330;
                break;
            }
            case SectionKind::DescendTurn: {
                Section& s = add(k, RouteStyle::Transit, "DESCENDING TURN", "Turn through 360 degrees while descending at least 600 m with 40 to 60 degrees of bank, without overspeeding.",
                                 "", "Descending turn. Power back, forty to sixty degrees of bank, and lose six hundred metres through a full turn. Watch your speed.", 125);
                s.value = 600; s.value2 = 330;
                break;
            }
            case SectionKind::HighG: {
                // in the thin air above 4.5 km no jet can hold 5.5 g for long: 4.5 g up there
                bool high = cursor.y > 4500.0;
                Section& s = add(k, RouteStyle::Transit, high ? "HIGH G: 4.5 G FOR 3 S" : "HIGH G: 5.5 G FOR 3 S",
                                 high ? "Pull at least 4.5 g and hold it for 3 seconds." : "Pull at least 5.5 g and hold it for 3 seconds.", "PULL",
                                 high ? "High G. Pull four and a half G and hold it for three seconds. Tense up."
                                      : "High G. Pull five and a half G and hold it for three seconds. Tense up.", 18);
                s.value = high ? 4.5 : 5.5; s.value2 = 3.0;
                break;
            }
            case SectionKind::Loop: {
                Section& s = add(k, RouteStyle::Transit, "LOOP", "Full power, wings level, pull through a full loop.", "PULL",
                                 "Loop. Full power, wings level, and pull all the way over the top.", 35);
                s.value = 2.0 * kPi * 0.92;
                break;
            }
            case SectionKind::Roll: {
                Section& s = add(k, RouteStyle::Transit, "AILERON ROLL", "Roll through a full 360 degrees.", "ARROWS", "Aileron roll. Full stick to the side, one complete roll.", 10);
                s.value = 2.0 * kPi * 0.95;
                break;
            }
            case SectionKind::Recovery: {
                Section& s = add(k, RouteStyle::Transit, "UNUSUAL ATTITUDE RECOVERY", "Pull the nose up past 45 degrees, then recover to wings-level flight within 15 seconds.",
                                 "", "Recovery. Pull the nose up past forty-five degrees, then recover to level flight. Power up, roll level, nose to the horizon.", 30);
                s.value = 45; s.value2 = 15;
                break;
            }
            default: break;
        }
    }

    // ---- obstacles -----------------------------------------------------------------------------
    /// A straight, gentle stretch of low ground of the given length, as close to the cursor as
    /// possible (start point and direction).
    bool flatStretch(double len, Vec3& start, Vec3& dir, double maxRelief, double maxFactor = 10.0) {
        for (double k : {1.0, 2.0, 3.5, 6.0, 10.0})
            if (k <= maxFactor)
            if (flatStretchOnce(len, start, dir, maxRelief * k)) return true;
        return false;
    }
    bool flatStretchOnce(double len, Vec3& start, Vec3& dir, double maxRelief) {
        double best = 1e18;
        bool found = false;
        std::vector<Vec3> cand = basins;
        for (int k = 0; k < 60; ++k) cand.push_back({rng.range(-12000, 12000), 0, rng.range(-12000, 12000)});
        for (const Vec3& b : cand) {
            for (int h = 0; h < 12; ++h) {
                double a = h * kPi / 6.0;
                Vec3 u{std::cos(a), 0, std::sin(a)};
                Vec3 s0 = b - u * (len * 0.5);
                Vec3 s1 = b + u * (len * 0.5);
                if (!insideMap(s0, 2200) || !insideMap(s1, 2200)) continue;
                double lo = 1e9, hi = -1e9;
                bool ok = true;
                for (double d = -1700; d <= len + 400 && ok; d += 150) {
                    Vec3 q = s0 + u * d;
                    for (double lat : {-320.0, 0.0, 320.0}) {
                        Vec3 r{-u.z, 0, u.x};
                        Vec3 qq = q + r * lat;
                        if (t.onRunway(qq.x, qq.z, 300)) { ok = false; break; }
                        double h0 = t.surfaceHeight(qq.x, qq.z);   // sea stacks are fine
                        lo = std::min(lo, h0);
                        hi = std::max(hi, h0);
                    }
                }
                if (!ok || hi - lo > maxRelief) continue;
                // keep clear of the route flown so far (a new obstacle field must not sit on it)
                Vec3 e0 = s0 - u * 500.0, e1 = s0 + u * (len + 500.0);
                bool nearRoute = false;
                for (size_t gi = 1; gi < c.gates.size() && !nearRoute; ++gi) {
                    const Vec3& p0 = c.gates[gi - 1].pos;
                    const Vec3& p1 = c.gates[gi].pos;
                    for (int k = 0; k <= 8 && !nearRoute; ++k) {
                        Vec3 q = p0 + (p1 - p0) * (k / 8.0);
                        if (segDist(q, e0, e1) < 500.0) nearRoute = true;
                    }
                }
                if (nearRoute) continue;
                double dist = std::hypot(s0.x - cursor.x, s0.z - cursor.z);
                double reuse = 0;
                for (const Obstacle& o : c.obstacles)
                    if (std::hypot(o.pos.x - b.x, o.pos.z - b.z) < len) reuse += 1.0;
                // approach from behind: the cursor should be on the entry side, not alongside the field
                double behind = std::max(0.0, dot(cursor - s0, u) + 2500.0);
                double score = (hi - lo) * 8.0 + dist * 0.6 + reuse * 5000.0 + behind * 1.5;
                if (score < best) { best = score; start = s0; dir = u; found = true; }
            }
        }
        return found;
    }

    /// Route to the entry of an obstacle stretch (so the obstacles are flown straight).
    void approachStretch(Vec3 start, Vec3 dir, RouteStyle st, const std::string& title, bool arriveLow = true) {
        Vec3 A = start - dir * 1500.0;
        if (std::hypot(A.x - cursor.x, A.z - cursor.z) > 9000.0) {
            legTo(start - dir * 4500.0, st, title);
        } else {
            Section& s = add(SectionKind::Route, st, title, "Fly through every gate. Hold the speed band shown on the HUD.", "", styleIntro(st), 6);
            s.speedMin = spec(st).vMin;
            s.speedMax = spec(st).vMax;
        }
        alignTo(A, dir, st, 140, 90);
        // arrive low: the obstacle gates are 70-100 m above the ground, so the last approach
        // gate may not hold the jet high above the run (fjord cliffs beside it raise it)
        Gate& ga = c.gates.back();
        double runGround = surf(start.x, start.z);
        double cap = std::max(runGround + 170.0, maxSurfaceAround(t, ga.pos.x, ga.pos.z, 90.0) + 60.0);
        if (arriveLow && ga.pos.y > cap) {
            ga.pos.y = cap;
            if (ga.maxAgl > 0) { ga.minAgl = std::max(30.0, cap - ga.ground - 60.0); ga.maxAgl = cap - ga.ground + 60.0; }
            limitDescentInto(static_cast<int>(c.gates.size()) - 1, 6.0, 120.0);
            cursor = ga.pos;
        }
    }

    /// Obstacle runs on sloping ground: the gate line may not bob up and down more than
    /// `step` metres from gate to gate (raises the low ones; windows follow the new heights).
    void smoothObstacleGates(int first, double step) {
        int last = static_cast<int>(c.gates.size());
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = first + 1; i < last; ++i) c.gates[i].pos.y = std::max(c.gates[i].pos.y, c.gates[i - 1].pos.y - step);
            for (int i = last - 2; i >= first; --i) c.gates[i].pos.y = std::max(c.gates[i].pos.y, c.gates[i + 1].pos.y - step);
        }
        for (int i = first; i < last; ++i) {
            Gate& g = c.gates[i];
            double agl = g.pos.y - g.ground;
            if (g.maxAgl > 0 && agl + 40.0 > g.maxAgl) { g.maxAgl = agl + 60.0; g.minAgl = std::max(35.0, agl - 60.0); }
        }
    }

    void slalom(int pillars) {
        Vec3 s0, u;
        double len = 1300.0 * (pillars + 1);
        if (!flatStretch(len, s0, u, 140.0)) return;
        approachStretch(s0, u, RouteStyle::Valley, "TO THE PILLARS");
        Section& s = add(SectionKind::Route, RouteStyle::Obstacles, "PILLAR SLALOM", "Weave around the rock pillars: pass each gate on its side of the pillar.",
                         "", "Pillar slalom. Slow down, and weave left and right around the rock towers through the gates.", 0);
        s.speedMin = spec(RouteStyle::Obstacles).vMin;
        s.speedMax = spec(RouteStyle::Obstacles).vMax;
        Vec3 r{-u.z, 0, u.x};
        std::vector<Vec3> pts;
        for (int k = 0; k < pillars; ++k) {
            Vec3 p = s0 + u * (1300.0 * (k + 1));
            Obstacle o;
            o.kind = Obstacle::Kind::Pillar;
            o.pos = {p.x, t.height(p.x, p.z), p.z};
            o.radius = 38.0 + rng.uniform() * 16.0;
            o.height = 260.0 + rng.uniform() * 170.0;
            o.seed = static_cast<unsigned>(rng.next() & 0xffff);
            o.height += std::max(0.0, surf(o.pos.x, o.pos.z) - o.pos.y);   // standing in water: rises from the bed
            c.obstacles.push_back(o);
            double side = (k % 2 == 0) ? 1.0 : -1.0;
            pts.push_back(p + r * (side * 210.0));
        }
        pts.push_back(s0 + u * (1300.0 * (pillars + 1)));
        int first = static_cast<int>(c.gates.size());
        addGates(pts, RouteStyle::Obstacles, 75, 95);
        for (int i = first; i < static_cast<int>(c.gates.size()); ++i) c.gates[i].cue = GateCue::Pillars;
        smoothObstacleGates(first, 70.0);
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = len / (0.8 * midSpeed(RouteStyle::Obstacles)) + 4;
        c.routeLength += len;
        cursorDir = u;
    }

    void arches(int count) {
        Vec3 s0, u;
        double len = 1500.0 * (count + 1);
        if (!flatStretch(len, s0, u, 70.0, 1.0) && !flatStretch(len, s0, u, 110.0, 2.0)) return;   // arches need a real valley floor
        approachStretch(s0, u, RouteStyle::Valley, "TO THE ARCHES");
        Section& s = add(SectionKind::Route, RouteStyle::Obstacles, "ARCH RUN", "Fly through the natural arches: below the rock, between the legs.",
                         "", "Arch run. Line up early and fly through the middle of each arch, below the rock.", 0);
        s.speedMin = spec(RouteStyle::Obstacles).vMin;
        s.speedMax = spec(RouteStyle::Obstacles).vMax;
        std::vector<Vec3> pts;
        for (int k = 0; k < count; ++k) {
            Vec3 p = s0 + u * (1500.0 * (k + 1));
            Obstacle o;
            o.kind = Obstacle::Kind::Arch;
            o.pos = {p.x, t.height(p.x, p.z), p.z};
            o.radius = 28.0;
            o.span = 175.0;
            o.height = 195.0;
            o.thickness = 36.0;
            o.heading = std::atan2(u.z, u.x) + kPi / 2;
            o.seed = static_cast<unsigned>(rng.next() & 0xffff);
            o.height += std::max(0.0, surf(o.pos.x, o.pos.z) - o.pos.y);   // standing in water: rises from the bed
            c.obstacles.push_back(o);
            pts.push_back(p);
        }
        pts.push_back(s0 + u * len);
        int first = static_cast<int>(c.gates.size());
        // gate heights are exact here: 55 m above the ground under each arch
        for (size_t k = 0; k < pts.size(); ++k) {
            Gate gt;
            gt.ground = surf(pts[k].x, pts[k].z);
            bool arch = k + 1 < pts.size();
            gt.pos = {pts[k].x, gt.ground + (arch ? 72.0 : 95.0), pts[k].z};
            gt.radius = arch ? 40.0 : 120.0;
            gt.minAgl = arch ? 35.0 : 40.0;
            gt.maxAgl = arch ? 125.0 : 220.0;
            gt.cue = GateCue::Arch;
            gt.section = static_cast<int>(c.sections.size()) - 1;
            c.gates.push_back(gt);
        }
        cursor = c.gates.back().pos;
        cursorDir = u;
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = len / (0.8 * midSpeed(RouteStyle::Obstacles)) + 4;
        c.routeLength += len;
        (void)first;
    }

    void spanBridge() {
        // a suspension bridge across a valley: towers on the slopes, a gate under the deck
        Vec3 s0, u;
        if (!flatStretch(5000.0, s0, u, 120.0, 3.5)) return;
        approachStretch(s0, u, RouteStyle::Valley, "TO THE BRIDGE");
        Section& s = add(SectionKind::Route, RouteStyle::Obstacles, "UNDER THE BRIDGE", "Fly under the bridge deck, between the towers.", "",
                         "Bridge ahead. Get down and fly under the deck, right through the middle.", 0);
        s.speedMin = spec(RouteStyle::Obstacles).vMin;
        s.speedMax = spec(RouteStyle::Obstacles).vMax;
        Vec3 p = s0 + u * 2000.0;
        double ground = t.height(p.x, p.z);
        Obstacle o;
        o.kind = Obstacle::Kind::Deck;
        o.pos = {p.x, ground, p.z};
        o.heading = std::atan2(u.z, u.x) + kPi / 2;   // deck runs across the route
        o.span = 560.0;
        o.width = 16.0;
        o.height = 105.0;      // underside above the ground
        o.thickness = 7.0;
        o.radius = 9.0;        // tower radius
        o.seed = 7;
        o.height += std::max(0.0, surf(o.pos.x, o.pos.z) - o.pos.y);
        c.obstacles.push_back(o);
        Gate gt;
        gt.ground = surf(p.x, p.z);
        gt.pos = {p.x, gt.ground + 45.0, p.z};
        gt.radius = 30.0;
        gt.minAgl = 20.0;
        gt.maxAgl = 85.0;
        gt.cue = GateCue::Bridge;
        gt.section = static_cast<int>(c.sections.size()) - 1;
        c.gates.push_back(gt);
        cursor = gt.pos;
        cursorDir = u;
        addGates({p + u * 2000.0}, RouteStyle::Obstacles, 140, 90);
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = 4000.0 / (0.8 * midSpeed(RouteStyle::Obstacles)) + 4;
        c.routeLength += 4000.0;
    }

    void masts(int count) {
        Vec3 s0, u;
        double len = 1100.0 * (count + 1);
        const Town* it = L.industrialTown();
        bool ok = false;
        if (it) {
            // run past the industrial zone if there is a straight approach to it
            for (int h = 0; h < 12 && !ok; ++h) {
                double a = h * kPi / 6.0;
                u = {std::cos(a), 0, std::sin(a)};
                s0 = it->center - u * (len * 0.5) + Vec3{-u.z, 0, u.x} * (it->radius + 350.0);
                Vec3 s1 = s0 + u * len;
                if (!insideMap(s0, 2200) || !insideMap(s1, 2200)) continue;
                double lo = 1e9, hi = -1e9;
                for (double d = 0; d <= len; d += 150) {
                    Vec3 q = s0 + u * d;
                    double h0 = t.height(q.x, q.z);
                    lo = std::min(lo, h0); hi = std::max(hi, h0);
                    if (t.isWater(q.x, q.z)) { hi = 1e9; break; }
                }
                ok = hi - lo < 120.0;
                for (size_t gi = 1; gi < c.gates.size() && ok; ++gi)
                    for (int k = 0; k <= 8 && ok; ++k) {
                        Vec3 q = c.gates[gi - 1].pos + (c.gates[gi].pos - c.gates[gi - 1].pos) * (k / 8.0);
                        if (segDist(q, s0 - u * 500.0, s1 + u * 500.0) < 500.0) ok = false;
                    }
            }
        }
        if (!ok && !flatStretch(len, s0, u, 120.0)) return;
        approachStretch(s0, u, RouteStyle::Valley, "TO THE INDUSTRIAL ZONE");
        Section& s = add(SectionKind::Route, RouteStyle::Obstacles, "MAST SLALOM", "Weave between the radio masts of the industrial zone.", "",
                         "Masts ahead. Weave between them through the gates, and stay off the wires: keep it smooth.", 0);
        s.speedMin = spec(RouteStyle::Obstacles).vMin;
        s.speedMax = spec(RouteStyle::Obstacles).vMax;
        Vec3 r{-u.z, 0, u.x};
        std::vector<Vec3> pts;
        for (int k = 0; k < count; ++k) {
            Vec3 p = s0 + u * (1100.0 * (k + 1));
            Obstacle o;
            o.kind = Obstacle::Kind::Mast;
            o.pos = {p.x, t.height(p.x, p.z), p.z};
            o.radius = 7.0;
            o.height = 240.0;
            o.seed = static_cast<unsigned>(k);
            o.height += std::max(0.0, surf(o.pos.x, o.pos.z) - o.pos.y);   // standing in water: rises from the bed
            c.obstacles.push_back(o);
            pts.push_back(p + r * ((k % 2 == 0 ? 1.0 : -1.0) * 180.0));
        }
        pts.push_back(s0 + u * len);
        int first = static_cast<int>(c.gates.size());
        addGates(pts, RouteStyle::Obstacles, 70, 100);
        for (int i = first; i < static_cast<int>(c.gates.size()); ++i) c.gates[i].cue = GateCue::Masts;
        smoothObstacleGates(first, 70.0);
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = len / (0.8 * midSpeed(RouteStyle::Obstacles)) + 4;
        c.routeLength += len;
        cursorDir = u;
    }

    void compound() {
        Vec3 spot;
        Vec3 near = cursor + cursorDir * 6000.0;
        std::vector<Vec3> avoid;
        for (const Obstacle& o : c.obstacles) avoid.push_back(o.pos);
        for (const Town& tw : L.towns) avoid.push_back(tw.center);
        near.x = clampd(near.x, -10000, 10000);
        near.z = clampd(near.z, -10000, 10000);
        if (!findFlatSpot(t, near, 9000.0, 0.06, 260.0, spot, avoid, 1300.0) || !insideMap(spot, 3500.0)) {
            near = cursor * 0.4;
            if (!findFlatSpot(t, near, 9000.0, 0.06, 260.0, spot, avoid, 1300.0) || !insideMap(spot, 3500.0)) return;
        }
        Vec3 u0 = normalize(Vec3{spot.x - cursor.x, 0, spot.z - cursor.z});
        approachStretch(spot - u0 * 2300.0, u0, RouteStyle::Valley, "TO THE COMPOUND", false);   // overflown high, not low
        Section& s = add(SectionKind::Route, RouteStyle::Obstacles, "COMPOUND OVERFLIGHT", "Overfly the military compound no lower than 250 m above the ground.", "",
                         "Military compound ahead. Overfly it no lower than two hundred and fifty metres.", 0);
        s.speedMin = 160; s.speedMax = 280;
        const GroundKind kinds[] = {GroundKind::Hangar, GroundKind::Radar, GroundKind::SAM, GroundKind::Tank, GroundKind::Tank, GroundKind::Bunker, GroundKind::Truck, GroundKind::Command};
        for (int k = 0; k < 8; ++k) {
            double a = k * 0.8, d = 70.0 + 45.0 * k;
            Vec3 p = spot + Vec3{std::cos(a) * d, 0, std::sin(a) * d};
            p.y = t.height(p.x, p.z);
            c.props.push_back({kinds[k], p, a + 1.2, ""});
        }
        Vec3 u = u0;
        Gate gt;
        gt.ground = surf(spot.x, spot.z);
        gt.pos = {spot.x, gt.ground + 340.0, spot.z};
        gt.radius = 260.0;
        gt.minAgl = 250.0;
        gt.maxAgl = 620.0;
        gt.cue = GateCue::Compound;
        gt.section = static_cast<int>(c.sections.size()) - 1;
        c.gates.push_back(gt);
        cursor = gt.pos;
        cursorDir = u;
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = 3500.0 / 220.0 + 3;
        c.routeLength += 3500.0;
    }

    void viaduct() {
        // the longest bridge (rail preferred) whose deck really stands clear above the ground
        // at mid-span and that sits well inside the map
        int bi = -1;
        double bestLen = 0;
        for (int pass = 0; pass < 2 && bi < 0; ++pass)
            for (size_t i = 0; i < L.bridges.size(); ++i) {
                const Bridge& cb = L.bridges[i];
                if (pass == 0 && !cb.rail) continue;
                Vec3 m = (cb.a + cb.b) * 0.5;
                double len = std::hypot(cb.b.x - cb.a.x, cb.b.z - cb.a.z);
                if (cb.maxClearance < 15.0 || m.y - surf(m.x, m.z) < 20.0 || !insideMap(m, 2500)) continue;
                if (std::fabs(cb.b.y - cb.a.y) > len * 0.08) continue;   // a ramp, not a viaduct
                if (len > bestLen) { bestLen = len; bi = static_cast<int>(i); }
            }
        if (bi < 0) return;
        const Bridge& b = L.bridges[bi];
        Vec3 mid = (b.a + b.b) * 0.5;
        Vec3 axis = normalize(Vec3{b.b.x - b.a.x, 0, b.b.z - b.a.z});
        Vec3 u{-axis.z, 0, axis.x};
        if (dot(u, mid - cursor) < 0) u = -u;
        if (!insideMap(mid + u * 2400.0, 1500) || !insideMap(mid - u * 2400.0, 1500)) return;
        approachStretch(mid - u * 1200.0, u, RouteStyle::Valley, "TO THE VIADUCT", false);   // the deck may stand high above the valley
        Section& s = add(SectionKind::Route, RouteStyle::Obstacles, "VIADUCT CROSSING", "Cross the viaduct low: 30 to 120 m above the deck.", "",
                         "Viaduct ahead. Cross it low, just above the deck.", 0);
        s.speedMin = 150; s.speedMax = 260;
        Gate gt;
        gt.ground = surf(mid.x, mid.z);
        gt.pos = {mid.x, mid.y + 70.0, mid.z};
        gt.radius = 70.0;
        gt.minAgl = (mid.y - gt.ground) + 30.0;
        gt.maxAgl = (mid.y - gt.ground) + 120.0;
        gt.cue = GateCue::Bridge;
        gt.section = static_cast<int>(c.sections.size()) - 1;
        c.gates.push_back(gt);
        cursor = gt.pos;
        cursorDir = u;
        addGates({mid + u * 2200.0}, RouteStyle::Valley);
        Section& s2 = c.sections.back();
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = 5200.0 / 210.0 + 3;
        c.routeLength += 5200.0;
    }

    // ---- formation, tanker -----------------------------------------------------------------------
    /// Highest surface under a circle of `radius` (+- `band`) around c.
    double ringTop(Vec3 c0, double radius, double band) const {
        double top = -1e9;
        for (int k = 0; k < 48; ++k) {
            double a = k * kPi / 24.0;
            for (double rr : {radius - band, radius - band * 0.5, radius, radius + band * 0.5, radius + band})
                top = std::max(top, surf(c0.x + std::cos(a) * rr, c0.z + std::sin(a) * rr));
        }
        return top;
    }
    /// Orbit centre near `near` (within `search` m) over the lowest ground, kept inside the map.
    Vec3 lowOrbitCentre(Vec3 near, double radius, double band, double search, double& top) const {
        Vec3 best = near;
        top = 1e18;
        double lim = Terrain::kHalfSize - radius - band - 600.0;
        for (int i = -4; i <= 4; ++i)
            for (int j = -4; j <= 4; ++j) {
                Vec3 c0{clampd(near.x + i * search / 4.0, -lim, lim), 0, clampd(near.z + j * search / 4.0, -lim, lim)};
                double tp = ringTop(c0, radius, band) + std::hypot(c0.x - near.x, c0.z - near.z) * 0.01;
                if (tp < top) { top = tp; best = c0; }
            }
        top = ringTop(best, radius, band);
        return best;
    }

    void formation() {
        // the lead's loop: a long racetrack near the current area, over the lowest ground around
        double top = 0;
        Vec3 ctr = lowOrbitCentre(cursor + cursorDir * 6000.0, 5500.0, 1800.0, 6000.0, top);
        // the join cuts across the loop: clear the whole disc, not just the ring
        top = std::max(top, ringTop(ctr, 2600.0, 2600.0));
        double h0 = std::atan2(cursorDir.z, cursorDir.x);
        // fly to the loop first, arriving along it in the lead's direction, so the join happens
        // over the cleared ground (a Dubins join: no U-turn gates)
        {
            double bd = 1e18;
            Vec3 entry{}, tang{};
            for (int k = 0; k < 16; ++k) {
                double a = h0 - kPi / 2 + k * kPi / 8.0;
                Vec3 q{ctr.x + std::cos(a) * 5500.0, 0, ctr.z + std::sin(a) * 5500.0};
                Vec3 tg{-std::sin(a), 0, std::cos(a)};
                double dd = std::hypot(q.x - cursor.x, q.z - cursor.z);
                double score = dd + (1.0 - dot(tg, cursorDir)) * 1500.0;
                if (score < bd) { bd = score; entry = q; tang = tg; }
            }
            if (std::hypot(entry.x - cursor.x, entry.z - cursor.z) > 1500.0 || dot(tang, cursorDir) < 0.5) {
                Section& rs = add(SectionKind::Route, RouteStyle::Transit, "TO THE RENDEZVOUS", "Fly the gates to the rendezvous point.", "",
                                  styleIntro(RouteStyle::Transit), 4);
                rs.speedMin = spec(RouteStyle::Transit).vMin;
                rs.speedMax = spec(RouteStyle::Transit).vMax;
                alignTo(entry, tang, RouteStyle::Transit, 300, 250);
                cursor = c.gates.back().pos;
            }
        }
        double alt = std::max(top + 750.0, safeAltitudeHere(4000.0, 700.0));
        climbTo(alt, "CLIMB TO JOIN THE LEAD", "Climb, your lead is joining you.");
        Section& s = add(SectionKind::Formation, RouteStyle::Transit, "FORMATION: RIGHT ECHELON",
                         "Join your lead and hold right echelon (behind and to the right) for 60 seconds.", "",
                         "Formation. Your lead will pull up ahead of you. Join right echelon: behind and to the right, and hold it.", 100);
        s.value = 60.0;
        c.formationRoute.clear();
        // the lead circles in gentle 22.5 deg steps (sharp corners would throw the wingman off)
        for (int k = 0; k < 16; ++k) {
            double a = h0 - kPi / 2 + k * kPi / 8.0;
            c.formationRoute.push_back({ctr.x + std::cos(a) * 5500.0, alt, ctr.z + std::sin(a) * 5500.0});
        }
        c.formationSpeed = 190.0;
        cursor = c.formationRoute[6];
        cursorDir = normalize(c.formationRoute[7] - c.formationRoute[6]);
        cursorDir.y = 0;
        cursorDir = normalize(cursorDir);
    }

    void tanker() {
        // orbit over the lowest ground near the middle of the map, 1.8 km above it
        c.tanker = true;
        c.tankerRadius = 8000.0;
        double top = 1e18;
        Vec3 ctr{};
        // the lowest ground for the orbit (a tighter orbit on a high map: jets have a ceiling)
        for (double R : {8000.0, 6500.0, 5200.0}) {
            double tp = 0;
            Vec3 cc = lowOrbitCentre({clampd(cursor.x * 0.3, -4000, 4000), 0, clampd(cursor.z * 0.3, -4000, 4000)}, R, 2000.0, R > 7000 ? 3000.0 : 7000.0, tp);
            double sc = tp + (8000.0 - R) * 0.6;   // a tighter orbit only to save a lot of height
            if (sc < top) { top = sc; ctr = cc; c.tankerRadius = R; }
        }
        top = ringTop(ctr, c.tankerRadius, 2000.0);
        double alt = std::ceil((top + 1800.0) / 500.0) * 500.0;
        alt = std::max(alt, 3000.0);
        c.tankerCenter = {ctr.x, alt, ctr.z};
        c.tankerSpeed = 175.0;
        climbTo(alt, "CLIMB TO THE TANKER", "Climb to the tanker's altitude.");
        Section& r = add(SectionKind::Rendezvous, RouteStyle::Transit, "JOIN THE TANKER", "Fly to the tanker and close to within 900 m. Come in from behind.", "",
                         "Tanker is orbiting at " + TrainingSession::numberWords(static_cast<int>(alt)) + " metres. Join from behind.", 0);
        double d = std::hypot(cursor.x - ctr.x, cursor.z - ctr.z) + c.tankerRadius;
        r.estSeconds = d / 240.0 + 45.0;
        r.value = 900.0;
        Section& f = add(SectionKind::Refuel, RouteStyle::Transit, "REFUEL: 25 S IN CONTACT", "Plug the basket and stay in contact for 25 seconds. Use the cues: UP, DOWN, LEFT, RIGHT, ADD POWER.", "",
                         "Stabilise behind the basket, match its speed, and push in gently.", 75);
        f.value = 25.0;
        cursor = {ctr.x + c.tankerRadius, alt, ctr.z};
        cursorDir = {0, 0, 1};
    }

    // ---- landings --------------------------------------------------------------------------------
    // Runway landing. A straight-in approach (two-segment glide path) where the terrain allows it;
    // where a ridge stands on the extended centreline (deep valleys, fjords) a circling approach:
    // a base leg from the side and a descending 90-degree turn onto a short straight final.
    void landRunway() {
        const RunwayInfo& rw = t.runway();
        const double eyeY = rw.center.y + FlightModel::kGearHeight;
        double bestScore = -1e18;
        Vec3 aim, u;
        double glide = 3.0, glideOut = 3.0, D = 4000.0, gap = 1600.0, brk = 2500.0;
        const double dk = 2500.0;   // the final 2.5 km are flown on the inner (shallow) glide path
        // straight-in candidates ------------------------------------------------------------------
        struct Curved { bool ok = false; double score = -1e18; Vec3 aim, u, n; double D = 800, gc = 4, g0 = 3.5, gap = 1200; };
        Curved best, steep;
        for (int combo = 0; combo < 6; ++combo) {
            double sgn = combo % 2 == 0 ? 1.0 : -1.0;
            // aim point down the runway: further in clears high ground before the threshold
            const double aimOff = combo < 2 ? 320.0 : (combo < 4 ? 700.0 : std::min(1100.0, rw.length * 0.45));
            Vec3 uu{std::cos(rw.heading) * sgn, 0, std::sin(rw.heading) * sgn};
            Vec3 thr = rw.center - uu * (rw.length * 0.5);
            Vec3 a = thr + uu * aimOff;
            a.y = rw.center.y;
            double room = 10100;
            for (double d = 1000; d <= 10100; d += 250)
                if (!insideMap(a - uu * d, 800)) { room = d - 250; break; }
            double gp = room > 7000.0 ? 1600.0 : 1200.0;
            double DD = clampd(room - 1400.0 - gp, 2200.0, 8500.0);
            auto clearInner = [&](double gl, double upTo) {
                for (double d = 300; d <= upTo; d += 100) {
                    Vec3 q = a - uu * d;
                    if (t.onRunway(q.x, q.z, 40.0)) continue;   // the runway itself is not an obstacle
                    double need = std::min(35.0, 8.0 + d * 0.02);
                    if (eyeY + d * std::tan(gl * kDeg) < maxSurfaceAround(t, q.x, q.z, 150.0) + need) return d;
                }
                return 1e9;
            };
            double g0 = 3.0;
            bool innerOk = false;
            for (double gl : {3.0, 3.5, 4.0, 4.5, 5.0}) {
                g0 = gl;
                if (clearInner(gl, std::min(dk, DD)) > 1e8) { innerOk = true; break; }
            }
            const double gMax = 6.0;
            auto needOuter = [&](double Dc) {
                double kb = std::min(dk, Dc);
                double hkb = kb * std::tan(g0 * kDeg);
                double g = g0;
                for (double d = kb + 100.0; d <= Dc + gp; d += 100) {
                    Vec3 q = a - uu * d;
                    bool preZone = d > Dc;
                    double floorY = preZone ? maxSurfaceAround(t, q.x, q.z, 150.0) + 70.0 : maxSurfaceAround(t, q.x, q.z, 120.0) + 45.0;
                    g = std::max(g, std::atan2(floorY - (eyeY + hkb), d - kb) / kDeg);
                }
                return g;
            };
            double g1 = innerOk ? needOuter(DD) : 99.0;
            while (innerOk && g1 > gMax && DD > 2500.0) {
                DD -= 250.0;
                g1 = needOuter(DD);
            }
            bool straightOk = innerOk && g1 <= gMax;
            if (std::getenv("SKY_APPROACH_DEBUG"))
                std::fprintf(stderr, "straight: dir %+.0f aimOff %.0f inner %.1f ok %d outer %.1f D %.0f\n", sgn, aimOff, g0, innerOk ? 1 : 0, g1, DD);
            if (straightOk) {
                double score = -g1 * 30.0 - (g0 - 3.0) * 40.0 + std::min(DD, 8500.0) * 0.05 - (aimOff - 320.0) * 0.12;
                if (score > bestScore) { bestScore = score; aim = a; u = uu; glide = g0; glideOut = g1; D = DD; gap = gp; brk = std::min(dk, DD); }
            } else {
                // steep straight-in (mountain airfield): up to 10 degrees, final starts close in
                double sg0 = 3.0;
                bool sok = false;
                for (double gl : {3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0})
                    if (clearInner(gl, std::min(dk, 2000.0)) > 1e8) { sg0 = gl; sok = true; break; }
                if (sok) {
                    g0 = sg0;
                    double SD = 2000.0, sg1 = needOuter(SD);
                    while (sg1 > 10.0 && SD > 1200.0) { SD -= 200.0; sg1 = needOuter(SD); }
                    sg1 = std::min(sg1, 11.0);
                    double score = -std::max(sg0, sg1) * 60.0 + SD * 0.05 - (aimOff - 320.0) * 0.12;
                    if (score > steep.score) { steep.ok = true; steep.score = score; steep.aim = a; steep.u = uu; steep.D = SD; steep.gc = sg1; steep.g0 = sg0; steep.gap = gp; }
                }
            }
            // circling candidate: the longest clear straight final at 3.5 deg, then a turn onto it
            double lc = std::min(clearInner(3.5, 2500.0), 2500.0) - 150.0;
            double Df = clampd(lc, 600.0, 2200.0);
            if (lc < 500.0) continue;
            Vec3 iaf = a - uu * Df;
            for (double side : {1.0, -1.0}) {
                Vec3 n = Vec3{-uu.z, 0, uu.x} * side;   // the side the base leg comes from
                const double R = 1300.0;
                Vec3 C = iaf + n * R;
                double gc = 3.5;
                bool ok = insideMap(C + n * 1600.0 - uu * R, 900.0) && insideMap(C, 900.0);
                for (int k = 1; k <= 12 && ok; ++k) {
                    double th = (kPi / 2) * k / 12.0;
                    Vec3 q = C - n * (R * std::cos(th)) - uu * (R * std::sin(th));
                    double sp = Df + R * th;   // along-path distance to the aim point
                    double floorY = maxSurfaceAround(t, q.x, q.z, 150.0) + 70.0;
                    gc = std::max(gc, std::atan2(floorY - (eyeY + Df * std::tan(3.5 * kDeg)), sp - Df) / kDeg);
                }
                for (double b = 300; b <= 1800 && ok; b += 300) {   // base leg
                    Vec3 q = C - uu * R + n * b;
                    double sp = Df + R * kPi / 2 + b;
                    double floorY = maxSurfaceAround(t, q.x, q.z, 150.0) + 90.0;
                    gc = std::max(gc, std::atan2(floorY - (eyeY + Df * std::tan(3.5 * kDeg)), sp - Df) / kDeg);
                }
                if (std::getenv("SKY_APPROACH_DEBUG"))
                    std::fprintf(stderr, "circling: dir %+.0f aimOff %.0f side %+.0f final %.0f inside %d gc %.1f\n", sgn, aimOff, side, Df, ok ? 1 : 0, gc);
                if (!ok || gc > 7.5) continue;
                double score = -gc * 30.0 + Df * 0.05 - (aimOff - 320.0) * 0.12 - 400.0;
                if (score > best.score) { best.ok = true; best.score = score; best.aim = a; best.u = uu; best.n = n; best.D = Df; best.gc = gc; }
            }
        }
        bool curved = bestScore < -1e17 && best.ok;
        if (bestScore < -1e17 && !best.ok && steep.ok) {
            aim = steep.aim; u = steep.u; glide = steep.g0; glideOut = steep.gc; D = steep.D; gap = steep.gap; brk = std::min(dk, steep.D);
        } else if (bestScore < -1e17 && !best.ok) {
            // nothing clean: straight in at the steepest supported angles (still flyable with airbrake)
            Vec3 uu{std::cos(rw.heading), 0, std::sin(rw.heading)};
            aim = rw.center - uu * (rw.length * 0.5) + uu * 700.0;
            aim.y = rw.center.y;
            u = uu; glide = 5.0; glideOut = 7.0; D = 2200; gap = 1200; brk = 2200;
        }
        if (curved) { aim = best.aim; u = best.u; glide = 3.5; glideOut = best.gc; D = best.D; brk = best.D; }
        c.landAim = aim;
        c.landDir = u;
        c.glideDeg = glide;
        c.glideOuterDeg = glideOut;
        c.glideBreak = brk;
        auto glideY = [&](double d) { return aim.y + 2.35 + approachHeight(c, d); };
        // the run-in point (end of the return leg) and the heading flown through it
        Vec3 pre, preDir;
        std::vector<Vec3> arcPts;   // circling approach: the turn onto final (base leg end .. before the IAF)
        std::vector<double> arcDist;
        if (curved) {
            const double R = 1300.0;
            Vec3 iaf = aim - u * D;
            Vec3 C = iaf + best.n * R;
            pre = C - u * R + best.n * 1600.0;
            preDir = -best.n;
            for (int k = 0; k < 4; ++k) {   // base-leg end and three points round the turn
                double th = (kPi / 2) * (1.0 - k / 4.0);
                arcPts.push_back(C - best.n * (R * std::cos(th)) - u * (R * std::sin(th)));
                arcDist.push_back(D + R * th);
            }
        } else {
            pre = aim - u * (D + gap);
            preDir = u;
        }
        double preDist = curved ? D + 1300.0 * kPi / 2 + 1600.0 : D + gap;
        Vec3 mid = pre - preDir * 3200.0;
        bool viaValleys = !directReturn && std::hypot(pre.x - cursor.x, pre.z - cursor.z) > 7000.0 && insideMap(mid, 1500.0);
        if (std::getenv("SKY_COURSE_DEBUG")) std::fprintf(stderr, "RTB from cursor (%.0f %.0f %.0f) to pre (%.0f %.0f %.0f) valleys %d\n", cursor.x, cursor.y, cursor.z, pre.x, pre.y, pre.z, viaValleys ? 1 : 0);
        if (viaValleys) {
            // a long way home: through the valleys (a straight line would cross the ridges and
            // then dive at the airfield), then the turn onto the run-in
            legTo(mid, RouteStyle::Valley, "RETURN TO BASE", 260.0);
            Section& rs = c.sections.back();
            rs.instruction = "Fly the gates back to the airfield and onto the approach.";
            for (int i = rs.gateBegin; i < static_cast<int>(c.gates.size()); ++i) c.gates[i].minAgl = c.gates[i].maxAgl = 0;
        } else {
            Section& rs = add(SectionKind::Route, RouteStyle::Transit, "RETURN TO BASE", "Fly the gates back to the airfield and onto the approach.", "",
                              styleIntro(RouteStyle::Transit), 6);
            rs.speedMin = spec(RouteStyle::Transit).vMin;
            rs.speedMax = spec(RouteStyle::Transit).vMax;
        }
        alignTo(pre, preDir, viaValleys ? RouteStyle::Valley : RouteStyle::Transit, 300, 200);
        {
            Gate& pg = c.gates.back();
            pg.pos.y = std::max(glideY(preDist) + 30.0, maxSurfaceAround(t, pg.pos.x, pg.pos.z, 150.0) + 75.0);
            limitDescentInto(static_cast<int>(c.gates.size()) - 1, 6.0, 160.0);
            cursor = pg.pos;
        }
        Section& s = add(SectionKind::Landing, RouteStyle::Approach, "LANDING", "Fly the approach rings down the glide path: gear down, approach speed, touch down and stop.",
                         "G = GEAR   CTRL = BRAKES", "Turn final. Gear down, and fly the rings down the glide path.", 0);
        // go-around pattern (flown only after a miss): climb out straight ahead, then a
        // constant-radius turn back round to the same run-in point as the first approach
        {
            Vec3 preGate = c.gates.back().pos;   // last gate of the return leg
            // the pattern rejoins the return leg a few km before the run-in point and then flies
            // its last gates again: those were built clear of the terrain on the way in
            int rEnd = static_cast<int>(c.gates.size());
            int rBegin = c.sections[c.sections.size() - 2].gateBegin;
            int j = rEnd - 1;
            double back = 0;
            while (j - 1 >= rBegin && back < 3800.0) {
                back += std::hypot(c.gates[j].pos.x - c.gates[j - 1].pos.x, c.gates[j].pos.z - c.gates[j - 1].pos.z);
                --j;
            }
            Vec3 joinP = c.gates[j].pos;
            Vec3 joinD = j + 1 < rEnd ? hdir(c.gates[j + 1].pos - c.gates[j].pos) : preDir;
            if (j == rEnd - 1) joinD = preDir;
            std::vector<Gate> tail(c.gates.begin() + j + 1, c.gates.end());
            Vec3 up = aim + u * 3500.0;
            up.x = clampd(up.x, -14500, 14500);
            up.z = clampd(up.z, -14500, 14500);
            Vec3 saveC = cursor, saveD = cursorDir;
            cursor = {aim.x, aim.y + 50.0, aim.z};
            cursorDir = u;
            addGates({up}, RouteStyle::Transit, 350, 300);
            double len = 0;
            std::vector<Vec3> arc;
            for (double k : {1.0, 0.8, 0.6, 0.45})
                if (arc.empty()) arc = dubins(cursor, u, joinP, joinD, 1500.0 * k, 1800.0, len);
            if (arc.empty()) arc = dubins(cursor, u, joinP, joinD, 700.0, 1800.0, len, -300.0);
            if (!arc.empty()) arc.pop_back();
            if (!arc.empty()) addGates(arc, RouteStyle::Transit, 350, 300);
            addGates({joinP}, RouteStyle::Transit, 300, 200);
            c.gates.back().pos.y = joinP.y;
            limitDescentInto(static_cast<int>(c.gates.size()) - 1, 6.0, 160.0);
            int sec = static_cast<int>(c.sections.size()) - 1;
            for (Gate g2 : tail) { g2.section = sec; c.gates.push_back(g2); }
            for (int i = c.sections.back().gateBegin; i < static_cast<int>(c.gates.size()); ++i) { c.gates[i].minAgl = c.gates[i].maxAgl = 0; c.gates[i].radius = 350.0; }
            cursor = saveC;
            cursorDir = saveD;
        }
        Section& s2 = c.sections.back();
        s2.gateStart = static_cast<int>(c.gates.size());
        // circling approach: gates round the descending turn (normal rings, then the final)
        for (size_t k = 0; k < arcPts.size(); ++k) {
            Gate gt;
            Vec3 p = arcPts[k];
            gt.ground = surf(p.x, p.z);
            gt.pos = {p.x, std::max(glideY(arcDist[k]), maxSurfaceAround(t, p.x, p.z, 150.0) + 60.0), p.z};
            gt.radius = 140.0;
            gt.section = static_cast<int>(c.sections.size()) - 1;
            c.gates.push_back(gt);
        }
        c.finalGate = curved ? static_cast<int>(c.gates.size()) : -1;
        std::vector<double> ds = {D, D * 0.62, D * 0.38, 1900.0, 950.0};
        std::sort(ds.begin(), ds.end(), std::greater<double>());
        for (size_t k = 1; k < ds.size();)
            if (ds[k - 1] - ds[k] < 400.0 || ds[k] > D) ds.erase(ds.begin() + k); else ++k;
        for (double d : ds) {
            Gate gt;
            Vec3 p = aim - u * d;
            gt.ground = surf(p.x, p.z);
            gt.pos = {p.x, std::max(glideY(d), maxSurfaceAround(t, p.x, p.z, 150.0) + 40.0), p.z};
            gt.radius = clampd(d * 0.03, 35.0, 240.0);
            gt.cue = GateCue::Glide;
            gt.section = static_cast<int>(c.sections.size()) - 1;
            c.gates.push_back(gt);
        }
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.speedMin = 0; s2.speedMax = 0;
        s2.estSeconds = preDist / 95.0 + 45.0;
        if (curved) {
            s2.instruction = "Circling approach: fly the base leg, the descending turn onto a short final, then the glide path. Gear down early.";
        }
        (void)s;
    }

    void landCarrier(const DeckInfo& deck) {
        c.carrier = true;
        c.deck = deck;
        LandingAxis la = carrierLandingAxis(deck);
        Vec3 u = la.dir, r{-u.z, 0, u.x};
        c.landAim = la.touchdown;
        c.landDir = u;
        c.glideDeg = 3.5;
        c.glideOuterDeg = 3.5;
        c.glideBreak = 1e9;
        auto glideY = [&](double d) { return deck.center.y + 1.5 + d * std::tan(3.5 * kDeg); };
        Vec3 pre = la.touchdown - u * 8100.0;
        {
            Section& rs = add(SectionKind::Route, RouteStyle::Transit, "TRANSIT TO THE CARRIER", "Fly the gates out to the carrier and onto its final approach.", "",
                              styleIntro(RouteStyle::Transit), 6);
            rs.speedMin = spec(RouteStyle::Transit).vMin;
            rs.speedMax = spec(RouteStyle::Transit).vMax;
        }
        alignTo(pre, u, RouteStyle::Transit, 300, 200);
        {
            Gate& pg = c.gates.back();
            pg.pos.y = std::max(glideY(8100.0) + 80.0, maxSurfaceAround(t, pg.pos.x, pg.pos.z, 500.0) + 150.0);
            limitDescentInto(static_cast<int>(c.gates.size()) - 1, 6.0, 160.0);
            cursor = pg.pos;
        }
        Section& s = add(SectionKind::CarrierLanding, RouteStyle::Approach, "CARRIER LANDING",
                         "Fly the rings down the 3.5 degree glide path onto the angled deck. Gear and hook down, no flare: fly it into the wires.",
                         "G = GEAR + HOOK", "Carrier approach. Gear and hook down. Fly the ball, keep your line-up, and do not flare.", 0);
        // bolter pattern (left-hand): climb out ahead, downwind abeam, base
        std::vector<Vec3> pattern = {la.touchdown + u * 2800.0, la.touchdown + u * 1200.0 - r * 2800.0, la.touchdown - u * 3600.0 - r * 2800.0,
                                     la.touchdown - u * 6200.0 - r * 1000.0};
        for (size_t k = 0; k < pattern.size(); ++k) {
            Gate gt;
            Vec3 p{clampd(pattern[k].x, -14800, 14800), 0, clampd(pattern[k].z, -14800, 14800)};
            gt.ground = surf(p.x, p.z);
            gt.pos = {p.x, std::max(k == 3 ? glideY(6200.0) + 30.0 : 330.0, maxSurfaceAround(t, p.x, p.z, 600.0) + 250.0), p.z};
            gt.radius = 320.0;
            gt.section = static_cast<int>(c.sections.size()) - 1;
            c.gates.push_back(gt);
        }
        Section& s2 = c.sections.back();
        s2.gateStart = static_cast<int>(c.gates.size());
        const double ds[] = {6500.0, 4300.0, 2700.0, 1600.0, 850.0, 400.0};
        const double rad[] = {240.0, 150.0, 100.0, 60.0, 34.0, 22.0};
        for (int k = 0; k < 6; ++k) {
            Gate gt;
            Vec3 p = la.touchdown - u * ds[k];
            gt.ground = surf(p.x, p.z);
            gt.pos = {p.x, glideY(ds[k]), p.z};
            gt.radius = rad[k];
            gt.cue = GateCue::Glide;
            gt.section = static_cast<int>(c.sections.size()) - 1;
            c.gates.push_back(gt);
        }
        s2.gateEnd = static_cast<int>(c.gates.size());
        s2.estSeconds = 6500.0 / 95.0 + 25.0;
        (void)s;
    }

    // ---- finish ----------------------------------------------------------------------------------
    /// Horizontal distance from p to the segment a-b.
    static double segDist(Vec3 p, Vec3 a, Vec3 b) {
        Vec3 ab{b.x - a.x, 0, b.z - a.z}, ap{p.x - a.x, 0, p.z - a.z};
        double L2 = dot(ab, ab);
        double t0 = L2 > 1e-9 ? clampd(dot(ap, ab) / L2, 0.0, 1.0) : 0.0;
        Vec3 q{a.x + ab.x * t0, 0, a.z + ab.z * t0};
        return std::hypot(p.x - q.x, p.z - q.z);
    }
    /// Route legs that pass close to an obstacle they are not meant to fly through climb over it.
    void clearObstacles(bool allowShift = true) {
        for (int pass = 0; pass < 3; ++pass)
            for (size_t i = 1; i < c.gates.size(); ++i) {
                Gate& g = c.gates[i];
                bool designed = g.cue == GateCue::Pillars || g.cue == GateCue::Arch || g.cue == GateCue::Bridge || g.cue == GateCue::Masts;
                const Section& sec = c.sections[g.section];
                bool pattern = sec.kind == SectionKind::Landing && static_cast<int>(i) < sec.gateStart;   // the go-around circuit
                if (designed || (sec.kind != SectionKind::Route && !pattern)) continue;
                if (c.gates[i - 1].section != g.section && (c.gates[i - 1].cue == GateCue::Pillars || c.gates[i - 1].cue == GateCue::Arch ||
                                                           c.gates[i - 1].cue == GateCue::Bridge || c.gates[i - 1].cue == GateCue::Masts))
                    continue;   // leaving an obstacle run along its axis
                for (const Obstacle& o : c.obstacles) {
                    double reach = o.kind == Obstacle::Kind::Arch || o.kind == Obstacle::Kind::Deck ? o.span * 0.5 + 40.0 : o.radius + 40.0;
                    double dmin = segDist(o.pos, c.gates[i - 1].pos, g.pos);
                    if (dmin > reach + 120.0) continue;
                    if (pass < 2 && allowShift) {
                        // first try to steer the leg round it: move the gate sideways, keep its height above ground
                        Vec3 a = c.gates[i - 1].pos, b = g.pos;
                        Vec3 ab{b.x - a.x, 0, b.z - a.z};
                        double L2 = std::max(1.0, ab.x * ab.x + ab.z * ab.z);
                        double tt = clampd(((o.pos.x - a.x) * ab.x + (o.pos.z - a.z) * ab.z) / L2, 0.0, 1.0);
                        Vec3 q{a.x + ab.x * tt, 0, a.z + ab.z * tt};
                        Vec3 away{q.x - o.pos.x, 0, q.z - o.pos.z};
                        if (length(away) < 1.0) away = {-ab.z, 0, ab.x};
                        away = normalize(away);
                        double push = (reach + 150.0 - dmin) / std::max(0.25, tt);
                        Vec3 np{b.x + away.x * push, 0, b.z + away.z * push};
                        if (push < 600.0 && insideMap(np, 1500.0)) {
                            double agl = g.pos.y - g.ground;
                            g.ground = surf(np.x, np.z);
                            g.pos = {np.x, std::max(g.ground + agl, maxSurfaceAround(t, np.x, np.z, 150.0) + 60.0), np.z};
                            continue;
                        }
                    }
                    double top = o.pos.y + o.height + (o.kind == Obstacle::Kind::Deck ? o.thickness : 0.0) + 150.0;
                    if (std::min(g.pos.y, c.gates[i - 1].pos.y) >= top) continue;
                    g.pos.y = std::max(g.pos.y, top);
                    if (g.maxAgl > 0) g.maxAgl = std::max(g.maxAgl, g.pos.y - g.ground + 60.0);
                    Gate& pg = c.gates[i - 1];
                    bool pdesigned = pg.cue == GateCue::Pillars || pg.cue == GateCue::Arch || pg.cue == GateCue::Bridge || pg.cue == GateCue::Masts;
                    if (!pdesigned) {
                        pg.pos.y = std::max(pg.pos.y, top);
                        if (pg.maxAgl > 0) pg.maxAgl = std::max(pg.maxAgl, pg.pos.y - pg.ground + 60.0);
                    }
                }
            }
    }

    /// Where a gated leg starts far above the previous gate (rising ground right after an
    /// obstacle run), a free climb is inserted first: no jet can follow a 20-degree gate line in
    /// thin air, but every jet can climb out over the ground it is flying above.
    void insertRidgeClimbs() {
        for (size_t k = 1; k < c.sections.size(); ++k) {
            Section& s = c.sections[k];
            const Section& p = c.sections[k - 1];
            if (s.kind != SectionKind::Route || s.gateBegin >= s.gateEnd || s.gateBegin == 0) continue;
            if (!(p.kind == SectionKind::Route && p.gateEnd == s.gateBegin && p.gateBegin < p.gateEnd)) continue;
            const Gate& a = c.gates[s.gateBegin - 1];
            const Gate& b = c.gates[s.gateBegin];
            double hd = std::hypot(b.pos.x - a.pos.x, b.pos.z - a.pos.z);
            double dh = b.pos.y - a.pos.y;
            bool obstacleExit = a.cue == GateCue::Pillars || a.cue == GateCue::Masts || a.cue == GateCue::Arch || a.cue == GateCue::Bridge;
            if (!obstacleExit || dh < 250.0 || dh < hd * std::tan(14.0 * kDeg)) continue;
            Section cl;
            cl.kind = SectionKind::Climb;
            cl.style = RouteStyle::Transit;
            cl.title = "CLIMB OVER THE HIGH GROUND";
            cl.instruction = "Climb to the altitude on the HUD.";
            cl.intro = "High ground ahead. Climb now, full power.";
            cl.value = b.pos.y - 40.0;
            cl.value2 = 150.0;
            cl.gateBegin = cl.gateStart = cl.gateEnd = s.gateBegin;
            cl.estSeconds = dh / 25.0 + 10.0;
            c.sections.insert(c.sections.begin() + static_cast<long>(k), cl);
            for (Gate& g : c.gates)
                if (g.section >= static_cast<int>(k)) ++g.section;
            ++k;
        }
    }

    /// No gate corner may ask for more than ~4 g at the slow end of its speed band: sharp corners
    /// (where the valley path bends hard) are eased by moving the corner gate towards the chord.
    void relaxSharpTurns() {
        auto cornerR = [](Vec3 a, Vec3 b, Vec3 c) {
            Vec3 d0{b.x - a.x, 0, b.z - a.z}, d1{c.x - b.x, 0, c.z - b.z};
            double l0 = length(d0), l1 = length(d1);
            if (l0 < 1 || l1 < 1) return 1e9;
            double th = std::acos(clampd(dot(d0, d1) / (l0 * l1), -1.0, 1.0));
            return th < 1e-4 ? 1e9 : 0.5 * std::min(l0, l1) / std::tan(th * 0.5);
        };
        for (const Section& sec : c.sections) {
            if (sec.kind != SectionKind::Route || sec.style == RouteStyle::Obstacles || sec.gateEnd - sec.gateBegin < 3) continue;
            double v = sec.speedMin > 0 ? sec.speedMin : 170.0;
            for (int pass = 0; pass < 8; ++pass) {
                bool changed = false;
                for (int i = sec.gateBegin + 1; i + 1 < sec.gateEnd; ++i) {
                    Gate& g = c.gates[i];
                    if (g.cue == GateCue::Pillars || g.cue == GateCue::Masts || g.cue == GateCue::Arch || g.cue == GateCue::Bridge) continue;
                    double R = cornerR(c.gates[i - 1].pos, g.pos, c.gates[i + 1].pos) + g.radius * 0.5;
                    double n = std::sqrt(std::pow(v * v / (kG * R), 2) + 1.0);
                    if (std::getenv("SKY_RELAX_DEBUG")) std::fprintf(stderr, "relax: gate %d R %.0f n %.1f v %.0f\n", i, R, n, v);
                    if (n <= 4.0) continue;
                    Vec3 m = (c.gates[i - 1].pos + c.gates[i + 1].pos) * 0.5;
                    Vec3 np{g.pos.x + (m.x - g.pos.x) * 0.3, 0, g.pos.z + (m.z - g.pos.z) * 0.3};
                    double agl = g.pos.y - g.ground;
                    g.ground = surf(np.x, np.z);
                    g.pos = {np.x, std::max(g.ground + agl, maxSurfaceAround(t, np.x, np.z, 150.0) + 60.0), np.z};
                    changed = true;
                }
                if (!changed) break;
            }
        }
    }

    // ---- combat -------------------------------------------------------------------------------
    /// The forward outpost of the layout nearest the cursor (or low ground ahead if there is none).
    Vec3 outpostArea() {
        const MilitarySite* best = nullptr;
        double bd = 1e18;
        for (const MilitarySite& m : L.sites) {
            if (m.airbase) continue;
            double d = std::hypot(m.center.x - cursor.x, m.center.z - cursor.z);
            for (const Vec3& u : used) if (std::hypot(u.x - m.center.x, u.z - m.center.z) < 500) d += 20000;   // not the same one twice
            if (d < bd && insideMap(m.center, 2500.0)) { bd = d; best = &m; }
        }
        Vec3 a = best ? best->center : pickAnchor(6000, 11000, true);
        a.y = surf(a.x, a.z);
        used.push_back(a);
        return a;
    }
    /// Navigation leg towards an area, ending short of it so no gate stands in the fight.
    void toArea(Vec3 area, const std::string& title) {
        double dist = std::hypot(area.x - cursor.x, area.z - cursor.z);
        if (dist < 4000.0) return;
        Vec3 d = hdir(area - cursor);
        if (dot(d, cursorDir) < 0.3) return;   // behind: no gated U-turn, the HUD leads you to the fight
        legTo(area - d * 2200.0, RouteStyle::Navigation, title);
    }
    /// Climb to a safe fighting height before an air-to-air exercise.
    void climbToFight() {
        climbTo(safeAltitudeHere(9000.0, 1500.0), "CLIMB TO THE COMBAT AREA", "Climb to a safe manoeuvring height.", 1100.0);
    }
    void engage(Engagement e, const std::string& title, const std::string& instruction, int intro, double est) {
        if (e.jetAlt <= 0 && !e.jets.empty()) e.jetAlt = std::max(cursor.y, safeAltitudeHere(9000.0, 1300.0));
        Section& s = add(SectionKind::Engage, RouteStyle::Transit, title, instruction, "", combatIntroText(intro), est);
        s.value = static_cast<double>(c.engagements.size());
        s.value2 = e.surviveSeconds;
        c.engagements.push_back(e);
    }
    /// Open sea near the carrier for a maritime strike (deep water, a few km from the deck).
    Vec3 seaArea(const DeckInfo& deck) {
        Vec3 best = deck.center;
        double bs = 1e18;
        for (int k = 0; k < 24; ++k) {
            double a = k * kPi / 12.0;
            for (double r : {5000.0, 6500.0, 8000.0}) {
                Vec3 p{deck.center.x + std::cos(a) * r, 0, deck.center.z + std::sin(a) * r};
                if (!insideMap(p, 2000.0)) continue;
                double deep = 0;
                for (int q = 0; q < 8; ++q) deep = std::max(deep, t.height(p.x + std::cos(q * kPi / 4) * 900, p.z + std::sin(q * kPi / 4) * 900));
                if (deep > -6.0) continue;   // every point around it must be sea
                double sc = std::hypot(p.x - cursor.x, p.z - cursor.z) * 0.2 + r;
                if (sc < bs) { bs = sc; best = p; }
            }
        }
        best.y = 0;
        return best;
    }

    void finalize() {
        insertRidgeClimbs();
        clearObstacles();
        relaxSharpTurns();          // after the obstacle shifts, which can sharpen a corner
        clearObstacles(false);      // and anything eased back towards a rock climbs over it
        // gate directions: along the route (central differences between neighbours of a section)
        for (size_t i = 0; i < c.gates.size(); ++i) {
            Gate& gt = c.gates[i];
            Vec3 prev = i > 0 ? c.gates[i - 1].pos : gt.pos - Vec3{1, 0, 0};
            Vec3 next = i + 1 < c.gates.size() ? c.gates[i + 1].pos : gt.pos + (gt.pos - prev);
            const Section& s = c.sections[gt.section];
            bool firstOfSection = static_cast<int>(i) == s.gateBegin || static_cast<int>(i) == s.gateStart;
            bool lastOfSection = static_cast<int>(i) + 1 == s.gateEnd || static_cast<int>(i) + 1 == s.gateStart;
            Vec3 d;
            if (gt.cue == GateCue::Glide) d = c.landDir;
            else if (gt.cue == GateCue::Arch || gt.cue == GateCue::Bridge || gt.cue == GateCue::Pillars || gt.cue == GateCue::Masts)
                d = lastOfSection ? hdir(gt.pos - prev) : firstOfSection ? hdir(next - gt.pos) : hdir(next - prev);
            else if (firstOfSection && !lastOfSection) d = hdir((next - gt.pos) + (gt.pos - prev) * 0.25);
            else if (lastOfSection) d = hdir(gt.pos - prev);
            else d = hdir(next - prev);
            if (length(d) < 0.5) d = {1, 0, 0};
            gt.dir = d;
        }
        // obstacle gates use the straight run direction (exactly through the arch / under the deck)
        for (size_t i = 0; i < c.gates.size(); ++i) {
            Gate& gt = c.gates[i];
            if (gt.cue == GateCue::Arch || gt.cue == GateCue::Bridge) {
                Vec3 prev = i > 0 ? c.gates[i - 1].pos : gt.pos;
                gt.dir = hdir(gt.pos - prev);
                for (const Obstacle& o : c.obstacles)
                    if ((o.kind == Obstacle::Kind::Arch || o.kind == Obstacle::Kind::Deck) && std::hypot(o.pos.x - gt.pos.x, o.pos.z - gt.pos.z) < 5.0)
                        gt.dir = {std::cos(o.heading - kPi / 2), 0, std::sin(o.heading - kPi / 2)};
                if (i > 0 && dot(gt.dir, gt.pos - c.gates[i - 1].pos) < 0) gt.dir = -gt.dir;
            }
        }
        // cues for the instructor: turns, climbs, descents, passes, canyons
        for (size_t i = 1; i + 1 < c.gates.size(); ++i) {
            Gate& gt = c.gates[i];
            if (gt.cue != GateCue::None) continue;
            const Gate& a = c.gates[i - 1];
            const Gate& b = c.gates[i + 1];
            if (a.section != gt.section || b.section != gt.section) continue;
            Vec3 d0 = hdir(gt.pos - a.pos), d1 = hdir(b.pos - gt.pos);
            double turn = std::atan2(d0.x * d1.z - d0.z * d1.x, dot(d0, d1));
            Vec3 side{-d0.z, 0, d0.x};
            double l = surf(gt.pos.x + side.x * 550.0, gt.pos.z + side.z * 550.0) - gt.ground;
            double rr = surf(gt.pos.x - side.x * 550.0, gt.pos.z - side.z * 550.0) - gt.ground;
            bool saddle = gt.ground > a.ground + 60 && gt.ground > b.ground + 60 && l > 120 && rr > 120;
            if (saddle) gt.cue = GateCue::Pass;
            else if (l > 280 && rr > 280) gt.cue = GateCue::Canyon;
            else if (turn > 40 * kDeg) gt.cue = GateCue::TurnRight;
            else if (turn < -40 * kDeg) gt.cue = GateCue::TurnLeft;
            else if (b.pos.y - gt.pos.y > 160) gt.cue = GateCue::Climb;
            else if (gt.pos.y - b.pos.y > 160) gt.cue = GateCue::Descend;
        }
        // tightest turn the gated route asks for
        double rmin = 1e9;
        for (const Section& s : c.sections) {
            if (s.kind != SectionKind::Route) continue;
            std::vector<Vec3> pts;
            for (int i = s.gateBegin; i < s.gateEnd; ++i) pts.push_back(c.gates[i].pos);
            if (pts.size() >= 3) rmin = std::min(rmin, minTurnRadius(pts));
        }
        c.minRequiredTurnRadius = rmin;
        double tsum = 0;
        for (const Section& s : c.sections) tsum += s.estSeconds;
        c.estimatedSeconds = tsum;
    }
};

void buildLevel(Builder& b, int level, int flex, const DeckInfo* deck, bool lite) {
    auto legs = [&](RouteStyle st, int n, const char* title) {
        for (int i = 0; i < n; ++i) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s %d", title, i + 1);
            b.leg(st, n > 1 ? buf : title);
        }
    };
    auto fight = [&](std::vector<int> jets, int skill, const std::string& title, int intro, double est, bool fromBase = false) {
        Engagement e;
        e.jets = std::move(jets);
        e.skill = skill;
        e.fromBase = fromBase;
        b.engage(e, title, "Destroy the bandit" + std::string(e.jets.size() > 1 ? "s" : "") + ". Missiles and gun.", intro, est);
    };
    using AI = AircraftId;
    auto I = [](AI a) { return static_cast<int>(a); };
    auto G = [](GroundKind k) { return static_cast<int>(k); };
    switch (level) {
        case 1:   // carrier scramble (runway cold start where the map has no sea), navigation, landing
            if (deck) b.departureCarrier(*deck);
            else b.departure(true);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            if (!deck) legs(RouteStyle::Valley, 1, "VALLEY INTRODUCTION");   // from the carrier the circuit is long enough
            b.landRunway();
            break;
        case 2: {   // aircraft handling, then rings (gated legs)
            b.departure(false);
            {
                // the manoeuvres are flown over the lowest ground in reach: on a high map the
                // safe height above the peaks would be beyond what some jets can loop at
                double top = 0;
                Vec3 area = b.lowOrbitCentre(b.cursor + b.cursorDir * 7000.0, 3500.0, 3500.0, 7000.0, top);
                if (std::hypot(area.x - b.cursor.x, area.z - b.cursor.z) > 4000.0) b.legTo(area, RouteStyle::Navigation, "NAVIGATION LEG");
                else legs(RouteStyle::Navigation, 1, "NAVIGATION LEG");
            }
            b.climbTo(b.safeAltitudeHere(7000.0, 1500.0), "CLIMB TO MANOEUVRE HEIGHT", "Climb to a safe manoeuvring height.", 1300.0);
            b.manoeuvre(SectionKind::LevelTurn);
            b.manoeuvre(SectionKind::ClimbTurn);
            b.manoeuvre(SectionKind::DescendTurn);
            b.climbTo(b.safeAltitudeHere(7000.0, 2200.0), "CLIMB TO MANOEUVRE HEIGHT", "Climb to a safe manoeuvring height.", 1800.0);
            {
                double safe = b.safeAltitudeHere(7000.0, 2200.0);
                b.manoeuvre(SectionKind::HighG);
                b.climbTo(safe, "CLIMB FOR THE LOOP", "Climb to a safe manoeuvring height.", 1800.0);
                b.manoeuvre(SectionKind::Loop);
                b.manoeuvre(SectionKind::Roll);
                b.climbTo(safe, "CLIMB FOR THE RECOVERY", "Climb to a safe manoeuvring height.", 1600.0);
                b.manoeuvre(SectionKind::Recovery);
            }
            legs(RouteStyle::Valley, flex, "RING RUN");
            b.landRunway();
            break;
        }
        case 3:   // mountain navigation
            b.departure(false);
            legs(RouteStyle::Navigation, 1, "TO THE VALLEYS");
            legs(RouteStyle::Valley, flex, "VALLEY NAVIGATION");
            b.landRunway();
            break;
        case 4:   // low level, then pillars and arches
            b.departure(false);
            legs(RouteStyle::Valley, 1, "VALLEY WARM-UP");
            legs(RouteStyle::LowLevel, flex, "LOW-LEVEL RUN");
            b.slalom(lite ? 4 : 5);
            b.arches(2);
            b.landRunway();
            break;
        case 5: {   // helicopter interception in the valleys
            b.departure(false);
            legs(RouteStyle::Valley, flex, "VALLEY TRANSIT");
            Vec3 area = b.pickAnchor(4500, 9000, true);
            b.toArea(area, "TO THE HELICOPTERS");
            Engagement e;
            e.transportHelis = 2;
            e.friendlyHelis = 2;
            e.area = area;
            e.radius = 900;
            b.engage(e, "INTERCEPT THE HELICOPTERS", "Shoot down the two hostile helicopters (red); ours are green. Slow down: gun or air-to-air missile.", 0, 170);
            b.landRunway();
            break;
        }
        case 6: {   // attack helicopters and armour at an outpost
            b.departure(false);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            Vec3 area = b.outpostArea();
            b.toArea(area, "TO THE OUTPOST");
            Engagement e;
            e.attackHelis = 2;
            e.helisArmed = true;
            e.ground = {G(GroundKind::Tank), G(GroundKind::Tank), G(GroundKind::Truck)};
            e.area = area;
            e.radius = 700;
            b.engage(e, "ATTACK THE OUTPOST", "Destroy the attack helicopters, then the armour (air-to-surface missile: Q).", 1, 230);
            b.landRunway();
            break;
        }
        case 7: {   // gunnery: guns only, drones on a racetrack
            b.departure(false);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            b.climbToFight();
            Engagement e;
            e.jets = {I(AI::Su30), I(AI::MiG29), I(AI::Su30)};
            e.skill = 0;
            e.jetsArmed = false;
            e.jetsOnRoute = true;
            e.rule = WeaponRule::GunsOnly;
            b.engage(e, "GUNNERY: THREE DRONES", "Guns only. Close in behind each drone and shoot it down.", 2, 220);
            b.landRunway();
            break;
        }
        case 8: {   // missiles: drones, then an armed bandit
            b.departure(false);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            b.climbToFight();
            Engagement e;
            e.jets = {I(AI::MiG29), I(AI::Su30), I(AI::MiG29), I(AI::Su30)};
            e.skill = 0;
            e.jetsArmed = false;
            e.jetsOnRoute = true;
            e.rule = WeaponRule::MissilesOnly;
            b.engage(e, "MISSILES: FOUR DRONES", "Missiles only. Lock each drone and fire.", 3, 170);
            fight({I(AI::MiG29)}, 0, "AIR-TO-AIR: ONE BANDIT", 16, 170);
            b.landRunway();
            break;
        }
        case 9: case 10: case 11: case 12: {   // one-on-one air combat
            b.departure(false);
            legs(level <= 10 ? RouteStyle::Navigation : RouteStyle::Valley, flex, level <= 10 ? "NAVIGATION LEG" : "VALLEY RUN");
            b.climbToFight();
            AI who = level == 9 ? AI::F16 : level == 10 ? AI::Su35 : level == 11 ? AI::Su57 : AI::F35;
            fight({I(who)}, level <= 10 ? 1 : 2, std::string("ENGAGE THE ") + profile(who).shortName, 5 + (level - 9), level <= 10 ? 210 : 250);
            b.landRunway();
            break;
        }
        case 13: {   // helicopter swarm
            b.departure(false);
            legs(RouteStyle::Valley, flex, "VALLEY RUN");
            Vec3 area = b.outpostArea();
            b.toArea(area, "TO THE HELICOPTERS");
            Engagement e;
            e.transportHelis = 4;
            e.attackHelis = 2;
            e.friendlyHelis = 1;
            e.helisArmed = true;
            e.area = area;
            e.radius = 1300;
            b.engage(e, "HELICOPTER SWARM", "Shoot down all six helicopters. Attack helicopters first.", 9, 270);
            b.landRunway();
            break;
        }
        case 14: {   // fighters over an outpost, then the outpost
            b.departure(false);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            Vec3 area = b.outpostArea();
            b.toArea(area, "TO THE OUTPOST");
            b.climbToFight();
            fight({I(AI::MiG29), I(AI::Su30)}, 1, "AIR SUPERIORITY", 10, 230, true);
            Engagement g;
            g.ground = {G(GroundKind::Tank), G(GroundKind::Tank), G(GroundKind::Gun), G(GroundKind::Gun), G(GroundKind::Truck)};
            g.groundArmed = true;
            g.area = area;
            g.radius = 350;
            b.engage(g, "ATTACK THE OUTPOST", "Destroy the armour and the guns. Air-to-surface missile (Q) and gun.", 11, 170);
            b.cursor = area;
            b.landRunway();
            break;
        }
        case 15:   // high-speed canyon run, then two bandits
            b.departure(false);
            legs(RouteStyle::HighSpeed, std::max(1, flex), "HIGH-SPEED RUN");
            b.climbToFight();
            fight({I(AI::Su35), I(AI::MiG29)}, 1, "TWO BANDITS", 12, 260);
            b.landRunway();
            break;
        case 16:   // terrain following, then an enemy formation launching from its base
            b.departure(false);
            legs(RouteStyle::Valley, 1, "VALLEY WARM-UP");
            legs(RouteStyle::Precision, flex, "TERRAIN FOLLOWING");
            b.climbToFight();
            fight({I(AI::Su30), I(AI::MiG29), I(AI::Su30)}, 0, "INTERCEPT THE FORMATION", 17, 260, true);
            b.landRunway();
            break;
        case 17: {   // defensive flying, then the reversal
            b.departure(false);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            b.climbToFight();
            Engagement e;
            e.jets = {I(AI::Su35), I(AI::F16)};
            e.skill = 1;
            e.surviveSeconds = 90;
            b.engage(e, "SURVIVE 90 SECONDS", "Two attackers behind you. Survive: flares (X), break turns, change height.", 13, 95);
            Engagement r;
            r.continueFrom = static_cast<int>(b.c.engagements.size()) - 1;
            b.engage(r, "TURN THE FIGHT", "Now shoot both attackers down.", 12, 180);
            b.landRunway();
            break;
        }
        case 18: {   // mountain route, mixed strike, tanker
            b.departure(false);
            legs(RouteStyle::LowLevel, flex, "MOUNTAIN ROUTE");
            Vec3 area = b.outpostArea();
            b.toArea(area, "TO THE TARGET");
            Engagement e;
            e.jets = {I(AI::MiG29)};
            e.skill = 1;
            e.attackHelis = 2;
            e.helisArmed = true;
            e.ground = {G(GroundKind::Tank), G(GroundKind::Truck), G(GroundKind::Radar)};
            e.area = area;
            e.radius = 600;
            b.engage(e, "STRIKE", "Destroy the fighter, the helicopters and the ground targets.", 14, 300);
            b.cursor = area;
            b.tanker();
            legs(RouteStyle::Transit, 1, "CLEAR THE TANKER");
            b.landRunway();
            break;
        }
        case 19: {   // catapult launch, maritime strike, carrier recovery
            if (deck) b.departureCarrier(*deck);
            else b.departure(false);
            legs(RouteStyle::Navigation, flex, "NAVIGATION LEG");
            if (deck) {
                Vec3 sea = b.seaArea(*deck);
                b.toArea(sea, "TO THE FLEET");
                Engagement e;
                e.ground = {G(GroundKind::Ship), G(GroundKind::Ship), G(GroundKind::Ship)};
                e.groundArmed = true;
                e.area = sea;
                e.radius = 700;
                b.engage(e, "MARITIME STRIKE", "Sink the patrol boats: air-to-surface missile (Q) and gun.", 15, 200);
                b.cursor = sea;
                b.landCarrier(*deck);
            } else {
                b.landRunway();
            }
            break;
        }
        case 20:
        default:   // final qualification
            b.departure(false);
            legs(RouteStyle::Valley, flex, "MOUNTAIN NAVIGATION");
            b.slalom(4);
            if (!lite) b.arches(2);
            b.climbToFight();
            fight({I(AI::Su35), I(AI::MiG29)}, 0, "INTERCEPT", 18, 230, true);   // the final tests everything: a fair fight, not the hardest
            b.tanker();
            if (deck) b.landCarrier(*deck);
            else b.landRunway();
            break;
    }
    b.finalize();
}

}  // namespace

bool levelSupportsTerrain(int level, const Terrain& t, std::string* why) {
    if (!trainingLevel(level).needsSea) return true;
    HeightGrid g = sampleHeightGrid(t, 160.0, 15000.0);
    DeckInfo d;
    if (placeCarrierDeck(t, g, {0, 0, 0}, d)) return true;
    if (why) *why = "no open sea for the aircraft carrier";
    return false;
}

Course buildCourse(int level, const Terrain& t, const WorldLayout& layout) {
    level = std::clamp(level, 1, kTrainingLevels);
    DeckInfo deck;
    bool haveDeck = false;
    if (level == 1 && !trainingLevel(level).needsSea) {   // a carrier scramble where the map has sea
        HeightGrid g = sampleHeightGrid(t, 160.0, 15000.0);
        haveDeck = placeCarrierDeck(t, g, {0, 0, 0}, deck);
    }
    if (trainingLevel(level).needsSea) {
        HeightGrid g = sampleHeightGrid(t, 160.0, 15000.0);
        haveDeck = placeCarrierDeck(t, g, {0, 0, 0}, deck);
        if (!haveDeck) {
            Course c;
            c.level = level;
            c.error = "This location has no open sea for the carrier";
            return c;
        }
    }
    // pick the number of flexible legs that brings the estimate closest to 16.5 minutes
    const int flexMin = 0, flexMax = 20;
    Course best;
    double bestErr = 1e18;
    for (int attempt = 0; attempt < 3; ++attempt) {
        bool lite = attempt >= 1, direct = attempt >= 2;   // shorter variants only when the full one cannot fit
        if (attempt > 0 && bestErr < 50.0) break;
        for (int variant = 0; variant < 4 && bestErr > 1.2; ++variant) {
            for (int flex = flexMin; flex <= flexMax; ++flex) {
                Builder b(t, layout, level, variant);
                b.directReturn = direct;
                buildLevel(b, level, flex, haveDeck ? &deck : nullptr, lite);
                double mins = b.c.estimatedSeconds / 60.0;
                if (std::getenv("SKY_COURSE_DEBUG"))
                    std::fprintf(stderr, "level %d v%d flex %d%s: %.1f min, %zu gates\n", level, variant, flex, lite ? " lite" : "", mins, b.c.gates.size());
                double err = std::fabs(mins - 17.0);
                if (mins < 15.6 || mins > 19.9) err += 100.0 + std::fabs(mins - 17.0);   // inside 15.5-20 with a margin
                if (err < bestErr) { bestErr = err; best = b.c; }
                if (mins > 21.0 || bestErr < 1.2) break;
            }
        }
    }
    return best;
}

}  // namespace sky

// =============================================================================================
// Instructor vocabulary
// =============================================================================================
namespace sky {
namespace {

namespace say_ {
const std::vector<std::string> kGateHit = {"Good gate.", "Nice, right through it.", "Good, on the line.", "That's it. Next gate.", "Clean pass."};
const std::vector<std::string> kGateMiss = {"Missed that gate. Pick up the next one.", "Outside the gate. Keep going, next one.", "You missed it. Look ahead to the next gate."};
const std::vector<std::string> kGateLow = {"Through the gate, but too low."};
const std::vector<std::string> kGateHigh = {"Through the gate, but too high."};
const std::vector<std::string> kSectionDone = {"Section complete. Well flown.", "That's the section. Good work.", "Section complete."};
const char* kTooFast = "Too fast. Throttle back a little.";
const char* kTooSlow = "Too slow. Add power.";
const char* kCeiling = "You're above the ceiling. Get down lower.";
const char* kBelowGate = "You're below the gate. Climb a little.";
const char* kAboveGate = "You're above the gate. Bring it down.";
const char* kPullUp = "Terrain! Pull up!";
const char* kObstacle = "Obstacle ahead! Pull up!";
const char* kStall = "You're stalling. Nose down and add power.";
const char* kOverG = "Easy on the stick, you're over-stressing the airframe.";
const char* kLowFuel = "Fuel is getting low. Ease back on the afterburner.";
const char* kWingsLevel = "Wings level, small corrections.";
// manoeuvres
const char* kMoreBank = "More bank.";
const char* kLessBank = "Too much bank. Ease off a little.";
const char* kHoldHeight = "Hold your height. Use a little back pressure.";
const char* kKeepClimbing = "Keep the climb going, more power.";
const char* kKeepDescending = "Keep it coming down.";
const char* kPullHarder = "Pull harder.";
const char* kHoldG = "Hold it. Hold the G.";
const char* kKeepPulling = "Keep pulling, over the top.";
const char* kNowRecover = "Now recover! Power up, roll the wings level, nose to the horizon.";
const char* kRecovered = "Recovered. Nicely done.";
const char* kManoeuvreGood = "Good. That was precise.";
const char* kManoeuvreOk = "Done. A little rough, but done.";
// formation
const char* kLeadJoin = "Lead here. I'm ahead of you. Join on my right wing.";
const char* kMoveRight = "Move right.";
const char* kMoveLeft = "Move left.";
const char* kComeUp = "Come up a little.";
const char* kGoDown = "Down a little.";
const char* kAddPower = "Add power.";
const char* kReducePower = "Reduce power.";
const char* kInSlot = "Good. Hold it right there.";
const char* kTooClose = "Too close! Back off!";
const char* kFormationDone = "Formation complete. Lead is breaking away. You're on your own.";
// tanker
const char* kTankerInSight = "Tanker in sight. Stabilise behind the basket.";
const char* kTankerUp = "Up a little.";
const char* kTankerDown = "Down a little.";
const char* kTankerLeft = "Left a little.";
const char* kTankerRight = "Right a little.";
const char* kTankerAddPower = "Add a touch of power.";
const char* kTankerReducePower = "Ease the power back.";
const char* kContact = "Contact. Hold it there. Fuel is flowing.";
const char* kDisconnect = "Disconnect. Back off, stabilise and try again.";
const char* kRefuelDone = "Transfer complete. Disconnect and clear the tanker to the right.";
// landing
const char* kGearDown = "Gear down!";
const char* kOnGlide = "On glide path.";
const char* kHigh = "Slightly high. Ease the power back.";
const char* kLow = "Slightly low. Add power.";
const char* kFastApproach = "Too fast. Slow to approach speed.";
const char* kSlowApproach = "Too slow! Add power!";
const char* kLineLeft = "Come left for the centreline.";
const char* kLineRight = "Come right for the centreline.";
const char* kTouchdown = "Touchdown. Throttle idle and brakes.";
const char* kGoAround = "Go around! Full power, climb, and follow the pattern back round.";
const char* kLanded = "Stopped. Nice landing.";
const char* kHardTouchdown = "That was a firm arrival. Next time fly it down smoother.";
// carrier (landing signal officer)
const char* kPaddles = "Paddles contact. You're on the glide path. Call the ball.";
const char* kLsoHigh = "You're high.";
const char* kLsoLow = "You're low! Power!";
const char* kLsoLeft = "Come left.";
const char* kLsoRight = "Right for line-up.";
const char* kLsoFast = "You're fast.";
const char* kLsoSlow = "You're slow, power.";
const char* kLsoGood = "Looking good, keep it coming.";
const char* kBolter = "Bolter, bolter, bolter! Power up, climb out and come round again.";
const char* kHookDown = "Hook down. Gear down.";
const char* const kTrap[4] = {"Trap. Number one wire. Safe, but late on the ball.", "Trap. Number two wire. Good pass.",
                              "Trap! Number three wire. Perfect.", "Trap. Number four wire. A little high in close."};
// general
const char* kRetry = "Back to the last checkpoint. Take a breath and try it again.";
const char* kCrashed = "You crashed. Press R to retry from the last checkpoint.";
const char* kOffRoute = "You're off the route. Turn back towards the marker.";
const char* kRotate = "Rotate now. Gentle back pressure.";
const char* kAirborne = "Airborne. Positive climb.";
const char* kCourseComplete = "Course complete. Let's look at your score.";
const char* const kRemark[4] = {"There's plenty to work on. Fly it again when you're ready.", "Solid flying, Captain.",
                                "Good flying, Captain. Very clean.", "Outstanding flying, Captain."};
// combat
const std::vector<std::string> kSplash = {"Splash one. Good kill.", "Target destroyed.", "Good hit. Target down."};
const char* kAllDestroyed = "All targets destroyed. Well done.";
const char* kMissileInbound = "Missile inbound! Flares and break hard!";
const char* kBanditSix = "Bandit on your six! Break turn now!";
const char* kGunsOnly = "Guns only on this exercise. Get in close and put the pipper on him.";
const char* kMissilesOnly = "Missiles only. Wait for the lock tone, then fire.";
const char* kUseAts = "Ground targets. Switch to the air-to-surface missile with Q.";
const char* kOutOfMissiles = "You're out of missiles. Use the gun.";
const char* kSurvived = "Good, you survived it. Now turn the fight around and shoot them down.";
const char* kTooLowCombat = "You're too low to fight here. Climb!";
const char* kIff = "Check the IFF before you shoot: green boxes are ours, red are hostile.";
const char* kFormationClose = "The formation is breaking up and turning in. Fight's on.";
const char* kHeliSlow = "Slow down, you're overshooting the helicopter.";
const char* kKeepSurviving = "Keep it moving. Don't give him a steady target.";
const char* kTargetsLeft[7] = {"", "One target left.", "Two targets left.", "Three targets left.", "Four targets left.", "Five targets left.", "Six targets left."};
const char* const kCombatIntros[] = {
    "Helicopters ahead, low in the valley. Slow down and hunt them.",
    "Outpost ahead. Attack helicopters first, then the armour.",
    "Drones on the range. Guns only. Close in behind them.",
    "Drones on the range. Missiles only. Lock and fire.",
    "Ground targets on the range. Switch to the air-to-surface missile.",
    "Bandit ahead, one F-16. Fight's on.",
    "Bandit ahead, one Su-35. Fight's on.",
    "Bandit ahead, one Su-57. Fight's on.",
    "Bandit ahead, one F-35. Fight's on.",
    "Six helicopters in the area. Attack helicopters first.",
    "Two fighters are covering the outpost. Kill them first.",
    "Now the outpost. Armour and guns. Attack from a shallow dive.",
    "Two bandits ahead. Fight's on.",
    "Two attackers behind you! Survive ninety seconds. Break, flares!",
    "Enemy outpost ahead, with fighter cover. Clear the air, then hit the ground targets.",
    "Patrol boats ahead. Attack with the air-to-surface missile and keep moving.",
    "Bandit ahead, armed this time. Lock, fire, and watch for his missile.",
    "Radar contact: an enemy formation is launching from their base, three aircraft. Intercept them.",
    "Radar contact: a pair of bandits launching from their base. Intercept.",
};
const char* const kFixedIntros[] = {
    "Arch run. Line up early and fly through the middle of each arch, below the rock.",
    "Brakes off, run it up to full power.",
    "Bridge ahead. Get down and fly under the deck, right through the middle.",
    "Carrier approach. Gear and hook down. Fly the ball, keep your line-up, and do not flare.",
    "Climb straight ahead.",
    "Formation. Your lead will pull up ahead of you. Join right echelon: behind and to the right, and hold it.",
    "Masts ahead. Weave between them through the gates, and stay off the wires: keep it smooth.",
    "Military compound ahead. Overfly it no lower than two hundred and fifty metres.",
    "Pillar slalom. Slow down, and weave left and right around the rock towers through the gates.",
    "Positive climb, gear up.",
    "Stabilise behind the basket, match its speed, and push in gently.",
    "Start the engine with E.",
    "Turn final. Gear down, and fly the rings down the glide path.",
    "Viaduct ahead. Cross it low, just above the deck.",
    "Level turn. Roll to sixty degrees of bank and turn all the way round. Hold your height.",
    "Climbing turn. Full power, forty to sixty degrees of bank, and climb six hundred metres through a full turn.",
    "Descending turn. Power back, forty to sixty degrees of bank, and lose six hundred metres through a full turn. Watch your speed.",
    "High G. Pull five and a half G and hold it for three seconds. Tense up.",
    "High G. Pull four and a half G and hold it for three seconds. Tense up.",
    "Loop. Full power, wings level, and pull all the way over the top.",
    "Aileron roll. Full stick to the side, one complete roll.",
    "Recovery. Pull the nose up past forty-five degrees, then recover to level flight. Power up, roll level, nose to the horizon.",
    "Climb, your lead is joining you.",
    "Climb to the tanker's altitude.",
    "Climb to a safe manoeuvring height.",
    "High ground ahead. Climb now, full power.",
    "You're on the catapult. Full afterburner, then brakes off and the cat fires.",
};
}  // namespace say_

const char* combatIntroText(int i) {
    const int n = static_cast<int>(sizeof(say_::kCombatIntros) / sizeof(say_::kCombatIntros[0]));
    return say_::kCombatIntros[std::clamp(i, 0, n - 1)];
}

std::string headingCall(bool right, int deg10) {
    return std::string(right ? "Right" : "Left") + " turn coming up, heading " + TrainingSession::headingWords(deg10) + ".";
}
std::string steerCall(int deg10) { return "Off the route. Come to heading " + TrainingSession::headingWords(deg10) + "."; }

int kmh10(double ms) { return static_cast<int>(std::lround(ms * 3.6 / 10.0)) * 10; }
std::string rotateCall(const AircraftProfile& p) {
    return "In the " + std::string(p.shortName) + " you rotate at " + TrainingSession::numberWords(kmh10(p.rotateSpeedMs)) + " kilometres an hour.";
}
std::string approachCall(const AircraftProfile& p) {
    return "Your approach speed is " + TrainingSession::numberWords(kmh10(p.approachSpeedMs)) + " kilometres an hour.";
}
std::string tankerIntro(int alt) {
    return "Tanker is orbiting at " + TrainingSession::numberWords(alt) + " metres. Join from behind.";
}
std::string cueLine(GateCue c) {
    switch (c) {
        case GateCue::Pass: return "Mountain pass ahead. Climb over the saddle and follow the ground down.";
        case GateCue::Climb: return "Rising ground ahead. Start your climb now.";
        case GateCue::Descend: return "The ground falls away ahead. Follow it down.";
        case GateCue::Canyon: return "Canyon ahead. Stay in the middle, walls on both sides.";
        case GateCue::Pillars: return "Pillars ahead. Weave through the gates.";
        case GateCue::Arch: return "Arch ahead. Line up and fly through the middle.";
        case GateCue::Bridge: return "Bridge ahead. Steady, line it up.";
        case GateCue::Masts: return "Masts ahead. Smooth turns between them.";
        case GateCue::Compound: return "Compound ahead. Hold your minimum height.";
        default: return "";
    }
}
int compassTens(Vec3 d) {
    double deg = std::atan2(d.z, d.x) / kDeg + 90.0;
    int t = static_cast<int>(std::lround(deg / 10.0));
    return ((t % 36) + 36) % 36;
}

}  // namespace

const char* trainingCourseCompleteLine() { return say_::kCourseComplete; }
const char* trainingRemarkLine(double total) {
    int k = total >= 90.0 ? 3 : (total >= 78.0 ? 2 : (total >= 62.0 ? 1 : 0));
    return say_::kRemark[k];
}
bool academyLocationAllowed(const std::string& id, int level) {
    if (id == "everest") return false;   // 5-8 km up: too thin for low-level work and air combat
    if (trainingLevel(level).needsSea) return id == "desert" || id == "musandam" || id == "rio";
    return true;
}


std::string aircraftHandlingLine(int id) {
    switch (static_cast<AircraftId>(id)) {
        case AircraftId::F22: return "You're in the Raptor. Lots of thrust and thrust vectoring: it turns hard at low speed, so don't let the speed run away.";
        case AircraftId::F35: return "You're in the Lightning. It is heavy for its thrust: plan your climbs early and keep your energy up.";
        case AircraftId::Rafale: return "You're in the Rafale. Light and agile, a delta wing: it bleeds speed in hard turns, so use the power.";
        case AircraftId::Su57: return "You're in the Felon. Huge thrust and vectoring nozzles: smooth inputs, it reacts quickly.";
        case AircraftId::Su35: return "You're in the Flanker. A big, heavy jet with vectoring: give yourself room in the tight turns.";
        case AircraftId::MiG29: return "You're in the Fulcrum. Quick and nimble, but small tanks: watch your fuel and your afterburner.";
        case AircraftId::Su30: return "You're in the Su-30. Big and stable, heavier in the roll: start your turns early.";
        case AircraftId::F16: return "You're in the Viper. Light, quick in the roll, single engine: be gentle, it responds fast.";
        default: return "";
    }
}

std::string handlingModeLine(int mode) {
    switch (mode) {
        case 0: return "Arcade handling: the jet goes where the nose points. Fly it like a fighter, but the ground is still hard.";
        case 1: return "Assisted handling: the fly-by-wire protects you from stalling. Pull as hard as you need to.";
        default: return "Simulation handling: no protection. Watch your angle of attack and your speed in the turns.";
    }
}

std::vector<VoiceLine> trainingVoiceLines() {
    std::vector<VoiceLine> v;
    std::set<std::string> seen;
    auto add = [&](Speaker s, const std::string& t) {
        if (!t.empty() && seen.insert(voiceKey(s, t)).second) v.push_back({s, t});
    };
    using namespace say_;
    const Speaker I = Speaker::Instructor;
    for (const auto& L : trainingLevels())
        for (const auto& b : L.briefing) add(I, b);
    for (int a = 0; a < static_cast<int>(AircraftId::Count); ++a) {
        add(I, aircraftHandlingLine(a));
        add(I, rotateCall(profile(static_cast<AircraftId>(a))));
        add(I, approachCall(profile(static_cast<AircraftId>(a))));
    }
    for (int m = 0; m < 3; ++m) add(I, handlingModeLine(m));
    for (int r = 0; r < 8; ++r) add(I, styleIntro(static_cast<RouteStyle>(r)));
    for (int alt = 3000; alt <= 12000; alt += 500) add(I, tankerIntro(alt));
    for (int h = 0; h < 36; ++h) { add(I, headingCall(true, h)); add(I, headingCall(false, h)); add(I, steerCall(h)); }
    for (int c = 0; c <= static_cast<int>(GateCue::Formation); ++c) add(I, cueLine(static_cast<GateCue>(c)));
    for (const auto* list : {&kGateHit, &kGateMiss, &kGateLow, &kGateHigh, &kSectionDone})
        for (const auto& s : *list) add(I, s);
    for (const char* s : {kTooFast, kTooSlow, kCeiling, kBelowGate, kAboveGate, kPullUp, kObstacle, kStall, kOverG, kLowFuel, kWingsLevel, kMoreBank, kLessBank,
                          kHoldHeight, kKeepClimbing, kKeepDescending, kPullHarder, kHoldG, kKeepPulling, kNowRecover, kRecovered, kManoeuvreGood, kManoeuvreOk,
                          kTankerInSight, kTankerUp, kTankerDown, kTankerLeft, kTankerRight, kTankerAddPower, kTankerReducePower, kContact, kDisconnect, kRefuelDone,
                          kGearDown, kOnGlide, kHigh, kLow, kFastApproach, kSlowApproach, kLineLeft, kLineRight, kTouchdown, kGoAround, kLanded, kHardTouchdown,
                          kRetry, kCrashed, kOffRoute, kRotate, kAirborne, kCourseComplete, kHookDown})
        add(I, s);
    for (const char* s : kRemark) add(I, s);
    for (const auto& s : kSplash) add(I, s);
    for (const char* s : {kIff, kFormationClose, kAllDestroyed, kMissileInbound, kBanditSix, kGunsOnly, kMissilesOnly, kUseAts, kOutOfMissiles, kSurvived, kTooLowCombat, kHeliSlow, kKeepSurviving})
        add(I, s);
    for (int n = 1; n <= 6; ++n) add(I, kTargetsLeft[n]);
    for (const char* s : kCombatIntros) add(I, s);
    for (const char* s : {kLeadJoin, kMoveRight, kMoveLeft, kComeUp, kGoDown, kAddPower, kReducePower, kInSlot, kTooClose, kFormationDone}) add(Speaker::Wingman, s);
    for (const char* s : {kPaddles, kLsoHigh, kLsoLow, kLsoLeft, kLsoRight, kLsoFast, kLsoSlow, kLsoGood, kBolter}) add(Speaker::Tower, s);
    for (const char* s : kTrap) add(Speaker::Tower, s);
    for (const char* s : kFixedIntros) add(I, s);
    return v;
}

}  // namespace sky

// =============================================================================================
// Session
// =============================================================================================
namespace sky {

namespace {
bool hasGatesKind(SectionKind k) { return k == SectionKind::Route || k == SectionKind::Landing || k == SectionKind::CarrierLanding; }
bool isManoeuvre(SectionKind k) {
    return k == SectionKind::LevelTurn || k == SectionKind::ClimbTurn || k == SectionKind::DescendTurn || k == SectionKind::HighG ||
           k == SectionKind::Loop || k == SectionKind::Roll || k == SectionKind::Recovery;
}
double avg(const std::vector<double>& v) {
    if (v.empty()) return -1;
    double s = 0;
    for (double x : v) s += x;
    return s / v.size();
}
Vec3 flat(Vec3 v) { v.y = 0; double l = length(v); return l > 1e-9 ? v / l : Vec3{1, 0, 0}; }
}  // namespace

bool TrainingSession::sectionHasGates() const {
    if (section_ >= course_.sections.size()) return false;
    const Section& s = course_.sections[section_];
    return hasGatesKind(s.kind) && gate_ >= s.gateBegin && gate_ < s.gateEnd;
}

void TrainingSession::begin(Course course, Battle& battle, int aircraftId, int handlingMode) {
    *this = TrainingSession{};
    course_ = std::move(course);
    aircraft_ = aircraftId;
    handling_ = handlingMode;
    for (const Section& s : course_.sections)
        if (s.kind == SectionKind::Refuel) refuelRequired_ = true;
    if (course_.tanker) battle.startTanker(course_.tankerCenter, course_.tankerRadius, course_.tankerSpeed, 0.0);
    spawned_.assign(course_.engagements.size(), Spawned{});
    for (const Engagement& e : course_.engagements)
        if (e.continueFrom < 0) targets_ += static_cast<int>(e.jets.size() + e.ground.size()) + e.transportHelis + e.attackHelis;
    {
        const Jet& p = battle.player();
        playerMissiles_ = p.missiles; playerAgms_ = p.agms; playerRounds_ = p.rounds; playerFlares_ = p.flareCharges;
    }
    for (const Prop& p : course_.props) {
        GroundTarget g;
        g.kind = p.kind;
        g.pos = p.pos;
        g.heading = p.heading;
        g.armed = false;
        g.label = p.label;
        battle.addGround(g);
    }
}

Vec3 TrainingSession::slotPosition(const Battle& battle) const {
    const Jet* lead = nullptr;
    for (const auto& j : battle.jets())
        if (j->id == leadId_) lead = j.get();
    if (!lead) return {};
    const FlightState& l = lead->fm->state();
    Vec3 f = flat(l.vel);
    Vec3 r{-f.z, 0, f.x};
    return l.pos - f * 40.0 + r * 40.0 - Vec3{0, 5.0, 0};
}

void TrainingSession::say(Speaker who, const std::string& text, int priority, const std::string& key, double cooldown) {
    if (text.empty()) return;
    for (auto& c : cooldowns_)
        if (c.first == key && clock_ < c.second) return;
    if (priority < 2 && clock_ - lastCallTime_ < (priority == 0 ? 6.0 : 2.5)) return;
    bool found = false;
    for (auto& c : cooldowns_)
        if (c.first == key) { c.second = clock_ + cooldown; found = true; }
    if (!found) cooldowns_.push_back({key, clock_ + cooldown});
    lastCallTime_ = clock_;
    radio_.push_back({who, text, priority});
}

void TrainingSession::sayVariant(Speaker who, const std::vector<std::string>& variants, int priority, const std::string& key, double cooldown) {
    if (variants.empty()) return;
    say(who, variants[static_cast<size_t>(variantCounter_) % variants.size()], priority, key, cooldown);
    // advance only when something was actually said
    if (!radio_.empty() && radio_.back().text == variants[static_cast<size_t>(variantCounter_) % variants.size()]) ++variantCounter_;
}

std::vector<RadioCall> TrainingSession::drainRadio() { auto r = std::move(radio_); radio_.clear(); return r; }
std::vector<TrainingEvent> TrainingSession::drainEvents() { auto e = std::move(events_); events_.clear(); return e; }

void TrainingSession::notifyCrash() {
    ++crashes_;
    events_.push_back({TrainingEventKind::Crash, static_cast<int>(section_), 0});
    say(Speaker::Instructor, say_::kCrashed, 2, "crashed", 3);
}

void TrainingSession::beginSection(size_t idx, const MissionContext& ctx, Battle& battle) {
    using namespace say_;
    const FlightState& f = *ctx.flight;
    if (section_ < course_.sections.size() && idx > section_) {
        events_.push_back({TrainingEventKind::SectionDone, static_cast<int>(section_), 0});
        SectionKind pk = course_.sections[section_].kind;
        if (pk == SectionKind::Route) sayVariant(Speaker::Instructor, kSectionDone, 0, "secdone", 20);
    }
    if (engagement_ >= 0) {
        // leaving an engagement: a survive exercise hands its attackers to the next section
        bool handOver = idx < course_.sections.size() && course_.sections[idx].kind == SectionKind::Engage &&
                        course_.engagements[static_cast<size_t>(course_.sections[idx].value)].continueFrom == engagement_;
        if (!handOver) endEngagement(battle, false);
        else engagement_ = -1;
    }
    section_ = idx;
    const Section& s = course_.sections[idx];
    gate_ = (s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding) ? s.gateStart : s.gateBegin;
    // after a fight the player can be anywhere: rejoin the route at the gate that makes the
    // shortest way home (at least 3 km away, so there is room to descend onto it)
    // (also after the tanker or any exercise that leaves the player well above or away from it)
    bool rejoin = false;
    if (s.kind == SectionKind::Route && idx > 0 && s.gateEnd - s.gateBegin > 1) {
        SectionKind pk = course_.sections[idx - 1].kind;
        const Vec3& g0 = course_.gates[s.gateBegin].pos;
        double hd0 = std::hypot(g0.x - f.pos.x, g0.z - f.pos.z);
        rejoin = pk == SectionKind::Engage;
        (void)hd0;
    }
    if (rejoin) {
        double rest = 0;
        std::vector<double> remain(static_cast<size_t>(s.gateEnd - s.gateBegin), 0.0);
        for (int k = s.gateEnd - 1; k >= s.gateBegin; --k) {
            remain[static_cast<size_t>(k - s.gateBegin)] = rest;
            if (k > s.gateBegin) rest += length(course_.gates[k].pos - course_.gates[k - 1].pos);
        }
        double best = 1e18;
        int bk = s.gateBegin;
        const int half = s.gateEnd - 1;
        for (int k = s.gateBegin; k < half; ++k) {
            const Vec3& gp = course_.gates[k].pos;
            double hd = std::hypot(gp.x - f.pos.x, gp.z - f.pos.z);
            double drop = std::max(0.0, f.pos.y - gp.y);
            if (hd < std::max(3000.0, drop * 4.0) && k + 1 < half) continue;   // room for a gentle descent
            double cost = hd + remain[static_cast<size_t>(k - s.gateBegin)];
            if (cost < best) { best = cost; bk = k; }
        }
        gate_ = bk;
    }
    gateLinger_ = 0;
    prevS_ = -1e9;
    sectionComplete_ = false;
    taskAcc_ = taskAcc2_ = taskTimer_ = 0;
    taskPhase_ = 0;
    taskProgress_ = 0;
    taskStatus_.clear();
    taskErrSum_ = taskErrN_ = 0;
    taskStartAlt_ = f.pos.y;
    taskPrevHeading_ = toEuler(f.q).heading;
    announcedCueGate_ = -1;
    offCourseTimer_ = 0;
    prevDist_ = 1e18;
    fastTimer_ = slowTimer_ = ceilingTimer_ = 0;
    pullCalled_ = false;
    sectionStartSim_ = f.simTime;
    sectionClock_ = 0;
    // checkpoint at the start of every section
    cp_.onGround = f.onGround;
    cp_.engineRunning = f.engine == EngineState::Running || f.engine == EngineState::Starting;
    cp_.gearDown = f.gearDown;
    cp_.pos = f.pos;
    cp_.vel = f.vel;
    cp_.q = f.q;
    cp_.throttle = f.throttle;
    cp_.fuel = f.fuelKg;
    cp_.section = idx;
    cp_.gateScores = gateScores_.size();
    cp_.manoeuvreScores = manoeuvreScores_.size();
    cp_.hit = gatesHit_;
    cp_.missed = gatesMissed_;
    cp_.combatScores = combatScores_.size();
    cp_.kills = kills_;
    checkpointValid_ = !f.crashed;

    const AircraftProfile& prof = *ctx.aircraft;
    switch (s.kind) {
        case SectionKind::Takeoff:
            say(Speaker::Instructor, rotateCall(prof), 1, "rotatespeed", 30);
            rotateCalled_ = airborneCalled_ = false;
            break;
        case SectionKind::Formation: {
            slotTime_ = formationTime_ = 0;
            inSlot_ = false;
            inSlotCalled_ = false;
            Jet* lead = battle.jetById(leadId_);
            Vec3 fw = flat(f.vel);
            Vec3 p = f.pos + fw * 1100.0 + Vec3{0, 20.0, 0};
            size_t startIdx = static_cast<size_t>(-1);
            const auto& rt = course_.formationRoute;
            if (!rt.empty()) {
                // the lead appears on its loop (over the cleared ground), at the point of the loop
                // ahead of us, flying the loop's way: a head-on or a reversal would be a collision
                double bd = 1e18;
                for (size_t k = 0; k < rt.size(); ++k) {
                    Vec3 q = rt[k];
                    Vec3 leg = flat(rt[(k + 1) % rt.size()] - rt[k]);
                    double dd = std::hypot(q.x - f.pos.x, q.z - f.pos.z);
                    double sc = std::fabs(dd - 1500.0) + (1.0 - dot(leg, fw)) * 1500.0;
                    if (sc < bd) { bd = sc; startIdx = k; }
                }
                p = rt[startIdx];
                fw = flat(rt[(startIdx + 1) % rt.size()] - rt[startIdx]);
            }
            double hdg = std::atan2(fw.z, fw.x);
            if (!lead) {
                Jet& j = battle.debugAddJet(Team::Blue, static_cast<AircraftId>(aircraft_), p, hdg, course_.formationSpeed);
                j.callsign = "LEAD";
                j.missiles = 0;
                j.rounds = 0;
                leadId_ = j.id;
                lead = &j;
            } else {
                lead->alive = true;
                lead->health = 100;
                lead->fm->resetAirborne(p, hdg, course_.formationSpeed, 0.8);
            }
            lead->route = course_.formationRoute;
            lead->routeLoop = true;
            lead->formationLead = true;
            lead->routeSpeed = course_.formationSpeed;
            // first route point ahead of the lead
            size_t best = 0;
            double bd = 1e18;
            for (size_t k = 0; k < lead->route.size(); ++k) {
                Vec3 d = lead->route[k] - p;
                if (dot(flat(d), fw) < 0.3) continue;
                double l = length(d);
                if (l < bd) { bd = l; best = k; }
            }
            if (startIdx != static_cast<size_t>(-1)) best = (startIdx + 1) % lead->route.size();
            lead->routeIdx = best;
            say(Speaker::Wingman, kLeadJoin, 1, "leadjoin", 20);
            break;
        }
        case SectionKind::Refuel:
            refuelBase_ = battle.tanker().contactTime;
            refuelContact_ = 0;
            wasContact_ = false;
            disconnects_ = 0;
            break;
        case SectionKind::Engage:
            beginEngagement(s, ctx, battle);
            break;
        case SectionKind::Landing:
        case SectionKind::CarrierLanding:
            baseBolters_ = f.bolters;
            boltersSeen_ = 0;
            landed_ = trapped_ = touchdownScored_ = false;
            goAroundArmed_ = false;
            briefedApproach_ = false;
            paddlesCalled_ = false;
            break;
        default: break;
    }
    if (!s.intro.empty()) say(Speaker::Instructor, s.intro, 1, "intro" + std::to_string(idx), 1e9);
}

void TrainingSession::update(const MissionContext& ctx, Battle& battle, size_t step, double dt) {
    clock_ += dt;
    if (!ctx.flight || course_.sections.empty()) return;
    if (step >= course_.sections.size()) return;
    if (step != section_) beginSection(step, ctx, battle);
    const FlightState& f = *ctx.flight;
    if (f.crashed) return;
    sectionClock_ += dt;
    if (!f.onGround || f.airspeed > 1.0) flightTime_ += dt;
    const Section& s = course_.sections[section_];
    switch (s.kind) {
        case SectionKind::Route: updateGates(ctx, dt); break;
        case SectionKind::Landing:
        case SectionKind::CarrierLanding:
            updateGates(ctx, dt);
            updateLanding(ctx, dt);
            break;
        case SectionKind::Formation: updateFormation(ctx, battle, dt); break;
        case SectionKind::Engage: updateEngagement(s, ctx, battle, dt); break;
        case SectionKind::Rendezvous: {
            const Tanker& tk = battle.tanker();
            double d = length(f.pos - tk.pos);
            Vec3 tf{std::cos(tk.heading), 0, std::sin(tk.heading)};
            bool behind = dot(f.pos - tk.pos, tf) < 0.0;
            bool aligned = dot(flat(f.vel), tf) > 0.7;
            taskProgress_ = clampd(1.0 - (d - s.value) / 20000.0, 0.0, 1.0);
            if (d < s.value && behind && aligned) sectionComplete_ = true;
            break;
        }
        case SectionKind::Refuel: {
            const Tanker& tk = battle.tanker();
            refuelContact_ = tk.contactTime - refuelBase_;
            if (wasContact_ && !tk.contact) {
                ++disconnects_;
                say(Speaker::Instructor, say_::kDisconnect, 1, "disconnect", 6);
            }
            if (!wasContact_ && tk.contact) say(Speaker::Instructor, say_::kContact, 1, "contact", 8);
            wasContact_ = tk.contact;
            // guided capture: within a few metres of the basket at a gentle closure speed the
            // drogue's bow wave draws the probe in (moderately accessible for every aircraft)
            if (!tk.contact) {
                FlightState& ps = battle.player().fm->mutableState();
                Vec3 probe = ps.pos + ps.q.forward() * 8.0 + ps.q.up() * 1.0;
                Vec3 rel = tk.basket() - probe;
                double rs = length(ps.vel - tk.vel);
                if (length(rel) < 28.0 && rs < 16.0) {
                    double k = std::min(1.0, 0.9 * dt);
                    ps.pos = ps.pos + rel * k;
                    ps.vel = ps.vel + (tk.vel - ps.vel) * std::min(1.0, 0.8 * dt);
                }
            }
            taskProgress_ = clampd(refuelContact_ / s.value, 0.0, 1.0);
            if (refuelContact_ >= s.value && !sectionComplete_) {
                sectionComplete_ = true;
                refuelDone_ = true;
                refuelScore_ = clampd(100.0 - disconnects_ * 8.0 - std::max(0.0, sectionClock_ - 70.0) * 0.4, 30.0, 100.0);
                events_.push_back({TrainingEventKind::RefuelContact, 0, refuelContact_});
                say(Speaker::Instructor, say_::kRefuelDone, 1, "refueldone", 30);
            }
            break;
        }
        default:
            if (isManoeuvre(s.kind)) updateTask(ctx, dt);
            break;
    }
    updateScoring(ctx, dt);
    instructor(ctx, battle, dt);
}

void TrainingSession::updateGates(const MissionContext& ctx, double dt) {
    if (!sectionHasGates()) return;
    const FlightState& f = *ctx.flight;
    const Section& s = course_.sections[section_];
    const Gate& g = course_.gates[gate_];
    double sd = dot(f.pos - g.pos, g.dir);
    if (prevS_ < -1e8) { prevS_ = sd; return; }
    auto advance = [&]() {
        ++gate_;
        gateLinger_ = 0;
        prevS_ = -1e9;
        if (gate_ < s.gateEnd) {
            const Gate& n = course_.gates[gate_];
            prevS_ = dot(f.pos - n.pos, n.dir);
        }
    };
    bool obstacleGate = g.cue == GateCue::Pillars || g.cue == GateCue::Arch || g.cue == GateCue::Bridge || g.cue == GateCue::Masts || g.cue == GateCue::Compound;
    if (prevS_ < 0 && sd >= 0) {
        double frac = (sd - prevS_) > 1e-9 ? sd / (sd - prevS_) : 0.0;
        Vec3 p = f.pos - f.vel * (dt * frac);
        Vec3 off = p - g.pos;
        off = off - g.dir * dot(off, g.dir);
        double d = length(off);
        if (d > std::max(2500.0, 6.0 * g.radius)) { prevS_ = sd; return; }   // a stray crossing far from the gate
        double agl = p.y - g.ground;
        bool ring = d <= g.radius;
        bool windowOk = g.maxAgl <= 0 || (agl >= g.minAgl - 5.0 && agl <= g.maxAgl + 5.0);
        double score = 0;
        if (ring && windowOk) {
            score = 100.0 * (1.0 - 0.45 * d / g.radius);
            ++gatesHit_;
            events_.push_back({TrainingEventKind::GateHit, gate_, d});
            sayVariant(Speaker::Instructor, say_::kGateHit, 0, "gatehit", 14);
        } else if (ring) {
            score = 40.0;
            ++gatesHit_;
            events_.push_back({TrainingEventKind::GateHit, gate_, d});
            sayVariant(Speaker::Instructor, agl < g.minAgl ? say_::kGateLow : say_::kGateHigh, 1, "gatewin", 8);
        } else {
            ++gatesMissed_;
            events_.push_back({TrainingEventKind::GateMissed, gate_, d});
            sayVariant(Speaker::Instructor, say_::kGateMiss, 1, "gatemiss", 6);
        }
        gateScores_.push_back(score);
        if (obstacleGate) manoeuvreScores_.push_back(score);
        advance();
        return;
    }
    // circling near a gate without getting through it (or round it): after 45 s the
    // instructor moves the route on, so nobody is ever trapped at one gate
    if (std::hypot(f.pos.x - g.pos.x, f.pos.z - g.pos.z) < 2000.0) gateLinger_ += dt;
    if (gateLinger_ > 45.0 && gate_ + 1 < s.gateEnd) {
        ++gatesMissed_;
        gateScores_.push_back(0);
        if (obstacleGate) manoeuvreScores_.push_back(0);
        events_.push_back({TrainingEventKind::GateMissed, gate_, 0});
        sayVariant(Speaker::Instructor, say_::kGateMiss, 1, "gatemiss", 6);
        gateLinger_ = 0;
        advance();
        return;
    }
    // cut a corner: reached the next gate without crossing this one
    if (gate_ + 1 < s.gateEnd) {
        const Gate& n = course_.gates[gate_ + 1];
        if (length(f.pos - n.pos) < std::max(n.radius, 150.0) && length(f.pos - g.pos) > g.radius * 2.0) {
            ++gatesMissed_;
            gateScores_.push_back(0);
            if (obstacleGate) manoeuvreScores_.push_back(0);
            events_.push_back({TrainingEventKind::GateMissed, gate_, 0});
            advance();
            return;
        }
    }
    prevS_ = sd;
}

void TrainingSession::updateTask(const MissionContext& ctx, double dt) {
    using namespace say_;
    const FlightState& f = *ctx.flight;
    const Section& s = course_.sections[section_];
    if (sectionComplete_) return;
    Euler e = toEuler(f.q);
    double bank = e.bank / kDeg, pitch = e.pitch / kDeg;
    double dh = wrapPi(e.heading - taskPrevHeading_);
    taskPrevHeading_ = e.heading;
    taskTimer_ += dt;
    double score = -1;
    char buf[160];
    switch (s.kind) {
        case SectionKind::LevelTurn: {
            if (std::fabs(bank) > 35.0 && std::fabs(pitch) < 60.0) taskAcc_ += dh;
            double turned = std::fabs(taskAcc_) / kDeg;
            double altErr = f.pos.y - taskStartAlt_;
            if (std::fabs(bank) > 35.0) {
                taskErrSum_ += std::fabs(altErr) * dt;
                taskAcc2_ += std::fabs(std::fabs(bank) - s.value) * dt;
                taskErrN_ += dt;
            }
            taskProgress_ = clampd(turned / s.value2, 0, 1);
            std::snprintf(buf, sizeof buf, "BANK %d / %d   TURNED %d / %d   ALT %+d M", static_cast<int>(std::fabs(bank)), static_cast<int>(s.value),
                          static_cast<int>(turned), static_cast<int>(s.value2), static_cast<int>(altErr));
            taskStatus_ = buf;
            if (turned >= s.value2) {
                double n = std::max(taskErrN_, 1.0);
                score = std::max(0.0, 100.0 - std::max(0.0, taskErrSum_ / n - 30.0) * 0.25 - std::max(0.0, taskAcc2_ / n - 8.0) * 1.2);
            }
            break;
        }
        case SectionKind::ClimbTurn:
        case SectionKind::DescendTurn: {
            bool climb = s.kind == SectionKind::ClimbTurn;
            if (std::fabs(bank) > 15.0 && std::fabs(pitch) < 70.0) taskAcc_ += dh;
            double turned = std::fabs(taskAcc_) / kDeg;
            double dz = (f.pos.y - taskStartAlt_) * (climb ? 1.0 : -1.0);
            if (std::fabs(bank) > 15.0) {
                double be = std::fabs(bank) < 40.0 ? 40.0 - std::fabs(bank) : (std::fabs(bank) > 60.0 ? std::fabs(bank) - 60.0 : 0.0);
                taskAcc2_ += be * dt;
                taskErrN_ += dt;
            }
            if (!climb && f.airspeed > 320.0) taskErrSum_ += dt;
            taskProgress_ = clampd(std::min(turned / s.value2, dz / s.value), 0, 1);
            std::snprintf(buf, sizeof buf, "BANK %d   TURNED %d / %d   %s %d / %d M", static_cast<int>(std::fabs(bank)), static_cast<int>(turned),
                          static_cast<int>(s.value2), climb ? "CLIMBED" : "DESCENDED", static_cast<int>(std::max(0.0, dz)), static_cast<int>(s.value));
            taskStatus_ = buf;
            if (turned >= s.value2 && dz >= s.value) {
                double n = std::max(taskErrN_, 1.0);
                score = std::max(0.0, 100.0 - std::max(0.0, taskAcc2_ / n - 4.0) * 1.5 - taskErrSum_ * 4.0);
            }
            break;
        }
        case SectionKind::HighG: {
            taskAcc2_ = std::max(taskAcc2_, f.gLoad);
            if (f.gLoad >= s.value) taskAcc_ += dt;
            else if (f.gLoad < s.value - 0.6) taskAcc_ = 0;
            if (f.gLoad > ctx.aircraft->gLimit + 0.3) taskErrSum_ += dt;
            taskProgress_ = clampd(taskAcc_ / s.value2, 0, 1);
            std::snprintf(buf, sizeof buf, "G %.1f / %.1f   HELD %.1f / %.0f S", f.gLoad, s.value, taskAcc_, s.value2);
            taskStatus_ = buf;
            if (taskAcc_ >= s.value2) score = std::max(0.0, 100.0 - taskErrSum_ * 25.0 - std::max(0.0, taskTimer_ - 20.0) * 1.0);
            break;
        }
        case SectionKind::Loop: {
            taskAcc_ += f.omega.z * dt;
            taskAcc2_ += std::fabs(f.omega.x) * dt;
            taskProgress_ = clampd(taskAcc_ / s.value, 0, 1);
            std::snprintf(buf, sizeof buf, "LOOP %d%%   PITCH %+d", static_cast<int>(taskProgress_ * 100), static_cast<int>(pitch));
            taskStatus_ = buf;
            if (taskAcc_ < -1.0) taskAcc_ = -1.0;
            if (taskAcc_ >= s.value) score = std::max(0.0, 100.0 - std::max(0.0, taskAcc2_ / kDeg - 40.0) * 0.25 - std::max(0.0, taskTimer_ - 35.0) * 0.8);
            break;
        }
        case SectionKind::Roll: {
            taskAcc_ += f.omega.x * dt;
            taskProgress_ = clampd(std::fabs(taskAcc_) / s.value, 0, 1);
            std::snprintf(buf, sizeof buf, "ROLL %d%%   ALT %+d M", static_cast<int>(taskProgress_ * 100), static_cast<int>(f.pos.y - taskStartAlt_));
            taskStatus_ = buf;
            if (std::fabs(taskAcc_) >= s.value) score = std::max(0.0, 100.0 - std::max(0.0, std::fabs(f.pos.y - taskStartAlt_) - 60.0) * 0.2 - std::max(0.0, taskTimer_ - 12.0) * 1.5);
            break;
        }
        case SectionKind::Recovery: {
            if (taskPhase_ == 0) {
                taskProgress_ = clampd(pitch / s.value, 0, 0.5);
                std::snprintf(buf, sizeof buf, "NOSE UP: PITCH %+d / %d", static_cast<int>(pitch), static_cast<int>(s.value));
                if (pitch >= s.value) {
                    taskPhase_ = 1;
                    taskAcc_ = 0;
                    say(Speaker::Instructor, kNowRecover, 2, "recover", 10);
                }
            } else {
                taskAcc_ += dt;
                taskProgress_ = 0.5 + 0.5 * clampd(taskAcc_ / s.value2, 0, 1);
                std::snprintf(buf, sizeof buf, "RECOVER: BANK %d  PITCH %+d   %.1f S", static_cast<int>(bank), static_cast<int>(pitch), taskAcc_);
                if (std::fabs(bank) < 15.0 && std::fabs(pitch) < 10.0 && f.airspeed > ctx.aircraft->rotateSpeedMs) {
                    score = std::max(0.0, 100.0 - std::max(0.0, taskAcc_ - 6.0) * 5.0);
                    say(Speaker::Instructor, kRecovered, 1, "recovered", 30);
                }
            }
            taskStatus_ = buf;
            break;
        }
        default: break;
    }
    if (score >= 0) {
        score = clampd(score, 20.0, 100.0);
        manoeuvreScores_.push_back(score);
        sectionComplete_ = true;
        taskProgress_ = 1.0;
        if (s.kind != SectionKind::Recovery) say(Speaker::Instructor, score >= 80 ? kManoeuvreGood : kManoeuvreOk, 1, "mdone", 4);
    }
}

void TrainingSession::updateFormation(const MissionContext& ctx, Battle& battle, double dt) {
    const FlightState& f = *ctx.flight;
    const Section& s = course_.sections[section_];
    Jet* lead = battle.jetById(leadId_);
    if (!lead || !lead->alive) { sectionComplete_ = true; return; }
    if (sectionComplete_) return;
    const FlightState& l = lead->fm->state();
    Vec3 slot = slotPosition(battle);
    double dist = length(f.pos - slot);
    double rel = length(f.vel - l.vel);
    inSlot_ = dist < 60.0 && rel < 25.0;
    formationTime_ += dt;
    if (inSlot_) {
        slotTime_ += dt;
        taskErrSum_ += dist * dt;
        taskErrN_ += dt;
    }
    double far = length(f.pos - l.pos);
    lead->routeSpeed = far > 3000.0 ? course_.formationSpeed * 0.75 : far > 1200.0 ? course_.formationSpeed * 0.85 : course_.formationSpeed;
    if (far < 14.0) battle.player().fm->forceCrash(CrashReason::Obstacle);   // mid-air collision with the lead
    taskProgress_ = clampd(slotTime_ / s.value, 0, 1);
    char buf[120];
    std::snprintf(buf, sizeof buf, "%s   SLOT %d M   %.0f / %.0f S", inSlot_ ? "IN POSITION" : "JOIN RIGHT ECHELON", static_cast<int>(dist), slotTime_, s.value);
    taskStatus_ = buf;
    if (slotTime_ >= s.value) {
        double avgErr = taskErrSum_ / std::max(taskErrN_, 1.0);
        double score = clampd(100.0 - std::max(0.0, avgErr - 18.0) * 1.2 - std::max(0.0, formationTime_ - s.value - 90.0) * 0.25, 30.0, 100.0);
        manoeuvreScores_.push_back(score);
        sectionComplete_ = true;
        say(Speaker::Wingman, say_::kFormationDone, 1, "formdone", 30);
        // the lead leaves the area: it climbs away on its loop and is removed from the training
        lead->routeSpeed = course_.formationSpeed * 1.3;
    }
}

bool TrainingSession::onApproach() const {
    if (section_ >= course_.sections.size()) return false;
    const Section& s = course_.sections[section_];
    if (s.kind != SectionKind::Landing && s.kind != SectionKind::CarrierLanding) return false;
    int first = course_.finalGate >= s.gateStart && course_.finalGate < s.gateEnd ? course_.finalGate : s.gateStart;
    return gate_ >= first && !landed_ && !trapped_;
}
double TrainingSession::distanceToAim(const FlightState& f) const { return -dot(f.pos - course_.landAim, course_.landDir); }
double TrainingSession::glideDeviation(const FlightState& f) const {
    double d = distanceToAim(f);
    double eye = course_.carrier ? 1.5 : FlightModel::kGearHeight;
    return f.pos.y - (course_.landAim.y + eye + approachHeight(course_, d));
}
double TrainingSession::lineupOffset(const FlightState& f) const {
    Vec3 r{-course_.landDir.z, 0, course_.landDir.x};
    return dot(f.pos - course_.landAim, r);
}

void TrainingSession::updateLanding(const MissionContext& ctx, double dt) {
    using namespace say_;
    const FlightState& f = *ctx.flight;
    const Section& s = course_.sections[section_];
    if (sectionComplete_) return;
    double along = -distanceToAim(f);    // + = past the aim point
    if (s.kind == SectionKind::CarrierLanding) {
        if (f.trapped && !trapped_) {
            trapped_ = true;
            wire_ = f.wireCaught;
            touchdownSink_ = f.touchdownSink;
            const double wireScore[4] = {72, 90, 100, 82};
            int w = std::clamp(wire_, 1, 4);
            landingScore_ = clampd(wireScore[w - 1] - 12.0 * boltersSeen_ - std::max(0.0, touchdownSink_ - 5.0) * 5.0, 25.0, 100.0);
            events_.push_back({TrainingEventKind::Trap, wire_, touchdownSink_});
            say(Speaker::Tower, kTrap[w - 1], 2, "trap", 30);
        }
        if (trapped_ && f.trapped) { sectionComplete_ = true; return; }
        if (f.bolters > baseBolters_ + boltersSeen_) {
            ++boltersSeen_;
            gate_ = s.gateBegin;
            prevS_ = -1e9;
            paddlesCalled_ = false;
            events_.push_back({TrainingEventKind::Bolter, boltersSeen_, 0});
            say(Speaker::Tower, kBolter, 2, "bolter", 4);
            return;
        }
        // flew past the deck without touching it: wave off, round again
        if (gate_ >= s.gateStart && along > 350.0 && !f.onGround && !f.arresting) {
            ++goArounds_;
            gate_ = s.gateBegin;
            prevS_ = -1e9;
            paddlesCalled_ = false;
            events_.push_back({TrainingEventKind::GoAround, goArounds_, 0});
            say(Speaker::Instructor, kGoAround, 2, "goaround", 4);
        }
        return;
    }
    // runway
    bool onRunway = ctx.terrain && ctx.terrain->onRunway(f.pos.x, f.pos.z, 15.0);
    if (f.onGround && !landed_ && f.touchdownTime >= sectionStartSim_) {
        landed_ = true;
        touchdownSink_ = f.touchdownSink;
        Vec3 r{-course_.landDir.z, 0, course_.landDir.x};
        double lat = dot(f.pos - course_.landAim, r);
        double sc = 100.0 - std::max(0.0, touchdownSink_ - 1.2) * 16.0 - std::max(0.0, std::fabs(along) - 300.0) / 12.0 - std::max(0.0, std::fabs(lat) - 4.0) * 1.5 -
                    goArounds_ * 8.0;
        if (!onRunway) sc -= 40.0;
        landingScore_ = clampd(sc, 10.0, 100.0);
        events_.push_back({TrainingEventKind::Touchdown, 0, touchdownSink_});
        say(Speaker::Instructor, touchdownSink_ > 3.0 ? kHardTouchdown : kTouchdown, 1, "touchdown", 20);
    }
    if (landed_ && f.onGround && f.airspeed < 8.0) {
        sectionComplete_ = true;
        say(Speaker::Instructor, kLanded, 1, "landed", 30);
        return;
    }
    if (landed_ && !f.onGround && f.agl > 25.0) {   // touch and go
        landed_ = false;
        landingScore_ = -1;
    }
    bool pastWithGatesLeft = gate_ >= s.gateStart && gate_ < s.gateEnd && along > 300.0;
    if (!landed_ && !f.onGround && (pastWithGatesLeft || (gate_ >= s.gateEnd && (along > 1600.0 || (along > 0 && f.agl > 120.0 && f.vel.y > 2.0))))) {
        ++goArounds_;
        gate_ = s.gateBegin;
        prevS_ = -1e9;
        events_.push_back({TrainingEventKind::GoAround, goArounds_, 0});
        say(Speaker::Instructor, kGoAround, 2, "goaround", 4);
    }
    (void)dt;
}

void TrainingSession::updateScoring(const MissionContext& ctx, double dt) {
    const FlightState& f = *ctx.flight;
    const Section& s = course_.sections[section_];
    bool groundPhase = s.kind == SectionKind::ColdStart || s.kind == SectionKind::Lineup || s.kind == SectionKind::Takeoff ||
                       s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding;
    if (s.speedMin > 0 && s.speedMax > 0 && !f.onGround) {
        disciplineTime_ += dt;
        bool speedOk = f.airspeed >= s.speedMin * 0.95 && f.airspeed <= s.speedMax * 1.05;
        bool ceilOk = s.ceilingAgl <= 0 || f.agl <= s.ceilingAgl;
        if (speedOk && ceilOk) disciplineGood_ += dt;
    }
    if (!groundPhase && !f.onGround && f.agl < 25.0) lowTime_ += dt;
    nearTimer_ = std::max(0.0, nearTimer_ - dt);
    if (ctx.terrain && !f.onGround && nearTimer_ <= 0 && ctx.terrain->obstacleDistance(f.pos, 60.0) < 18.0) {
        ++nearMisses_;
        nearTimer_ = 2.0;
    }
    // stalls and over-stressing, held for more than half a second (arcade has no G limiter, so
    // only gross over-G counts there)
    double gMax = ctx.aircraft->gLimit + 0.5;
    bool overG = handling_ != 0 && (f.gLoad > gMax || f.gLoad < -3.0);
    bool unstable = !f.onGround && (f.stalled || overG);
    unstableHold_ = unstable ? unstableHold_ + dt : 0.0;
    if (unstableHold_ > 0.5) unstableTime_ += dt;
}

ScoreCard TrainingSession::liveScore() const {
    ScoreCard c;
    c.accuracy = avg(gateScores_);
    c.manoeuvre = avg(manoeuvreScores_);
    c.discipline = disciplineTime_ > 5.0 ? clampd(100.0 * (disciplineGood_ / disciplineTime_) / 0.9, 0.0, 100.0) : -1;
    c.clearance = clampd(100.0 - lowTime_ * 6.0 - nearMisses_ * 12.0 - crashes_ * 25.0, 0.0, 100.0);
    c.stability = clampd(100.0 - unstableTime_ * 4.0 - crashes_ * 10.0, 0.0, 100.0);
    c.refuel = refuelRequired_ ? (refuelDone_ ? refuelScore_ : 0.0) : -1;
    c.landing = landingScore_;
    c.combat = course_.engagements.empty() ? -1 : (combatScores_.empty() ? -1 : avg(combatScores_));
    c.kills = kills_;
    c.targets = targets_;
    c.gatesHit = gatesHit_;
    c.gatesMissed = gatesMissed_;
    int total = 0;
    for (const Section& s : course_.sections)
        if (hasGatesKind(s.kind)) total += s.gateEnd - (s.kind == SectionKind::Route ? s.gateBegin : s.gateStart);
    c.gatesTotal = total;
    c.crashes = crashes_;
    c.bolters = boltersSeen_;
    c.wire = wire_;
    c.touchdownSink = touchdownSink_;
    c.flightSeconds = flightTime_;
    return c;
}

ScoreCard TrainingSession::finalScore() const {
    ScoreCard c = liveScore();
    bool hasLanding = false;
    for (const Section& s : course_.sections)
        if (s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding) hasLanding = true;
    if (hasLanding && c.landing < 0) c.landing = 0;
    if (!course_.engagements.empty() && c.combat < 0) c.combat = 0;
    return finalizeScore(c);
}

// ---------------------------------------------------------------------------------------------
// Combat exercises
// ---------------------------------------------------------------------------------------------
int TrainingSession::engagementAlive(const Battle& battle) const {
    if (engagement_ < 0 || engagement_ >= static_cast<int>(spawned_.size())) return 0;
    const Spawned& sp = spawned_[static_cast<size_t>(engagement_)];
    int n = 0;
    for (int id : sp.jets) {
        const Jet* j = nullptr;
        for (const auto& jp : battle.jets()) if (jp->id == id) j = jp.get();
        if (j && j->alive) ++n;
    }
    for (int gi : sp.ground)
        if (gi >= 0 && gi < static_cast<int>(battle.ground().size()) && battle.ground()[static_cast<size_t>(gi)].alive) ++n;
    return n;
}

void TrainingSession::engagementTargets(std::vector<int>& jets, std::vector<int>& ground) const {
    jets.clear();
    ground.clear();
    if (engagement_ < 0 || engagement_ >= static_cast<int>(spawned_.size())) return;
    jets = spawned_[static_cast<size_t>(engagement_)].jets;
    ground = spawned_[static_cast<size_t>(engagement_)].ground;
}

double TrainingSession::surviveRemaining() const {
    if (section_ >= course_.sections.size()) return 0;
    const Section& s = course_.sections[section_];
    if (s.kind != SectionKind::Engage || s.value2 <= 0) return 0;
    return std::max(0.0, s.value2 - surviveTimer_);
}

void TrainingSession::beginEngagement(const Section& s, const MissionContext& ctx, Battle& battle) {
    using namespace say_;
    const int ei = static_cast<int>(s.value);
    const Engagement& e = course_.engagements[static_cast<size_t>(ei)];
    const FlightState& f = *ctx.flight;
    const Terrain& t = *ctx.terrain;
    engagement_ = ei;
    combatTime_ = 0;
    combatDamage_ = 0;
    surviveTimer_ = 0;
    resupply_ = 0;
    lastHealth_ = battle.player().health;
    Spawned& sp = spawned_[static_cast<size_t>(ei)];
    if (e.continueFrom >= 0) {
        // fight the survivors of the earlier exercise (or fresh ones after a retry)
        Spawned& from = spawned_[static_cast<size_t>(e.continueFrom)];
        sp = from;
        from = Spawned{};
        bool any = false;
        for (int id : sp.jets)
            for (const auto& jp : battle.jets()) if (jp->id == id && jp->alive) any = true;
        if (!any) {
            sp = Spawned{};
            const Engagement& src = course_.engagements[static_cast<size_t>(e.continueFrom)];
            Engagement fresh = src;
            fresh.surviveSeconds = 0;
            // spawn as for a normal engagement below, using the original's aircraft
            Vec3 fw = flat(f.vel);
            if (length(fw) < 0.5) fw = {1, 0, 0};
            Vec3 rt{-fw.z, 0, fw.x};
            for (size_t k = 0; k < fresh.jets.size(); ++k) {
                Vec3 p = f.pos + fw * 6500.0 + rt * ((static_cast<double>(k) - 0.5 * (fresh.jets.size() - 1)) * 900.0);
                p.x = clampd(p.x, -Terrain::kHalfSize + 2500, Terrain::kHalfSize - 2500);
                p.z = clampd(p.z, -Terrain::kHalfSize + 2500, Terrain::kHalfSize - 2500);
                p.y = std::max(f.pos.y, t.surfaceHeight(p.x, p.z) + 900.0);
                Jet& j = battle.debugAddJet(Team::Red, static_cast<AircraftId>(fresh.jets[k]), p, std::atan2(-fw.z, -fw.x), 230);
                j.ai = AiState::Engage;
                j.skill = static_cast<AiSkill>(std::clamp(fresh.skill, 0, 2));
                sp.jets.push_back(j.id);
            }
        }
    } else {
        sp = Spawned{};
        // bandits / drones ahead of the player
        Vec3 fw = flat(f.vel);
        if (length(fw) < 0.5) fw = {1, 0, 0};
        Vec3 rt{-fw.z, 0, fw.x};
        const bool defensive = e.surviveSeconds > 0;
        Vec3 ctr = f.pos + fw * 5200.0;   // drone racetrack centre
        ctr.x = clampd(ctr.x, -Terrain::kHalfSize + 5000, Terrain::kHalfSize - 5000);
        ctr.z = clampd(ctr.z, -Terrain::kHalfSize + 5000, Terrain::kHalfSize - 5000);
        double alt = std::max(e.jetAlt, f.pos.y);
        // launching from an enemy base: the farthest military site within 16 km (the formation
        // climbs out from over it and flies at you); elsewhere 12 km ahead
        Vec3 base = f.pos + fw * 12000.0;
        if (e.fromBase && battle.layout()) {
            double bd = -1;
            for (const MilitarySite& m : battle.layout()->sites) {
                double d = std::hypot(m.center.x - f.pos.x, m.center.z - f.pos.z);
                if (d > bd && d < 16000.0 && d > 6000.0) { bd = d; base = m.center; }
            }
        }
        Vec3 toMe = flat(f.pos - base);
        if (length(toMe) < 1.0) toMe = -fw;
        Vec3 bRt{-toMe.z, 0, toMe.x};
        for (size_t k = 0; k < e.jets.size(); ++k) {
            double side = (static_cast<double>(k) - 0.5 * (e.jets.size() - 1)) * 900.0;
            Vec3 p = defensive ? f.pos - fw * 2600.0 + rt * side : f.pos + fw * 7500.0 + rt * side;
            double hdg = defensive ? std::atan2(fw.z, fw.x) : std::atan2(-fw.z, -fw.x);
            if (e.fromBase) {
                // echelon: the lead in front, wingmen 120 m back and 150 m out on alternate sides
                double back = 120.0 * static_cast<double>((k + 1) / 2), out = 150.0 * static_cast<double>((k + 1) / 2) * ((k % 2) ? 1.0 : -1.0);
                p = base - toMe * back + bRt * out;
                hdg = std::atan2(toMe.z, toMe.x);
            }
            if (e.jetsOnRoute) {
                double a = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(e.jets.size());
                p = ctr + Vec3{std::cos(a), 0, std::sin(a)} * 2600.0;
                hdg = a + kPi / 2;
            }
            p.x = clampd(p.x, -Terrain::kHalfSize + 2500, Terrain::kHalfSize - 2500);
            p.z = clampd(p.z, -Terrain::kHalfSize + 2500, Terrain::kHalfSize - 2500);
            p.y = std::max(alt + (e.jetsOnRoute ? 0.0 : 150.0 * static_cast<double>(k)), t.surfaceHeight(p.x, p.z) + 900.0);
            Jet& j = battle.debugAddJet(Team::Red, static_cast<AircraftId>(e.jets[k]), p, hdg, e.jetsOnRoute ? 175 : 230);
            j.ai = AiState::Engage;
            j.skill = static_cast<AiSkill>(std::clamp(e.skill, 0, 2));
            if (!e.jetsArmed) { j.missiles = 0; j.rounds = 0; j.flareCharges = 0; j.ai = AiState::Patrol; }
            if (defensive) { j.missiles = std::max(j.missiles, 4); j.flareCharges = 0; }
            if (e.fromBase) {
                // in formation towards the player until the merge (updateEngagement turns them in)
                p.y = 0;
                j.fm->resetAirborne({p.x, std::max(alt, t.surfaceHeight(p.x, p.z) + 900.0), p.z}, hdg, 230, 0.8);
                Vec3 off = p - base;
                j.route = {f.pos + off};
                j.route[0].y = std::max(alt, maxSurfaceAround(t, f.pos.x, f.pos.z, 2000.0) + 700.0);
                j.routeLoop = false;
                j.routeSpeed = 235;
                j.ai = AiState::Patrol;
                if (k > 0) j.missiles = std::min(j.missiles, 1);   // wingmen carry one missile each
            }
            if (e.jetsOnRoute) {
                // a racetrack the drone flies for ever (smooth loop, steady speed)
                j.route.clear();
                for (int q = 0; q < 8; ++q) {
                    double a = 2.0 * kPi * q / 8.0;
                    Vec3 w = ctr + Vec3{std::cos(a) * 3200.0, 0, std::sin(a) * 2200.0};
                    w.y = std::max(alt, maxSurfaceAround(t, w.x, w.z, 1500.0) + 700.0);
                    j.route.push_back(w);
                }
                double a0 = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(e.jets.size());
                j.routeIdx = static_cast<size_t>(std::lround(a0 / (2.0 * kPi) * 8.0) + 1) % 8;
                j.routeLoop = true;
                j.formationLead = true;
                j.routeSpeed = 175;
                j.callsign = "DRONE " + std::to_string(k + 1);
            }
            sp.jets.push_back(j.id);
        }
        // helicopters orbiting the area
        Rng rng(0xC0FFEEull + static_cast<unsigned long long>(ei) * 977ull);
        int nh = e.transportHelis + e.attackHelis;
        for (int k = 0; k < nh; ++k) {
            GroundTarget g;
            g.kind = GroundKind::Helicopter;
            g.attack = k < e.attackHelis;
            g.armed = e.helisArmed && g.attack;
            double a = 2.0 * kPi * k / std::max(1, nh) + rng.range(-0.3, 0.3);
            g.pathCenter = {e.area.x + std::cos(a) * e.radius * 0.45, 0, e.area.z + std::sin(a) * e.radius * 0.45};
            g.pathRadius = rng.range(450.0, 800.0);
            g.pathAngle = rng.range(0.0, 2.0 * kPi);
            g.pos = {g.pathCenter.x + std::cos(g.pathAngle) * g.pathRadius, 0, g.pathCenter.z + std::sin(g.pathAngle) * g.pathRadius};
            g.flyAgl = g.attack ? rng.range(60.0, 90.0) : rng.range(90.0, 150.0);
            g.flySpeed = g.attack ? 45.0 : 38.0;
            g.team = Team::Red;
            g.label = std::string(g.attack ? "ATTACK HELO " : "HELO ") + std::to_string(k + 1);
            sp.ground.push_back(battle.addGround(g));
        }
        // own helicopters in the same area: green on the HUD, never lockable (IFF)
        for (int k = 0; k < e.friendlyHelis; ++k) {
            GroundTarget g;
            g.kind = GroundKind::Helicopter;
            g.armed = false;
            double a = 2.0 * kPi * (k + 0.5) / std::max(1, e.friendlyHelis) + 0.9;
            g.pathCenter = {e.area.x + std::cos(a) * e.radius * 0.5, 0, e.area.z + std::sin(a) * e.radius * 0.5};
            g.pathRadius = rng.range(500.0, 800.0);
            g.pathAngle = rng.range(0.0, 2.0 * kPi);
            g.pos = {g.pathCenter.x + std::cos(g.pathAngle) * g.pathRadius, 0, g.pathCenter.z + std::sin(g.pathAngle) * g.pathRadius};
            g.flyAgl = rng.range(100.0, 160.0);
            g.flySpeed = 36.0;
            g.team = Team::Blue;
            g.label = "FRIENDLY " + std::to_string(k + 1);
            sp.friendly.push_back(battle.addGround(g));
        }
        if (e.friendlyHelis > 0) say(Speaker::Instructor, kIff, 1, "iff", 60);
        // ground targets / ships around the area (on suitable ground or water)
        int placed = 0;
        for (size_t k = 0; k < e.ground.size(); ++k) {
            GroundKind kind = static_cast<GroundKind>(e.ground[k]);
            bool naval = groundIsNaval(kind);
            Vec3 p = e.area;
            for (int tries = 0; tries < 40; ++tries) {
                double a = 2.0 * kPi * (static_cast<double>(k) + tries * 0.37) / std::max<size_t>(1, e.ground.size());
                double r = e.radius * (naval ? 0.6 : 0.12) + tries * 6.0;
                Vec3 q{e.area.x + std::cos(a) * r, 0, e.area.z + std::sin(a) * r};
                double h = t.height(q.x, q.z);
                bool ok = naval ? h < -4.0 : (h > 1.5 && t.normal(q.x, q.z, 8.0).y > 0.9);
                if (ok) { p = q; break; }
            }
            GroundTarget g;
            g.kind = kind;
            g.pos = p;
            g.heading = rng.range(0.0, 2.0 * kPi);
            g.armed = e.groundArmed;
            g.team = Team::Red;
            if (naval) { g.pathCenter = e.area; g.pathRadius = std::hypot(p.x - e.area.x, p.z - e.area.z) + 1.0; g.pathAngle = std::atan2(p.z - e.area.z, p.x - e.area.x); }
            g.label = std::string(groundKindName(kind)) + " " + std::to_string(k + 1);
            sp.ground.push_back(battle.addGround(g));
            ++placed;
        }
    }
    // weapons for the exercise: a full load, restricted by the rule
    Jet& p = battle.player();
    p.missiles = playerMissiles_;
    p.agms = playerAgms_;
    p.rounds = playerRounds_;
    p.flareCharges = std::max(playerFlares_, e.surviveSeconds > 0 ? 6 : playerFlares_);
    if (e.rule == WeaponRule::GunsOnly) { p.missiles = 0; p.agms = 0; say(Speaker::Instructor, kGunsOnly, 1, "gunsonly", 60); }
    if (e.rule == WeaponRule::MissilesOnly) { p.rounds = 0; say(Speaker::Instructor, kMissilesOnly, 1, "missilesonly", 60); }
    p.weaponAG = false;
    lastAlive_ = engagementAlive(battle);
}

void TrainingSession::updateEngagement(const Section& s, const MissionContext& ctx, Battle& battle, double dt) {
    using namespace say_;
    if (engagement_ < 0) return;
    const Engagement& e = course_.engagements[static_cast<size_t>(engagement_)];
    const FlightState& f = *ctx.flight;
    combatTime_ += dt;
    Jet& p = battle.player();
    if (p.health < lastHealth_) combatDamage_ += lastHealth_ - p.health;
    lastHealth_ = p.health;
    // the range resupplies the exercise slowly (one missile of each kind and a gun belt every
    // 15 s), so a long fight never ends with empty rails
    resupply_ += dt;
    if (resupply_ >= 15.0) {
        resupply_ = 0;
        if (e.rule != WeaponRule::GunsOnly) {
            p.missiles = std::min(playerMissiles_, p.missiles + 1);
            p.agms = std::min(playerAgms_, p.agms + 1);
        }
        if (e.rule != WeaponRule::MissilesOnly) p.rounds = std::min(playerRounds_, p.rounds + playerRounds_ / 3);
    }
    // a formation from a base flies at the player, then breaks and engages at 9 km (or when shot at)
    if (e.fromBase || course_.engagements[static_cast<size_t>(engagement_)].fromBase) {
        routeRefresh_ -= dt;
        bool merge = false;
        for (int id : spawned_[static_cast<size_t>(engagement_)].jets) {
            Jet* j = battle.jetById(id);
            if (!j || !j->alive || j->route.empty()) continue;
            if (length(j->fm->state().pos - f.pos) < 9000.0 || j->health < 100.0) merge = true;
        }
        for (int id : spawned_[static_cast<size_t>(engagement_)].jets) {
            Jet* j = battle.jetById(id);
            if (!j || !j->alive || j->route.empty()) continue;
            if (merge) { j->route.clear(); j->ai = AiState::Engage; continue; }
            if (routeRefresh_ <= 0) { Vec3 w = f.pos; w.y = std::max(j->route[0].y, f.pos.y); j->route[0] = w; j->routeIdx = 0; }
        }
        if (routeRefresh_ <= 0) routeRefresh_ = 4.0;
        if (merge) say(Speaker::Instructor, kFormationClose, 1, "formclose", 1e9);
    }
    int alive = engagementAlive(battle);
    const Spawned& sp = spawned_[static_cast<size_t>(engagement_)];
    int total = static_cast<int>(sp.jets.size() + sp.ground.size());
    if (lastAlive_ >= 0 && alive < lastAlive_) {
        int n = lastAlive_ - alive;
        kills_ += n;
        for (int k = 0; k < n; ++k) events_.push_back({TrainingEventKind::TargetDestroyed, alive, 0});
        if (alive > 0 && e.surviveSeconds <= 0) {
            sayVariant(Speaker::Instructor, kSplash, 1, "splash", 2);
            if (alive <= 6) say(Speaker::Instructor, kTargetsLeft[alive], 0, "left", 4);
        }
    }
    lastAlive_ = alive;
    // the ground targets are next: remind about the ATS missile once
    bool airLeft = false;
    for (int id : sp.jets) for (const auto& jp : battle.jets()) if (jp->id == id && jp->alive) airLeft = true;
    for (int gi : sp.ground) if (battle.ground()[static_cast<size_t>(gi)].airborne()) airLeft = true;
    if (!airLeft && alive > 0 && e.rule != WeaponRule::GunsOnly && !p.weaponAG && p.agms > 0) say(Speaker::Instructor, kUseAts, 1, "useats", 40);
    // threat calls
    for (const Missile& m : battle.missiles())
        if (m.active && m.targetJet == p.id && length(m.pos - f.pos) < 4500.0) { say(Speaker::Instructor, kMissileInbound, 2, "inbound", 6); break; }
    for (const auto& jp : battle.jets()) {
        if (!jp->alive || jp->team == p.team) continue;
        const FlightState& js = jp->fm->state();
        Vec3 rel = f.pos - js.pos;
        double d = length(rel);
        if (d < 1200.0 && d > 1.0 && dot(js.q.forward(), rel * (1.0 / d)) > 0.94 && dot(f.q.forward(), rel * (1.0 / d)) > 0.5) {
            say(Speaker::Instructor, kBanditSix, 2, "six", 10);
            break;
        }
    }
    if (f.agl < 250.0 && !sp.jets.empty() && airLeft) say(Speaker::Instructor, kTooLowCombat, 1, "lowcombat", 20);
    if (p.missiles == 0 && e.rule == WeaponRule::Any && alive > 0 && airLeft) say(Speaker::Instructor, kOutOfMissiles, 1, "nomissiles", 60);
    // helicopters: warn about overshooting
    for (int gi : sp.ground) {
        const GroundTarget& g = battle.ground()[static_cast<size_t>(gi)];
        if (!g.airborne()) continue;
        if (length(g.pos - f.pos) < 900.0 && f.airspeed > 230.0) { say(Speaker::Instructor, kHeliSlow, 1, "helislow", 30); break; }
    }
    if (e.surviveSeconds > 0) {
        surviveTimer_ += dt;
        taskProgress_ = clampd(surviveTimer_ / e.surviveSeconds, 0.0, 1.0);
        char b[64];
        std::snprintf(b, sizeof b, "SURVIVE %.0f S", std::max(0.0, e.surviveSeconds - surviveTimer_));
        taskStatus_ = b;
        if (surviveTimer_ > 25.0) say(Speaker::Instructor, kKeepSurviving, 0, "keepmoving", 30);
        if (surviveTimer_ >= e.surviveSeconds && !sectionComplete_) {
            sectionComplete_ = true;
            combatScores_.push_back(clampd(100.0 - combatDamage_ * 0.6, 30.0, 100.0));
            say(Speaker::Instructor, kSurvived, 2, "survived", 30);
        }
        return;
    }
    taskProgress_ = total > 0 ? clampd(1.0 - static_cast<double>(alive) / total, 0.0, 1.0) : 1.0;
    char b[64];
    std::snprintf(b, sizeof b, "TARGETS %d / %d", total - alive, total);
    taskStatus_ = b;
    if (alive == 0 && !sectionComplete_) {
        sectionComplete_ = true;
        double slow = std::max(0.0, combatTime_ - s.estSeconds);
        combatScores_.push_back(clampd(100.0 - combatDamage_ * 0.5 - slow * 0.15, 30.0, 100.0));
        say(Speaker::Instructor, kAllDestroyed, 2, "alldestroyed", 10);
    }
}

void TrainingSession::endEngagement(Battle& battle, bool removeSurvivors) {
    if (engagement_ >= 0 && engagement_ < static_cast<int>(spawned_.size())) {
        Spawned& sp = spawned_[static_cast<size_t>(engagement_)];
        if (removeSurvivors) {
            for (int id : sp.jets) battle.removeJet(id);
            for (int gi : sp.ground) battle.retireGround(gi);
            for (int gi : sp.friendly) battle.retireGround(gi);
            sp = Spawned{};
        }
    }
    engagement_ = -1;
    // a full, unrestricted load for whatever comes next
    Jet& p = battle.player();
    p.missiles = playerMissiles_;
    p.agms = playerAgms_;
    p.rounds = playerRounds_;
    p.flareCharges = playerFlares_;
}

void TrainingSession::retry(Battle& battle) {
    if (!checkpointValid_) return;
    // an interrupted engagement starts again from scratch (its survivors leave)
    if (engagement_ >= 0) endEngagement(battle, true);
    if (cp_.section < course_.sections.size() && course_.sections[cp_.section].kind == SectionKind::Engage) {
        int ei = static_cast<int>(course_.sections[cp_.section].value);
        int from = course_.engagements[static_cast<size_t>(ei)].continueFrom;
        if (from >= 0) {   // the reversal is flown again against fresh attackers
            engagement_ = from;
            endEngagement(battle, true);
        }
    }
    combatScores_.resize(std::min(combatScores_.size(), cp_.combatScores));
    kills_ = cp_.kills;
    Jet& p = battle.player();
    FlightModel& fm = *p.fm;
    if (cp_.onGround) {
        fm.resetOnRunway(cp_.engineRunning);
        FlightState& st = fm.mutableState();
        st.pos = cp_.pos;
        st.q = cp_.q;
    } else {
        fm.resetAirborne(cp_.pos, toEuler(cp_.q).heading, std::max(length(cp_.vel), 120.0), cp_.throttle);
        FlightState& st = fm.mutableState();
        st.q = cp_.q;
        st.vel = cp_.vel;
        st.gearDown = cp_.gearDown;
        st.gearPos = cp_.gearDown ? 1.0 : 0.0;
        st.hookDown = cp_.gearDown && fm.state().hookDown;
    }
    fm.mutableState().fuelKg = std::max(cp_.fuel, fm.profile().fuelMassKg * 0.5);
    p.alive = true;
    p.health = 100.0;
    p.deadTimer = 0;
    gateScores_.resize(std::min(gateScores_.size(), cp_.gateScores));
    manoeuvreScores_.resize(std::min(manoeuvreScores_.size(), cp_.manoeuvreScores));
    gatesHit_ = cp_.hit;
    gatesMissed_ = cp_.missed;
    section_ = static_cast<size_t>(-1);   // the section restarts (and re-saves this checkpoint)
    radio_.clear();
    say(Speaker::Instructor, say_::kRetry, 2, "retry", 2);
}

bool TrainingSession::sectionDone(size_t idx, const MissionContext& c) const {
    if (idx != section_ || !c.flight) return false;
    const FlightState& f = *c.flight;
    const Section& s = course_.sections[idx];
    switch (s.kind) {
        case SectionKind::ColdStart: return f.engine == EngineState::Running;
        case SectionKind::Lineup:
            if (course_.carrierStart) return f.catFiring || f.launched || (!f.onGround && !f.catArmed);
            return !f.parkingBrake && f.throttle >= 0.8 && f.engine == EngineState::Running;
        case SectionKind::Takeoff: return !f.onGround && f.agl > 30.0;
        case SectionKind::GearUp: return !f.gearDown && f.gearPos < 0.1;
        case SectionKind::Climb: return f.pos.y >= s.value - 60.0 && f.agl >= s.value2;
        case SectionKind::Route: return gate_ >= s.gateEnd;
        default: return sectionComplete_;
    }
}

double TrainingSession::sectionProgress(size_t idx, const MissionContext& c) const {
    if (idx != section_ || !c.flight) return 0;
    const FlightState& f = *c.flight;
    const Section& s = course_.sections[idx];
    switch (s.kind) {
        case SectionKind::ColdStart: return f.engine == EngineState::Running ? 1.0 : clampd(f.rpm / 0.62, 0, 1);
        case SectionKind::Lineup: return clampd(f.throttle / 0.8, 0, 1) * (f.parkingBrake ? 0.5 : 1.0);
        case SectionKind::Takeoff: return clampd(f.airspeed / c.aircraft->rotateSpeedMs, 0, 1);
        case SectionKind::GearUp: return 1.0 - f.gearPos;
        case SectionKind::Climb: return clampd((f.pos.y - taskStartAlt_) / std::max(1.0, s.value - 60.0 - taskStartAlt_), 0, 1);
        case SectionKind::Route:
        case SectionKind::Landing:
        case SectionKind::CarrierLanding: {
            int b = s.kind == SectionKind::Route ? s.gateBegin : s.gateStart;
            int n = s.gateEnd - b;
            if (n <= 0) return 1.0;
            return clampd(static_cast<double>(gate_ - b) / n, 0, 1);
        }
        default: return taskProgress_;
    }
}

Mission TrainingSession::buildMission() {
    std::vector<MissionStep> steps;
    for (size_t i = 0; i < course_.sections.size(); ++i) {
        const Section& s = course_.sections[i];
        MissionStep st;
        st.title = s.title;
        st.instruction = s.instruction;
        st.keyHint = s.hint;
        st.done = [this, i](const MissionContext& c) { return sectionDone(i, c); };
        st.progress = [this, i](const MissionContext& c) { return sectionProgress(i, c); };
        st.holdSeconds = s.kind == SectionKind::Climb ? 0.5 : 0.0;
        steps.push_back(std::move(st));
    }
    char id[32];
    std::snprintf(id, sizeof id, "training_%02d", course_.level);
    return Mission(id, "TRAINING " + std::to_string(course_.level) + ": " + trainingLevel(course_.level).title, std::move(steps));
}

// ---------------------------------------------------------------------------------------------
// Instructor
// ---------------------------------------------------------------------------------------------
void TrainingSession::instructor(const MissionContext& ctx, const Battle& battle, double dt) {
    using namespace say_;
    const FlightState& f = *ctx.flight;
    const Section& s = course_.sections[section_];
    const Speaker I = Speaker::Instructor;
    const AircraftProfile& prof = *ctx.aircraft;
    bool landingSec = s.kind == SectionKind::Landing || s.kind == SectionKind::CarrierLanding;

    // ---- safety warnings (urgent) ----
    if (!f.onGround && ctx.terrain) {
        Vec3 ahead = f.pos + f.vel * 3.0;
        double groundAhead = ctx.terrain->surfaceHeight(ahead.x, ahead.z);
        bool approachOk = landingSec && f.gearDown && f.vel.y > -9.0;
        if (!approachOk && f.timeAirborne > 4.0 && (ahead.y < groundAhead + 15.0 || (f.agl < 40.0 && f.vel.y < -6.0)))
            say(I, kPullUp, 2, "pullup", 5);
        if (ctx.terrain->obstacleDistance(f.pos + f.vel * 1.5, 90.0) < 12.0) say(I, kObstacle, 2, "obstacle", 4);
        if (f.stalled && f.agl > 50.0) say(I, kStall, 2, "stall", 7);
        if (f.gLoad > prof.gLimit + 0.3) say(I, kOverG, 1, "overg", 12);
        if (f.fuelKg < prof.fuelMassKg * 0.18) say(I, kLowFuel, 1, "lowfuel", 90);
    }

    switch (s.kind) {
        case SectionKind::Takeoff:
            if (!rotateCalled_ && f.onGround && f.airspeed >= prof.rotateSpeedMs - 2.0) { rotateCalled_ = true; say(I, kRotate, 2, "rotate", 10); }
            if (!airborneCalled_ && !f.onGround && f.agl > 15.0) { airborneCalled_ = true; say(I, kAirborne, 1, "airborne", 10); }
            break;
        case SectionKind::LevelTurn:
            if (taskTimer_ > 4.0 && !sectionComplete_) {
                double bank = std::fabs(toEuler(f.q).bank / kDeg);
                if (bank < 45.0) say(I, kMoreBank, 1, "bank", 7);
                else if (bank > 75.0) say(I, kLessBank, 1, "bank", 7);
                else if (std::fabs(f.pos.y - taskStartAlt_) > 90.0) say(I, kHoldHeight, 1, "height", 9);
            }
            break;
        case SectionKind::ClimbTurn:
            if (taskTimer_ > 6.0 && !sectionComplete_ && f.vel.y < 2.0) say(I, kKeepClimbing, 1, "climb", 9);
            break;
        case SectionKind::DescendTurn:
            if (taskTimer_ > 6.0 && !sectionComplete_) {
                if (f.airspeed > 310.0) say(I, kTooFast, 1, "fast", 8);
                else if (f.vel.y > -2.0) say(I, kKeepDescending, 1, "desc", 9);
            }
            break;
        case SectionKind::HighG:
            if (!sectionComplete_ && taskTimer_ > 3.0) {
                if (f.gLoad < s.value) say(I, kPullHarder, 1, "pullg", 5);
                else if (!pullCalled_) { pullCalled_ = true; say(I, kHoldG, 1, "holdg", 10); }
            }
            break;
        case SectionKind::Loop:
            if (!sectionComplete_ && !pullCalled_ && taskAcc_ > kPi * 0.6) { pullCalled_ = true; say(I, kKeepPulling, 1, "keeppull", 20); }
            break;
        case SectionKind::Formation: {
            if (sectionComplete_) break;
            const Jet* lead = nullptr;
            for (const auto& j : battle.jets())
                if (j->id == leadId_) lead = j.get();
            if (!lead) break;
            const FlightState& l = lead->fm->state();
            if (length(f.pos - l.pos) < 25.0) { say(Speaker::Wingman, kTooClose, 2, "tooclose", 5); break; }
            if (inSlot_) {
                if (!inSlotCalled_) { inSlotCalled_ = true; say(Speaker::Wingman, kInSlot, 1, "inslot", 20); }
                break;
            }
            Vec3 fw = flat(l.vel), rt{-fw.z, 0, fw.x};
            Vec3 rel = f.pos - slotPosition(battle);
            double along = dot(rel, fw), side = dot(rel, rt), vert = rel.y;
            if (std::fabs(along) > 300.0) {
                say(Speaker::Wingman, along < 0 ? kAddPower : kReducePower, 1, "form", 8);
            } else if (std::fabs(side) >= std::fabs(vert) && std::fabs(side) > 15.0) {
                say(Speaker::Wingman, side < 0 ? kMoveRight : kMoveLeft, 1, "form", 6);
            } else if (std::fabs(vert) > 12.0) {
                say(Speaker::Wingman, vert < 0 ? kComeUp : kGoDown, 1, "form", 6);
            } else if (std::fabs(along) > 25.0) {
                say(Speaker::Wingman, along < 0 ? kAddPower : kReducePower, 1, "form", 6);
            }
            break;
        }
        case SectionKind::Rendezvous: {
            double d = length(f.pos - battle.tanker().pos);
            if (d < 3500.0) say(I, kTankerInSight, 1, "tankersight", 1e9);
            break;
        }
        case SectionKind::Refuel: {
            const Tanker& tk = battle.tanker();
            if (tk.contact || sectionComplete_) break;
            Vec3 probe = f.pos + f.q.forward() * 8.0 + f.q.up() * 1.0;
            Vec3 rel = probe - tk.basket();
            if (length(rel) > 400.0) break;
            Vec3 fw{std::cos(tk.heading), 0, std::sin(tk.heading)}, rt{-fw.z, 0, fw.x};
            double closure = dot(f.vel - tk.vel, fw);
            double along = dot(rel, fw), side = dot(rel, rt), vert = rel.y;
            if (along < -60.0 && closure < 3.0) say(I, kTankerAddPower, 1, "tk", 5);
            else if (closure > (along < -60.0 ? 15.0 : 4.0) || along > 2.0) say(I, kTankerReducePower, 1, "tk", 5);
            else if (std::fabs(vert) > 2.5 && std::fabs(vert) >= std::fabs(side)) say(I, vert < 0 ? kTankerUp : kTankerDown, 1, "tk", 4);
            else if (std::fabs(side) > 2.5) say(I, side < 0 ? kTankerRight : kTankerLeft, 1, "tk", 4);
            else if (closure < 0.8) say(I, kTankerAddPower, 1, "tk", 5);
            break;
        }
        default: break;
    }

    // ---- route sections: cues, gate windows, speed band, ceiling, off-route ----
    if (s.kind == SectionKind::Route && sectionHasGates()) {
        const Gate& g = course_.gates[gate_];
        double dist = length(f.pos - g.pos);
        double lead = std::max(1800.0, f.airspeed * 9.0);
        if (g.cue != GateCue::None && announcedCueGate_ != gate_ && dist < lead) {
            announcedCueGate_ = gate_;
            bool repeat = gate_ > s.gateBegin && course_.gates[gate_ - 1].cue == g.cue &&
                          (g.cue == GateCue::Pillars || g.cue == GateCue::Masts || g.cue == GateCue::Arch);
            if (g.cue == GateCue::TurnLeft || g.cue == GateCue::TurnRight) {
                if (gate_ + 1 < s.gateEnd) {
                    Vec3 d = course_.gates[gate_ + 1].pos - g.pos;
                    say(I, headingCall(g.cue == GateCue::TurnRight, compassTens(d)), 1, "cue", 0);
                }
            } else if (!repeat) {
                say(I, cueLine(g.cue), 1, "cue", 0);
            }
        }
        if (g.maxAgl > 0 && dist < 1600.0 && dist > 250.0) {
            if (f.pos.y < g.ground + g.minAgl - 10.0) say(I, kBelowGate, 1, "window", 8);
            else if (f.pos.y > g.ground + g.maxAgl + 10.0) say(I, kAboveGate, 1, "window", 8);
        }
        if (s.speedMax > 0) {
            fastTimer_ = f.airspeed > s.speedMax * 1.08 ? fastTimer_ + dt : 0;
            slowTimer_ = f.airspeed < s.speedMin * 0.92 ? slowTimer_ + dt : 0;
            if (fastTimer_ > 4.0) say(I, kTooFast, 1, "speed", 14);
            if (slowTimer_ > 4.0) say(I, kTooSlow, 1, "speed", 14);
        }
        if (s.ceilingAgl > 0) {
            ceilingTimer_ = f.agl > s.ceilingAgl ? ceilingTimer_ + dt : 0;
            if (ceilingTimer_ > 3.0) say(I, kCeiling, 1, "ceiling", 10);
        }
        double hd = std::hypot(f.pos.x - g.pos.x, f.pos.z - g.pos.z);
        Vec3 to = flat(g.pos - f.pos);
        double off = std::acos(clampd(dot(flat(f.vel), to), -1.0, 1.0));
        if (hd > 1200.0 && (hd > prevDist_ + 0.05 || off > 75.0 * kDeg)) offCourseTimer_ += dt;
        else offCourseTimer_ = std::max(0.0, offCourseTimer_ - dt * 2.0);
        prevDist_ = hd;
        if (offCourseTimer_ > 6.0) {
            say(I, steerCall(compassTens(g.pos - f.pos)), 1, "offroute", 15);
            offCourseTimer_ = 0;
        }
    }

    // ---- approach guidance ----
    if (landingSec && onApproach()) {
        double d = distanceToAim(f);
        if (d > 0 && d < 9000.0) {
            bool carrier = s.kind == SectionKind::CarrierLanding;
            if (!briefedApproach_ && d < 8000.0) {
                briefedApproach_ = true;
                say(I, approachCall(prof), 1, "appspeed", 40);
            }
            double dev = glideDeviation(f), lat = lineupOffset(f);
            double tol = std::max(carrier ? 4.0 : 6.0, d * 0.012);
            double latTol = std::max(carrier ? 4.0 : 8.0, d * 0.02);
            double vref = prof.approachSpeedMs;
            if (!f.gearDown && d < 6500.0) say(I, carrier ? kHookDown : kGearDown, 2, "gear", 6);
            else if (carrier) {
                const Speaker L = Speaker::Tower;
                if (!paddlesCalled_ && d < 2600.0) { paddlesCalled_ = true; say(L, kPaddles, 1, "paddles", 5); }
                else if (dev < -tol) say(L, kLsoLow, d < 1000 ? 2 : 1, "lso", 3.5);
                else if (f.airspeed < vref - 10.0) say(L, kLsoSlow, 1, "lso", 4);
                else if (lat > latTol) say(L, kLsoLeft, 1, "lso", 3.5);
                else if (lat < -latTol) say(L, kLsoRight, 1, "lso", 3.5);
                else if (dev > tol) say(L, kLsoHigh, 1, "lso", 3.5);
                else if (f.airspeed > vref + 15.0) say(L, kLsoFast, 1, "lso", 4);
                else if (d < 2200.0) say(L, kLsoGood, 0, "lsogood", 9);
            } else {
                if (f.airspeed < vref - 12.0) say(I, kSlowApproach, 2, "app", 5);
                else if (dev < -tol) say(I, kLow, 1, "app", 5);
                else if (lat > latTol) say(I, kLineLeft, 1, "app", 5);
                else if (lat < -latTol) say(I, kLineRight, 1, "app", 5);
                else if (dev > tol) say(I, kHigh, 1, "app", 5);
                else if (f.airspeed > vref + 18.0) say(I, kFastApproach, 1, "app", 6);
                else say(I, kOnGlide, 0, "onglide", 12);
            }
        }
    }
}

}  // namespace sky
