# EmberCast Hardware Notes (Arduino UNO Q)

Status: measured on 2026-10-25. The user approved commands on the board
over USB (adb) in chat for this session. The results are in Section 7.
The board clock was wrong (September 2025); dates here are laptop dates.

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
| CPU clock 2.0 GHz | `lscpu` | verified: 4 cores, 300-2016 MHz, governor schedutil |
| RAM 2 GB / eMMC 16 GB, or RAM 4 GB / eMMC 32 GB | `free -h; df -h` | this board: RAM 3.6 GiB, root 9.8 GB (3.0 GB free) |
| Wi-Fi 5, Bluetooth 5.1 | — | not verified |
| Debian 13, kernel 7.0.x, Mesa 25.x | `cat /etc/os-release; uname -a` | Debian 13 (trixie), kernel 6.16.7, Mesa 25.2.6 |
| MCU serial device /dev/ttyHS1, FQBN arduino:zephyr:unoq | `ls -l /dev/ttyHS1` | verified (both); core arduino:zephyr 0.51.0 |
| STM32U585: 2 MB flash, 786 KB SRAM | ST datasheet | not verified |
| GPU name: Adreno 702 (Arduino) or 704 (one news source) | `vulkaninfo --summary; clinfo` | clinfo: "FD702" (Adreno 702), rusticl |

## 3. Open questions (measure, do not assume)
- Q1: What is the Bridge round-trip latency and jitter? (Step 11)
- Q2 ANSWERED: yes. Platform rusticl (Mesa 25.2.6), device FD702, OpenCL 3.0,
  OpenCL C 1.2, 1 compute unit, 844 MHz, 3.58 GiB global memory. No env
  variable was needed. Original question: Does an OpenCL device show in `clinfo`? Does Mesa need an
  environment variable or an extra package? (Step 12)
- Q3: What is the sustained CPU clock under load? Does the board throttle?
- Q4 ANSWERED: thermal_zone3 cpuss0-thermal, thermal_zone4 cpuss1-thermal,
  thermal_zone7 gpu-thermal (millidegrees C). Ten zones in total.
  Original question: What are the thermal sensor paths under `/sys/class/thermal`?
- Q5 ANSWERED: library Arduino_LED_Matrix (in the zephyr core), class with
  begin(), loadFrame(const uint32_t[4]), loadPixels(), draw(), playSequence().
  The sketch still uses LED_BUILTIN only. Original question: What is the LED matrix library name and API on the MCU core?
- Q6 ANSWERED: none. The board image has no gcc, g++ or cmake, and no
  internet. Binaries are cross-built on the laptop with zig (D-025);
  -mcpu=cortex-a53 is used. Original question: Which compiler and CMake versions does the board have?
  Does the compiler accept `-mcpu=cortex-a53`?
- Q7 ANSWERED: `$/register` (Arduino_RouterBridge 0.2.2 bridge.h:
  `#define BIND_METHOD "$/register"`; router v0.5.3). This is the sidecar
  default. Original question: How does a Linux client register a method with arduino-router, so
  that an MCU `Bridge.notify("ec_sensor", ...)` reaches it? The sidecar
  sends the request `$/register` with params `["ec_sensor"]` by default.
  This name is NOT verified. Look in the arduino-router source or docs on
  the board, and pass the right name with `--register-method`.
- Q8: Does a call from Linux to a method that the MCU registered with
  `Bridge.provide_safe` work as a MessagePack-RPC request through the
  router (as the sidecar assumes for `ec_status` and `ec_ping`)?
- Q9 PARTLY ANSWERED: the core provides analogReadResolution(). The
  sketch must call analogReadResolution(10) to match its 1023 scale.
  Original question: What is the ADC resolution of `analogRead(A0)` on the UNO Q MCU core
  (the knob scale in the sketch assumes 10 bits)? Connect the knob to the
  3.3 V MCU header only.
- Q10 ANSWERED: yes. arduino-cli 1.3.1 on the board lists
  `arduino:zephyr:unoq`. The sketch compile needs the MsgPack library
  (not on the board, see Section 6). Original question: Does `arduino-cli` on the board accept the FQBN
  `arduino:zephyr:unoq`, and which port does it use for upload?

