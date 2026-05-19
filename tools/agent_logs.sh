#!/usr/bin/env bash
# tools/agent_logs.sh
# Fetch recent firmware logs from the device API or print serial instructions.
# See docs/agent-scripts.md for details.
#
# Optional environment:
#   X4_DEVICE_IP         Device IP address
#   X4_DEVICE_API_TOKEN  Auth token; if set, uses /api/dev/logs (more detail)

set -euo pipefail

DEVICE_IP="${X4_DEVICE_IP:-}"
API_TOKEN="${X4_DEVICE_API_TOKEN:-}"
TIMEOUT=15

if [ -z "$DEVICE_IP" ]; then
  echo "[agent_logs] No X4_DEVICE_IP set."
  echo ""
  echo "To read logs via serial:"
  echo "  1. Connect USB and open a serial monitor at 115200 baud (idf.py monitor)."
  echo "  2. Filter for structured markers:  idf.py monitor | grep '\\[X4\\]'"
  echo "  3. Key markers:"
  echo "       [X4] BOOT_START    — firmware started"
  echo "       [X4] HEALTH_OK     — all health checks passed"
  echo "       [X4] HEALTH_FAILED stage=<s> reason=<r>"
  echo "       [X4] OTA_MARK_VALID"
  echo "       [X4] OTA_ROLLBACK_REQUESTED reason=<r>"
  exit 0
fi

CURL_ARGS=(-sf --max-time "$TIMEOUT")

# Prefer /api/dev/logs if a token is configured (returns more detail)
if [ -n "$API_TOKEN" ]; then
  LOGS_URL="http://${DEVICE_IP}/api/dev/logs"
  CURL_ARGS+=(-H "Authorization: Bearer $API_TOKEN")
else
  LOGS_URL="http://${DEVICE_IP}/api/logs"
fi

echo "[agent_logs] Fetching logs from $LOGS_URL ..."

RESPONSE=$(curl "${CURL_ARGS[@]}" "$LOGS_URL" 2>&1) || {
  echo "[agent_logs] ERROR: Could not reach device at $DEVICE_IP" >&2
  echo ""
  echo "Fallback: connect USB and run:  idf.py monitor | grep '\\[X4\\]'"
  exit 1
}

# Check for 501 Not Implemented
if echo "$RESPONSE" | grep -q '"log_buffer_not_available"'; then
  echo "[agent_logs] Log buffer not available via API."
  echo ""
  echo "Fallback: connect USB and run:  idf.py monitor | grep '\\[X4\\]'"
  exit 0
fi

# Print log lines (requires jq for pretty output; fall back to raw)
if command -v jq &>/dev/null; then
  echo "$RESPONSE" | jq -r '.lines[]?' 2>/dev/null || echo "$RESPONSE"
else
  echo "$RESPONSE"
fi
