#!/usr/bin/env bash
set -euo pipefail

# Cross-compile both Daisy Seed programs: the benchmark and the pedal firmware. Pass "program-dfu" to flash
# one as well (the Seed must be in DFU mode: hold BOOT, press RESET): the benchmark, or the firmware with
# APP=firmware.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || echo "$SCRIPT_DIR/../..")"

cd "$REPO_ROOT"

# Auto-detect toolchain
for p in /Users/ww/tools/xpack-arm-none-eabi-gcc-*/bin /opt/homebrew/bin /usr/local/bin; do
    [ -x "$p/arm-none-eabi-gcc" ] && export PATH="$p:$PATH" && break
done

# libDaisy
export LIBDAISY_DIR="${LIBDAISY_DIR:-/Users/ww/src/libDaisy}"
if [ ! -d "$LIBDAISY_DIR/core" ]; then
    echo "ERROR: libDaisy not found at $LIBDAISY_DIR"
    echo "  git clone --recurse-submodules https://github.com/electro-smith/libDaisy.git $LIBDAISY_DIR"
    exit 1
fi
if [ ! -f "$LIBDAISY_DIR/build/libdaisy.a" ]; then
    echo "==> Building libDaisy (first time only)..."
    make -C "$LIBDAISY_DIR" -j4
fi

# DaisySP (only its DaisySP-LGPL ReverbSc source is compiled)
export DAISYSP_SRC_DIR="${DAISYSP_SRC_DIR:-/Users/ww/src/DaisySP}"
if [ ! -f "$DAISYSP_SRC_DIR/DaisySP-LGPL/Source/Effects/reverbsc.cpp" ]; then
    echo "ERROR: DaisySP with its DaisySP-LGPL submodule not found at $DAISYSP_SRC_DIR"
    echo "  git clone --recurse-submodules https://github.com/electro-smith/DaisySP.git $DAISYSP_SRC_DIR"
    exit 1
fi

for app in bench firmware; do
    echo "==> Building the Daisy Seed $app..."
    make -C daisy -j4 APP=$app LIBDAISY_DIR="$LIBDAISY_DIR" DAISYSP_SRC_DIR="$DAISYSP_SRC_DIR"
done
echo "==> Built: daisy/build/vocal_daisy_bench.bin (benchmark), daisy/build/vocal_daisy.bin (firmware)"

if [ "${1:-}" = "program-dfu" ]; then
    make -C daisy APP="${APP:-bench}" LIBDAISY_DIR="$LIBDAISY_DIR" DAISYSP_SRC_DIR="$DAISYSP_SRC_DIR" program-dfu
fi
