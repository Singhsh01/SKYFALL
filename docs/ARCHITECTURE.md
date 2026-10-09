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
          |  mission/   Mission FSM  |        |  audio/   procedural synth  |
          +-------------+------------+        +-------------+---------------+
                        |                                   |
                        +-----------------+-----------------+
                                          |
                          telemetry/ FrameStats (p50/p95/p99, CSV/JSON)
                          tests/     24 deterministic tests (ctest)
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
