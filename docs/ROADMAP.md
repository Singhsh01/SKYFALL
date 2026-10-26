# Requirements checklist & roadmap

Legend: ✅ implemented and exercised · 🟡 partial / in progress · ⬜ planned

## v1.0 (this release)

| Requirement | Status | Notes |
|---|---|---|
| Remove rings, obstacle courses and medal-style exercises from the Academy | ✅ | waypoints only |
| 20 combat operations of 15-20 min | ✅ | estimates 16-20 min; the bot, flying fast, finishes in about 9-17 min |
| Enemy waves, wingmen, AWACS picture calls | ✅ | |
| C-17 escort, airlift, airdrop, resupply; convoy, rescue, airbase, fleet, troops defence | ✅ | asset loss fails the operation |
| F/A-18E, Typhoon, C-17 | ✅ | C-17 in Free Flight |
| Living airbases, more military and urban props | ✅ | |
| Air-force-inspired and environment / demo paint schemes, stencils | ✅ | 16 new, 40 total; national insignia and flags removed |
| Smarter AI (strike, hunting helicopters, break-off, squadron tactics) | ✅ | |
| Existing modes kept (campaign, arena, free flight) | ✅ | tests unchanged and passing |
| Bot flies every operation / fighter / mode | ✅ | 578 / 600 (17 lost fights, 5 other) |
| 120 fps on the MacBook Air | 🟡 | not measured for v1.0; see BENCHMARKS.md |
| Human play-testing of operation length and difficulty | ⬜ | bot-tested only |
| Graphics: fewer draw calls (merged terrain / tree blocks) | ✅ | -13 to -35 % draw calls, identical image |
| Graphics: HDR eye adaptation | ✅ | GPU-only, per-time-of-day key |
| Graphics: PBR for buildings, vehicles, props | ✅ | GGX metal / roughness |
| Graphics: adaptive detail after dynamic resolution | ✅ | FrameGovernor, tested |
| GPU instancing | 🟡 | not needed yet: repeated dynamic meshes are few (traffic is one buffer, airbase life 14 draws) |
| Frame rates of v1.x on the MacBook Air | ⬜ | to be measured with scripts/benchmark.sh |

## v0.9

| Requirement | Status | Notes |
|---|---|---|
| VS Code build / run / debug tasks | ✅ | `.vscode/tasks.json`, `launch.json` |
| SKYFALL.app with an original jet icon, Desktop alias, Release build | ✅ | `scripts/make_app.sh` |
| Download all maps once, checksums, offline, verify / repair, incremental | ✅ | `MapStore`, IndexedDB on the web |
| Arcade: top speed in 3-4 s, braking in 4-5 s, per-aircraft top speeds | ✅ | tested |
| Sense of speed (FOV, shake, streaks, dust, rush, audio) | ✅ | |
| Presets incl. Cinematic 4K and Performance 120, fps caps, VSync, interpolation | ✅ | |
| 4K / 120 fps measured on the Mac | 🟡 | see BENCHMARKS.md for what has been measured |
| Second (enemy-view) kill replay | ✅ | |
| Non-critical HUD panels at 55 % | ✅ | |
| Armour-inspired paint schemes for all 8 aircraft | ✅ | 8 new, 24 total |
| Populated maps: military sites, civilian props, traffic, night lighting | ✅ | `WorldDetail`, tested on 14 maps |
| 20-level Academy with helicopters and air combat, no medals / locks | ✅ | carrier launches, IFF, formations from bases; bot finishes 465 / 480 |
| Reference video of the requested look | ⬜ | could not be opened from the build environment |

## Core (spec sections A–D)