## 4. Commands for the user
### Cross-build with zig, install over USB (used on 2026-10-25)
The board has no compiler. Run on the laptop (macOS or Linux, needs
`brew install cmake zig android-platform-tools` or the same packages):
```
cmake -S . -B build-board -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-zig.cmake \
  -DCMAKE_BUILD_TYPE=Release -DEMBERCAST_NATIVE_ARCH=ON -DEMBERCAST_OPENCL=ON
cmake --build build-board -j
git ls-files | tar -cf /tmp/ec_src.tar -T -
adb shell 'mkdir -p ~/embercast' && adb push /tmp/ec_src.tar /home/arduino/embercast/
adb shell 'cd ~/embercast && tar -xf ec_src.tar && rm ec_src.tar && mkdir -p build/apps build/tests/cpp build-cl/apps'
for b in build-board/apps/embercast_*; do adb push $b /home/arduino/embercast/build/apps/; adb push $b /home/arduino/embercast/build-cl/apps/; done
adb push build-board/tests/cpp/embercast_tests /home/arduino/embercast/build/tests/cpp/
adb shell 'cd ~/embercast && ./build/tests/cpp/embercast_tests | tail -3'
```
The OpenCL build uses the GPU only with `--backend opencl`, so one binary
set serves both `build/` and `build-cl/`.

### Other commands
- Board facts: `bash scripts/print_board_probe.sh`
- Build, tests, benchmark, live run (Steps 8-10): `bash scripts/print_board_build.sh`
- MCU sketch and sidecar (Step 11): `bash scripts/print_mcu_commands.sh`

These scripts only print commands. Probe commands: Run each command on
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

## 5. API facts used by the MCU code (from the Arduino_RouterBridge README)
- `Bridge.begin()`, `Bridge.provide(name, fn)`, `Bridge.provide_safe(name, fn)`,
  `Bridge.notify(name, args...)`, `Bridge.call(name, args...).result(x)`.
- The README warns: a `Bridge.call` from inside an RPC callback can
  deadlock the MCU-CPU link. The sketch does not do this.
- Source: https://github.com/arduino-libraries/Arduino_RouterBridge (README).
- The Python client package "arduino-router-bridge" was not found in a web
  search. The sidecar uses its own MessagePack-RPC client
  (`bridge/msgpack_lite.py`, no package installation needed).

## 6. Status of the board steps
| Step | What exists | Waiting for |
|---|---|---|
| 8 | Native-build and cross-build commands; `cmake/aarch64-linux-gnu.cmake` | The user runs them. Cross-build not tested (no aarch64 sysroot in the container). |
| 9 | TCP link with reconnect and backoff, laptop-clock round trip, tests on localhost | A run against the board |
| 10 | `embercast_bench`, K sweep, daemon latency CSV | The user runs Step 10a/10b |
| 11 | Sketch, sidecar, daemon MCU socket, tests with a fake router | The MsgPack library on the board, then flash (section 6a) |

## 6a. Flash the MCU sketch (Step 11)
`arduino-cli compile` on the board fails: `MsgPack.h: No such file or
directory`. Arduino_RPCLite needs the libraries MsgPack 0.4.2, DebugLog
0.8.4, ArxContainer 0.7.0 and ArxTypeTraits 0.3.1 (the versions in the
example app `~/ArduinoApps/pretty-pictures/sketch/sketch.yaml`). The board
has no internet. Option A (simplest): connect the board to Wi-Fi in
Arduino App Lab, then on the board:
```
arduino-cli lib install MsgPack@0.4.2 DebugLog@0.8.4 ArxContainer@0.7.0 ArxTypeTraits@0.3.1
```
Option B (no Wi-Fi): on the laptop, get the four libraries from the
Arduino Library Manager (or GitHub, user hideakitai) at those versions,
then `adb push <LIB_DIR> /home/arduino/Arduino/libraries/` for each one.
Then compile and upload on the board (the upload command is not tested yet),
and start the sidecar:
```
adb shell 'cd ~/embercast && arduino-cli compile -b arduino:zephyr:unoq mcu/sensor_station && arduino-cli upload -b arduino:zephyr:unoq mcu/sensor_station'
bash scripts/print_mcu_commands.sh     # the sidecar and daemon commands
```

## 7. Results from the board (2026-10-25, over adb)
- C++ tests: 34/34 test cases, 218836 assertions pass (cross-built, D-025).
  OpenCL build: the opencl AC2 test passes on the Adreno 702 (16/16).
- Engine benchmark: results/bench_board.md. cpu_opt, 4 threads, K = 64:
  decision mean 21.9 ms, p99 60.6 ms. cpu_naive: mean 873.6 ms.
  opencl: mean 2049 ms.
- Live run over USB: results/bench_live_board.md. Round trip p50 75.9 ms,
  p99 98.1 ms (laptop clock).
- Q1 (Bridge round trip) and Q3 (clock under load) are still open.
  Q8 needs the flashed sketch.
