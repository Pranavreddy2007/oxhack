# Live path on localhost (daemon and sim_stream in the same container)

Made by `laptop/tools/bench_report.py` from: `results/raw/live/localhost_rtt.csv`, `results/raw/live/localhost_daemon.csv`

| Run | Stage | Count | Mean ms | p50 ms | p95 ms | p99 ms | p99.9 ms | Max ms |
|---|---|---|---|---|---|---|---|---|
| daemon | decision_policy | 480 | 0.101 | 0.093 | 0.155 | 0.231 | n/a (count < 10000) | 0.495 |
| daemon | decision_rollout | 480 | 13.809 | 13.250 | 21.535 | 25.580 | n/a (count < 10000) | 31.003 |
| daemon | decision_total | 480 | 15.762 | 15.165 | 24.764 | 28.165 | n/a (count < 10000) | 32.480 |
| daemon | tick_filter | 2400 | 1.870 | 1.708 | 3.454 | 4.190 | n/a (count < 10000) | 9.117 |
| laptop | board_engine | 480 | 15.762 | 15.165 | 24.764 | 28.165 | n/a (count < 10000) | 32.480 |
| laptop | round_trip | 480 | 16.397 | 15.565 | 25.189 | 28.641 | n/a (count < 10000) | 32.948 |
| laptop | transport | 480 | 0.634 | 0.389 | 1.082 | 6.828 | n/a (count < 10000) | 7.414 |
