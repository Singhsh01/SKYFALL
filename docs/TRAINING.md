# Training Academy: Twenty Combat Operations, and the Campaign on Every Location

The **Training Academy** is a set of twenty **combat operations** of 15-20 minutes each. The
ring, pillar and medal-style exercises of earlier versions are gone: every operation is a sortie
with a job to do (defend a base, escort a C-17, protect a convoy or a rescue, hold a fleet, beat
an enemy squadron) against enemy waves that come from their own bases, with a wingman on your
wing and an AWACS controller calling the picture. The **61-mission campaign**, Free Flight and
the Dogfight Arena are unchanged and fly over any world location.

## Where to find them

* Title → **TRAINING ACADEMY**: the twenty operations in two columns (all open, a green dot marks
  a completed one), location, aircraft (the ten fighters; the C-17 is flown in Free Flight),
  handling mode, the operation's details (about how many minutes, how many waves and waypoints,
  wingmen, the suggested aircraft), a map of the operation area with the routes of the
  protected assets, your record for the chosen aircraft, then **LAUNCH**.
* Title → **CAMPAIGN**: the 61-mission grid with **LOCATION**, **AIRCRAFT** and **PAINT** pickers.
* Title → **HANGAR**: 11 aircraft and 40 paint schemes.

## The twenty operations

| # | Operation | Recommended location | Length | What happens |
|---|---|---|---|---|
| 1 | Combat Scramble | Skyfall Desert | about 16 min | Scramble from the airbase with a wingman and intercept the raids crossing into the region: fighters, then helicopters, then a second pair of fighters. |
| 2 | Helicopter Hunt | Wadi Rum | about 16 min | Hostile helicopters are working around an enemy outpost. Identify ours, hunt theirs, and deal with the fighter escort that comes for you. |
| 3 | Airbase Under Attack | Glencoe | about 17 min | The airbase is under attack. Scramble and defend the hangars, the parked jets and the fuel farm against helicopters and strike aircraft. |
| 4 | Fighter Interception | Grand Canyon | about 17 min | Hold the line against fighters approaching allied airspace: a pair of F-16 aggressors, a pair of Su-35s, then a coordinated four-ship from two directions. |
| 5 | Carrier Combat Launch | Rio de Janeiro | about 17 min | Catapult off the carrier, defend the fleet against fighters and strike aircraft, then trap back aboard. Suggested: F/A-18E Super Hornet. |
| 6 | Enemy Helicopter Assault | Guilin | about 16 min | Our armour is dug in at a forward position. Stop three waves of attack helicopters from overrunning it. |
| 7 | C-17 Escort Operation | Erg Chebbi | about 16 min | Join a C-17 carrying critical supplies to the base and protect it all the way to the ground through two waves of fighters. |
| 8 | Military Outpost Strike | Aconcagua | about 18 min | Ingress low under the radar, destroy an enemy outpost, survive the interceptors they scramble, then hit the second target. |
| 9 | Typhoon Engagement | Mount Fuji | about 16 min | A demanding air combat against Eurofighter Typhoon aggressor formations: a pair, an ace pair, then a three-ship. |
| 10 | Mountain Air Warfare | Matterhorn | about 16 min | Fight through the mountains: fighters low in the valleys, helicopters in the passes, then a pair from above. No rings, only the terrain. |
| 11 | Logistics Airlift | Torres del Paine | about 18 min | Escort a C-17 from the base to a forward position: protect the climb-out, the airdrop and the return to the runway. |
| 12 | Airfield Defense | Mount Fuji | about 18 min | Waves of fighters and strike aircraft are coming for the airfield. Defend the parked aircraft, hangars and fuel farm. |
| 13 | Naval Air Operations | Musandam | about 18 min | Carrier operations: catapult launch, defensive patrol over the fleet against helicopters and strike aircraft, and carrier recovery. Best flown in the F/A-18. Suggested: F/A-18E Super Hornet. |
| 14 | Armored Convoy Support | Skyfall Desert | about 18 min | A supply convoy is driving to the front. Escort it along the road through helicopter and strike attacks. |
| 15 | Enemy Squadron Interception | Torres del Paine | about 17 min | Coordinated Su-35 and Su-57 formations with squadron tactics: bracketing, high cover and cranking. You have two wingmen. Suggested: F-22 Raptor. |
| 16 | Combat Search and Rescue | Geirangerfjord | about 17 min | Protect the rescue helicopters flying to a downed pilot, on the pick-up and all the way home. |
| 17 | C-17 Emergency Resupply | Wadi Rum | about 18 min | The airfield is contested. Clear the attack helicopters, then escort a C-17 into the base under fire and secure its landing. |
| 18 | Combined Air Combat | Guilin | about 18 min | Support our ground forces in contact: destroy the enemy armour while fighting off their helicopters and fighters. |
| 19 | Large-Scale Airbase Assault | Erg Chebbi | about 20 min | A multi-stage assault on an enemy air base: suppress the guns, beat the interceptors, destroy the hangars and parked aircraft, refuel and return. |
| 20 | Final Combat Qualification | Musandam | about 20 min | Everything at once: carrier launch, fighter waves, a protected C-17 resupply, helicopter attacks and an ace pair, then carrier recovery. |

