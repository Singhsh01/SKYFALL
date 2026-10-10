#!/usr/bin/env bash
# Build the browser (WebAssembly) version and serve it locally.
# Needs Emscripten: `brew install emscripten` (macOS) or `sudo apt install emscripten` (Ubuntu 24.04).
set -e
cd "$(dirname "$0")/.."
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DSKYFALL_BUILD_TESTS=OFF
cmake --build build-web --parallel 4
echo "Open http://localhost:8000/  (Ctrl+C to stop)"
cd build-web && python3 -m http.server 8000
