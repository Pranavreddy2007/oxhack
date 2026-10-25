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
