# EmberCast

**Simulation only. Not for operational use.**

EmberCast makes calibrated wildfire spread forecasts from noisy, late, and
partial data. It then sends simulated crews to protect asset clusters with a
risk-aware plan. It runs on a laptop. It can also run on an Arduino UNO Q at
the fire edge, without a cloud service.

## 1. Laptop-only demo (one command)
Available from Step 7. The command will be:
```
./scripts/demo_laptop.sh
```

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

## 3. Documents
- [PRD.md](PRD.md): product requirements
- [CLAUDE.md](CLAUDE.md): work rules and board access rules
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): data flow, interfaces, threads
- [docs/HARDWARE.md](docs/HARDWARE.md): UNO Q facts, open questions, user-run commands
- [docs/BENCHMARKING.md](docs/BENCHMARKING.md): method, flags, results
- [docs/DEMO.md](docs/DEMO.md): demo script and fallback plan
- [docs/DECISIONS.md](docs/DECISIONS.md): design decisions

## 4. Status
| Step | Content | Status |
|---|---|---|
| 1 | Repository skeleton, docs, test harness | Done |
| 2-7 | Laptop vertical slice (MVP cut line 1) | Not started |
| 8-10 | Live inference on the UNO Q (MVP cut line 2) | Not started |
| 11-15 | Stretch goals | Not started |
