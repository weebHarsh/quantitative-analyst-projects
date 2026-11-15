#!/usr/bin/env bash
# One command: build Release, generate (and cache) the SYNTHETIC benchmark feed, run the benchmark.
#   bash bench/run_bench.sh            (or: make bench)
# Feed cache : build/bench-feed/ (override with MD_BENCH_DIR). Never committed.
# Results    : build/bench-results.txt (key=value lines + the JSON line). Never committed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
BUILD_DIR="${MD_BUILD_DIR:-build}"
FEED_DIR="${MD_BENCH_DIR:-$BUILD_DIR/bench-feed}"
SEED=7
HOURS=3.8
REPS="${MD_BENCH_REPS:-5}"
WARMUP="${MD_BENCH_WARMUP:-1}"

echo "== build (Release, no sanitizers, -Werror) =="
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DMD_SANITIZE=OFF -DMD_WERROR=ON >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 2)" --target md_bench mdtool >/dev/null

echo "== benchmark feed =="
STAMP="$FEED_DIR/.params"
WANT="generator=tools/gen_feeds.py seed=$SEED hours=$HOURS"
if [[ -f "$FEED_DIR/venueA.jsonl" && -f "$FEED_DIR/venueB.csv" && -f "$FEED_DIR/meta.json" \
      && -f "$STAMP" && "$(cat "$STAMP")" == "$WANT" ]]; then
  echo "using cached feed in $FEED_DIR"
else
  echo "generating feed (seed $SEED, $HOURS h) into $FEED_DIR (about 15 s) ..."
  rm -rf "$FEED_DIR"
  mkdir -p "$FEED_DIR"
  python3 tools/gen_feeds.py --seed "$SEED" --hours "$HOURS" --no-verify --out "$FEED_DIR" >/dev/null
  echo "$WANT" > "$STAMP"
fi
echo "A: $(wc -l < "$FEED_DIR/venueA.jsonl") lines, B: $(($(wc -l < "$FEED_DIR/venueB.csv") - 1)) rows, $(du -sh "$FEED_DIR" | cut -f1) on disk (synthetic data)"

echo "== run (reps=$REPS, warm-up=$WARMUP) =="
JSON="$("$BUILD_DIR/md_bench" --a "$FEED_DIR/venueA.jsonl" --b "$FEED_DIR/venueB.csv" \
        --reps "$REPS" --warmup "$WARMUP" 2> >(tee "$BUILD_DIR/bench-report.txt" >&2))"
{
  echo "# key=value; written by bench/run_bench.sh; not committed"
  echo "$JSON" | python3 -c '
import json, sys
r = json.load(sys.stdin)
for k, v in r.items():
    if isinstance(v, dict):
        for k2, v2 in v.items():
            print(f"{k}.{k2}={v2}")
    else:
        print(f"{k}={v}")
'
  echo "json=$JSON"
} > "$BUILD_DIR/bench-results.txt"
echo "== wrote $BUILD_DIR/bench-results.txt =="
