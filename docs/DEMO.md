# EmberCast Demo

Status: stub (Step 1). Step 7, Step 13, and Step 15 fill this file.

Simulation only. Not for operational use.

## 1. Demo script (3 minutes)
1. Show the scenario: terrain, fuel, assets, fire start.
2. Turn the knob (or push a key) to change the wind.
3. The probability map moves and becomes wider.
4. EmberCast moves crews. The baseline does not react in time.
5. The live loss counters for EmberCast and the baselines diverge.
6. The latency panel shows p50 and p99 measured on the board.
7. Disconnect the laptop link. The board flags STALE. The MCU display
   shows the degraded state.
8. Show the final evaluation chart over the held-out TEST seeds, with
   95% confidence intervals.

## 2. Fallback plan
If the board is not available, run the laptop-only demo (Step 7). The
laptop runs the same engine in its process.

## 3. Manual test procedure
To be written. It will include the board steps that the user runs.
