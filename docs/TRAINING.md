# Training Academy and the Campaign on Every Location

The **Training Academy** has twenty courses with a live instructor: flying first, then
helicopters and weapons, one-on-one air combat against the F-16, Su-35, Su-57 and F-35, mixed
missions and a carrier finale. The **61-mission campaign** flies over any world location (with
your choice of aircraft and paint scheme); in the Skyfall Desert it is exactly the original.

## Where to find them

* Title → **TRAINING ACADEMY**: all twenty levels in two columns (all open, a green dot marks a
  completed level), location, aircraft, handling mode, optional corridor markers, level details,
  a route preview drawn on the map of the loaded location, your record for the chosen aircraft
  (completed or not, best score, attempts, best time) and which aircraft have completed the level,
  then **LAUNCH**.
* Title → **CAMPAIGN**: the 61-mission grid of earlier releases, with **LOCATION**, **AIRCRAFT**
  and **PAINT** pickers below it (arrow keys, Enter, or click the left / right half). Completed
  missions are ticked per location.

## The twenty levels

| # | Level | Recommended location | Content |
|---|---|---|---|
| 1 | Carrier Scramble & Safe Takeoff | Skyfall Desert | catapult launch from the carrier (a runway cold start on maps without sea), climb-out, navigation circuit, runway landing |
| 2 | Aircraft Handling & Waypoint Rings | Mount Fuji | level / climbing / descending turns, high G, loop, roll, recovery, then ring gates |
| 3 | Mountain Navigation & Terrain Avoidance | Matterhorn | valley navigation with speed and altitude discipline |
| 4 | Low-Altitude Tactical Flight | Guilin | low-level valleys under a ceiling, pillar slalom, arches |
| 5 | Helicopter Identification & Interception | Geirangerfjord | four helicopters in the valleys: two hostile (red), two of ours (green IFF, never lockable) |
| 6 | Helicopter Attack at a Military Outpost | Wadi Rum | two armed attack helicopters over a forward outpost, then its armour (ATS missiles) |
| 7 | Cannon Targeting & Precision Engagement | Erg Chebbi | guns only: three target drones on a racetrack |
| 8 | Missile Locking & Air-to-Air Combat | Rio de Janeiro | missiles only on four drones, then a fight with an armed bandit |
| 9 | F-16 Interception | Grand Canyon | one-on-one against an armed F-16 (veteran) |
| 10 | Su-35 Dogfight Training | Aconcagua | one-on-one against an armed Su-35 (veteran) |
| 11 | Advanced Su-57 Interception | Torres del Paine | one-on-one against an Su-57 flown by an ace |
| 12 | F-35 Tactical Engagement | Glen Coe | one-on-one against an F-35 flown by an ace |
| 13 | Multiple Helicopter Threats | Musandam (at dusk) | six hostile helicopters, two of them armed, with one of ours among them |
| 14 | Combined Air & Ground Attack | Mount Fuji | two fighters launch in formation from their base to cover an outpost, then its armour and anti-aircraft guns |
| 15 | High-Speed Mountain Combat | Grand Canyon | high-speed canyon run, then two bandits |
| 16 | Enemy Formation Interception | Geirangerfjord | terrain following, then a three-ship formation launching from its base (it breaks and turns in at 9 km) |
| 17 | Advanced Defensive Flying | Rio de Janeiro (at dusk) | survive 90 s with two attackers behind you (flares, breaks), then shoot them down |
| 18 | Multi-Target Combat & Refuelling | Torres del Paine | low-level route, strike on an outpost with a fighter, attack helicopters and ground targets, then the tanker |
| 19 | Carrier Strike Mission & Recovery | Rio de Janeiro | catapult launch, sink three armed patrol boats near the fleet, then a carrier trap |
| 20 | Final Advanced Combat Qualification | Musandam | valleys, pillars and arches, bandits launching from their base, the tanker, a carrier trap |

Every level starts with a radio briefing (the captain), then aircraft- and mode-specific advice
from the instructor; it ends with a debrief card (category scores including COMBAT, total score,
kills, a new-best flag) and a spoken remark on the score. There are **no medals, rewards or
locks**. Any level can be flown on any downloaded location except: levels 19 and 20 need open sea
(Skyfall Desert, Musandam, Rio de Janeiro), and **Everest is not offered in the Academy**.

