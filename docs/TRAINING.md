# Training Academy and Location Operations

Two additions to SKYFALL v0.8: a **Training Academy** of ten graded courses with a live
instructor, and an **Operations** campaign of ten missions for every world location. The original
61-mission campaign is unchanged and still plays in the Skyfall Desert.

## Where to find them

* Title → **TRAINING ACADEMY**: level list (medals, locks), location, aircraft, handling mode,
  optional corridor markers, level details, a route preview drawn on the map of the loaded
  location, your record for the chosen aircraft (best score, medal, attempts, best time) and the
  medals of all eight aircraft, then **LAUNCH**.
* Title → **CAMPAIGN**: **ORIGINAL CAMPAIGN** (the 61 missions) or **<LOCATION> OPERATIONS** for
  any of the 14 locations. Aircraft and handling mode are chosen on the same screen. Progress is
  saved per location.

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
Operations unlock one after the other per location.

## Location Operations (`src/mission/Operations.cpp`)

Ten mission templates are built from each location's terrain: Valley Patrol, Ridge Radar (low
ingress), The Pass (SAM site in the valley), Convoy Interdiction, Supply Line (the generated
railway or trucks, then a fuel depot), Strike Force (hangars, or patrol boats on coastal maps),
Long Reach (tanker first), Peak Hunter (relays on the peaks), Mountain Dogfight (two low waves)
and Final Operation (air defences, headquarters, last wave, then land at the airfield). Targets
are kept within reach of the airfield's height; waves spawn inside the map.

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
* `operations_are_built_on_every_location` — all 140 operations: valid placement, tanker clear of
  the ground, objective logic completes, briefings in the vocabulary.
* `operations_are_winnable_by_the_combat_bot` — the combat bot that proves the hard desert
  missions flies the operations against the full AI (5 locations by default, `SKY_OPS_ALL=1` for
  all 14).
* Debug tools: `course_report`, `course_dump` (`LOC=`, `LV=`, `ALL=1`), `bot_flies_one_course`
  (`LOC LV AC MODE`, `TRACE=<section kind>`, `SECT=1`, `RADIO=1`), `bot_sweep_over_every_location`
  (`SKY_SWEEP=<aircraft>,<mode>`).
* `skyfall --capture <dir>` with `SKYFALL_SET=flow` drives the real game loop through the Academy,
  a crash and checkpoint retry, completion and progress, operations and location switches;
  `SKYFALL_SET=academy` captures the new screens.

## Known limits

* The bot flies 239 of the 240 level / aircraft / mode combinations on the recommended locations;
  the exception (Su-35, assisted, level 9 at Aconcagua: pillar slalom at 4.5 km) is listed in the
  test. On other locations the bot completes most but not all combinations (it is a bot, not a
  proof that a human cannot). Sweep over the 13 Academy locations (129 courses each): F-16 arcade
  127, F-16 assisted 125, F-16 simulation 122, Su-30 assisted 124, F-35 simulation 120; most
  failures are on Everest-like high terrain (Everest itself is in the sweep but not the Academy).
* Everest is not offered in the Academy (5-8 km terrain: the low-level obstacle work exceeds
  several jets' performance in that air). Its Operations exist; the combat bot does not win
  operations 7, 9 and 10 there, nor operation 10 at Aconcagua.
* Level 10 needs open sea: only Skyfall Desert, Musandam and Rio de Janeiro.
* The estimate is 15.5-20 min; a fast, clean flight (like the bot's, especially in arcade mode)
  takes less, about 12-17 minutes.
* The original 61-mission campaign stays in the Skyfall Desert.
