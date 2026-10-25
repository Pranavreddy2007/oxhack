# EmberCast Demo

Simulation only. Not for operational use.

## 1. Start
Laptop only (one command, AC6):
```
./scripts/demo_laptop.sh
```
Open http://127.0.0.1:8000/ in a browser.

Variants:
- `ENGINE=inprocess ./scripts/demo_laptop.sh`: no daemon. The engine runs
  inside sim_stream. Use this if a port is busy.
- `BOARD=<BOARD_IP>:7070 ./scripts/demo_laptop.sh`: the engine runs on the
  UNO Q. First start the daemon on the board yourself (see
  `scripts/print_board_build.sh`, Step 9/10b).
- `RATE=8` makes the ticks faster. `SEED=1003` selects another TUNE scenario.

Controls: W = wind shift (+90 degrees, all lanes), C = cut link,
R = restore link, B = show B0 in place of B1, E = results screen,
Esc = close the results screen.

## 2. Demo script (3 minutes)
| Time | Action | What the audience sees |
|---|---|---|
| 0:00 | Show the page header. | "Simulation only. Not for operational use." |
| 0:10 | Point to the two maps. | Terrain, fuel shades, asset clusters (blue, letters), fire start. |
| 0:30 | Push W. | The wind arrow turns. The EmberCast orange P(burn) area moves and becomes wider. |
| 0:50 | Watch the crew markers. | EmberCast moves crews to the clusters at risk. B1 reacts later. |
| 1:20 | Point to the loss counters. | The EmberCast and baseline losses diverge (the size changes with the seed). |
| 1:40 | Point to the latency panel. | Round trip and engine time. With the board: board clock numbers. |
| 2:00 | Push C. | EmberCast badge shows STALE, "last plan kept". The daemon console prints STALE. |
| 2:20 | Push R. | The link reconnects with backoff. The badge returns to OK. |
| 2:35 | Push E. | TEST results with 95% CIs from results/summary.json. |

The live loss of one episode is one sample. The claim comes from the TEST
evaluation (results/report.md), not from the live run.

## 3. Fallback plan
| Problem | Action |
|---|---|
| Board not reachable | Run the laptop-only command. The same engine runs on the laptop. |
| Port 7070 busy | `ENGINE=inprocess ./scripts/demo_laptop.sh` |
| Browser problem | The sim_stream console prints the episode losses. results/report.md has the TEST results. |
| MCU not ready | Use the W key in place of the knob. |

## 4. Manual test procedure
Laptop:
1. `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j`
2. `ctest --test-dir build --output-on-failure` -> all tests pass.
3. `.venv/bin/pytest -q` -> all tests pass (needs the build from step 1).
4. `./scripts/demo_laptop.sh`, open the page. Check: the maps move,
   W turns the wind, C gives STALE on the EmberCast badge within one tick,
   R gives OK again, E shows the results.
5. `scripts/e2e_demo.sh` -> a scripted 60-second run on localhost with
   wind shift, cut link and restore. It prints PASS or FAIL.

Board (the user runs these; Claude does not):
1. `bash scripts/print_board_build.sh` and run the Step 8a commands.
2. Run the Step 10a benchmark commands. Copy the CSV back.
3. Run the Step 9/10b live commands. Repeat laptop step 4 with
   `BOARD=<BOARD_IP>:7070`. Unplug the network: the daemon console prints
   STALE; the dashboard shows STALE.