### How the combat exercises work

* A combat exercise is a COMBAT section (`SectionKind::Engage`, `Engagement` in `Training.hpp`).
  Bandits are spawned 7.5 km ahead of you when the section begins (in the defensive exercise,
  2.6 km behind you), at a safe height above the terrain; target drones fly a fixed racetrack.
  Helicopters fly circles 60-150 m above the ground around the target area (they climb ahead of
  rising ground), attack helicopters fire their cannon inside 1.5 km; ground targets stand at
  the area (one of the map's forward operating bases, see the world detail), ships sail circles.
* Bandits "from a base" (levels 14, 16, 20) take off in echelon from the farthest military site
  within 16 km and fly at you in formation; they break and engage at 9 km (or as soon as one is
  hit). Own helicopters (levels 5 and 13) fly among the hostiles: the IFF marks them green on the
  HUD and minimap and no weapon locks or hits them, so the job is to tell them apart.
* Levels 1 and 19 start on the carrier's catapult (afterburner, brake off, the shot) on maps with
  open sea; level 1 falls back to a runway cold start elsewhere.
* Helicopters are new flying targets (`GroundKind::Helicopter`): the air-to-air missile locks them
  like an aircraft (the air-to-surface missile and the gun work too); shot down, they fall and
  spin to the ground.
* The exercise ends when every target is destroyed (or, defensively, when the time is survived).
  Weapons are reloaded to a full load at the start; gunnery is guns only, missile employment is
  missiles only; during a long fight the range slowly resupplies one missile of each kind and a
  third of a gun belt every 15 s.
* Shot down: the replay, then **R** retries the exercise from its checkpoint with fresh enemies.
* After a fight the route home is rejoined at the gate that makes the shortest way back (at least
  3 km away, with room to descend). Anywhere on a route, circling near a gate for 45 s without
  getting through it moves the route on (counted as a miss), so nobody is trapped at one gate.
* The instructor coaches the fight: splash calls and targets left, missile and bandit-on-your-six
  warnings, too low, out of missiles, overshooting a helicopter, the switch to the ATS missile.

## How a course is built (`src/mission/Training.cpp`)

Courses are generated from the location's terrain, deterministically (same location + level =
same course):

* **Routes** follow the valleys (A* over a cost grid, Chaikin smoothing, a minimum turn radius
  per route style). A leg starts with a constant-radius (Dubins) turn onto the path; several join
  points and radii are compared and the one with the least extra distance is used, so legs do not
  begin with loops.
* **Gate heights** follow the terrain with climb (9°) and dive (8°) limits; every corner is checked
  against ~4 g at the slow end of the section's speed band and eased if needed.
* **Obstacles** are real geometry with collision (pillars, natural arches, a deck bridge, radio
  masts, a military compound, the railway viaduct). Gates that pass near an obstacle are first
  moved sideways, then lifted over it. The approach to an obstacle run arrives low.
* **Formation** (level 9): the lead orbits over the lowest ground in reach, appears on its loop
  and holds its speed closely; you join right echelon and hold it for 60 s.
* **Tanker** (levels 8-10): the orbit is placed over the lowest ground (a tighter orbit on a high
  map); fuel is never reduced artificially, the refuel is guided (UP / DOWN / LEFT / RIGHT / ADD
  POWER) and needs 25 s in contact.
* **Landing**: the runway approach is chosen from straight-in, circling (base leg + 90° turn) and
  steep straight-in, with a two-segment glide path that clears the terrain; a go-around rejoins
  the inbound route. The runway site on real maps is chosen for flat ground **and** a usable
  approach. Carrier approaches fly the ball (meatball and line-up cues), with four wires 12 m apart.
* **Duration**: about 17 minutes by estimate (every course is between 15.5 and 20 minutes;
  shorter variants are used only when the full course would run long).

## Instructor, voice and HUD

