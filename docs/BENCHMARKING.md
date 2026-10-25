# EmberCast Benchmarking

Simulation only. Not for operational use.

## 1. Rules
- Each number comes from a run that a script can do again.
- Raw samples go to CSV. Scripts make the tables from the CSV.
  This file gives no hand-typed numbers. It links to the generated tables.
- An estimate is never reported as a measurement.
- Cold start (first decision of the process, and first decision of each
  episode) is reported separately from the warm state.

## 2. What is measured
| Quantity | Clock | Where |
|---|---|---|
| tick_filter: feature update + filter propagate + scan updates | engine monotonic (`steady_clock`) | `Engine::tick()` |
| decision_rollout: H-tick rollout of K members | engine monotonic | `Engine::tick()` |
| decision_policy: greedy allocation with CVaR | engine monotonic | `Engine::tick()` |
| decision_total: whole decision tick | engine monotonic | `Engine::tick()` |
| decision_cold_process / decision_cold_episode | engine monotonic | tick 0 |
| round_trip: HEARTBEAT sent -> DECISION received | laptop monotonic | sim_stream `--rtt-out` |
| board_engine: decision_total as reported in the DECISION frame | board monotonic | sim_stream `--rtt-out` |
| transport: round_trip - board_engine | mixed (difference of durations only) | sim_stream `--rtt-out` |
| dashboard render | browser `performance.now()` | dashboard panel only (display) |
| Bridge latency (MCU path) | not measured | Step 11 (docs/HARDWARE.md Q1) |

The truth simulator, the dashboard, and file output are outside the
measured sections. Python is never inside a measured section.

Also reported by `embercast_bench` (`*_meta.json`): wall time, CPU time
from `getrusage` (CPU utilisation in cores), peak RSS (`VmHWM` from
`/proc/self/status`), rollout throughput in member-ticks/s and nominal
cell-updates/s (K x H x N x N per rollout; cpu_opt does not visit every cell).

Board temperature: not measured (no verified thermal path yet).
Power: not measured (no external USB power meter).

## 3. Commands
Laptop:
```
build/apps/embercast_bench --backend cpu_naive --threads 1 --episodes 10
build/apps/embercast_bench --backend cpu_opt --threads 1 --episodes 10
build/apps/embercast_bench --backend cpu_opt --threads 2 --episodes 10
build/apps/embercast_bench --backend cpu_opt --threads 4 --episodes 10
.venv/bin/python -m laptop.tools.bench_report --raw results/raw/bench/*_raw.csv --out results/bench_laptop.md
```
Performance regression check (fails with exit code 1 if a warm-stage p99
grows more than 50 % against the saved laptop baseline):
```
build/apps/embercast_bench --backend cpu_opt --threads 1 --episodes 10 \
  --baseline bench/baseline_laptop.txt --max-p99-growth 50
```
The limit is wide because the development container shares its CPU.
Board: `bash scripts/print_board_build.sh` prints the commands (Step 10a).

## 4. Compiler flags
| Build | Flags |
|---|---|
| Release (default) | `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic` |
| Board, optional | add `-mcpu=cortex-a53` (CMake option `EMBERCAST_NATIVE_ARCH`), only after the board compiler accepts it |

Laptop compiler in the development container: g++ 13.3.0, CMake 3.28.3.

## 5. Threads
- cpu_opt uses a fixed pool (`--threads`, 0 = all hardware threads).
- The caller thread works too. A pool call is a barrier (DECISIONS D-018).
- Threads are not pinned to cores.

## 6. Raw data and tables
| Data | Raw CSV (gitignored) | Table (committed) |
|---|---|---|
| Laptop engine stages | `results/raw/bench/*_raw.csv`, `*_meta.json` | [results/bench_laptop.md](../results/bench_laptop.md) |
| Offline evaluation stage times | `results/raw/test/main_latency.csv` | not tabulated (parallel episodes share cores) |
| Live round trip | `results/raw/live/rtt.csv` | `results/bench_live.md` (after a live run) |
| Board engine stages | `results/raw/board/*_raw.csv` | `results/bench_board.md` (after the user runs Step 10a) |

Note: the laptop numbers come from a shared cloud development container
with 4 hardware threads, not from the user laptop and not from the UNO Q.

## 7. Model and decision metrics
See [results/report.md](../results/report.md) (made by
`scripts/run_evaluation.sh` from the TEST CSV files).
