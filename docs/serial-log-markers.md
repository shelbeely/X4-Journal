# Serial Log Markers — Complete Reference

This document is the **canonical, stable contract** for all structured serial log markers
emitted by the Xteink X4 firmware.

**Stability contract:** markers listed here must never be renamed, repurposed, or removed.
New markers may be added; existing markers are immutable. AI agents and monitoring scripts
parse these markers by exact string match.

All markers are emitted as plain ASCII strings on a single line, prefixed with the tag
`[X4]` so they can be distinguished from general ESP-IDF log output:

```
[X4] MARKER_NAME [optional key=value pairs]
```

Example:
```
[X4] BOOT_START version=x4-agent-dev-42 slot=ota_1
[X4] WIFI_OK ip=192.168.1.55 rssi=-62
```

---

## Boot Sequence

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `BOOT_START` | First line of `app_main` / `setup()` | Include `version=` and `slot=` |
| `BOOT_OK` | After all subsystems have initialized successfully | Precedes health check start |
| `DEV_DIAGNOSTICS_ENABLED` | When `CONFIG_X4_DEV_DIAGNOSTICS=y` | Emitted early in boot to alert monitoring |

---

## Safe Mode

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `SAFE_MODE_ENTERED` | Immediately after button-hold safe mode is detected | Include `version=` and `slot=` |
| `CURRENT_VERSION` | In safe mode boot log | `version=<string>` |
| `CURRENT_SLOT` | In safe mode boot log | `slot=ota_0\|ota_1\|factory` |

---

## Wi-Fi

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `WIFI_OK` | Wi-Fi connected successfully | Include `ip=` and `rssi=` |
| `WIFI_FAILED` | Wi-Fi connection failed or timed out | Include `reason=` |

---

## OTA

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `OTA_CHECK_START` | Manifest fetch begins | Include `url=` (redact credentials) |
| `OTA_MANIFEST_OK` | Manifest fetched and parsed successfully | Include `version=` from manifest |
| `OTA_DOWNLOAD_START` | Firmware binary download begins | Include `url=` and `size=` if known |
| `OTA_DOWNLOAD_OK` | Download completed | Include `bytes=` written |
| `OTA_SHA256_OK` | SHA-256 verification passed | Include `hash=` (first 16 chars) |
| `OTA_APPLY_OK` | OTA slot written; pending reboot | Include `slot=` |
| `OTA_PENDING_VERIFY` | First boot after OTA; awaiting health check | Include `slot=` |
| `OTA_MARK_VALID` | All health checks passed; firmware accepted | Include `version=` and `slot=` |
| `OTA_ROLLBACK_REQUESTED` | Health check failed; rollback triggered | Include `reason=` |
| `OTA_READY` | OTA subsystem initialized and reachable (safe mode) | |
| `OTA_UNAVAILABLE` | OTA subsystem could not initialize (safe mode) | Include `reason=` |

---

## Display

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `DISPLAY_INIT_START` | Display driver `init()` called | |
| `DISPLAY_INIT_OK` | Display driver initialized successfully | Include `driver=` and `rotation=` |
| `DISPLAY_INIT_FAILED` | Display driver `init()` returned error | Include `error=` |
| `DISPLAY_FRAMEBUFFER_ALLOC_OK` | Framebuffer memory allocated | Include `size=` bytes |
| `DISPLAY_FRAMEBUFFER_ALLOC_FAILED` | Framebuffer allocation failed | Include `size_requested=` and `heap_free=` |
| `DISPLAY_FULL_REFRESH_START` | Full-panel refresh command sent | |
| `DISPLAY_FULL_REFRESH_OK` | Full-panel refresh completed | Include `duration_ms=` |
| `DISPLAY_PARTIAL_REFRESH_START` | Partial refresh command sent | Include `x=` `y=` `w=` `h=` |
| `DISPLAY_PARTIAL_REFRESH_OK` | Partial refresh completed | Include `duration_ms=` |
| `DISPLAY_REFRESH_OK` | Generic refresh completed (use full/partial variants when known) | |
| `DISPLAY_BUSY_TIMEOUT` | Display busy-pin wait exceeded timeout | Include `timeout_ms=` |
| `DISPLAY_SPI_ERROR` | SPI transaction returned error | Include `error=` |
| `DISPLAY_SLEEP_OK` | Display entered sleep/low-power mode | |
| `DISPLAY_WAKE_OK` | Display woken from sleep | |

---

## Input

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `INPUT_OK` | Button/input subsystem initialized | |
| `INPUT_FAILED` | Button/input subsystem failed to initialize | Include `reason=` |

---

## Health

| Marker | When emitted | Notes |
|--------|-------------|-------|
| `HEALTH_OK` | All health check stages passed | |
| `HEALTH_FAILED` | One or more health check stages failed | Include `stage=` and `reason=` |

---

## Parsing Notes

- Markers are case-sensitive. Use exact string match.
- Key-value pairs after the marker are optional and informational; their presence must not be required for a successful parse.
- A monitoring script should scan each line for `[X4] <MARKER>` and treat the rest of the line as structured metadata.
- Markers that are never emitted in a given boot sequence are simply absent; do not treat absence as failure unless the expected marker is part of a required sequence (e.g., `OTA_PENDING_VERIFY` → `OTA_MARK_VALID` or `OTA_ROLLBACK_REQUESTED`).
