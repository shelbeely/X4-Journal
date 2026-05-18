# Safe Mode — Recovery Spec

## Overview

Safe mode is a minimal, recovery-focused boot path that can be entered by holding
a physical button at power-on. It bypasses experimental and non-essential application
features while keeping OTA and Wi-Fi available so the device can be recovered remotely.

Safe mode is life-support infrastructure. It must never be removed or made
unreachable by other firmware changes.

---

## Entry Trigger

| Parameter | Value | Notes |
|-----------|-------|-------|
| GPIO | Boot button (GPIO0 on most ESP32 boards) or the primary button defined in the existing firmware | Must be determined from the hardware schematic or existing button driver |
| Hold duration | ≥ 3 seconds | Measured from power-on / reset release |
| Debounce | 50 ms | Applied before starting the hold timer |
| Detection point | Early in `app_main`, before any non-essential subsystem init | |

The hold-duration and GPIO are configurable via Kconfig:

| Kconfig Symbol | Default | Description |
|----------------|---------|-------------|
| `CONFIG_SAFE_MODE_GPIO` | `0` (GPIO0) | GPIO number of the safe mode entry button |
| `CONFIG_SAFE_MODE_HOLD_MS` | `3000` | Hold duration in milliseconds |

If the existing firmware already defines a primary button GPIO, `CONFIG_SAFE_MODE_GPIO`
should default to that value.

---

## What Safe Mode Skips

Safe mode does **not** start:

- Experimental or unverified application features
- Non-essential UI components (animated splash screens, complex widgets)
- Any subsystem that has previously caused a crash (tracked via NVS flag)
- Background tasks not required for OTA or health reporting

---

## What Safe Mode Always Does

In strict order:

1. **Emit `SAFE_MODE_ENTERED`** on serial before any other output.
2. **Emit `CURRENT_VERSION`** and `CURRENT_SLOT`.
3. **Initialize NVS/storage.** Required for reading saved Wi-Fi credentials.
4. **Connect Wi-Fi** using saved credentials from NVS. Do not prompt for new credentials.
   - On success: emit `[X4] WIFI_OK ip=<ip> rssi=<rssi>`
   - On failure: emit `[X4] WIFI_FAILED reason=<reason>`; continue to next step regardless.
5. **Initialize OTA subsystem.** Verify manifest URL is configured and reachable.
   - On success: emit `[X4] OTA_READY`
   - On failure: emit `[X4] OTA_UNAVAILABLE reason=<reason>`
6. **Initialize display.** Attempt e-paper driver init.
   - On success: render a recovery status screen (see below).
   - On failure: emit `[X4] DISPLAY_INIT_FAILED error=<error>`; continue without display.
7. **Start web API** (if the firmware includes a web server) with at minimum:
   - `GET /api/health`
   - `POST /api/ota/check`
   - `POST /api/ota/apply`
8. **Enter idle loop** waiting for OTA trigger or serial command.

Safe mode must not depend on the full UI stack. Each step above is independent.
Failure of any one step does not prevent the remaining steps from running.

---

## Recovery Status Screen

If display initialization succeeds, safe mode renders a minimal status screen on the
e-paper display:

```
┌─────────────────────────────┐
│  SAFE MODE                  │
│  Version: x4-agent-dev-42   │
│  Slot:    ota_1             │
│  Wi-Fi:   OK  192.168.1.55  │
│  OTA:     READY             │
└─────────────────────────────┘
```

The screen must use a simple, reliable rendering path — not the full application
UI renderer. A direct call to the display driver's text/bitmap API is preferred.

Display test patterns must also be accessible in safe mode (see `display-diagnostics.md`).

---

## Required Serial Log Output

In addition to the standard markers in `serial-log-markers.md`, safe mode emits:

```
[X4] SAFE_MODE_ENTERED
[X4] CURRENT_VERSION version=x4-agent-dev-42
[X4] CURRENT_SLOT slot=ota_1
[X4] WIFI_OK ip=192.168.1.55 rssi=-58
[X4] OTA_READY
[X4] DISPLAY_INIT_OK driver=gdew0213b74 rotation=0
```

Or on failure:
```
[X4] SAFE_MODE_ENTERED
[X4] CURRENT_VERSION version=x4-agent-dev-42
[X4] CURRENT_SLOT slot=ota_1
[X4] WIFI_FAILED reason=no_saved_credentials
[X4] OTA_UNAVAILABLE reason=no_manifest_url
[X4] DISPLAY_INIT_FAILED error=spi_timeout
```

These markers are stable and must not be changed.

---

## NVS Flags Used by Safe Mode

| Key | Type | Description |
|-----|------|-------------|
| `sm_boot_count` | uint32 | Number of consecutive boots without `BOOT_OK`; used for crash-loop detection |
| `sm_last_crash` | uint8 | Set to `1` if previous boot crashed before `BOOT_OK`; cleared on success |
| `sm_safe_entered` | uint8 | Set to `1` when safe mode is entered; allows remote diagnostics to know safe mode occurred |

---

## Interaction with Health Checks

Safe mode is not a normal OTA boot. When safe mode is entered:

- The health check pipeline still runs, but `display` and `input` may be marked `skipped`
  if they failed initialization (safe mode does not fail on these).
- `wifi` is allowed to be `failed` without triggering rollback (safe mode is the fallback).
- The firmware is **not** marked valid or invalid during safe mode; the OTA pending state
  is preserved so the operator can manually trigger rollback via `POST /api/ota/rollback`
  or by rebooting without entering safe mode again.

---

## Exiting Safe Mode

Safe mode is exited only by:

1. A normal reboot (power cycle or `POST /api/dev/reboot`) without holding the button.
2. A successful OTA update that triggers a reboot into the new slot.

---

## Related Documents

- `health-checks.md` — health check pipeline
- `ota.md` — OTA state machine
- `display-diagnostics.md` — display test patterns accessible from safe mode
- `serial-log-markers.md` — full marker reference
- `safety-rules.md` — safe mode invariants
