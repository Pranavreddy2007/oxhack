# EmberCast

**Simulation only. Not for operational use.**

EmberCast makes calibrated wildfire spread forecasts from noisy, late, and
partial data. It then sends simulated crews to protect asset clusters with a
risk-aware plan. It runs on a laptop. It can also run on an Arduino UNO Q at
the fire edge, without a cloud service.

## 1. Laptop-only demo (one command)
```
./scripts/demo_laptop.sh
```
Then open http://127.0.0.1:8000/. The script builds the C++ code and the
Python environment if they are not there. Keys: W wind shift, C cut link,
R restore link, B show B0, E results. See [docs/DEMO.md](docs/DEMO.md).

## 2. Build and test
Requirements: CMake 3.20 or later, a C++20 compiler, Python 3.11 or later,
and `uv`.
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

uv venv .venv
uv pip install --python .venv/bin/python -r requirements-dev.txt
.venv/bin/pytest -q
```

## 3. Evaluation and benchmarks
```
scripts/run_evaluation.sh            # TEST seeds, all policies, stress, ablations -> results/report.md
build/apps/embercast_bench --help    # engine stage latency -> results/raw/bench/
scripts/e2e_demo.sh                  # scripted 60-second live run on localhost
bash scripts/print_board_build.sh    # PRINTS the UNO Q commands (it does not run them)
```
Results: [results/report.md](results/report.md),
[results/bench_laptop.md](results/bench_laptop.md),
[docs/BENCHMARKING.md](docs/BENCHMARKING.md).

## 4. Documents
- [PRD.md](PRD.md): product requirements
- [CLAUDE.md](CLAUDE.md): work rules and board access rules
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): data flow, interfaces, threads
- [docs/HARDWARE.md](docs/HARDWARE.md): UNO Q facts, open questions, user-run commands
- [docs/BENCHMARKING.md](docs/BENCHMARKING.md): method, flags, results
- [docs/DEMO.md](docs/DEMO.md): demo script and fallback plan
- [docs/DECISIONS.md](docs/DECISIONS.md): design decisions

## 5. Status
| Step | Content | Status |
|---|---|---|
| 1 | Repository skeleton, docs, test harness | Done |
| 2 | Truth simulator, scenario generator, seed sets, replay test | Done |
| 3-4 | cpu_naive, baselines, particle filter, risk policy | Done |
| 5 | Offline replay harness and evaluation report | Done |
| 6 | cpu_opt backend, equivalence test, laptop benchmarks | Done |
| 7 | Laptop live demo (MVP cut line 1) | Done |
| 8-10 | UNO Q build, TCP link, board benchmark | Code and commands ready. Board results: waiting for the user. |
| 11 | MCU sensor station, Bridge sidecar | Code and tests with a fake router. Board: open questions Q7-Q10, flashing needs approval. |
| 12 | OpenCL backend | Not built: no OpenCL device is verified (DECISIONS D-020) |
| 13 | Full dashboard | Done (side-by-side, controls, results screen) |
| 14 | Optional integrations | Not done (DECISIONS D-021) |
| 15 | End-to-end benchmark, rehearsal script | scripts/e2e_demo.sh, docs/DEMO.md |
