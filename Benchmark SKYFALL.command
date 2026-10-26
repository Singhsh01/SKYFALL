#!/bin/bash
# Double-click in Finder: builds SKYFALL (Release) and measures it on this Mac: the arena,
# mission 52, operation 3 and operation 7 in High, Ultra, Cinematic 4K and Performance 120
# (about 15 minutes; the game window opens and closes for each run - leave the Mac alone and
# plugged in). Writes benchmarks/report-<date>.md.
cd "$(dirname "$0")" || exit 1
mkdir -p benchmarks
LOG="benchmarks/last-run.log"
{
  echo "== build $(date)"
  ./scripts/build_mac.sh --configure-only && cmake --build build --parallel 4 --target skyfall || { echo "BUILD FAILED"; exit 1; }
  echo "== benchmark $(date)"
  ./scripts/benchmark.sh
  echo "== done $(date)"
} 2>&1 | tee "$LOG"
echo
echo "Done. The report is in the benchmarks folder. You can close this window."
read -n 1 -s -r -p "Press any key to close..."
