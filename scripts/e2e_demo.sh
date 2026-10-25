#!/usr/bin/env bash
# Scripted 60-second end-to-end demo test on localhost (no board, no browser).
# Episode = 120 ticks at 4 Hz (30 s); the run covers two episodes.
# Wind shift at tick 20, cut link at tick 60, restore at tick 90.
set -uo pipefail
cd "$(dirname "$0")/.."
BUILD=${BUILD:-build}
TMP=$(mktemp -d)
printf '20 wind_shift\n60 cut_link\n90 restore_link\n' > "$TMP/script.txt"
"$BUILD/apps/embercast_board_daemon" --port 7270 --stale-ms 1000 > "$TMP/daemon.log" 2>&1 &
D=$!
sleep 0.5
# GNU timeout is not on macOS; the run is stopped with kill below anyway.
TO=""; command -v timeout >/dev/null && TO="timeout 70"
$TO "$BUILD/apps/embercast_sim_stream" --board 127.0.0.1:7270 --rate 4 --dash-port 0 \
  --script "$TMP/script.txt" --rtt-out "$TMP/rtt.csv" > "$TMP/stream.log" 2>&1 &
S=$!
sleep 60
kill -INT $S 2>/dev/null; wait $S 2>/dev/null
kill -INT $D 2>/dev/null; wait $D 2>/dev/null
cat "$TMP/stream.log" "$TMP/daemon.log"
ok=1
grep -q "control wind_shift" "$TMP/stream.log" || ok=0
grep -q "control cut_link" "$TMP/stream.log" || ok=0
grep -q "state STALE" "$TMP/daemon.log" || ok=0
[ "$(grep -c 'connected to board' "$TMP/stream.log")" -ge 2 ] || ok=0   # reconnect after restore
grep -q "bad_crc 0" "$TMP/stream.log" || ok=0
[ "$ok" = 1 ] && echo "E2E PASS" || { echo "E2E FAIL"; exit 1; }
