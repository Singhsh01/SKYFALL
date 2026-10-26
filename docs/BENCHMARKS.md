# Benchmarks

## v1.1 rendering work (draw-call merging, eye adaptation, PBR props, adaptive detail)

All figures from the cloud container (Mesa software rasteriser, `--bench 12 --size 640x360
--quality 2`, High, `SKYFALL_FULL_DETAIL=1` so the adaptive detail drop does not flatter the
"after" column). Draw calls are now counted exactly: every mesh draw passes one counter, the
shadow pass included (the v1.0 table below used hand-maintained counts, which missed the jets'
moving parts). Software frame times say nothing about a GPU; the draw-call column is what carries
over, because on macOS each OpenGL draw call costs CPU time in the driver.

| Scene | Draw calls v1.0 | Draw calls now | Change |
|---|---|---|---|
| Dogfight Arena | 238 | 206 | -13 % |
| Mission 52 (carrier, rain) | 195 | 160 | -18 % |
| Mission 14 | 262 | 207 | -21 % |
| Operation 7 (C-17 escort) | 249 | 185 | -26 % |
| Operation 3 (airbase defence, on the airfield) | 332 | 215 | -35 % |

Where the draws went (`SKYFALL_PROFILE=1` prints draws per pass): far terrain is now drawn as
4 x 4-chunk blocks (one draw instead of up to 16) whenever every chunk of the block would use its
125 m mesh anyway, so the image is unchanged (a pixel comparison of the same view before and after
differs by 0); trees are drawn 2 x 2 chunks per draw. In operation 3 the terrain pass went from
174 to 84 draws. Triangles rise slightly (725k vs 708k in the arena) because a merged block is
culled as a whole.

**Eye adaptation (new, on by default, Settings → Auto exposure).** Each frame the HDR scene is
reduced on the GPU to a 64 x 36 log-luminance image and then to one centre-weighted geometric
mean; a 1 x 1 ping-pong target adapts towards it (faster for glare than for darkness). The
exposure follows it partially (60 %, limited to -0.55 / +0.6 stops) around a per-time-of-day key
measured in this container: exposed log2 luminance -1.0 to -1.35 in daylight, -1.2 to -2.0 at
golden hour, -1.8 to -2.0 in the storm, -1.8 to -2.9 at dusk, -4.1 to -4.7 at night
(`SKYFALL_EXPOSURE_LOG=1`, `SKYFALL_TOD=n`). So dusk stays dusk; what changes is the spread within
a time of day (a sunlit plain, a canyon in shadow, the cockpit). Cost: three tiny passes and no
CPU read-back; in the software rasteriser the post pass grew from 12.7 to 14.5 ms (mission 52),
which is not a GPU figure.

**PBR for the world.** Buildings, vehicles, ships, props and missiles now use the same GGX /
Smith / Schlick metal-roughness model as the aircraft (with a Fresnel sky reflection), in place
of Blinn-Phong; the glossiness each object already had maps to roughness, so the look is close
to before (compare `docs/screenshots`).

**Adaptive detail (FrameGovernor).** When the 3D resolution has already dropped to 50 % and the
frame is still over budget, the governor sheds scene detail in up to three steps (view distances
of trees, buildings and fine terrain to 80 / 65 / 50 %, fewer rain drops, no shadow map from step
2), at most one step every 3 s, and restores detail before resolution once there is clear
headroom. The FPS overlay (`F3`) shows `DETAIL -n`. In the software rasteriser it reached step 3 in
operation 3: 158 draw calls, 722k triangles, mean 156 ms vs 175 ms at full detail. Unit-tested
(`frame_governor_lowers_resolution_then_detail_and_restores_in_reverse`).

**Not measured: any of this on the MacBook Air.** Run `./scripts/benchmark.sh` or
`./build/skyfall --bench 30 --mode op03` (add `SKYFALL_PROFILE=1` for the per-pass breakdown).

## v1.0 (combat operations, new aircraft, living airbases, 40 paint schemes)

