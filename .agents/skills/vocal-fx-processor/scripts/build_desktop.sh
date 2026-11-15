#!/usr/bin/env bash
set -euo pipefail

# Build the desktop app (requires PortAudio and raylib).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel 2>/dev/null || echo "$SCRIPT_DIR/../..")"

cd "$REPO_ROOT"

echo "==> Building desktop app..."
cmake -B build -S . -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build -j4

echo "==> Desktop build complete: ./build/vocal_desktop"
