# Cross-build toolchain for the UNO Q Linux side (Debian, aarch64).
# Needs a cross compiler on the LAPTOP, for example (Debian/Ubuntu laptop):
#   sudo apt install g++-aarch64-linux-gnu
# Not tested in the development container (no aarch64 sysroot there).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
