#!/usr/bin/env bash
# Prints the Step 11 MCU commands. It does NOT run them.
# Flashing the MCU needs explicit user approval (CLAUDE.md section 8).
# The FQBN and the serial port are REPORTED values. Verify them first.
set -euo pipefail
cat <<'CMDS'
### Step 11a: verify the tools and the board identity (on the board)
ssh <BOARD_USER>@<BOARD_IP> 'which arduino-cli arduino-app-cli; arduino-cli board list; arduino-cli lib list | grep -i -E "RouterBridge|Matrix"; ls -l /var/run/arduino-router.sock'

### Step 11b: compile, then (only with approval) flash the sketch
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && arduino-cli compile --fqbn arduino:zephyr:unoq mcu/sensor_station'
# FLASH (approval needed). Use the port that "arduino-cli board list" printed:
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && arduino-cli upload --fqbn arduino:zephyr:unoq -p <MCU_PORT> mcu/sensor_station'
# (If you use Arduino App Lab, put the sketch in an App and start it there.)

### Step 11c: run the daemon with the MCU socket, then the sidecar (two terminals on the board)
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && ./build/apps/embercast_board_daemon --listen 0.0.0.0 --port 7070 --threads 4 --mcu-uds /tmp/embercast_mcu.sock --sensor-source laptop'
ssh <BOARD_USER>@<BOARD_IP> 'cd ~/embercast && python3 bridge/mcu_sidecar.py --daemon-uds /tmp/embercast_mcu.sock --ping-out results/raw/board/bridge_ping.csv'
# Expected: "[sidecar] connected to router", "[daemon] MCU sidecar connected", "[daemon] MCU sensor frames OK".
# If the sidecar prints "register failed", find the router registration method (docs/HARDWARE.md Q7)
# and pass it with --register-method.

### Step 11d: Bridge latency table (laptop, after you copy the CSV back)
scp <BOARD_USER>@<BOARD_IP>:~/embercast/results/raw/board/bridge_ping.csv results/raw/board/
.venv/bin/python -m laptop.tools.bench_report --raw results/raw/board/bridge_ping.csv --out results/bench_bridge.md --title "Bridge round trip (Linux -> MCU -> Linux)"

### Failure checks
# MCU silent: stop the sketch (or unplug the knob path). The daemon prints SENSOR_LOST.
# Linux silent: stop the daemon (Ctrl-C). After 3 s the MCU LED stays ON (FAULT).
CMDS
