cmake_minimum_required(VERSION 3.20)
project(EmberCast VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
# Rule F: use -O3. docs/BENCHMARKING.md lists all flags.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

option(EMBERCAST_OPENCL "Build the optional OpenCL backend" OFF)
option(EMBERCAST_NATIVE_ARCH "Add -mcpu=cortex-a53 (use only after the board compiler accepts it)" OFF)
option(EMBERCAST_BUILD_TESTS "Build the C++ tests" ON)

add_compile_options(-Wall -Wextra -Wpedantic)
if(EMBERCAST_NATIVE_ARCH)
  add_compile_options(-mcpu=cortex-a53)
endif()

find_package(Threads REQUIRED)

add_subdirectory(engine)
add_subdirectory(sim)
add_subdirectory(apps)

if(EMBERCAST_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/cpp)
endif()
