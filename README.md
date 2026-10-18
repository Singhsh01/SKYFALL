# SKYFALL — Real-Time Combat Flight Simulator & Computer Vision Laboratory

[![build](https://github.com/Singhsh01/SKYFALL/actions/workflows/build.yml/badge.svg)](https://github.com/Singhsh01/SKYFALL/actions/workflows/build.yml)
[![web](https://github.com/Singhsh01/SKYFALL/actions/workflows/pages.yml/badge.svg)](https://github.com/Singhsh01/SKYFALL/actions/workflows/pages.yml)

### ▶ [Play in your browser](https://singhsh01.github.io/SKYFALL/) &nbsp;·&nbsp; [straight into the Dogfight Arena](https://singhsh01.github.io/SKYFALL/?mode=arena)

Runs instantly in Chrome, Edge, Firefox or Safari (WebGL 2): no install, the game runs inside the
browser's sandbox. Click **Play**; the world is generated in a few seconds. Keyboard required.

Prefer the native app (higher frame rate, full window)? [Download for macOS](https://github.com/Singhsh01/SKYFALL/releases/latest/download/SKYFALL-macOS.zip)
— Apple Silicon and Intel, macOS 12+. Unzip, then right-click **Play-SKYFALL.command → Open**
(the build is not notarised, so macOS asks once).

A native **C++20 / raylib** combat flight game and simulation sandbox built for macOS first
(Apple Clang, CMake, 8 GB MacBook Air class hardware), portable to Linux and Windows.

It is inspired by the *feel* of modern jet gameplay (third-person chase camera, missile locks,
flares, explosions, a big battlefield landscape) while keeping the engineering honest: a
deterministic fixed-step flight model with real lift/drag/thrust equations, unit tests, measured
frame-time telemetry and clearly labelled approximations. **Everything in it is original**: no
copyrighted models, textures, sounds or code from commercial games.

![Chase view over the desert mountains at golden hour](docs/screenshots/golden_hour_chase.jpg)

| | |
|---|---|
| ![Missile camera](docs/screenshots/missile_cam.jpg) | ![Hit camera](docs/screenshots/hit_cam.jpg) |
| ![Crash replay](docs/screenshots/crash_replay.jpg) | ![Canyon](docs/screenshots/canyon.jpg) |
| ![Cockpit](docs/screenshots/cockpit_air.jpg) | ![Radio briefing in the cockpit](docs/screenshots/briefing.jpg) |
| ![Gun line](docs/screenshots/gun_line.jpg) | ![Hangar](docs/screenshots/hangar.jpg) |
| ![Refuelling](docs/screenshots/refuelling.jpg) | ![Dogfight arena](docs/screenshots/arena_chase.jpg) |
| ![Town and railway](docs/screenshots/scenery_town.jpg) | ![Camouflage schemes](docs/screenshots/skins.jpg) |
| ![Carrier deck in the rain](docs/screenshots/carrier_deck.jpg) | ![On the catapult in full afterburner](docs/screenshots/catapult_afterburner.jpg) |
| ![Afterburner climb-out in a storm](docs/screenshots/afterburner_rain.jpg) | ![Campaign: 61 missions](docs/screenshots/campaign_menu.jpg) |
| ![Everest, real elevation data](docs/screenshots/real_everest.jpg) | ![Geirangerfjord, real elevation data](docs/screenshots/real_geiranger.jpg) |
| ![High-G wing condensation](docs/screenshots/wing_vapour.jpg) | ![Gear, wheels and landing light](docs/screenshots/gear_lights.jpg) |
| ![Forested valleys and fields, Glen Coe](docs/screenshots/landscape_glencoe.jpg) | ![Patchwork farmland below Mount Fuji](docs/screenshots/landscape_farmland.jpg) |

> Screenshots are captured from the running executable (`skyfall --capture`) on a Linux
> container with Mesa's software rasteriser: they show the real renderer output, not macOS
> performance. Measured on a MacBook Air (v0.3 build): 66 fps mean, p95 20 ms at 1680x1050 High.
> See [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## What you can do in this build (v0.7)

* **Carrier operations (missions 52-61)** — ten missions that start on the catapult of an aircraft
  carrier in a rainstorm: push the throttle into afterburner (blue-white flame with shock
  diamonds), release the brake, and the catapult throws you off the bow; the flight controls
  rotate to a climb by themselves. Then patrols, picket boats, a coastal radar, a harbour, fleet
  defence, oil platforms, a beachhead, a rail viaduct and a final strike, each ending with a low
  pass over the carrier. Every launch is test-flown in all 8 aircraft and all 3 handling modes.
* **A difficulty curve that builds to the finale** — missions 1-46 play as before. From mission 47
  the enemy flies with tactics: it turns across your missiles and drags them down (beam / notch)
  with well-timed flares, breaks and reverses when you sit on its six, extends to regain speed
  instead of stalling in a turn, and paces its missile shots. Missions 47-51 add gun lines, SAM
  sites and interceptors between the objectives. In the carrier missions (52-60) the bandits fly
  as squadrons: flankers bracket you from both sides while a third comes in high, they crank away
  after a missile shot and their reversals are unpredictable. The air defences are sharper, and
  objectives come in stages: strike, fight off the response, strike again. Mission 61, *Typhoon*,
  is the final battle: break the SAM belt, beat two counterattacks, hit the flak-ringed command
  post, then a duel with Typhoon flight, their two aces, with your two wingmen beside you.
  Difficulty comes from tactics and mission design only; enemy aircraft are never tougher, and
  never more than one enemy missile is in the air at you at once. A test bot flies every one of
  missions 47-61 for real, in all three handling modes with five different random seeds each, and
  must win them all (`hard_campaign_missions_are_winnable_by_a_real_pilot_in_every_handling_mode`).
* **A real voice for the whole game** — every briefing and every radio call (AWACS, tower,
  instructor, your wingman) is spoken by a natural neural female voice, recorded once with the
  open-source Kokoro-82M model (`tools/make_voice.py`) and shipped as `assets/voice/*.mp3`. Radio
  calls get a radio-band filter. If a recording is missing the
  system voice reads the line instead.
* **Download maps in the game** — Settings → DOWNLOAD WORLD MAPS (or click a location marked NOT
  DOWNLOADED) fetches and converts the real elevation data in the background with a progress bar.
* **Ultra HD and 75 fps** — Settings → Quality ULTRA HD renders the 3D scene at the display's real
  pixel count (Retina / 4K, up to 3840 wide); Settings → Frame rate limit 60 / 75 / 120 /
  unlimited (default 75). Dynamic resolution holds the chosen frame rate.
* **More realistic image** — filmic ACES tone mapping with proper sRGB output and a photographic
  grade, storm lighting with a slate-grey sea and whitecaps, rain, lightning, and real sea level
  on the real maps (fjords and coasts are water).
* **Detailed, animated aircraft** — every jet has three levels of detail (about 9k / 2.5k / 0.7k
  triangles, picked by distance). Close up: ailerons, flaps / elevons, rudders, canards and
  all-moving stabilisers follow the stick, F-22 and Flanker-family nozzles vector with pitch,
  wheels (with visible hubs) roll and the oleos compress on the ground, and navigation lights,
  a flashing strobe and a landing light on the nose gear are lit. The skin shader adds panel
  grooves and rivets (bump-mapped), per-panel roughness and grime streaks; damage scorches and
  blisters the paint, glows when critical, and can shear off an aileron or a stabiliser.
* **Air effects** — wing condensation sheets under G, a transonic vapour cone near Mach 1 in moist
  low-level air, heat shimmer behind hot engines, and afterburner plumes whose length and shock
  diamonds change with speed and altitude.
* **Cockpit** — sharper multifunction displays (rendered at twice the resolution), windscreen
  frame, backlit switches and annunciators, master caution / warning lights.
* **Camouflage everywhere** — the scheme you paint in the Hangar is what you fly.

* **World locations with real terrain** (title → WORLD LOCATIONS): Everest & Khumbu, Matterhorn,
  Geirangerfjord, Glen Coe, Erg Chebbi (Sahara), Wadi Rum, Musandam fjords, Grand Canyon,
  Aconcagua, Torres del Paine, Rio de Janeiro, Guilin karst and Mount Fuji, plus the procedural
  Skyfall Desert. Real elevation comes from open data (AWS Terrain Tiles: SRTM, GMTED2010, ETOPO1,
  NED...) via `tools/fetch_real_terrain.py` or the one-click *real terrain* GitHub workflow (see
  [Real terrain](#real-terrain)). The runway is laid on the flattest ground; towns, roads, a railway
  with viaducts, wind farms and forests are generated on top; materials follow the climate
  (desert, alpine snow, green temperate, tropical). Fly the Dogfight Arena or Free Flight there.
* **Living scenery everywhere** — towns with street grids and gardens, a road network with
  embankments and bridges, a railway with viaducts and a freight train, an industrial zone with
  smoking chimneys, wind farms with turning rotors.
* **All eight aircraft are flyable** (Hangar → select → FLY): F-22, F-35, Rafale, Su-57, Su-35,
  MiG-29, Su-30, F-16, each with **eight camouflage schemes** (factory, desert, woodland, arctic,
  naval, digital urban, night, tiger) — ← / → on an aircraft in the Hangar. Choices are saved.
* **Briefings over the radio, in the cockpit** — no briefing board: every campaign mission and the
  Dogfight Arena starts with you strapped in, and Operations briefs you over the radio ("Captain,
  mission 55, Coastal Radar…"), reads the objectives and signs off with "Good luck, Captain."
  Captions appear in the radio box; Settings → Voice turns the voice off.
* **Living landscapes to the horizon** — a land-cover map drives the ground: forest canopy with
  sunlit crowns and dark gaps on the valley sides, meadows and alpine grass, patchwork farmland
  (rapeseed, wheat, pasture, ploughed fields, hedgerows) on the valley floors, granite and snow on
  the peaks; in the desert, golden grassland over red soil, juniper-dotted foothills and red
  sandstone cliffs. Real maps no longer stop at their edge: the land continues seamlessly and then
  becomes generated mountains with the map's own height and relief, all the way to the horizon.
* **Arcade handling (default)** — Battlefield-style: the flight path follows the nose, hard turns
  bleed energy, every jet has enough afterburner thrust to climb out of a low dive.
  **Double-tap ← / →** for an aileron roll. Assisted and Simulation handling remain in Settings.
* **ATA and ATS missiles** (`Q` swap, `1` / `2` select, `F` fire): air-to-air locks aircraft,
  air-to-surface locks ground targets and has a larger blast. Ground attack is arcade-friendly:
  ATS locks in 0.8 s within 34° / 7.5 km, fires at the target nearest the nose even before the
  lock completes, and leads moving vehicles; the cannon bends onto targets within 4° of the nose.
  (Tested: 56 / 56 ATS hits on static and moving trucks from 2.5-6 km.)
* **Missile / target camera** — a picture-in-picture sensor feed beside the weapons panel:
  `TGT CAM` / `TGT POD` while locking (zoomed, with a track box), `ATA CAM` / `ATS CAM` riding behind
  your missile with range and time-to-impact, then a 2.8 s `HIT CAM` hold on the impact with the
  result (`SPLASH`, `HIT`, `MISSED`) and the measured miss distance.
* **Crash & shoot-down replay** (Arena and campaign) — when you are lost, the last ~5 s are replayed from side and
  ground observers, in slow motion around the impact, with a *What went wrong* analysis built
  from recorded telemetry (bank, dive angle and pull-out height, PULL UP warning lead time,
  missile warning vs. flares, sink rate at touchdown…). `Enter` / `Space` skips.
* **Rolling gauges** — airspeed (km/h) and altitude (m) are odometer-style rolling drums with
  moving tapes, in both the minimal and the full HUD (`U`).
* **Desert battlefield** — eroded arid mountains (procedural noise shaped by a deterministic
  hydraulic-erosion simulation), dry river bed with palms, desert town, coast to the east.
* **61-mission campaign** (all unlocked, progress saved): training (takeoff, solo flight, landing,
  navigation, high-g, low-level, gunnery / ATS / ATA ranges), strikes and air battles (gun lines
  GUN 1-4, offshore oil rigs, convoys, SAM sites, fuel depots, hangars, patrol boats, artillery),
  **air-to-air refuelling** behind a tanker (mission 41), and ten **critical-infrastructure
  strikes** (power station, refinery, rail viaduct and supply train, comms masts, armoured train,
  arms factories, parked fighters, mobile launchers against the clock, ammunition dump, and a final
  strike that ends with a landing), plus ten **carrier missions** (see above). Objective targets are orange on the HUD and minimap with live
  counters; a debrief card shows time, kills, accuracy and hull at the end.
* **Dogfight Arena**: endless waves of AI bandits with wingmen, SAM sites, AAA flak and a town garrison.
* **Steady frame rate**: dynamic resolution lowers the 3D resolution in 10 % steps when frames drop
  below ~55 fps and raises it again when there is headroom (Settings → Dynamic resolution; the HUD
  stays sharp). `F3` shows simulation and HUD CPU time per frame.

## Real terrain

The real-world locations need elevation data, which is downloaded once (about 2 MB per location,
~27 MB for all 13) and stored in `assets/terrain/`:

* **On GitHub (one click):** *Actions → real terrain → Run workflow*. It downloads, converts and
  commits the files; the browser version and the next macOS release pick them up automatically.
* **In the game:** Settings → DOWNLOAD WORLD MAPS (uses the system's `curl`).
* **On your Mac:** `python3 tools/fetch_real_terrain.py` (or `--only everest,rio`). No installs needed.

Data: [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Mapzen "terrarium"
encoding) — derived from SRTM, GMTED2010, ETOPO1, NED and other open sources
([attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md)). Each location is
a 32 × 32 km square at ~31 m resolution. The sea and fjords are water; lakes above sea level are not rendered as water yet, and
buildings/roads are generated (not OpenStreetMap), so they are plausible rather than exact.

## Voice

All spoken lines (`assets/voice/*.mp3`, about 200 lines) are synthesised offline with
[Kokoro-82M](https://huggingface.co/hexgrad/Kokoro-82M) (Apache-2.0) through
[kokoro-onnx](https://github.com/thewh1teagle/kokoro-onnx) (MIT): `bf_isabella` (British English) for the captain,
AWACS and instructor, `bf_emma` for the tower, `af_bella` for your wingman. The game lists its own lines
(`build/skyfall_voicelines`), so after changing any text run:

```bash
pip install kokoro-onnx soundfile        # plus ffmpeg
python3 tools/make_voice.py --model-dir <folder with kokoro-v1.0.onnx and voices-v1.0.bin>
```

Only new or changed lines are recorded; stale files are removed.

## Browser version (WebAssembly)

The same C++ code compiles to WebAssembly with Emscripten; raylib targets WebGL 2 (OpenGL ES 3.0)
and the GLSL 3.30 shaders are converted to GLSL ES 3.00 at load time (`src/render/ShaderLoad.hpp`).
The terrain build and light bakes run single-threaded in the browser (multi-threaded natively), so
world generation takes a few seconds longer. Settings are not saved between browser sessions.
`.github/workflows/pages.yml` builds and publishes it to GitHub Pages on every push.

```bash
./scripts/build_web.sh          # needs Emscripten (brew install emscripten); serves http://localhost:8000
```

## Build & run (macOS)

```bash
xcode-select --install          # only if the Command Line Tools are missing
brew install cmake raylib       # raylib optional: CMake downloads 5.5 automatically if absent
cd SKYFALL
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/skyfall
```

Or simply `./scripts/build_mac.sh` (configure + build + test + launch; it also works around a
Command Line Tools linker/SDK mismatch by falling back to an older installed SDK). Afterwards,
`./scripts/run.sh` rebuilds incrementally and launches in seconds. Every run writes
`logs/skyfall.log` (GPU info, warnings, and a frame-time summary on exit).

Useful flags: `--mode arena` / `--mode m01` (skip the menu), `--size 1440x900`, `--windowed`,
`--quality 0|1|2|3` (3 = Ultra HD), `--bench 30` (writes `benchmarks/benchmark.json` + `frame_times.csv`),
`--capture shots/` (scripted screenshots from the real renderer).

## Controls

| Key | Action | Key | Action |
|---|---|---|---|
| `W` / `S` | throttle up / down (full = afterburner) | `E` | engine start |
| `↓` / `↑` | pitch up / down (stick back = ↓) | `B` | parking brake |
| `←` / `→` | roll | `Left Ctrl` (hold) | wheel brakes / air brake |
| `A` / `D` | yaw / nosewheel steering | `G` | landing gear |
| `Space` / left mouse | cannon | `F` | missile (lock first) |
| `X` | flares | `T` / `Tab` | cycle target |
| `Q` / `1` / `2` | swap / select ATA (air-to-air) or ATS (air-to-surface) missiles | double-tap `←`/`→` | aileron roll |
| `Enter` / `Space` | skip the crash replay | | |
| `U` | HUD style (minimal / full) | `Cmd+Q` | quit immediately |
| Hangar `←` / `→` | change camouflage | | |
| `C` | camera: cockpit → chase → external | right-drag, `I J K L` | look / orbit, wheel = zoom |
| `R` | reset / respawn | `H` | help overlay |
| `F3` | telemetry (p50/p95/p99 frame times, physics) | `F5` | time of day |
| `F12` | screenshot | `Esc` / `P` | pause menu (Esc again resumes) |

Gamepad: left stick fly, triggers throttle, RB cannon, LB missile, A flares, B target,
X gear, Y camera, d-pad yaw.

## Engineering highlights

* `src/sim` — raylib-free physics: ISA atmosphere, lift `L = ½ρV²S·CL(α)`, drag
  `D = ½ρV²S·CD(α)`, spooling engine + afterburner with density lapse, gravity, ground reaction,
  brakes and tyre friction, touchdown/crash rules, quaternion attitude with an assisted
  fly-by-wire (G-command pitch, AoA limiter). Semi-implicit Euler at a fixed **120 Hz** with a
  bounded accumulator (no spiral of death). Bit-for-bit deterministic (tested).
* `src/combat` — deterministic battle sandbox: AI pilots, lock-on, PN missiles, flares, guns,
  ground units, events.
* `src/mission` — reusable mission FSM driven by telemetry predicates.
* `src/sim/Terrain` — procedural heightfield + **particle hydraulic erosion** (700k droplets,
  deterministic) on a 2049² grid (15.6 m); physics and every mesh sample the same grid.
* `src/render` — HDR pipeline: analytic sky, height fog shared by every shader; terrain with
  three LODs (15.6 m chunks built lazily near the camera), a 2048² terrain-data texture with
  per-pixel normals, horizon-based ambient occlusion and **soft sun shadows baked by marching the
  heightfield** (multi-threaded, ~0.2 s, rebaked on time-of-day change); desert material shader
  (strata, sediment in gullies, desert pavement, scrub); sea with depth colour; cumulus sprites
  with baked shading; sun shadow map for aircraft; aircraft with airfoil-section wings,
  **voxel-baked per-vertex ambient occlusion** and a clear-coat paint BRDF (GGX + Smith);
  picture-in-picture second view; afterburners, smoke, explosions, flares, tracers, bloom + ACES.
* `src/ui` — Battlefield-style HUD (markers, lock box, gun pipper, minimap, kill feed, warnings),
  virtual cockpit with five live displays and a conformal HUD in the combiner.
* `src/audio` — fully procedural sound (no sample files).
* `src/telemetry/Replay` — 30 Hz flight recorder and crash/shoot-down analysis.
* `tests/` — 31 deterministic tests (`ctest`).

Read more: [Architecture](docs/ARCHITECTURE.md) · [Physics](docs/PHYSICS.md) ·
[Aircraft data](docs/AIRCRAFT.md) · [Roadmap & checklist](docs/ROADMAP.md) ·
[Benchmarks](docs/BENCHMARKS.md) · [Interview notes](docs/INTERVIEW_NOTES.md)

## Honest limits

This is a prototype, not a certified simulator and not AAA art. Aircraft performance values are
public rounded figures plus tuned illustrative aerodynamics; rotational dynamics use a
documented rate-command approximation rather than a full moment model; terrain is fictional and
procedural and fictional. Aircraft are original procedural meshes (not licensed models), so they
are clean silhouettes rather than photoreal assets. OpenGL is deprecated on macOS; a Metal renderer
is a roadmap item. The Computer
Vision Laboratory is planned, not implemented yet.

## License

MIT (see `LICENSE`). Fonts are loaded from your operating system at runtime and are not
redistributed.
