#!/usr/bin/env bash
# Live demo with the engine on the UNO Q, over the USB cable (adb). No Wi-Fi.
# The USER runs this script (D-026). It starts board_daemon on the board,
# forwards TCP 7070 over adb, and runs sim_stream + dashboard on the laptop.
#   ./scripts/demo_board_usb.sh                 cpu_opt, 4 threads
#   BACKEND=opencl ./scripts/demo_board_usb.sh  engine on the board GPU
# Needs: adb, the board binaries in ~/embercast/build on the board
# (see docs/HARDWARE.md section 4, "Cross-build with zig").
# Then open http://127.0.0.1:8000/
set -euo pipefail
cd "$(dirname "$0")/.."
BACKEND=${BACKEND:-cpu_opt}
THREADS=${THREADS:-4}
SEED=${SEED:-1000}
RATE=${RATE:-4}
BOARD_DIR=${BOARD_DIR:-embercast}
BIN=build
[ "$BACKEND" = "opencl" ] && BIN=build-cl
adb get-state >/dev/null || { echo "No board on adb. Connect the UNO Q with USB."; exit 1; }
adb shell "pkill -INT -f embercast_board_daemon" 2>/dev/null || true
adb forward tcp:7070 tcp:7070
cleanup() {
  adb shell "pkill -INT -f embercast_board_daemon" 2>/dev/null || true
  adb forward --remove tcp:7070 2>/dev/null || true
}
trap cleanup EXIT INT TERM
adb shell "cd ~/$BOARD_DIR && mkdir -p results/raw/board && exec ./$BIN/apps/embercast_board_daemon \
  --listen 127.0.0.1 --port 7070 --seed $SEED --backend $BACKEND --threads $THREADS \
  --log results/raw/board/live_decisions.jsonl --bench-out results/raw/board/live_daemon_latency.csv" &
sleep 1.5
ENGINE=daemon BOARD=127.0.0.1:7070 SEED=$SEED RATE=$RATE ./scripts/demo_laptop.sh
