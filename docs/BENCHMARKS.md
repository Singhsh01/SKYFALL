# Benchmarks

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
