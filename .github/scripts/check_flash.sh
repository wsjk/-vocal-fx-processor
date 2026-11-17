#!/usr/bin/env bash
set -euo pipefail

# Fail if a Daisy Seed program does not fit the STM32H750's 128 KB internal flash.
# The .bin is the flash image (libDaisy's default BOOT_NONE build), so its size is the flash used:
# the same figure as the linker's "FLASH:" line.
#   Usage: check_flash.sh daisy/build/*.bin
LIMIT=$((128 * 1024))
[ "$#" -gt 0 ] || { echo "No .bin files given"; exit 1; }
status=0
for bin in "$@"; do
    size=$(wc -c < "$bin" | tr -d ' ')
    pct=$((size * 100 / LIMIT))
    if [ "$size" -le "$LIMIT" ]; then
        echo "OK    $bin: $size of $LIMIT bytes ($pct %)"
    else
        echo "FAIL  $bin: $size of $LIMIT bytes ($pct %), too big for the internal flash"
        echo "      Shrink it, or move to the Daisy bootloader and QSPI (docs/HARDWARE_PLAN.md §3)."
        status=1
    fi
done
exit "$status"
