# Benchmarks

## How to measure on your Mac

```bash
./build/skyfall --bench 30            # 30 s scripted Dogfight Arena flight, chase camera
cat benchmarks/benchmark.json         # mean / p50 / p95 / p99 frame time, fps, resolution
open benchmarks/frame_times.csv       # every frame, for plotting
```

Repeat with `--quality 0`, `--quality 1`, `--quality 2` and with `--size 1280x720`. In-game,
press `F3` for live p50/p95/p99, world-pass CPU time, draw calls and particle count.

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
