# CLAUDE.md — Rules for work on EmberCast

Simulation only. Not for operational use.
Write all documentation in ASD-STE100 Simplified Technical English.

## 1. Work protocol
1. Follow the build order in PRD.md and in the project brief (Steps 1-15).
2. Do one step (or the approved range of steps) at a time.
3. After each step, run all tests and builds that exist.
4. Report the exact commands and their results.
5. List the changes and each deviation from PRD.md.
6. Stop. Wait for approval ("approved" or "approved through step N").
7. After approval, commit with a clear message, for example
   "step 4: particle filter and risk policy".
8. If a step uses more than its time budget, stop. Tell the user.
   Propose a smaller version of the step.
9. Always keep a working laptop-only demo.
10. Never make up a benchmark number or an evaluation result.

## 2. Hot-path rules (filter + rollout + policy, per tick)
- Do not allocate heap memory in the measured loop. Allocate arenas at start.
- Do not use locks in the measured loop. A per-tick barrier in the thread
  pool is permitted. Document it and measure its cost.
- Do not do file I/O, logging, or network calls in the measured loop.
- Use fixed-size buffers where practical.
- Use deterministic seeds for all tests.
- Keep measurement separate from visualisation.
- Do not report Python overhead as C++ engine performance.
- Measure cold start (first tick) and warm state separately.
- If you pin threads to cores, document it. Else document that you did not.
- Document all compiler flags in docs/BENCHMARKING.md. Use -O3.
- Use -mcpu=cortex-a53 only after the board compiler accepts it
  (CMake option EMBERCAST_NATIVE_ARCH).
- Keep cpu_naive and cpu_opt in the same codebase and the same binary.
- If you must relax a rule, write the reason in docs/DECISIONS.md and
  measure the cost.
- Choose K, N, and H from measured budgets. Do not guess them.

## 3. Build commands
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```
Options: `-DEMBERCAST_OPENCL=ON` (Step 12, off by default),
`-DEMBERCAST_NATIVE_ARCH=ON` (board only, after verification).

## 4. Test commands
```
uv venv .venv && uv pip install --python .venv/bin/python -r requirements-dev.txt
ctest --test-dir build --output-on-failure
.venv/bin/pytest -q
```

## 5. Benchmark and evaluation commands
```
build/apps/embercast_bench --backend cpu_opt --threads 1 --episodes 10
build/apps/embercast_bench --backend cpu_opt --threads 1 --episodes 10 --baseline bench/baseline_laptop.txt
.venv/bin/python -m laptop.tools.bench_report --raw results/raw/bench/*_raw.csv --out results/bench_laptop.md
scripts/run_evaluation.sh        # TEST seeds -> results/report.md
scripts/e2e_demo.sh              # 60-second live run on localhost
./scripts/demo_laptop.sh         # laptop demo, http://127.0.0.1:8000/
bash scripts/print_board_build.sh   # prints the board commands only
```
Never tune on TEST seeds. Use `--set tune` for parameter choice.

## 6. Repository layout
| Path | Content |
|---|---|
| `engine/include/embercast/` | Public headers: types, rng, model, filter, forecast, policy, transport, clock, metrics |
| `engine/src/` | Engine sources |
| `engine/backends/` | cpu_naive, cpu_opt, opencl (optional) |
| `sim/` | Truth simulator and scenario generator |
| `apps/` | board_daemon, sim_stream, replay, bench |
| `laptop/` | Python dashboard and tools |
| `bridge/` | MCU sidecar (router bridge to UDS) |
| `mcu/sensor_station/` | Arduino sketch for the STM32U585 |
| `integrations/` | Optional sinks. All off by default. |
| `scripts/` | Laptop scripts (demo, evaluation, e2e). `print_board_*.sh` PRINT board commands; they never run them. |
| `bench/` | Saved laptop p99 baseline for the regression check |
| `results/` | Generated reports (raw CSV in `results/raw/`, gitignored) |
| `third_party/doctest/` | Vendored doctest single header |
| `tests/cpp/`, `tests/py/` | Tests |
| `datasets/` | Generated data (gitignored) |
| `docs/` | Architecture, hardware, benchmarking, demo, decisions |

## 7. Board access rules (Arduino UNO Q)
- Never SSH into the UNO Q. Never run commands on it. The user does this.
- Give the user exact commands with placeholders, for example:
  `ssh <BOARD_USER>@<BOARD_IP> 'lscpu; free -h; uname -a'`
- Never hard-code credentials, user names, or IP addresses.
- Scripts in `scripts/` only print commands.
- All work must run and be testable without the board.
- Do not invent Bridge APIs, GPIO behaviour, kernel parameters, GPU
  runtime support, OpenCL devices, performance numbers, clock speeds,
  memory bandwidth, or latency. Write each unknown as a question in
  docs/HARDWARE.md, with a command for the user.
- MPU GPIO is 1.8 V. MCU I/O is 3.3 V. Do not connect them directly.
- The router socket is a trust boundary.

## 8. Actions that need explicit user approval in chat
- Any SSH session to the board, or any command on the board.
- Flashing MCU firmware.
- Changes to boot configuration, device tree overlays, kernel
  parameters, or system services.
- Installing system packages on the board.
- Any network connection from the board to a service outside the
  laptop-board link.

## 9. Never do
- Never connect to real exchanges, brokers, emergency systems, vehicles,
  industrial control, or production infrastructure.
- Never commit `.env` or API keys. Read keys only from environment variables.
