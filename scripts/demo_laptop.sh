#!/usr/bin/env bash
# One-command laptop demo (AC6). Laptop only: it never connects to the board.
#   ./scripts/demo_laptop.sh                 engine in board_daemon on localhost
#   ENGINE=inprocess ./scripts/demo_laptop.sh   engine inside sim_stream (no daemon)
#   BOARD=<BOARD_IP>:7070 ./scripts/demo_laptop.sh  use a daemon that YOU started on the board
# Then open http://127.0.0.1:8000/
set -euo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
SEED=${SEED:-1000}
RATE=${RATE:-4}
if [ ! -x "$BUILD/apps/embercast_sim_stream" ]; then
  cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
  cmake --build "$BUILD" -j
fi
if [ ! -x .venv/bin/python ]; then
  uv venv .venv
  uv pip install --python .venv/bin/python -r requirements-dev.txt
fi
pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done; }
trap cleanup EXIT INT TERM
mkdir -p results/raw/live
if [ "${ENGINE:-daemon}" = "inprocess" ]; then
  TARGET=none
elif [ -n "${BOARD:-}" ]; then
  TARGET=$BOARD
else
  "$BUILD/apps/embercast_board_daemon" --listen 127.0.0.1 --port 7070 --seed "$SEED" \
    --log results/raw/live/daemon_decisions.jsonl --bench-out results/raw/live/daemon_latency.csv &
  pids+=($!)
  TARGET=127.0.0.1:7070
  sleep 0.5
fi
"$BUILD/apps/embercast_sim_stream" --board "$TARGET" --seed "$SEED" --rate "$RATE" \
  --rtt-out results/raw/live/rtt.csv &
pids+=($!)
.venv/bin/python -m laptop.dashboard.server &
pids+=($!)
echo "EmberCast demo: open http://127.0.0.1:8000/   (Simulation only. Not for operational use.)"
wait
