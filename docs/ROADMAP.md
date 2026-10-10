# Requirements checklist & roadmap

Legend: ✅ implemented and exercised · 🟡 partial / in progress · ⬜ planned

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
| Crash & shoot-down replay with telemetry analysis | ✅ |
| SSAO, true volumetric clouds | ⬜ |
| Draw-call/LOD instrumentation | 🟡 | draw calls + world pass time in `F3` |
| Real-world terrain (Cesium etc.) | ⬜ separate heavy-engine track |
| Metal renderer (OpenGL is deprecated on macOS) | ⬜ |

## Campaign (G)

| # | Mission | Status |
|---|---|---|
| 01 | Cold Start & Takeoff | ✅ |
| 08/19 | Dogfight / multi-wave battle | ✅ via Dogfight Arena |
| 02–07, 09–18, 20 | | ⬜ catalogue defined in `Mission.cpp`, FSM ready |

## Vision laboratory (H) — ⬜ next major milestone

1. Export annotated frames (intrinsics, extrinsics, ground-truth target pose) from the renderer.
2. Optional OpenCV build flag; tracking + error vs ground truth; latency/lost-track stats.
3. Terrain image registration (features, RANSAC homography, reprojection error).
4. PnP pose estimation with confidence; COLMAP experiment offline.

## Platforms (I) — ⬜

iPhone native (Metal) port, remote-play streaming, VR — after desktop performance is measured.
