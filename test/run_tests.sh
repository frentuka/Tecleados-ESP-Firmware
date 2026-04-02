#!/usr/bin/env bash
#
# TEF-Oxide Firmware — Host Test Runner
#
# Compiles and runs all unit tests on the host machine.
# No ESP-IDF or target hardware required.
#
# Usage:
#   cd test/
#   bash run_tests.sh
#
# Requirements:
#   - GCC (any reasonably modern version with C99 support)
#   - OR: Clang, MSVC (cl), MinGW — adjust CC below
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

CC="${CC:-gcc}"
CFLAGS="-std=c99 -Wall -Wextra -Wpedantic -Wno-unused-function -Wno-unused-variable -O2"
INCLUDES="-Iinclude"
OUTPUT="test_runner"

echo "============================================"
echo "  TEF-Oxide Firmware — Building Host Tests"
echo "============================================"
echo ""
echo "Compiler: $CC"
echo "Flags:    $CFLAGS"
echo ""

# Compile
$CC $CFLAGS $INCLUDES -o "$OUTPUT" main.c -lm 2>&1

if [ $? -ne 0 ]; then
    echo ""
    echo "BUILD FAILED"
    exit 1
fi

echo "Build successful."
echo ""

# Run
./"$OUTPUT"
EXIT_CODE=$?

# Cleanup (optional — comment out to keep the binary)
# rm -f "$OUTPUT"

exit $EXIT_CODE
