# Training Academy and the Campaign on Every Location

Two additions to SKYFALL v0.8: a **Training Academy** of ten graded courses with a live
instructor, and the **61-mission campaign over any world location** (with your choice of aircraft
and paint scheme). In the Skyfall Desert the campaign is exactly the original.

## Where to find them

* Title → **TRAINING ACADEMY**: level list (medals, locks), location, aircraft, handling mode,
  optional corridor markers, level details, a route preview drawn on the map of the loaded
  location, your record for the chosen aircraft (best score, medal, attempts, best time) and the
  medals of all eight aircraft, then **LAUNCH**.
* Title → **CAMPAIGN**: the 61-mission grid of earlier releases, with **LOCATION**, **AIRCRAFT**
  and **PAINT** pickers below it (arrow keys, Enter, or click the left / right half). Completed
  missions are ticked per location.

## The ten levels

| # | Level | Recommended location | Content |
|---|---|---|---|
| 1 | Scramble & Takeoff | Skyfall Desert | cold start, takeoff, climb-out, navigation circuit, runway landing |
| 2 | Mountain Navigation | Matterhorn | valley navigation with speed and altitude discipline |
| 3 | Low-Altitude Flight | Glen Coe | low-level glens and passes under a ceiling |
| 4 | Obstacle Manoeuvring | Guilin | pillar slalom, arches, under a bridge, mast slalom, compound overflight |
| 5 | Advanced Aircraft Control | Mount Fuji | level / climbing / descending turns, high G, loop, roll, unusual-attitude recovery |
| 6 | Precision Terrain Following | Geirangerfjord | tight height windows, arches |
| 7 | High-Speed Tactical Navigation | Grand Canyon | fast canyon run, compounds, masts, viaduct |
| 8 | Challenging Refuelling Approach | Torres del Paine | mountain route, tanker rendezvous, guided refuelling |
| 9 | Long-Distance Tactical Flight | Aconcagua | terrain navigation, obstacles, formation with a lead, refuelling, precision waypoints |
| 10 | Final Pilot Qualification | Musandam | everything, ending with a carrier trap |

Every level starts with a radio briefing (the captain), then aircraft- and mode-specific advice
from the instructor; it ends with a debrief card (seven category scores, total, medal, new-best
flag). Any level can be flown on any downloaded location except: level 10 needs open sea (Skyfall
Desert, Musandam, Rio de Janeiro), and **Everest is not offered in the Academy** (see limits).

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
speed), touchdown or trap and the medal. The tower, your lead and the tanker have their own calls.
Every line is fixed text from a finite vocabulary, so all of it is in the neural voice pack
(`assets/voice`, Kokoro-82M); captions are always shown, and the system voice reads any line that
has no recording. Urgent calls interrupt; chatter only plays into silence.

In flight: the next gates are rings in the world (current gate bright, the next ones dimmer;
obstacle gates blue, approach rings amber), HUD markers with distance and gate number, the gate
height window and speed band, optional corridor beads (Academy menu toggle), the section list with
progress, and approach guidance.

## Scoring, medals, retries, progress (`src/mission/Progress.cpp`)

Categories (0-100, only those the level uses): waypoint accuracy, manoeuvring, altitude / speed
discipline, terrain clearance, stability / control, refuelling, landing. Medals: bronze 62,
silver 78, gold 90. A crash shows the replay and then **R retries from the last checkpoint** (the
start of the section, with the aircraft state saved there). History is kept per level and per
aircraft (best score, best medal, attempts, best time); a medal on level N unlocks level N+1.

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

* `courses_are_valid_on_every_location` — all 10 levels × 14 locations (level 10 on the three sea
  locations): builds, 15.5-20 min, on the map, ≥ 30 m above the ground, ≤ 4.5 g corners, glide
  limits, no gate cut by an obstacle, tanker / formation / carrier present where expected.
* `courses_are_deterministic`.
* `bot_flies_every_level_with_every_aircraft_in_every_mode` — an autopilot bot flies the real
  flight model (no teleports) through all 10 levels × 8 aircraft × 3 handling modes on the
  recommended locations: takeoff, gates, manoeuvres, formation, refuelling, runway landing or
  carrier trap. It must land (or trap) without running the tanks dry.
* `every_training_radio_call_is_in_the_voice_vocabulary` — every call heard in 40 real flights is
  in the voice pack's line list.
* `academy_progress_medals_history_and_unlocks`.
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

* The bot flies 239 of the 240 level / aircraft / mode combinations on the recommended locations;
  the exception (Su-35, assisted, level 9 at Aconcagua: pillar slalom at 4.5 km) is listed in the
  test. On other locations the bot completes most but not all combinations (it is a bot, not a
  proof that a human cannot). Sweep over the 13 Academy locations (129 courses each): F-16 arcade
  127, F-16 assisted 125, F-16 simulation 122, Su-30 assisted 124, F-35 simulation 120; most
  failures are on Everest-like high terrain (Everest itself is in the sweep but not the Academy).
* Everest is not offered in the Academy (5-8 km terrain: the low-level obstacle work exceeds
  several jets' performance in that air). The campaign does fly there.
* Level 10 needs open sea: only Skyfall Desert, Musandam and Rio de Janeiro.
* The estimate is 15.5-20 min; a fast, clean flight (like the bot's, especially in arcade mode)
  takes less, about 12-17 minutes.
* Campaign over other locations: the combat bot (assisted, F-22, up to 3 attempts) wins 48 of
  missions 4-61 in the desert itself (it cannot fly mission 5's aerobatics, the low-level route of
  mission 6, and loses some of the big dogfights of missions 27-40). Over the 12 other real
  locations it wins 46-53 of 58 (Everest 41); the missions it wins in the desert but loses
  elsewhere are at most 4 per location (mostly dogfights decided by luck) except Everest, where
  it loses 8 more (ground attacks between 8 km peaks). Over all 14 maps: 689 of 812. Without sea, the story text of the briefings still speaks of boats
  and the sea where the mission had them; only the objectives change.