| Requirement | Status | Notes |
|---|---|---|
| C++20, CMake ≥ 3.20, raylib, Apple Clang | ✅ | raylib via Homebrew or automatic FetchContent 5.5 |
| Fixed-step 120 Hz physics, bounded accumulator | ✅ | `FixedStepper`, tested |
| Frame-rate-independent rendering, p50/p95/p99 telemetry | ✅ | `F3`, `--bench` → JSON/CSV |
| Physics independent of raylib | ✅ | `skyfall_core` library |
| Runway start, original F-22-inspired mesh, explicit axes/CG | ✅ | body frame documented in `Math.hpp` |
| Mission 01 guided cold start/takeoff with telemetry predicates | ✅ | 8 steps, tested (no timer completion) |
| Three cameras (cockpit default in M01, chase, ~5 m external auto-framed) | ✅ | |
| Lift/drag/thrust/gravity, stall, ground friction, landing rules | ✅ | see PHYSICS.md |
| Full 6-DOF moment model with inertia tensor | 🟡 | FBW rate-command approximation today |
| HUD: speed, altitude, VS, heading, throttle, gear, brakes, state, FPS | ✅ | plus AoA, G, Mach, radar alt |
| Collision / ground contact + readable failure feedback | ✅ | crash reasons shown |
| Settings persistence (graphics, input, audio) | ✅ | INI in user config dir, unknown keys preserved |
| Keyboard; gamepad | ✅ | HOTAS profiles ⬜ |
| Assisted vs Simulation handling | ✅ | |
| Audio buses, music OFF by default, mixes with other apps | ✅ | procedural synth; iOS audio session ⬜ |
| Camera shake slider, G-blackout toggle, FOV, UI scale, captions | ✅ | |
| Virtual cockpit with moving stick/throttle, live displays | ✅ | 5 displays + conformal HUD |

## Training Academy and the campaign on every location (v0.8)

| Requirement | Status | Notes |
|---|---|---|
| The 61-mission campaign over any location, with aircraft and paint choice | ✅ | fitted to each map's terrain; progress per location; bot-tested (see TRAINING.md) |
| Modern paint schemes | ✅ | 16 schemes: 8 camouflages + 8 modern (counter-shaded, shard, display, metal, carbon) |
| Academy: 10 levels, 8 aircraft, 3 handling modes, ~17 min (15.5-20 by estimate) | ✅ | Everest excluded; level 10 needs sea |
| Briefing → takeoff → navigation → terrain → skills → refuel → landing / trap → debrief | ✅ | level 10 ends with a carrier trap |
| No teleports; routes flyable by every aircraft | ✅ | autopilot test: 239 / 240 on the recommended locations |
| Continuous context-aware instructor, neural voice, subtitles, fallback speech | ✅ | every call checked against the voice vocabulary |
| Obstacles with collision and LOD, gate rings, corridor option | ✅ | pillars, arches, bridge deck, masts, compound, viaduct |
| Guided refuelling (no artificial fuel drain), glide-path guidance | ✅ | runway: straight / circling / steep; carrier: ball + line-up |
| Scoring (7 categories), medals, checkpoint retry, per-aircraft history, unlocks | ✅ | `mission/Progress` |
| Academy menu: aircraft, location, level details, route preview, previous scores, launch | ✅ | |

## Aircraft (E)

| | Status |
|---|---|
| 8 data-driven slots with provenance notes | ✅ |
| All eight aircraft flyable (arcade handling per profile, tested) | ✅ |
| Licensed or hand-modelled detailed exterior models | ⬜ |

## Rendering (F)

| | Status |
|---|---|
| HDR, bloom, ACES, analytic sky, height fog, sun glare | ✅ |
| Procedural terrain with LOD, farmland, forest, rock, snow, coast, cloud shadows | ✅ |
| Sea with depth colour, glitter, shoreline foam | ✅ |
| Cloud layer (two sheets) + cumulus billboards | ✅ |
| PBR-lite aircraft, panel lines, camouflage, afterburner plumes | ✅ |
| Smoke ribbons, explosions, debris, flares, tracers, wingtip vapour | ✅ |
| Sun shadow map (aircraft), baked terrain shadows + AO, cumulus sprites | ✅ |
| Hydraulic-erosion terrain, desert material, 3-level terrain LOD | ✅ |
| Missile / target / hit picture-in-picture camera | ✅ |
| Dynamic resolution to hold ~60 fps | ✅ |
| Ground target models: gun emplacement, artillery, oil rig, fuel tank, hangar, command post, patrol boat | ✅ |
| Crash & shoot-down replay with telemetry analysis | ✅ |
| SSAO, true volumetric clouds | ⬜ |
| Draw-call/LOD instrumentation | 🟡 | draw calls + world pass time in `F3` |
| Real-world terrain (Cesium etc.) | ⬜ separate heavy-engine track |
| Metal renderer (OpenGL is deprecated on macOS) | ⬜ |

