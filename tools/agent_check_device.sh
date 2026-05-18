#!/usr/bin/env bash
# tools/agent_check_device.sh
# Query device health endpoint and report status.
# Falls back to serial log instructions if device is unreachable.
# See docs/agent-scripts.md for details.
#
# Optional environment:
#   X4_DEVICE_IP          Device IP address on the local network
#   X4_DEVICE_API_TOKEN   Auth token for /api/dev/* endpoints

set -euo pipefail

DEVICE_IP="${X4_DEVICE_IP:-}"
API_TOKEN="${X4_DEVICE_API_TOKEN:-}"
HEALTH_URL="http://${DEVICE_IP}/api/health"
TIMEOUT=10

if [ -z "$DEVICE_IP" ]; then
  echo "[agent_check_device] No X4_DEVICE_IP set."
  echo ""
  echo "To check health via serial:"
  echo "  1. Connect USB and open a serial monitor at 115200 baud."
  echo "  2. Look for: [X4] HEALTH_OK or [X4] HEALTH_FAILED"
  echo "  3. After OTA, look for: [X4] OTA_MARK_VALID or [X4] OTA_ROLLBACK_REQUESTED"
  exit 0
fi

echo "[agent_check_device] Querying $HEALTH_URL ..."

CURL_ARGS=(-sf --max-time "$TIMEOUT")
if [ -n "$API_TOKEN" ]; then
  CURL_ARGS+=(-H "Authorization: Bearer $API_TOKEN")
fi

if ! RESPONSE=$(curl "${CURL_ARGS[@]}" "$HEALTH_URL" 2>&1); then
  echo "[agent_check_device] ERROR: Device unreachable at $DEVICE_IP" >&2
  echo ""
  echo "To check health via serial:"
  echo "  1. Connect USB and open a serial monitor at 115200 baud."
  echo "  2. Look for: [X4] HEALTH_OK or [X4] HEALTH_FAILED"
  echo "  3. After OTA, look for: [X4] OTA_MARK_VALID or [X4] OTA_ROLLBACK_REQUESTED"
  exit 1
fi

# Parse key fields (requires jq)
if command -v jq &>/dev/null; then
  STATUS=$(echo "$RESPONSE" | jq -r '.status // "unknown"')
  VERSION=$(echo "$RESPONSE" | jq -r '.version // "unknown"')
  SLOT=$(echo "$RESPONSE" | jq -r '.slot // "unknown"')
  PENDING=$(echo "$RESPONSE" | jq -r '.pending_verify // false')
  FAILED_STAGE=$(echo "$RESPONSE" | jq -r '.last_failed_stage // "none"')
  FAILED_REASON=$(echo "$RESPONSE" | jq -r '.last_failed_reason // "none"')

  echo "[agent_check_device] Status:         $STATUS"
  echo "[agent_check_device] Version:         $VERSION"
  echo "[agent_check_device] Slot:            $SLOT"
  echo "[agent_check_device] Pending verify:  $PENDING"
  if [ "$FAILED_STAGE" != "none" ] && [ "$FAILED_STAGE" != "null" ]; then
    echo "[agent_check_device] Failed stage:    $FAILED_STAGE"
    echo "[agent_check_device] Failed reason:   $FAILED_REASON"
  fi

  if [ "$STATUS" = "ok" ]; then
    echo "[agent_check_device] HEALTH CHECK PASSED"
    exit 0
  else
    echo "[agent_check_device] HEALTH CHECK FAILED (status=$STATUS)" >&2
    exit 1
  fi
else
  echo "[agent_check_device] jq not found; raw response:"
  echo "$RESPONSE"
  # Try a simple grep-based check
  if echo "$RESPONSE" | grep -q '"status":"ok"'; then
    echo "[agent_check_device] HEALTH CHECK PASSED"
    exit 0
  else
    echo "[agent_check_device] HEALTH CHECK FAILED or unable to parse" >&2
    exit 1
  fi
fi
