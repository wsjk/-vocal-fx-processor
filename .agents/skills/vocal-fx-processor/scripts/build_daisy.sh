#!/usr/bin/env bash
set -euo pipefail

# Cross-compile the Daisy Seed benchmark firmware. Pass "program-dfu" to flash it as well
# (the Seed must be in DFU mode: hold BOOT, press RESET).
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

echo "==> Building benchmark firmware for the Daisy Seed..."
make -C daisy -j4 LIBDAISY_DIR="$LIBDAISY_DIR"
echo "==> Firmware built: daisy/build/vocal_daisy_bench.bin"

if [ "${1:-}" = "program-dfu" ]; then
    make -C daisy LIBDAISY_DIR="$LIBDAISY_DIR" program-dfu
fi