The instructor talks continuously and in context: section intros, upcoming turns and headings,
passes and canyons, speed / height / ceiling corrections, terrain and obstacle warnings, gate hits
and misses, manoeuvre coaching, formation and refuelling cues, the approach (glide path, line-up,
speed), touchdown or trap, combat calls and the closing remark on the score. The tower, your lead and the tanker have their own calls.
Every line is fixed text from a finite vocabulary, so all of it is in the neural voice pack
(`assets/voice`, Kokoro-82M); captions are always shown, and the system voice reads any line that
has no recording. Urgent calls interrupt; chatter only plays into silence.

In flight: the next gates are rings in the world (current gate bright, the next ones dimmer;
obstacle gates blue, approach rings amber), HUD markers with distance and gate number, the gate
height window and speed band, optional corridor beads (Academy menu toggle), the section list with
progress, and approach guidance.

## Scoring, retries, progress (`src/mission/Progress.cpp`)

Categories (0-100, only those the level uses): waypoint accuracy, manoeuvring, altitude / speed
discipline, terrain clearance, stability / control, refuelling, landing and combat (damage taken
and time over the estimate). There are no medals and no locks: all twenty levels are open. A crash
shows the replay and then **R retries from the last checkpoint** (the start of the section, with
the aircraft state saved there). History is kept per level and per aircraft: completed (yes / no),
best score, attempts and best time. Records of the earlier ten-level academy are carried over to
the level that grew out of each one (old 1 → 1, 5 → 2, 2 → 3, 3 and 4 → 4, 7 → 15, 6 → 16,
8 and 9 → 18, 10 → 20); the old keys stay in the settings file untouched, so older builds still
read them.

## The campaign on every location (`src/mission/CampaignWorld.cpp`)

The 61 missions are written for the Skyfall Desert (sea to the east, airfield in the west). All
maps are 32 x 32 km, so a mission keeps its layout and is fitted to the location:

* **Orientation** — the whole mission is mirrored or rotated about the map centre (one of the 8
  symmetries of the square), whichever puts most ships and rigs on water, ground targets on
  reachable ground and waypoints over lower terrain.
* **Targets** — each target group moves as a whole by the smallest offset that puts its members on
  suitable ground: dry, gentle, off the runway, no more than 1800 m above the airfield, and with no
  wall towering over it within 1.2 km (room for an attack run). Ships and their sailing circles go
  on water (smaller circles in narrow water). Bridge strikes use the longest reachable bridge
  (rail first), not one at the bottom of a deep canyon.
* **Heights** — enemy waves, the start and the tanker keep the clearance they had above the desert
  floor, measured from the local terrain (never below 450 m above the highest ground nearby). The
  tanker orbit goes over the lowest ground in reach of where it was.
* **Carrier** — on a map with real sea (Skyfall Desert, Musandam, Rio de Janeiro) the carrier is
  placed on open water with an approach and climb-out over water. Elsewhere the carrier missions
  start in the air over the airfield (the catapult, gear and climb objectives are dropped) and
  end with a low pass over the runway.
* **No sea / no railway** — boats become armoured vehicles and oil rigs fuel tanks; trains become
  truck convoys near a town; factories are built where the map has no industrial zone. Objective
  titles change with them ("DESTROY 3 PATROL VEHICLES"); every variant is in the voice pack.
* **Landings** — the approach gate is on the side of the runway with the clearer glide path.
  Mission 2's heights are raised on a high airfield ("CLIMB 750 METRES"), and mission 3 lands on
  runway 27 where the terrain blocks the approach to runway 09 (Matterhorn, Musandam, Aconcagua).

## Tests (`tests/training_tests.cpp`, `skyfall_training_tests`)

* `courses_are_valid_on_every_location` — all 20 levels × 14 locations (levels 19 and 20 on the
  three sea locations): builds, 15.5-20 min, on the map, ≥ 30 m above the ground, ≤ 4.5 g corners,
  glide limits, no gate cut by an obstacle, tanker / formation / carrier present where expected,
  every combat level has its engagements with targets and an area on the map.
* `courses_are_deterministic`.
* `bot_flies_every_level_with_every_aircraft_in_every_mode` — an autopilot bot flies the real
  flight model (no teleports) through all 20 levels × 8 aircraft × 3 handling modes on the
  recommended locations: takeoff, gates, manoeuvres, formation, combat (it hunts every target with
  the permitted weapons against the real AI, retrying from the checkpoint when shot down, as a
  player would with R), refuelling, runway landing or carrier trap. It must destroy every target
  and land (or trap) without running the tanks dry. `SKY_LEVELS=a-b` runs a range of levels.
