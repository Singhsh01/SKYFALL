#!/usr/bin/env bash
# Measure SKYFALL on this machine: the same scripted scenes in every graphics preset.
# Writes benchmarks/report-<date>.md (and keeps each run's frame_times / json next to it).
#
#   ./scripts/benchmark.sh              arena + mission 52 (rain, carrier) in High, Ultra,
#                                       Cinematic 4K and Performance 120, 20 s each
#   SECONDS_PER_RUN=30 ./scripts/benchmark.sh
set -euo pipefail
cd "$(dirname "$0")/.."
[ -x build/skyfall ] || { echo "Build first: ./scripts/build_mac.sh --configure-only && cmake --build build"; exit 1; }
DUR="${SECONDS_PER_RUN:-20}"
STAMP="$(date +%Y%m%d-%H%M)"
DIR="benchmarks/$STAMP"
mkdir -p "$DIR"
REPORT="benchmarks/report-$STAMP.md"
{
  echo "# SKYFALL benchmark $STAMP"
  echo
  echo "* Version: $(git describe --tags --always --dirty 2>/dev/null || echo unknown)"
  if command -v sysctl >/dev/null; then
    echo "* Machine: $(sysctl -n hw.model 2>/dev/null) - $(sysctl -n machdep.cpu.brand_string 2>/dev/null) - $(( $(sysctl -n hw.memsize 2>/dev/null || echo 0) / 1073741824 )) GB"
  fi
  echo "* Window 1280x720 points; frame times measured in the game loop (VSync off, no cap); $DUR s per run after warm-up"
  echo
  echo "| Scene | Preset | Scene resolution | mean ms | fps | p50 | p95 | p99 | draw calls | triangles |"
  echo "|---|---|---|---|---|---|---|---|---|---|"
} > "$REPORT"
for scene in arena c52; do
  for q in 2 3 4 5; do
    log="$DIR/${scene}_q$q.txt"
    ./build/skyfall --bench "$DUR" --mode "$scene" --quality "$q" --size 1280x720 --windowed > "$log" 2>&1 || true
    cp benchmarks/benchmark.json "$DIR/${scene}_q$q.json" 2>/dev/null || true
    b=$(grep -E "^benchmark:" "$log" | tail -1)
    sc=$(grep -E "^scene " "$log" | tail -1)
    mean=$(echo "$b" | sed -E 's/.*mean ([0-9.]+) ms \(([0-9]+) fps\), p50 ([0-9.]+), p95 ([0-9.]+), p99 ([0-9.]+).*/\1|\2|\3|\4|\5/')
    res=$(echo "$sc" | sed -E 's/scene ([0-9x]+), quality ([^,]+), draw calls ([0-9]+), triangles ([0-9]+k).*/\1|\2|\3|\4/')
    IFS='|' read -r m f p50 p95 p99 <<< "$mean"
    IFS='|' read -r r qn dc tr <<< "$res"
    echo "| $scene | $qn | $r | $m | $f | $p50 | $p95 | $p99 | $dc | $tr |" >> "$REPORT"
    echo "$scene $qn: $b"
  done
done
# one profiled run for the per-pass breakdown (the profiler waits for the GPU after every pass,
# so its frame times are longer than the timing runs above and are not used for them)
SKYFALL_PROFILE=1 ./build/skyfall --bench "$DUR" --mode c52 --quality 3 --size 1280x720 --windowed > "$DIR/c52_q3_profile.txt" 2>&1 || true
{
  echo
  echo "Per-pass GPU time, mission 52, Ultra (profiled run, passes serialised):"
  echo
  echo '```'
  sed -n '/gpu passes/,/^display/p' "$DIR/c52_q3_profile.txt" | sed '$d'
  echo '```'
} >> "$REPORT"
echo
echo "Report: $REPORT"
