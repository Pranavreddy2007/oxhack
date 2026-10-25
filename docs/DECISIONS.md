# EmberCast Design Decisions

Each entry gives the decision, the reason, and the cost. Each relaxed
rule also has an entry.

## D-001: Counter-based RNG is Philox4x32-10
- Decision: use Philox4x32-10 with the key (seed, member, tick, cell).
- Reason: it has no state. CPU and GPU give the same numbers. Replay is
  exact. It is a well-tested generator.
- Cost: more integer multiplies per draw than splitmix64. Step 6 measures it.

## D-002: Probability to threshold conversion
- Decision: the CPU makes one lookup table that converts probabilities to
  uint32 thresholds. All backends get the same table. Backends compare
  integers only (draw < threshold means ignition).
- Reason: floating-point results can differ between backends. Integer
  compares do not.
- Final form (Step 2): `T[fuel 0..3][neighbour dir 0..7][slope bin 0..8][crews 0..8]`.
  The CPU builds it per (member, tick) from theta and weather in
  `build_thresholds()` (engine/include/embercast/model.hpp). One table is
  10 KiB.
- The draws for one cell come from two Philox calls (lane 0: directions
  0..3, lane 1: directions 4..7). A cell ignites if any burning neighbour
  draw is below its threshold. The result does not depend on the order of
  the checks. Thus a backend can skip cells and still give identical output.

## D-003: Python version in the development container
- Decision: the code targets Python 3.11 or later (`requires-python >=3.11`).
  The development container has Python 3.13.17. Tests run on 3.13 there.
- Reason: the brief asks for Python 3.11. The container does not have 3.11.
- Cost: a 3.12+ feature can enter by mistake. Do not use features newer
  than 3.11.

## D-004: Python environment with uv
- Decision: use `uv venv .venv` and `requirements-dev.txt` with pinned
  versions (pytest, hypothesis, numpy, matplotlib, websockets).
- Reason: the system Python has no pytest. A local venv does not change
  the system.

## D-005: doctest is vendored
- Decision: `third_party/doctest/doctest.h`, doctest v2.4.11, with its
  licence file. SHA-256 of the header:
  `44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb`.
- Reason: the brief asks for a vendored single header. The build then
  needs no network.

## D-006: Provisional parameter values
- Decision: PRD.md Section 17 gives start values for N, K, H, D, B, M, C,
  J, alpha, and lambda.
- Reason: rule F says to choose K, N, and H from measured budgets.
  Step 6 (laptop) and Step 10 (board) measure the budgets. This entry
  then records the final values.

## D-007: Spread rate calibration (Step 2)
- Decision: base_rate = {0, 0.04, 0.07, 0.10} per tick, B = 6 ticks. The
  fire starts as 5 cells (a plus shape).
- Reason: the first values ({0, 0.10, 0.18, 0.28}, B = 3) burned most of
  the 96 x 96 grid in 60 ticks, so crews had almost no effect. Lower rates
  with B = 3 made the fire go out early on most seeds (percolation). The
  final values burn about 15-75 % of the grid in 120 ticks on TUNE seeds
  1000-1009 and reach some clusters but not all.
- Method: a probe program on TUNE seeds only. No TEST seed was used.

## D-008: PERIMETER "fire" bitset
- Decision: the second bitset in a PERIMETER frame marks cells that burn
  OR have burnt (the fire footprint). PRD.md calls it the "burning bitset".
- Reason: a burnt cell never returns to UNBURNED, so the footprint is
  monotone. This makes the filter likelihood simple.

## D-009: Sensor faults in the truth simulator
- Dropout: the frame is not delivered. The seq number still increments.
- Stuck: the station repeats the same values. It does not set a flag. The
  engine must find it (three identical frames).
- Out of range: humidity = 1.7. The engine must reject it.

## D-010: Rollout crews and the crew-free loss (Step 4)
- Decision: the forecast rolls out each member with the crews of the
  current plan (target clusters, as if the crews have arrived). The policy
  then divides L[k][j] by (1 - e(n_j)) to get the crew-free loss and
  applies loss_k(a) = sum_j (1 - e(n_j(a))) * L_free[k][j].
- Reason: one rollout gives P(burn) for the plan in force (good for
  calibration) and the loss matrix for the optimiser. A second crew-free
  rollout doubles the cost.
- Cost: the linear model is an approximation. The truth simulator gives
  the measured loss, so the approximation cannot inflate the reported gain.

## D-011: Observation assimilation in every member
- Decision: after the weights, the filter sets each observed fire cell
  that is UNBURNED in a member to BURNING with B/2 ticks left. B1 does the
  same with its single member.
- Reason: with K = 64 members the stochastic front drifts away from the
  observed front. The correction keeps all members near the data. This is
  common practice for fire data assimilation.

## D-012: Late perimeter scans
- Decision: a scan older than max_late_ticks (6) is dropped and counted.
  A newer late scan is compared with the member state at the current tick.
