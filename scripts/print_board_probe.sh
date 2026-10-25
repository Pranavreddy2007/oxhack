#!/usr/bin/env bash
# Prints the UNO Q probe commands. It does NOT run them.
# The user runs them by hand and pastes the output into docs/HARDWARE.md.
set -euo pipefail
cat <<'CMDS'
# Run on your laptop. Replace <BOARD_USER> and <BOARD_IP>.
ssh <BOARD_USER>@<BOARD_IP> 'lscpu; free -h; df -h; uname -a; cat /etc/os-release'
ssh <BOARD_USER>@<BOARD_IP> 'ls /sys/class/thermal; for z in /sys/class/thermal/thermal_zone*; do echo "$z $(cat $z/type) $(cat $z/temp)"; done'
ssh <BOARD_USER>@<BOARD_IP> 'gcc --version; g++ --version; cmake --version'
ssh <BOARD_USER>@<BOARD_IP> 'vulkaninfo --summary; clinfo'
ssh <BOARD_USER>@<BOARD_IP> 'ls -l /var/run/arduino-router.sock; ls -l /dev/ttyHS1'
CMDS