* `every_training_radio_call_is_in_the_voice_vocabulary` — every call heard in 40 real flights is
  in the voice pack's line list.
* `academy_progress_history_without_medals_or_locks` — all levels open, completion, best score,
  per-aircraft history, the carry-over of the ten-level records, no medal words in the remarks.
* `world_detail_is_populated_on_every_location` — the army compound, forward bases, traffic and
  lights on every map; nothing on water or the runway; no tall prop in the path between any two
  consecutive Academy gates.
* `campaign_is_laid_out_on_every_location` — all 61 missions on all 14 locations (812 mission
  layouts): targets on suitable ground / water, waves, start and tanker clear of the terrain and
  on the map, the carrier on open water, the landing on the runway, every objective group
  populated, every title and briefing in the voice pack, the objective logic completes; missions
  2 and 3 start clear of the terrain. The desert layouts are checked to be the originals.
* `campaign_is_winnable_over_other_locations` — the combat bot that proves the hard desert
  missions flies missions 4-61 against the full AI over other locations and is compared with
  itself in the desert (desert, Matterhorn, Musandam, Aconcagua by default; `SKY_CAMP_ALL=1` for
  all 14, `SKY_CAMP_LOC=<id>` for one).
* Debug tools: `course_report`, `course_dump` (`LOC=`, `LV=`, `ALL=1`), `bot_flies_one_course`
  (`LOC LV AC MODE`, `TRACE=<section kind>`, `SECT=1`, `RADIO=1`), `bot_sweep_over_every_location`
  (`SKY_SWEEP=<aircraft>,<mode>`).
* `skyfall --capture <dir>` with `SKYFALL_SET=flow` drives the real game loop through the Academy,
  a crash and checkpoint retry, completion and progress, the campaign over Guilin, Musandam
  (carrier) and the Matterhorn (airborne start), and location switches;
  `SKYFALL_SET=academy` captures the new screens.

## Known limits

* Last full run: the bot finishes 465 of the 480 level / aircraft / mode combinations on the
  recommended locations. In 11 it loses the fight (shot down nine times in a row by the AI in
  levels 12-17 and 20; the test reports these and fails if they exceed 5 %), and 4 are listed
  bot limitations (its gate autopilot sinks a heavy jet into the terrain on the way home in
  levels 9, 10 and 18, or it chases a bandit into the sea in level 17). These are limits of the
  test bot's flying, not of the courses: the same course is finished by the other aircraft and
  modes, and which fights it loses changes from run to run. On other locations the bot completes most but not all combinations (it is a bot, not a
  proof that a human cannot). Sweep over the 13 Academy locations (129 courses each): F-16 arcade
  127, F-16 assisted 125, F-16 simulation 122, Su-30 assisted 124, F-35 simulation 120; most
  failures are on Everest-like high terrain (Everest itself is in the sweep but not the Academy).
* Everest is not offered in the Academy (5-8 km terrain: the low-level obstacle work exceeds
  several jets' performance in that air). The campaign does fly there.
* Levels 19 and 20 need open sea: only Skyfall Desert, Musandam and Rio de Janeiro.
* The AI pilots are the campaign's: an ace is genuinely hard. A fast, clean bot flight finishes a
  combat level in 10-15 minutes; the 15-20 minute estimates assume a human-paced fight.
* The estimate is 15.5-20 min; a fast, clean flight (like the bot's, especially in arcade mode)
  takes less, about 12-17 minutes.
* Campaign over other locations: the combat bot (assisted, F-22, up to 3 attempts) wins 48 of
  missions 4-61 in the desert itself (it cannot fly mission 5's aerobatics, the low-level route of
  mission 6, and loses some of the big dogfights of missions 27-40). Over the 12 other real
  locations it wins 46-53 of 58 (Everest 41); the missions it wins in the desert but loses
  elsewhere are at most 4 per location (mostly dogfights decided by luck) except Everest, where
  it loses 8 more (ground attacks between 8 km peaks). Over all 14 maps: 689 of 812. Without sea, the story text of the briefings still speaks of boats
  and the sea where the mission had them; only the objectives change.
