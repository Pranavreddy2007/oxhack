# Cross-build toolchain for the UNO Q Linux side (Debian 13, aarch64), with
# zig as the cross compiler. Works on macOS and Linux laptops:
#   brew install zig        (or download from ziglang.org)
#   cmake -S . -B build-board -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-zig.cmake -DCMAKE_BUILD_TYPE=Release
# Target glibc 2.36 or later (the board has glibc 2.41, see docs/HARDWARE.md).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zcc)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zcxx)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# The macOS ar writes BSD archives that ld.lld cannot link for Linux.
set(CMAKE_AR ${CMAKE_CURRENT_LIST_DIR}/zig/zar CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB ${CMAKE_CURRENT_LIST_DIR}/zig/zranlib CACHE FILEPATH "" FORCE)
