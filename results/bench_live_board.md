# Live run, engine on the UNO Q over USB (adb forward)

Made by `laptop/tools/bench_report.py` from: `results/raw/live/rtt.csv`, `results/raw/board/live_daemon_latency.csv`

| Run | Stage | Count | Mean ms | p50 ms | p95 ms | p99 ms | p99.9 ms | Max ms |
|---|---|---|---|---|---|---|---|---|
| daemon | decision_policy | 70 | 0.378 | 0.373 | 0.557 | 0.594 | n/a (count < 10000) | 0.594 |
| daemon | decision_rollout | 70 | 58.096 | 61.869 | 76.923 | 84.284 | n/a (count < 10000) | 84.284 |
| daemon | decision_total | 70 | 68.187 | 72.794 | 93.650 | 95.268 | n/a (count < 10000) | 95.268 |
| daemon | tick_filter | 346 | 8.680 | 8.358 | 16.533 | 19.282 | n/a (count < 10000) | 20.683 |
| laptop | board_engine | 63 | 72.132 | 73.603 | 93.650 | 95.268 | n/a (count < 10000) | 95.268 |
| laptop | round_trip | 63 | 75.525 | 75.856 | 95.103 | 98.098 | n/a (count < 10000) | 98.098 |
| laptop | transport | 63 | 3.392 | 1.919 | 2.829 | 47.525 | n/a (count < 10000) | 47.525 |

## Notes (2026-10-25)
Simulation only. Not for operational use.
- Command: `./scripts/demo_board_usb.sh` (cpu_opt, 4 threads, K = 64,
  seed 1000, 4 Hz). The laptop connects to 127.0.0.1:7070, which adb
  forwards over the USB cable to board_daemon on the board.
- "laptop" rows use the laptop clock; "daemon" rows use the board clock.
- The run includes one cut link / restore link (C, R). The episode losses
  are the same as in the in-process laptop run (ensemble 60.8, B1 69.2,
  B0 127.0), so the board engine gives the same plans (D-019).
- The live decision time (p50 72.8 ms) is larger than in the bench (p50
  19.0 ms, same K and threads). Not verified: a probable cause is that the
  cores clock down between the 4 Hz ticks (schedutil governor).