## Campaign (G)

| # | Missions | Status |
|---|---|---|
| 01–03 | Cold start & takeoff, first solo flight, runway landing | ✅ hand-written telemetry FSMs |
| 04–09 | Navigation, high-g (6 g, loop, aileron roll), low-level, gunnery / ATS / ATA ranges | ✅ |
| 10–20 | Intercepts, gun line, oil rigs, convoy, SEAD, fuel depot, mountain ace, coastal patrol, airbase defence | ✅ |
| 21–30 | Dusk strike, artillery, radar network, offshore complex, hangars, tank column, combined arms, ace duel, fortress | ✅ |
| 31–40 | Supply lines, mountain SAM belt, harbour raid, interceptor waves, oil-field recapture, final assault + landing | ✅ |
| 41 | Air-to-air refuelling behind a tanker (probe-and-drogue, steering cues, collision) | ✅ |
| 42–51 | Critical infrastructure: power station, refinery, rail viaduct + train, comms masts, armoured train, factories, airfield, mobile launchers (time limit), ammo dump, final strike + landing | ✅ |
| 52–61 | Carrier operations in a rainstorm: catapult launch (afterburner, brake release, hands-off rotation), patrol, picket boats, coastal radar, harbour, fleet defence, platforms, beachhead, viaduct, final strike; low pass over the carrier | ✅ launch tested in 8 aircraft × 3 handling modes |
| — | Arrested landing back on the carrier | ⬜ |

Missions 04–61 are data (`CampaignMissionDef`); targets can be anchored to generated
infrastructure (railway position, largest bridge, industrial zone). Tests validate every layout
against the terrain and play every mission to completion; refuelling, trains, bridges, ATS
hit-rates and the cannon assist have their own tests.

## World (G2)

| | Status |
|---|---|
| Generated scenery: towns, roads, railway with viaducts, bridges, wind farms, industry, train | ✅ deterministic from any terrain |
| 13 real-world locations from open elevation data (fetch script + GitHub workflow) | ✅ data downloaded on demand |
| Climate materials: desert / alpine / temperate / tropical | ✅ |
| OpenStreetMap buildings and roads for real locations | ⬜ needs an OSM extract pipeline |
| Lakes and rivers above sea level | ⬜ |
| Sea level from real data (0 m samples become sea floor: fjords, coasts) | ✅ |
| In-game map download (Settings) | ✅ native (curl) and browser (Pages) |
| Neural female voice for every briefing and radio call (Kokoro-82M, pre-recorded) | ✅ |
| Rain, lightning, storm sea | ✅ carrier missions |

## Vision laboratory (H) — ⬜ next major milestone

1. Export annotated frames (intrinsics, extrinsics, ground-truth target pose) from the renderer.
2. Optional OpenCV build flag; tracking + error vs ground truth; latency/lost-track stats.
3. Terrain image registration (features, RANSAC homography, reprojection error).
4. PnP pose estimation with confidence; COLMAP experiment offline.

## Platforms (I)

| | Status |
|---|---|
| macOS universal binary, built and tested by CI, released on `v*` tags | ✅ |
| Browser (WebAssembly + WebGL 2) on GitHub Pages | ✅ single-threaded world build; settings not persisted |
| iPhone native (Metal) port, remote-play streaming, VR | ⬜ after desktop performance is measured |