- Cost: the "extra" term (member has fire where the scan saw none) is too
  large for late scans, because the member front has moved. extra_w = 0.3
  reduces this effect.

## D-013: Baseline definitions
- B1 (deterministic): K = 1, theta = prior mean (no bias, s = 1), no
  weights, no resampling, no process noise, lambda = 0, same greedy
  optimiser and switching cost, same assimilation.
- B0 (front-nearest): score_j = value_j / (1 + distance to the nearest
  observed fire cell). Greedy on score_j * (e(n+1) - e(n)). No model.
- ORACLE: a copy of the truth simulator runs H ticks with no crews and the
  true random numbers. The greedy optimiser uses that true loss (lambda = 0).

## D-014: Cluster "lost" and the metric cell set
- A cluster is lost when the newly burned value in the horizon is at least
  25 % of the cluster value.
- P(burn) metrics use the cells that are UNBURNED in the truth at the
  forecast tick and within 12 cells (Chebyshev) of the true footprint.
  Far cells have P = 0 and outcome 0. They would make the scores look
  better than they are.

## D-015: Parameters chosen on TUNE (Step 5)
- Decision: beta = 0.05, lambda = 0.5, alpha = 0.95, switch_cost = 4,
  K = 64, H = 20, D = 5, N = 96.
- Method: sweep of beta {0.02, 0.05, 0.1} and lambda {0, 0.5, 1.5} x
  switch_cost {1, 4, 10} on the 40 TUNE seeds only
  ([results/tune_summary.md](../results/tune_summary.md)). The differences
  between most settings were small (within the seed noise). The defaults
  were kept. TEST seeds were not used to choose any value.
- The TEST ablations later show that lambda = 0 gives a similar mean loss
  and CVaR95 (results/report.md). The value was not changed after TEST.
- K = 64 and H = 20: on the laptop, one cpu_opt decision at K = 64 takes
  well under the 250 ms tick of the 4 Hz demo (results/bench_laptop.md).
  The board budget is not measured yet. Step 10a has a K sweep for it.

## D-016: FORECAST frame type
- Decision: frame type 5 carries the P(burn) map (one byte per cell,
  round(255 p)) and the ESS from the engine to the laptop.
- Reason: C12 needs the probability heatmap from the board. PRD.md
  Section 8 lists only four types. kMaxPayload is 16 + kMaxCells bytes.

## D-017: Stress scenarios and the regime shift
- Stress "none" (the main run) has a regime shift with probability 0.5.
  Stress "regime" always has one. The other stress types (dropout30,
  late_scans, stuck) have no regime shift. Thus their losses are not
  directly comparable with the main run. Each stress row compares the
  policies on the same scenarios.

## D-018: Thread pool barrier (relaxed rule: spin wait)
- The caller spins on an atomic counter until all tasks are done. Workers
  sleep on `std::atomic::wait` between calls. No mutex is used.
- Cost: the caller uses one core while it waits. The barrier time is part
  of the measured decision_rollout and tick_filter times.

## D-019: Live protocol
- sim_stream sends SENSOR and PERIMETER frames for tick t, then a
  HEARTBEAT with t_sim_tick = t. The daemon advances the engine on the
  HEARTBEAT. If ticks were missed (link loss), the daemon advances through
  each missed tick, so the members stay in step with the truth.
- The daemon and sim_stream make the same scenario from the seed. No
  terrain data goes over the link.
- With no lost frames, the remote engine gives the same plans as the
  in-process engine (tests/py/test_live_e2e.py).

## D-020: OpenCL backend (Step 12) not built
- No OpenCL device is verified on the board (docs/HARDWARE.md Q2). The
  development container has no OpenCL headers or device. The brief says
  to build it only if clinfo shows a device. The CMake option stays OFF.

## D-021: Optional integrations (Step 14) not built
- Reason: time box. They are optional and off by default. The core loop
  has no cloud dependency.

## D-022: MCU role in the live demo (Step 11)
- Decision: the truth simulator runs on the laptop. Thus the engine gets
  its weather frames from the laptop sensor emulation by default
  (`--sensor-source laptop`). The MCU frames then give (1) the knob wind
  shift (flag bit 31; the daemon relays the frame to the laptop, and the
  laptop shifts the true wind) and (2) MCU liveness (SENSOR_LOST, state
  DEGRADED). With `--sensor-source mcu`, the engine uses the MCU frames and
  the laptop frames take over when the MCU is silent.
- Reason: MCU weather values that are not linked to the true weather would
  make the forecast wrong for reasons that are not in the model.
- The MCU display uses LED_BUILTIN only, because the LED matrix API is not
  verified (HARDWARE.md Q5).

## D-023: Own MessagePack-RPC client in the sidecar
- Decision: `bridge/msgpack_lite.py` and a minimal RPC client in
  `bridge/mcu_sidecar.py`.
- Reason: the Python client package was not found, and installing
  packages on the board needs approval. The router method for
  registration is a command-line option because it is not verified (Q7).
