# EmberCast Hardware Notes (Arduino UNO Q)

Status: stub (Step 1). No command has run on the board yet.
Claude does not run commands on the board. The user runs the commands
below and pastes the output in Section 5.

## 1. Facts documented by Arduino
- MPU: Qualcomm Dragonwing QRB2210, 4x Arm Cortex-A53, Debian Linux.
- MCU: STMicroelectronics STM32U585, Arm Cortex-M33 up to 160 MHz,
  Arduino Core on Zephyr OS.
- GPU: Adreno 702, integrated, 845 MHz, memory shared with system RAM.
- GPU APIs listed by Arduino: OpenGL ES 3.1 (freedreno), Vulkan (turnip,
  driver 1.0.318), OpenCL 2.0 (Mesa).
- Arduino gives no UNO Q-specific OpenGL or Vulkan examples.
- MPU GPIO is 1.8 V. MCU I/O is 3.3 V. Do not connect them directly.
- MPU-MCU link: the arduino-router daemon brokers MessagePack-RPC.
  Linux clients connect to a Unix socket. The Python client
  "arduino-router-bridge" uses `unix:///var/run/arduino-router.sock` by
  default. It supports notify, call (with timeout), and provide.
- MCU library: Arduino_RouterBridge (Bridge.provide, provide_safe, notify).
- Any process that can open the router socket can call provided methods.
  The socket is a trust boundary.
- Two board variants exist.

## 2. Reported by other sources (verify on the board)
| Claim | Command | Result |
|---|---|---|
| CPU clock 2.0 GHz | `lscpu` | not verified |
| RAM 2 GB / eMMC 16 GB, or RAM 4 GB / eMMC 32 GB | `free -h; df -h` | not verified |
| Wi-Fi 5, Bluetooth 5.1 | — | not verified |
| Debian 13, kernel 7.0.x, Mesa 25.x | `cat /etc/os-release; uname -a` | not verified |
| MCU serial device /dev/ttyHS1, FQBN arduino:zephyr:unoq | `ls -l /dev/ttyHS1` | not verified |
| STM32U585: 2 MB flash, 786 KB SRAM | ST datasheet | not verified |
| GPU name: Adreno 702 (Arduino) or 704 (one news source) | `vulkaninfo --summary; clinfo` | not verified |

## 3. Open questions (measure, do not assume)
- Q1: What is the Bridge round-trip latency and jitter? (Step 11)
- Q2: Does an OpenCL device show in `clinfo`? Does Mesa need an
  environment variable or an extra package? (Step 12)
- Q3: What is the sustained CPU clock under load? Does the board throttle?
- Q4: What are the thermal sensor paths under `/sys/class/thermal`?
- Q5: What is the LED matrix library name and API on the MCU core?
- Q6: Which compiler and CMake versions does the board have?
  Does the compiler accept `-mcpu=cortex-a53`?

## 4. Commands for the user
Print the list with `scripts/print_board_probe.sh`. Run each command on
your laptop. Replace `<BOARD_USER>` and `<BOARD_IP>`.
```
ssh <BOARD_USER>@<BOARD_IP> 'lscpu; free -h; df -h; uname -a; cat /etc/os-release'
ssh <BOARD_USER>@<BOARD_IP> 'ls /sys/class/thermal; for z in /sys/class/thermal/thermal_zone*; do echo "$z $(cat $z/type) $(cat $z/temp)"; done'
ssh <BOARD_USER>@<BOARD_IP> 'gcc --version; g++ --version; cmake --version'
ssh <BOARD_USER>@<BOARD_IP> 'vulkaninfo --summary; clinfo'
ssh <BOARD_USER>@<BOARD_IP> 'ls -l /var/run/arduino-router.sock; ls -l /dev/ttyHS1'
```
If a command is not found, do not install a package. Tell Claude. Claude
then asks for approval before any installation.

## 5. Results from the board
No results yet.