Measured in the cloud container with Mesa's **software** rasteriser (`--bench 20 --size 640x360
--quality 2`, High). Frame times here say nothing about a GPU; what carries over to the Mac is the
CPU cost of the simulation and HUD and the draw-call / triangle load each scene submits.

| Scene | Draw calls | Triangles | Simulation (CPU) | HUD (CPU) |
|---|---|---|---|---|
| Dogfight Arena | 190 (212 before v1.0) | 543k (460k) | 0.11 ms | 0.25 ms |
| Mission 52 (carrier, rain) | 192 (187) | 288k (288k) | 0.02 ms | 0.57 ms |
| Mission 14 | 243 (232) | 786k (803k) | 0.04 ms | 0.60 ms |
| Operation 7 (C-17 escort, Erg Chebbi) | 246 | 860k | 0.02 ms | 0.72 ms |
| Operation 3 (airbase defence, Glencoe, on the airfield) | 329 | 887k | 0.03 ms | 0.70 ms |

Any operation can now be benchmarked with `--mode op01` … `--mode op20`.

What changed the load: the arena's extra triangles are the new Hornet and Typhoon models; the
operations add wingmen, the C-17, cargo and the asset units. The
airfield in operation 3 is the busiest view in the game: the living airbase (C-17 unloading,
forklift, tugs, fuel truck, taxiing and departing jets, helicopter circuit, rotating radars) is
drawn object by object, which is why that scene has the most draw calls of those measured; it is
culled by distance like the rest of the scenery. The paint markings are computed in the jet shader (no
textures, no extra draw calls).

**Not measured: frame rates on the MacBook Air** for v1.0 — no 120 fps figure is claimed. Run
`./scripts/benchmark.sh` (or `./build/skyfall --bench 30 --mode op03`) on the Mac to get them;
if the airfield scenes are the ones that miss the target, the next step is instancing the
airbase vehicles into one draw call per mesh.

## v0.9: how to measure all presets

```bash
./scripts/make_app.sh --no-build || ./scripts/build_mac.sh --configure-only && cmake --build build
./scripts/benchmark.sh                 # or VS Code: Run Task → "SKYFALL: Benchmark all presets"
```

`scripts/benchmark.sh` flies the Dogfight Arena and mission 52 (carrier, rain) in High, Ultra,
Cinematic 4K and Performance 120 for 20 s each (VSync off, no cap: timing runs, no per-pass GPU
waits), then one profiled run for the per-pass breakdown, and writes
`benchmarks/report-<date>.md`. The profiled run waits for the GPU after every pass, so its frame
times are longer than the timing runs and are never compared with them.

### What has been measured so far

| Where | Scene | Setting | Result |
|---|---|---|---|
| MacBook Air M1 (before v0.9, from your `bench.txt`) | mission 52, 2560×1440, MSAA 4x | **profiled** run | mean 26.3 ms, p95 36.7 ms, p99 39.8 ms; passes: scenery 6.9, post 5.7, terrain + sea 3.6, clouds 2.8, targets 2.3, HUD (CPU) 2.1, rain 1.9, sky 1.4, shadow 1.2 ms |
| Cloud container (software GL, 640×360 scene) | arena / mission 52 / mission 14, Ultra | draw calls per frame | before the v0.9 culling work 273 / 227 / 280 → after it 218 / 186 / 235 → with the v0.9 world detail (compound, bases, traffic, lamps) 216 / 187 / 238 |
| Cloud container | mission 52, Ultra | frame time | 153 ms mean (software rasteriser: says nothing about a GPU) |

**Not measured yet: v0.9 frame times on the Mac** in any preset, so no 4K or 120 fps figure is
claimed. Run `./scripts/benchmark.sh` on the Mac and the report has the numbers. What the work
targets: the profile above shows the scenery and post-processing passes dominate on the M1 at
1440p, which is why v0.9 culls scenery by frustum and distance per preset, merges all new props
into a few meshes per km², keeps traffic in one draw call, and lets Performance 120 render at up
to 1600 px wide with an 8.3 ms budget for dynamic resolution. Whether an M1 Air holds 120 fps or
native 4K in a given scene is exactly what the benchmark will show; a fanless M1 at native 4K is
unlikely to.


## How to measure on your Mac

```bash
./build/skyfall --bench 30            # 30 s scripted Dogfight Arena flight, chase camera
cat benchmarks/benchmark.json         # mean / p50 / p95 / p99 frame time, fps, resolution
open benchmarks/frame_times.csv       # every frame, for plotting
```

Repeat with `--quality 0`, `--quality 1`, `--quality 2` and with `--size 1280x720`. In-game,
press `F3` for live p50/p95/p99, world-pass CPU time, draw calls and particle count.

Any mode can be benchmarked: `--bench 20 --mode m03` (missions `m01`-`m03`, `c04`-`c40`, `arena`).
The run also prints the CPU time of the simulation and of the HUD per frame.

### Mission 03 slowdown (fixed in v0.5)

The landing guidance (glide slope / localiser) built a temporary `Terrain` object every frame to
read the runway position. Since v0.4 a `Terrain` runs the full hydraulic-erosion world build
(~0.5 s on the MacBook Air), so Mission 03 dropped to 1-2 fps and the simulation, limited to 0.25 s
per frame, ran in slow motion. The HUD now receives the runway from the game. Measured in the
container after the fix (CPU only, software GL): HUD 1.03 ms per frame in Mission 03 vs 1.13 ms in
the arena; simulation 0.03-0.14 ms per frame. The unit test `campaign_battle_step_fits_the_frame_budget`
keeps the busiest campaign battles (19 jets, or 15 ground targets) under 1 ms per physics step
(measured 0.002-0.04 ms).

## Recorded results

| Machine | Renderer | Res | Quality | mean | p50 | p95 | p99 |
|---|---|---|---|---|---|---|---|
| Cloud container, 2 vCPU, no GPU | Mesa llvmpipe (software) | 1280×720 | High | 265 ms | 265 ms | 280 ms | 284 ms |
| MacBook Air 8 GB (v0.3, real play session, 138k frames) | Apple GPU, OpenGL 4.1 | 1680x1050 | High | 15.1 ms (66 fps) | 14.6 ms | 20.2 ms | 30.2 ms |
| MacBook Air 8 GB (v0.4: erosion terrain, PiP camera) | Apple GPU | | | *to be measured — quit the game normally and read logs/skyfall.log* | | | |

Browser build: in the container (Chromium, software WebGL, 2 vCPU) world generation takes ~14 s
and the frame rate is not meaningful; on a real GPU it is expected to sit below the native build.
Measure it in the browser with `F3`.

The container number only proves the pipeline runs end to end; a software rasteriser is 50–100×
slower than any GPU and says nothing about macOS performance. No FPS figure for the Mac is
claimed until it has been measured there.
