# Architecture

```
                         +--------------------------------------------+
                         |  app/  Game: screens, modes, fixed-step     |
                         |        loop, cameras, capture & benchmark   |
                         +-------+------------------+-----------------+
                                 |                  |
          +----------------------+---+        +-----+-----------------------+
          |  SIMULATION (no raylib)  |        |  PRESENTATION (raylib)      |
          |                          |        |                             |
          |  sim/       FlightModel  |  state |  render/  Renderer (HDR)    |
          |             Atmosphere   +------->+           WorldMeshes, Jets |
          |             Terrain      |  (read |           Effects, Shaders  |
          |  aircraft/  Profiles x8  |   only)|  ui/      Hud, Cockpit,     |
          |  combat/    Battle, AI   |        |           Menus, Fonts      |
          |  mission/   Mission FSM, |        |  audio/   procedural synth  |
          |             Campaign,    |        |                             |
          |             Training,    |        |                             |
          |             CampaignWorld|        |                             |
          +-------------+------------+        +-------------+---------------+
                        |                                   |
                        +-----------------+-----------------+
                                          |
                          telemetry/ FrameStats (p50/p95/p99, CSV/JSON)
                          tests/     skyfall_tests + skyfall_training_tests (ctest)
```

## Frame

1. `Input` samples keyboard / gamepad / mouse (smoothed axes, dead zones, expo curves).
2. Discrete commands (engine, brakes, gear, missile, flares, target) are applied once.
3. `FixedStepper` converts real elapsed time into N × 1/120 s steps (max 10 per frame; the
   backlog is dropped after a stall and counted). Each step runs `Battle::update`, which steps
   every `FlightModel`, the AI, missiles, bullets, flares and ground units, then
   `Mission::update`.
4. Combat events (explosions, kills, launches, hits) feed the effects system, audio and HUD.
5. Cameras update (cockpit / chase / external, smoothing, FOV, shake).
6. Rendering:
   * cockpit MFD render textures (only in cockpit view)
   * HDR scene target (RGBA16F): sky → terrain chunks → sea → runway → airbase/town/trees →
     ground units → aircraft (+ afterburner plumes) → missiles → blob shadows → cloud sheets →
     cloud puffs → particles/ribbons (alpha, then additive)
   * cockpit overlay pass in **camera-relative coordinates** (mm-scale geometry stays precise
     10 km from the origin) with a cleared depth buffer
   * bloom (bright-pass, ½ and ¼ resolution separable blur) + ACES tone map + vignette +
     G-effects + grain → back buffer at native (Retina) resolution
   * lens flare, HUD, menus in 2D on top

## Why these boundaries

* **Physics never includes raylib.** It can be tested headless, stepped deterministically,
  and reused by a future Metal/Unreal front-end.
* **Shaders share one atmosphere function** (`Shaders.hpp::kAtmosphere`) and the CPU mirrors it
  (`Lighting.hpp`) so particles, sprites, sky, fog, terrain and aircraft agree on colour.
* **Assets are generated, not loaded.** Terrain, trees, town, airbase, aircraft, textures,
  sprites and audio are procedural — tiny repository, no licensing risk, fast iteration on an
  8 GB machine. Licensed models can replace the procedural ones later behind the same interfaces.
* **Scene resolution is decoupled from window pixels**: the 3D scene renders at the window size
  in points × quality scale; the HUD is drawn at native Retina resolution.

## World detail

* `sim/WorldDetail` (called at the end of `WorldLayout::build`) places props (`SceneProp`), night
  lights (`LightPoint`), military sites and traffic lanes / cars on every map, deterministically.
  Cars are a pure function of time (`WorldLayout::carAt`), so replays and every view agree.
* `render/Props` merges all props into culled static meshes (a few draw calls per km²); traffic
  is one dynamic mesh rebuilt per frame; windows carry a vertex-alpha flag the lit shader turns
  into glass by day and lit windows at night (`Lighting::night`).

## Training Academy and the campaign on every location

* `mission/Training` builds a `Course` (sections, gates, obstacles, tanker, formation loop,
  approach) from a `Terrain` + `WorldLayout`, deterministically; `TrainingSession` runs it once
  per fixed step *before* `Mission::update` and owns gates, tasks, scoring, checkpoints and the
  instructor's radio calls. Its `Mission` (one step per section) is an ordinary mission FSM whose
  predicates read the session, so the HUD and menus treat it like any other mission.
* `Terrain::setObstacles` installs the course's pillars / arches / masts / deck: the flight model
  collides with them and the renderer rebuilds their meshes when the obstacle version changes.
* `mission/CampaignWorld` fits a desert `CampaignMissionDef` to the loaded location
  (`adaptCampaignMission`: symmetry, target placement, heights, tanker, carrier or airborne start,
  landing) and describes what the location offers (`CampaignVariant`: sea, carrier, railway,
  mission 2 / 3 variants). `retitleCampaignMission` applies only the text and kind changes, so the
  voice pack can list every variant's lines. The same `CampaignDirector` runs it.
* `app/Academy.cpp` holds the Academy screen and the training glue
  (radio routing by priority, gate markers, retries, debrief); progress lives in `Settings`
  (`mission/Progress`).
