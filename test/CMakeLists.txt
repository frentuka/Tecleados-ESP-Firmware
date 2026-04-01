# DF-ONE Firmware — Host Test Build
#
# This CMakeLists.txt is for the HOST test suite only.
# It does NOT interact with ESP-IDF or the firmware build.
#
# Usage:
#   cd test/
#   cmake -B build
#   cmake --build build
#   ./build/test_runner
#
cmake_minimum_required(VERSION 3.16)
project(df_one_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-function -Wno-unused-variable)

# Include paths (mocks and test harness)
include_directories(include)

# Single-translation-unit build (main.c includes all test files)
add_executable(test_runner main.c)
target_link_libraries(test_runner m)

# CTest integration
enable_testing()
add_test(NAME df_one_tests COMMAND test_runner)