Every operation starts with a radio briefing, then the operation's opening calls; it ends with
a debrief card (category scores including COMBAT, total score, kills, a new-best flag) and a
spoken remark on the score. There are **no medals, rewards or locks**. Any operation can be
flown on any downloaded location except Everest; operations 5, 13 and 20 start and end on the
carrier and need open sea (Skyfall Desert, Musandam, Rio de Janeiro).

### How an operation works

* **Waypoints, not rings.** Transit legs follow terrain-aware waypoints (route through the
  valleys, heights limited for climbs and dives, the capture radius grows with height, no
  waypoint behind a ridge from the previous one). They are beacons and HUD diamonds, not rings.
* **Waves.** Each combat section is a wave (`Engagement` in `Training.hpp`): fighters, strike
  aircraft, attack and transport helicopters, ground units and ships, at rookie, veteran or ace
  skill. Waves aimed at a protected asset spawn 8-14 km out on the side away from you, so the
  intercept has to be flown; "split" waves come from two directions at once; others take off in
  formation from the nearest enemy base. AWACS calls each wave (bearing, range, type, count).
* **Protected assets** (`Asset` in `Training.hpp`, flown in `TrainingOps.cpp`):
  * **Transport** — a C-17 that takes off, climbs, flies its route, airdrops cargo pallets on
    parachutes or lands, rolls out and opens its ramp. It has a hull figure on the HUD; if it is
    lost the operation fails.
  * **Convoy** — trucks and armour driving a real road; **Rescue** — helicopters flying to a
    downed pilot, holding for the pick-up and flying home; **Base** — hangars, parked jets and
    the fuel farm on the airfield; **Fleet** — the carrier group; **Troops** — armour dug in at a
    forward position. Each has a loss limit: lose more than that and the operation fails with a
    reason on the debrief.
  * Our ships, armour and AAA fire back at close range, so a defended asset is not helpless, but
    without you it does not survive the waves.
* **The enemy AI** (`smartAi` in `Combat.cpp`): strike aircraft fire air-to-surface missiles at
  the asset, attack helicopters hunt ground units, damaged jets break off and run, squadrons
  bracket and crank, and the scripted battle moves and fires the ground units.
* **Wingmen** follow you through the transit legs, fight in the waves and call their kills; in
  operation 15 you have two.
* Between waves your health is reset and weapons are topped up; after the last wave the route
  home is rejoined at the most sensible waypoint, and the operation ends with a runway landing
  or a carrier trap.
* **Shot down:** the replay, then **R** retries from the start of the section, with the
  protected assets restored to where they were at that checkpoint.

## How an operation is built (`src/mission/Training.cpp`)

Operations are generated from the location's terrain, deterministically (same location +
operation = same layout): the home base is the map's runway (or the carrier), the asset routes
are cleared against the terrain and rounded into flyable curves, the C-17's approach is chosen
for a usable glide path, convoys use the map's roads, and the enemy bases are the map's own
military sites. The typical length is 15-20 minutes for a human-paced flight; the menu shows
the estimate.

