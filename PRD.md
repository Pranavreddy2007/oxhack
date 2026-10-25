# EmberCast — Product Requirements Document

Status: Draft 1. Simulation only. Not for operational use.

## 1. Problem
A wildfire spreads. Wind and fuel moisture are not known exactly.
An incident commander has C crews and J asset clusters.
The commander must send crews to clusters every few minutes.
The data is noisy. The data can be late. Some data can be missing.

## 2. User
The user is an incident commander at a forward post.
The network connection at the post is poor.

## 3. Goals
- G1: Give calibrated burn probabilities from noisy, late, and partial data.
- G2: Give a risk-aware crew allocation. It must have a lower mean loss and a
  lower CVaR95 than both baselines. Show paired 95% confidence intervals.
- G3: Measure the latency and throughput of each stage on the UNO Q.
- G4: Give a reliable live demo. Keep a laptop-only fallback.

## 4. Non-goals
- Do not use real fire data, real maps, or real operations.
- Do not make a physically exact fire model. The model is a calibrated
  stochastic cellular automaton.
- Do not put an LLM in the decision path.
- Do not make the core loop depend on a cloud service.

## 5. User journey
1. The commander starts a scenario.
2. Sensor data and perimeter data arrive.
3. The probability map and the asset risk table update at each tick.
4. Every D ticks, EmberCast proposes a crew plan. The plan shows the
   expected loss, the CVaR, and the change from the last plan.
5. The commander sees the result and the comparison with the baselines.

## 6. Prediction and decision tasks
At each tick, EmberCast predicts:
1. P(cell burns within H ticks), for each grid cell.
2. P(asset cluster j is lost within H ticks), for each cluster.
3. The distribution of burned area at tick t+H.

EmberCast then assigns C crews to J clusters. It maximises:
U = -(E[loss] + lambda * CVaR_alpha[loss]) - switching_cost.

## 7. Simulation model
The truth simulator and the forecaster use the same model family.
- Grid: N x N cells. The default N is 96. N is configurable.
- Static data per cell: fuel class (0-3), elevation, asset cluster id,
  asset value.
- Cell state: UNBURNED, BURNING, or BURNT. A cell burns for B ticks.
  A BURNT cell never changes state again.
- Spread: each BURNING neighbour gives an ignition probability per tick:
  p = 1 - exp(-r * dt), where
  r = base_rate[fuel] * f_wind(cos(theta), wind_speed)
      * f_slope(delta_elevation) * f_moisture(humidity) * s.
- Hidden parameters: theta_h = {wind_bias, spread_scale s, moisture_bias}.
  In the truth simulator, theta_h changes as a bounded random walk.
  A scenario can include a regime shift (a wind change).
- Crews: n_j crews at cluster j multiply the ignition probability of the
  cells in cluster j by (1 - e(n_j)). The function e(n) has diminishing
  returns. A crew needs a travel delay to move.
- Random numbers: use a counter-based RNG (Philox4x32-10) with the key
  (seed, member, tick, cell). CPU and GPU results are then identical.
  Replay is then exact.
- Thresholds: the CPU makes one lookup table that converts probabilities
  to uint32 thresholds. Every backend uses this table. Backends compare
  integers only.

## 8. Data format
### 8.1 Frame header (all frames, little-endian)
| Field | Type |
|---|---|
| magic | u32 |
| version | u16 |
| type | u16 |
| seq | u32 |
| t_sim_tick | u32 |
| t_send_ns | u64 (sender clock) |
| payload_len | u32 |
| crc32 | u32 |

### 8.2 Frame types
- SENSOR: wind_speed f32, wind_dir f32, humidity f32, temp f32,
  flags u32 (dropout, stuck, out-of-range).
- PERIMETER: observed-mask bitset and burning bitset. The maximum size is
  fixed.
- DECISION: crew assignment array, expected loss, CVaR, top-k risk
  clusters, the echoed t_send_ns of the frame that caused the decision,
  and the board stage timings in ns.
- HEARTBEAT: seq and state (OK, STALE, DEGRADED, FAULT).

### 8.3 Rules
- The maximum payload size is fixed at compile time.
- Logs and replay files use JSONL or a binary log.
- Do not write logs from the hot path. Use a ring buffer and a separate
  writer thread.

## 9. Observation model
- Sensor frames arrive at a fixed rate. They have Gaussian noise, a
  dropout probability, stuck-sensor episodes, and delay jitter.
- A perimeter scan arrives every M ticks. Each scan covers a random subset
  of cells. A scan has a false-negative rate. A scan can arrive late.

## 10. Prediction model (EmberCast)
1. Feature update: smooth the wind and the humidity. Count the staleness.
2. Particle filter: K members. Each member has its own theta and its own
   copy of the state. On a perimeter scan, set the weight
   w_k ~ exp(-beta * mismatch_k). Use only the observed cells.
   If ESS < K/2, do a systematic resample. After a resample, add jitter
   to theta.
