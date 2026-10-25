#!/usr/bin/env bash
# Prints the UNO Q build, test, benchmark, and live-run commands (Steps 8-10).
# It does NOT run them. The user runs them by hand and pastes the output
# into docs/HARDWARE.md. Replace <BOARD_USER> and <BOARD_IP>.
set -euo pipefail
cat <<'CMDS'
### Step 8a: native build on the board (preferred)
# 1. Copy the source (from the repository root on the laptop):
rsync -a --exclude build --exclude 'build-*' --exclude .venv --exclude results/raw ./ <BOARD_USER>@<BOARD_IP>:~/embercast/
# 2. Tool versions, then build and run the C++ tests:
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && cmake --version | head -1 && g++ --version | head -1 && nproc'
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j4 2>&1 | tail -3 && ./build/tests/cpp/embercast_tests | tail -3'
# 3. Does the compiler accept -mcpu=cortex-a53? (prints ACCEPTED or an error)
ssh <BOARD_USER>@<BOARD_IP> 'echo "int main(){}" | g++ -mcpu=cortex-a53 -x c++ - -o /tmp/ec_a53 && echo ACCEPTED'
# 4. Only if step 3 printed ACCEPTED:
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && cmake -S . -B build-a53 -DCMAKE_BUILD_TYPE=Release -DEMBERCAST_NATIVE_ARCH=ON && cmake --build build-a53 -j4 2>&1 | tail -1'

### Step 8b: cross-build on the laptop (optional; needs g++-aarch64-linux-gnu on the laptop)
cmake -S . -B build-aarch64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-aarch64 -j
scp build-aarch64/apps/embercast_board_daemon build-aarch64/apps/embercast_bench <BOARD_USER>@<BOARD_IP>:~/

### Step 10a: board-only engine benchmark (board monotonic clock)
# Thermal paths first (temperature is reported only from a verified path):
ssh <BOARD_USER>@<BOARD_IP> 'for z in /sys/class/thermal/thermal_zone*; do echo "$z $(cat $z/type) $(cat $z/temp)"; done'
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && for t in 1 2 4; do ./build/apps/embercast_bench --backend cpu_opt --threads $t --episodes 100 --out-dir results/raw/board; done; ./build/apps/embercast_bench --backend cpu_naive --threads 1 --episodes 5 --out-dir results/raw/board'
# K and H budget sweep (choose K and H from these numbers):
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && for k in 32 64 128 256; do ./build/apps/embercast_bench --backend cpu_opt --threads 4 --K $k --episodes 20 --out-dir results/raw/board; done'
# Copy the raw CSV back and make the tables on the laptop:
scp -r <BOARD_USER>@<BOARD_IP>:~/embercast/results/raw/board results/raw/
.venv/bin/python -m laptop.tools.bench_report --raw results/raw/board/*_raw.csv --out results/bench_board.md --title "UNO Q benchmark tables"

### Step 9/10b: live inference on the board, laptop drives it over TCP
# On the board (leave it running; Ctrl-C writes the latency CSV):
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && ./build/apps/embercast_board_daemon --listen 0.0.0.0 --port 7070 --threads 4 --log results/raw/board/live_decisions.jsonl --bench-out results/raw/board/live_daemon_latency.csv'
# On the laptop:
BOARD=<BOARD_IP>:7070 ./scripts/demo_laptop.sh
# Round-trip CSV (laptop clock) is then in results/raw/live/rtt.csv:
.venv/bin/python -m laptop.tools.bench_report --raw results/raw/live/rtt.csv --out results/bench_live.md --title "Live round trip, laptop clock"
CMDS
