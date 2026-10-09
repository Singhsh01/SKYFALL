# Design decisions & trade-offs (interview notes)

**Why raylib and not Unreal/Unity?** Small, debuggable C++ codebase that builds in seconds on an
8 GB laptop; everything from the integrator to the tone mapper is code I can explain. The cost is
writing my own terrain, effects, HUD and post-processing — which is exactly the portfolio point.

**Determinism.** Fixed 120 Hz steps, no wall-clock in the simulation, a seeded xorshift RNG, and
pure functions for terrain. Same inputs → bit-identical state; this enables replays, regression
tests and future computer-vision ground truth.

**Spiral of death.** The accumulator is capped at 10 steps per frame; after a long stall the backlog
is discarded and counted (`dropped` in the F3 overlay) instead of freezing the app.

**Flight model honesty.** Translational forces are physical; rotation is a G-command fly-by-wire
approximation. I would rather ship a stable, documented approximation than an unvalidated
"realistic" moment model; the next milestone replaces it with derivatives + inertia (PHYSICS.md).

**Rendering budget on 8 GB.** No texture files: a single 256² tiling noise texture drives terrain
detail, water normals, clouds, cirrus, weathering and camouflage. Terrain is 256 chunks × 2 LODs with
skirts plus a coarse far ring. Scene resolution follows window points × quality scale while the HUD
stays native Retina. HDR + bloom run at ½ and ¼ resolution.

**Precision.** A cockpit 10 km from the origin has float precision ≈ 1 mm, which made bezels and
screens z-fight. The cockpit pass therefore renders in camera-relative coordinates.

**Missiles.** True proportional navigation (`a = N·V_c·(Ω × r̂)`) with a pure-pursuit blend just
after launch, a seeker gimbal limit, motor burn then drag, and proximity fusing on a swept segment
so fast missiles cannot tunnel through targets. Flares create probabilistic decoys resolved to a
concrete flare the seeker then tracks.

**Testing.** 24 headless tests: ISA, quaternion round-trips, lift curve, engine start, brakes,
gear interlock, scripted takeoff, determinism, fixed stepper, level flight, G-limited turns,
terrain impact, Mission 01 (cannot complete on time alone; completes with real flying), PN hit,
flare decoy, AI terrain avoidance, arena waves, terrain invariants, camera cycling, settings
round-trip, percentiles, aircraft data.
