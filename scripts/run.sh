#!/usr/bin/env bash
# Rebuild incrementally (fast) and launch. Logs go to logs/skyfall.log.
set -e
cd "$(dirname "$0")/.."
[ -f build/Makefile ] || exec ./scripts/build_mac.sh "$@"
cmake --build build --parallel 2
exec ./build/skyfall "$@"
