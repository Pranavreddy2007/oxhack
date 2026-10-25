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
- Open item: Step 2 sets the table index (for example, quantised rate
  bins). This entry gets the final form in Step 2.

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
