#!/usr/bin/env bash
# tools/agent_build_release.sh
# Build production firmware with all diagnostics explicitly disabled.
# See docs/agent-scripts.md and docs/build-guide.md for details.

set -euo pipefail

echo "[agent_build_release] Starting release firmware build (all diagnostics disabled)..."

pio run --environment x4_journal

BIN_PATH=$(find .pio/build/x4_journal -maxdepth 1 -name "*.bin" | head -n 1)
if [ -z "$BIN_PATH" ]; then
  echo "[agent_build_release] ERROR: No .bin file found in .pio/build/x4_journal after build." >&2
  exit 1
fi

# Verify that no dev-diagnostics symbols made it into the release binary
if command -v strings &>/dev/null; then
  if strings "$BIN_PATH" | grep -q "DEV_DIAGNOSTICS_ENABLED"; then
    echo "[agent_build_release] WARNING: DEV_DIAGNOSTICS_ENABLED string found in release binary — check build flags" >&2
  fi
fi

echo "[agent_build_release] Release build succeeded: $BIN_PATH"
