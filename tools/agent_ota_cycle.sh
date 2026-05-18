#!/usr/bin/env bash
# tools/agent_ota_cycle.sh
# Full OTA iteration cycle: build → publish → trigger check → wait for reboot → verify health.
# See docs/agent-scripts.md for details.
#
# Optional environment:
#   X4_DEVICE_IP         Device IP address
#   X4_BUILD_DEV         Set to 1 to use agent_build_dev.sh instead of agent_build.sh
#   X4_OTA_BASE_URL      Required for publishing
#   X4_DEVICE_API_TOKEN  Auth token for /api/* endpoints
#   X4_REBOOT_WAIT_SEC   Seconds to wait for device reboot (default: 120)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEVICE_IP="${X4_DEVICE_IP:-}"
API_TOKEN="${X4_DEVICE_API_TOKEN:-}"
REBOOT_WAIT="${X4_REBOOT_WAIT_SEC:-120}"
BUILD_DEV="${X4_BUILD_DEV:-0}"

fail() {
  echo "[agent_ota_cycle] OTA CYCLE FAILED: $*" >&2
  exit 1
}

# Step 1: Build
echo "[agent_ota_cycle] Step 1/5: Building firmware..."
if [ "$BUILD_DEV" = "1" ]; then
  "$SCRIPT_DIR/agent_build_dev.sh" || fail "Build failed"
else
  "$SCRIPT_DIR/agent_build.sh" || fail "Build failed"
fi

# Step 2: Publish OTA artifact
echo "[agent_ota_cycle] Step 2/5: Publishing OTA artifact..."
"$SCRIPT_DIR/agent_publish_ota.sh" || fail "Publish failed"

if [ -z "$DEVICE_IP" ]; then
  echo ""
  echo "[agent_ota_cycle] No X4_DEVICE_IP set. Cannot trigger OTA automatically."
  echo ""
  echo "Manual steps:"
  echo "  1. Ensure the manifest file is accessible at the configured URL."
  echo "  2. On the device, trigger: POST /api/ota/check then POST /api/ota/apply"
  echo "  3. After the device reboots, run: ./tools/agent_check_device.sh"
  exit 0
fi

CURL_ARGS=(-sf --max-time 15)
if [ -n "$API_TOKEN" ]; then
  CURL_ARGS+=(-H "Authorization: Bearer $API_TOKEN")
fi
CURL_ARGS+=(-H "Content-Type: application/json")

BASE_URL="http://${DEVICE_IP}"

# Step 3: Trigger OTA check
echo "[agent_ota_cycle] Step 3/5: Triggering OTA manifest check..."
CHECK_RESPONSE=$(curl "${CURL_ARGS[@]}" -X POST "$BASE_URL/api/ota/check" 2>&1) || \
  fail "OTA check request failed (device unreachable?)"

echo "[agent_ota_cycle] OTA check response: $CHECK_RESPONSE"

if echo "$CHECK_RESPONSE" | grep -q '"result":"up_to_date"'; then
  echo "[agent_ota_cycle] Device is already up to date. OTA CYCLE PASSED (no update needed)."
  exit 0
fi

if ! echo "$CHECK_RESPONSE" | grep -q '"result":"update_available"'; then
  fail "OTA check did not return update_available. Response: $CHECK_RESPONSE"
fi

# Apply the update
echo "[agent_ota_cycle] Applying OTA update..."
curl "${CURL_ARGS[@]}" -X POST "$BASE_URL/api/ota/apply" \
  -d '{"confirm":true}' > /dev/null || fail "OTA apply request failed"

# Step 4: Wait for reboot
echo "[agent_ota_cycle] Step 4/5: Waiting for device to reboot (up to ${REBOOT_WAIT}s)..."

# Wait for device to go offline
OFFLINE_DEADLINE=$(( SECONDS + 60 ))
while [ $SECONDS -lt $OFFLINE_DEADLINE ]; do
  if ! curl -sf --max-time 3 "$BASE_URL/api/health" > /dev/null 2>&1; then
    echo "[agent_ota_cycle] Device went offline (rebooting)."
    break
  fi
  sleep 2
done

# Wait for device to come back online
ONLINE_DEADLINE=$(( SECONDS + REBOOT_WAIT ))
echo "[agent_ota_cycle] Waiting for device to come back online..."
while [ $SECONDS -lt $ONLINE_DEADLINE ]; do
  if curl -sf --max-time 5 "$BASE_URL/api/health" > /dev/null 2>&1; then
    echo "[agent_ota_cycle] Device is back online."
    break
  fi
  sleep 3
done

if ! curl -sf --max-time 5 "$BASE_URL/api/health" > /dev/null 2>&1; then
  fail "Device did not come back online within ${REBOOT_WAIT}s"
fi

# Step 5: Verify health
echo "[agent_ota_cycle] Step 5/5: Verifying device health..."
"$SCRIPT_DIR/agent_check_device.sh" || fail "Health check failed after OTA"

echo ""
echo "[agent_ota_cycle] OTA CYCLE PASSED"