## Instructor, voice and HUD

The instructor talks continuously and in context: section intros, upcoming turns and headings,
passes and canyons, speed / height / ceiling corrections, terrain and obstacle warnings, gate hits
and misses, manoeuvre coaching, formation and refuelling cues, the approach (glide path, line-up,
speed), touchdown or trap, combat calls and the closing remark on the score. The tower, your lead and the tanker have their own calls.
Every line is fixed text from a finite vocabulary, so all of it is in the neural voice pack
(`assets/voice`, Kokoro-82M); captions are always shown, and the system voice reads any line that
has no recording. Urgent calls interrupt; chatter only plays into silence.

In flight: waypoint beacons and HUD diamonds with distance, the section list with progress,
TARGETS k/n during a wave, asset markers (the C-17 with its hull figure, convoys, rescue
helicopters, the fleet), friendly units within 6 km, and approach guidance. The voice lines
include AWACS picture calls and the wingman's calls (`trainingOpsLines()`).

## Scoring, retries, progress (`src/mission/Progress.cpp`)

Categories (0-100, only those the operation uses): waypoint accuracy, manoeuvring, altitude /
speed discipline, terrain clearance, stability / control, refuelling, landing and combat (the
largest weight: targets destroyed, assets protected, damage taken). There are no medals and no
locks: all twenty operations are open. A crash
shows the replay and then **R retries from the last checkpoint** (the start of the section, with
the aircraft state and the protected assets saved there). History is kept per operation and per
aircraft: completed (yes / no), best score, attempts and best time. The operations are new
sorties, so records of the earlier ring-course academies are not carried over; their keys stay in
the settings file untouched, so older builds still read them.

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

* `courses_are_valid_on_every_location` — all 20 operations × 14 locations (280 layouts): builds,
  no ring or obstacle-course sections left, waypoints on the map and at least 30 m above the
  ground, at least three waves each with targets and an area on the map, the right assets per
  operation (C-17, base, fleet or base, troops, convoy, rescue), wingmen where expected, the
  carrier at sea for the carrier operations, the C-17's path long enough with take-off, airdrop
  and touchdown in order and its ground roll on the runway.
* `courses_are_deterministic`.
* `bot_flies_every_level_with_every_aircraft_in_every_mode` — an autopilot bot flies the real
  flight model (no teleports) through all 20 operations × 10 fighters × 3 handling modes on the
  recommended locations: takeoff or catapult, waypoints, joining and escorting the asset, every
  wave against the real AI (hunting the targets that threaten the asset first, retrying from the
  checkpoint when shot down, as a player would with R), the way home and the landing or trap.
  It must destroy every target, keep the asset alive and land. `SKY_LEVELS=a-b` runs a range.
* `every_training_radio_call_is_in_the_voice_vocabulary` — every call heard in 40 real flights is
  in the voice pack's line list.
* `academy_progress_history_without_medals_or_locks` — all operations open, completion, best
  score, per-aircraft history, earlier academies' records not carried over (and left untouched),
  no medal words in the remarks.
* `world_detail_is_populated_on_every_location` — the army compound, forward bases, traffic and
  lights on every map; nothing on water or the runway; no tall prop in the path between any two
  consecutive Academy waypoints.
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
* `skyfall --capture <dir>` with `SKYFALL_SET=flow` drives the real game loop through the Academy
  (operation 7's C-17 flying and the wingman joining, operation 3's airbase installations,
  operation 2's helicopters, the carrier operations at sea),
  a crash and checkpoint retry, completion and progress, the campaign over Guilin, Musandam
  (carrier) and the Matterhorn (airborne start), and location switches;
  `SKYFALL_SET=ops` and `SKYFALL_SET=airbase` capture the operations and the living airbase,
  `SKYFALL_SET=skins` the paint schemes (`SKYFALL_JETS`, `SKYFALL_SKIN_FROM`, `SKYFALL_SKIN_TOP`).

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
