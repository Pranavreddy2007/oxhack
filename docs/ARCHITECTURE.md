# EmberCast Architecture

Simulation only. Not for operational use.

## 1. Data flow

### 1.1 Live (laptop and UNO Q)
```
sim_stream (laptop) --TCP 7070--> board_daemon (UNO Q Linux) --TCP--> sim_stream
sim_stream --TCP 7072 (JSON lines)--> dashboard server --WebSocket 8001--> browser
browser --WebSocket--> dashboard server --TCP--> sim_stream (controls)
MCU --Bridge--> bridge/mcu_sidecar.py --UDS--> board_daemon     (Step 11, see docs/HARDWARE.md)
```

### 1.2 Laptop only
`scripts/demo_laptop.sh` starts board_daemon on 127.0.0.1, sim_stream, and
the dashboard server. With `ENGINE=inprocess`, sim_stream runs the engine
in its own process and no daemon starts.

### 1.3 Offline evaluation
`embercast_replay` runs `run_episode()` (sim/episode.cpp) for each
(seed, policy). It uses no transport. `laptop/tools/evaluate.py` makes the
report from the CSV.

## 2. Per-tick engine pipeline (engine/src/engine.cpp)
1. Feature update: validate sensor frames (seq, age, flags, range, stuck),
   smooth wind (as a vector), humidity and temperature. Count drops.
2. STALE: no valid sensor frame for more than `stale_ticks`, or link down.
   When STALE, the filter inflates the process noise.
3. Filter propagate: per member, process noise on theta, threshold table,
   then one tick with `IForecastBackend::advance()`.
4. Filter update for each perimeter scan: weights from the mismatch on the
   observed cells, assimilation of observed fire cells, systematic
   resample when ESS < K/2, theta jitter.
5. Every D ticks: `IForecastBackend::rollout()` for H ticks, then
   `IPolicy::decide()`.

## 3. Interfaces
| Interface | Header | Implementations |
|---|---|---|
| ITransport | transport.hpp | StreamTransport (TCP or Unix socket), InProcessTransport |
| IForecastBackend | forecast.hpp | cpu_naive, cpu_opt (opencl: not built, see DECISIONS D-020) |
| IPolicy | policy.hpp | EnsembleRisk (EmberCast and B1), FrontNearest (B0); oracle in sim/episode.cpp |
| ISensorSource | transport.hpp | laptop emulation through the truth simulator; MCU through the sidecar (Step 11) |
| IStatusSink | transport.hpp | console in board_daemon; MCU display through the sidecar (Step 11) |

## 4. Threads
| Process | Thread | Work |
|---|---|---|
| board_daemon | main (engine) | pops frames from the SPSC ring, runs the engine, sends replies |
| board_daemon | receiver | `recv()`, frame cut, CRC check, push to the SPSC ring (64 frames) |
| board_daemon | pool workers (threads - 1) | cpu_opt members and p_burn blocks |
| board_daemon | log writer | drains the log ring to the JSONL file |
| sim_stream | main | three truth simulators, two in-process baselines, dashboard stream |
| sim_stream | receiver | as in board_daemon |

The thread pool (thread_pool.hpp) has no locks. A call to `run()` is a
barrier: the caller waits until all indices are done. cpu_opt uses two
barriers per rollout (members, then p_burn blocks) and one per filter
advance. The cost of a barrier is part of the measured stage times.
Threads are not pinned to cores.

## 5. Frame format
See PRD.md Section 8 and engine/include/embercast/frame.hpp. One extra
frame type, FORECAST (type 5), carries the P(burn) map as one byte per cell
for the dashboard (DECISIONS D-016).

## 6. Determinism
- All random numbers come from Philox4x32-10 keyed by seed and counters.
- Truth: member id 0xFFFFFFFF, streams kStreamSpread, kStreamWeather, ...
- Engine: its own seed (derived from the scenario seed), member id k.
- cpu_opt keeps the float summation order of cpu_naive (AC2).
- The same seed gives byte-identical decision logs (AC1).