3. Forecast: move each member forward H ticks.
4. Prediction distribution: weighted P(burn) for each cell, the loss
   matrix L[k][j] for each member and cluster, and the burned-area
   distribution.
5. Uncertainty: entropy per cell, ensemble spread, and ESS.
6. Candidate actions: crew assignments.
7. Expected utility: for each member k,
   loss_k(a) = sum_j (1 - e(n_j(a))) * L[k][j].
   Calculate the weighted E[loss] and CVaR_alpha over the members.
8. Selected action: allocate crews one at a time (greedy). Add a
   switching penalty when a crew moves.
9. Measured outcome: the truth simulator applies the crews and reports
   the realised loss.

Missing data: if a sensor frame is missing or stale, do not update the
filter from it. Increase the process noise. Report STALE.

Note: loss_k(a) is a linear approximation. The evaluation uses the truth
simulator. Thus the approximation cannot make the reported gain larger.

## 11. Baselines
- B0 "front-nearest": no model. Send crews to the highest-value clusters
  that are nearest to the current observed front.
- B1 "deterministic": one forecast run with a point-estimate theta. No
  filter. lambda = 0. Same optimiser.
- ORACLE: allocation that knows the true future, with the same seeds.
  Use it only to report regret.

## 12. Evaluation method
- Seed sets: TUNE (to choose parameters) and TEST (held out). The sets
  must not overlap. Keep them in a file in the repository
  (`laptop/tools/seeds.json`).
- For each TEST seed, run all policies on the same truth realisation
  (common random numbers).
- Prediction metrics: Brier score, log loss, ECE (10 bins), reliability
  plot, CRPS of burned area, spread-skill ratio.
- Decision metrics: mean loss, CVaR95, paired win rate, paired bootstrap
  95% CI of the loss difference against B0 and B1, regret against ORACLE.
- Ablations: K in {32, 64, 128, 256}, no filter, lambda = 0.
- Stress scenarios: regime shift, 30% sensor dropout, late scans, stuck
  sensor.

## 13. Failure modes and required behaviour
| Failure | Required behaviour |
|---|---|
| Missing data | Continue. Increase uncertainty. Flag STALE. |
| Late or out-of-order frames | Use seq and t_sim_tick. Drop frames older than a set limit. Count them. |
| Corrupt frame | If the CRC check fails, drop the frame and count it. |
| Transport disconnect | The board keeps the last plan, flags STALE, and reconnects with backoff. |
| Board not available | The laptop runs the same engine in its process. |
| MCU silent | Linux flags SENSOR_LOST. Laptop sensor emulation can replace the MCU. |
| Linux silent | The MCU watchdog shows FAULT on its display. |

## 14. Privacy and safety
- Do not use personal data. Do not use real locations.
- Read API keys only from environment variables. Commit only `.env.example`.
- Do not connect to real emergency, vehicle, industrial, or financial
  systems.
- The UI must show: "Simulation only. Not for operational use."

## 15. Demo requirements
- Side-by-side view: EmberCast and B1. B0 is a toggle.
- Probability heatmap, crew markers, asset risk table, live loss counters,
  and a latency histogram from the board timings.
- A "wind shift" control. Use the MCU knob if it is available. Else use a
  key.
- A "cut link" control that shows the degraded behaviour.
- A final results screen. The TEST evaluation makes this screen.

## 16. Acceptance criteria
- AC1: Deterministic replay. The same seed gives byte-identical decision
  logs on the same build.
- AC2: The cpu_naive and cpu_opt backends give identical outputs for the
  same seeds.
- AC3: On the TEST set, the EmberCast mean loss is lower than the B1 mean
  loss, and the paired 95% CI does not include zero. If this fails, report
  the true result. Do not tune on TEST.
- AC4: Report the ECE of P(burn) with a reliability plot.
- AC5: The board latency report gives count, mean, p50, p95, p99, p99.9
  (only if count >= 10000), and max for each stage.
- AC6: One command starts the laptop-only demo.
- AC7: Each failure mode in Section 13 has a test.

## 17. Provisional values (measure before you fix them)
These values are starting points only. Steps 6 and 10 measure the
budgets. docs/DECISIONS.md records the final values and the reason.
| Symbol | Meaning | Start value |
|---|---|---|
| N | grid side | 96 |
| K | ensemble members | 64 |
| H | forecast horizon (ticks) | 20 |
| D | ticks between plans | 5 |
| B | burn duration (ticks) | 3 |
| M | ticks between perimeter scans | 4 |
| C | crews | 4 |
| J | asset clusters | 8 |
| alpha | CVaR level | 0.95 |
| lambda | risk weight | 0.5 (choose on TUNE only) |
