#!/usr/bin/env bash
# Runs the full offline evaluation on the held-out TEST seeds and makes the
# report. Laptop only. Parameters were chosen on TUNE seeds (docs/DECISIONS.md).
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
R=$BUILD/apps/embercast_replay
OUT=results/raw/test
mkdir -p "$OUT"
ALL=ensemble,deterministic,front_nearest,oracle
$R --set test --policies $ALL --tag main --out $OUT/main.csv --log $OUT/main_decisions.jsonl --latency $OUT/main_latency.csv
for s in regime dropout30 late_scans stuck; do
  $R --set test --policies $ALL --stress $s --tag stress --out $OUT/stress_$s.csv
done
for k in 32 128 256; do
  $R --set test --policies ensemble,deterministic --K $k --tag ablation_K$k --out $OUT/abl_K$k.csv
done
$R --set test --policies ensemble,deterministic --no-filter --tag ablation_nofilter --out $OUT/abl_nofilter.csv
$R --set test --policies ensemble,deterministic --lambda 0 --tag ablation_lambda0 --out $OUT/abl_lambda0.csv
.venv/bin/python -m laptop.tools.evaluate --csv $OUT/main.csv $OUT/stress_*.csv $OUT/abl_*.csv --out results
