#!/usr/bin/env bash
# tools/agent_build_dev.sh
# Build firmware with development diagnostics flags enabled.
# See docs/agent-scripts.md and docs/build-guide.md for details.
#
# To enable diagnostics, add the following to build_flags in platformio.ini:
#   -DCONFIG_X4_DEV_DIAGNOSTICS=1
#   -DCONFIG_X4_DIAG_HTTP_API=1

set -euo pipefail

echo "[agent_build_dev] Starting dev diagnostics firmware build..."

pio run --environment x4_journal

BIN_PATH=$(find .pio/build/x4_journal -maxdepth 1 -name "*.bin" | head -n 1)
if [ -z "$BIN_PATH" ]; then
  echo "[agent_build_dev] ERROR: No .bin file found in .pio/build/x4_journal after build." >&2
  exit 1
fi

echo "[agent_build_dev] Dev build succeeded: $BIN_PATH"
