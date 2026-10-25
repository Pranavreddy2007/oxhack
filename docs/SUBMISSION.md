# EmberCast: hackathon submission text

Simulation only. Not for operational use.

Copy the sections below into the submission form. Every number comes from a
file in this repository (the file is named next to the number).

## Name
EmberCast

## Tagline (one line)
Calibrated wildfire spread forecasts and risk-aware crew plans from noisy,
late, partial data, computed at the fire edge on an Arduino UNO Q with no cloud.

## Inspiration
At a forward post, an incident commander must move crews every few minutes.
The wind and fuel moisture are not known exactly, sensor data drops out or
arrives late, and the network is poor. A single "best guess" fire forecast
hides the uncertainty that matters for that decision. We wanted a forecast
that says how sure it is, and a planner that uses that uncertainty, on a
small board that works without the cloud.

## What it does
- Runs a 64-member particle-filter ensemble of a stochastic fire-spread
  model. Each member has its own hidden wind bias, spread scale and
  moisture bias. Perimeter scans re-weight and resample the members.
- Every tick it gives P(burn) for each cell, P(lost) for each asset
  cluster, and the burned-area distribution.
- Every 5 ticks it assigns crews to asset clusters. The objective is
  expected loss + lambda x CVaR95 + a switching cost.
- A live dashboard shows EmberCast next to a deterministic baseline on the
  same fire. You can shift the wind (W), cut the link (C, the engine goes
  STALE and keeps the last plan), restore it (R), and open the held-out
  results (E).
- It runs on a laptop, or with the engine on the Arduino UNO Q over the
  USB cable. The engine has three bit-identical backends: cpu_naive,
  cpu_opt (sparse front, thread pool) and opencl (the UNO Q Adreno GPU).

## Results (held-out TEST seeds, 100 scenarios, results/report.md)
- Mean realised asset loss: EmberCast 159.7, deterministic B1 185.9,
  front-nearest B0 190.8, oracle 147.0.
- Paired difference vs B1: -26.3 (95% CI [-36.6, -16.4]). Vs B0: -31.1
  (95% CI [-40.2, -22.6]). Both CIs exclude zero. The result holds under
  all four stress tests (sensor dropout, late scans, regime shift, stuck
  sensor).
- Calibration: ECE of P(burn) is 0.023 (B1: 0.242). See
  results/reliability.png.
- EmberCast closes about 67 % of the gap between B1 and the oracle.
- Honest limit: CVaR95 is lower than B0 (CI excludes 0) but not
  significantly lower than B1, and the CVaR term itself has no measurable
  effect (docs/DECISIONS.md D-024).
- On the Arduino UNO Q (results/bench_board.md): one forecast + crew
  decision (64 members x 20 ticks on a 96x96 grid) takes 21.9 ms mean,
  60.6 ms p99 with cpu_opt on 4 cores, against 874 ms for the reference
  backend (40x faster). Live, laptop -> board -> laptop round trip over
  USB: 75.9 ms p50, 98.1 ms p99 (results/bench_live_board.md), well inside
  the 250 ms tick.
- The GPU backend is bit-identical but about 90x slower than cpu_opt on
  the board's 1-compute-unit Adreno 702. We report it as a negative result.

## How we built it
- C++20 engine (no heap allocation and no locks in the per-tick loop),
  counter-based Philox RNG and integer threshold tables, so every backend
  and every replay is bit-identical.
- Truth simulator with hidden parameter drift and regime shifts; separate
  TUNE and TEST seed sets; paired bootstrap confidence intervals.
- OpenCL backend loaded with dlopen() (no SDK needed), verified
  bit-identical to the reference on the Mac GPU and the UNO Q Adreno GPU.
- Board binaries cross-built on a Mac with zig (the board has no compiler).
- Python dashboard (WebSocket) and evaluation tools.
- MCU sensor-station sketch (STM32U585) and a Linux sidecar that talks
  MessagePack-RPC to arduino-router.

## Challenges
- Keeping CPU, multi-threaded and GPU backends bit-identical.
- Honest evaluation: parameters chosen only on TUNE seeds; the CVaR result
  is reported as it is.
- The board has no compiler and no internet, so we cross-built with zig
  and ran the live demo over adb port forwarding.

## Accomplishments
- A statistically significant loss reduction on held-out scenarios, with a
  well calibrated forecast.
- The full pipeline runs on a small edge board, without the cloud.

## What we learned
- Calibration matters more than a sharper point forecast for this decision.
- Tail-risk (CVaR) terms need many more scenarios to show an effect.

## What's next
- Real terrain and fuel data, and a learned spread model.
- Run the policy on the GPU too, and use larger ensembles.
- Use the MCU knob and LED matrix as a field-console input and alarm.

## Built with
C++20, CMake, OpenCL, Python, WebSockets, zig (cross-compiler), Arduino UNO Q
(Qualcomm QRB2210 + STM32U585), arduino-router / Arduino_RouterBridge, Debian.

## Try it out
- Source: https://github.com/Pranavreddy2007/oxhack
- Laptop demo: `./scripts/demo_laptop.sh`, then open http://127.0.0.1:8000/

## Media (in `media/` and `results/`)
| File | Use |
|---|---|
| media/dashboard_live.png | Cover image: EmberCast vs deterministic, live |
| media/dashboard_wind_shift.png | After a wind shift: losses diverge |
| media/dashboard_results.png | Held-out results screen with 95% CIs |
| media/dashboard_board_usb.png | Engine running on the UNO Q ("engine on board_daemon") |
| media/dashboard_link_cut.png | Link cut: STALE, last plan kept |
| results/paired_diff.png | Paired loss differences with 95% CIs |
| results/reliability.png | Calibration (reliability) plot |

## Video (2 minutes): shot list
Follow docs/DEMO.md section 2. Record the screen with the macOS screenshot
tool (Cmd-Shift-5, "Record Entire Screen").
1. 0:00 Problem in one sentence; show the safety notice.
2. 0:15 Show the board on the desk and the terminal with
   `./scripts/demo_board_usb.sh` ("engine on the UNO Q").
3. 0:30 Point to the two maps: orange P(burn) cloud vs the single
   deterministic forecast.
4. 0:45 Press W: the wind turns, EmberCast moves crews first.
5. 1:10 Press C: STALE, last plan kept. Press R: OK again.
6. 1:25 Press E: TEST results with confidence intervals.
7. 1:45 Close: calibrated, risk-aware, edge-only.
