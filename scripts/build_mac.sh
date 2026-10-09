#!/usr/bin/env bash
# Configure, build, test and launch SKYFALL on macOS (Apple Clang + CMake + raylib).
set -euo pipefail
cd "$(dirname "$0")/.."
command -v cmake >/dev/null || { echo "cmake missing: brew install cmake"; exit 1; }
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2          # 2 jobs keeps memory use low on 8 GB machines
ctest --test-dir build --output-on-failure
exec ./build/skyfall "$@"
