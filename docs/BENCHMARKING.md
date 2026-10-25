# EmberCast Benchmarking

Status: stub (Step 1). No benchmark exists yet. No number in this file is
a measurement until a script makes it.

## 1. Method
- Each number comes from a run that a script can do again.
- Raw samples go to CSV. Scripts make the tables from the CSV.
- Do not type a number by hand. Do not report an estimate as a measurement.
- Measure cold start (first tick) and warm state separately.

## 2. Quantities to report
For each quantity: sample count, mean, p50, p95, p99, p99.9 (only if
count >= 10000), and max. Also: throughput (ticks/s, member-ticks/s,
cell-updates/s), CPU utilisation from /proc, peak RSS (VmHWM), board
temperature (only from a verified thermal path), and power (only with an
external USB power meter; else "not measured").

Keep these latencies separate:
- End-to-end round trip (laptop clock).
- Board-only engine latency (board monotonic clock).
- Transport latency (round trip minus board engine time).
- Filter, rollout, and policy latency.
- Dashboard render latency (browser timing).
- Bridge latency (MCU path).

## 3. Compiler flags
| Build | Flags |
|---|---|
| Release (default) | `-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wpedantic` |
| Board, optional | add `-mcpu=cortex-a53` (option `EMBERCAST_NATIVE_ARCH`), only after verification |

## 4. Thread pinning
Not decided yet (Step 6).

## 5. Raw data locations
`results/raw/` (gitignored) for raw CSV. Summary tables go in this file.

## 6. Results
No results yet.
