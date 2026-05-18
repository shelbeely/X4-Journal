# OTA System Spec — Remote Firmware Updates

## Overview

The Xteink X4 uses a **pull-based OTA system**. The device periodically or on-demand
fetches a JSON manifest from a configured HTTPS URL, validates it, downloads the firmware
binary into the inactive OTA slot, verifies its integrity, and reboots. The new firmware
only becomes permanently accepted after passing all health checks.

The device never listens for inbound OTA connections. All communication is outbound from
the device to a static HTTPS server the operator controls.

---

## Configuration

| Kconfig Symbol | Default | Description |
|----------------|---------|-------------|
| `CONFIG_OTA_MANIFEST_URL` | `""` (must be set) | Full HTTPS URL of the manifest JSON file |
| `CONFIG_OTA_CHANNEL` | `"dev"` | Channel string matched against manifest `channel` field |
| `CONFIG_OTA_MIN_HEAP_FOR_OTA` | `65536` | Minimum free heap (bytes) required before starting download |
| `CONFIG_OTA_CHECK_INTERVAL_SEC` | `3600` | How often to poll manifest in the background (0 = manual only) |
| `CONFIG_OTA_SERVER_CERT_PEM` | `""` | PEM-encoded server CA certificate for TLS verification |

These values are set in the project's `sdkconfig` or `sdkconfig.defaults` file.
They can be overridden at build time with `idf.py -DCONFIG_OTA_MANIFEST_URL="https://..."`.

---

## Firmware Version String

The firmware version string is built at compile time and includes:

```
<project>-<channel>-<build_number>
```

Example: `x4-agent-dev-42`

Components:
- `<project>`: fixed string `x4-agent`
- `<channel>`: value of `CONFIG_OTA_CHANNEL`
- `<build_number>`: monotonically increasing integer, sourced from the CI/CD build number
  or a local counter in `tools/agent_publish_ota.sh`

The full version is also embedded in the firmware binary app description
(`esp_app_desc_t.version`) so it is readable by the bootloader and the OTA API.

---

## Manifest Format

The manifest is a UTF-8 JSON file served over HTTPS:

```json
{
  "device": "xteink-x4",
  "channel": "dev",
  "version": "x4-agent-dev-42",
  "url": "https://example.com/firmware/x4-agent-dev-42.bin",
  "sha256": "a3f1c2d4e5b6a7f8...",
  "min_battery_percent": 40,
  "notes": "optional human-readable release notes"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `device` | string | yes | Must equal `"xteink-x4"` |
| `channel` | string | yes | Must match `CONFIG_OTA_CHANNEL` |
| `version` | string | yes | New firmware version string |
| `url` | string | yes | HTTPS URL of the firmware `.bin` file |
| `sha256` | string | yes | Lowercase hex SHA-256 of the firmware binary |
| `min_battery_percent` | number | no | Minimum battery % required; omit or set 0 to skip |
| `notes` | string | no | Human-readable release notes; ignored by firmware |

---

## Manifest Validation Rules

Each rule is checked in order. On any failure, the reason is logged and the OTA attempt
is aborted. No download begins until all checks pass.

1. **Wrong device** — `manifest.device != "xteink-x4"` → reject, log `reason=wrong_device`
2. **Wrong channel** — `manifest.channel != CONFIG_OTA_CHANNEL` → reject, log `reason=wrong_channel`
3. **Stale/same version** — `manifest.version <= running_version` (if semver or numeric comparison is available) → reject, log `reason=version_not_newer`
4. **Insufficient flash** — inactive OTA partition size < binary size if known → reject, log `reason=insufficient_flash`
5. **Low battery** — `manifest.min_battery_percent > 0` and `battery_percent < manifest.min_battery_percent` → reject, log `reason=low_battery`
6. **Low heap** — `heap_free < CONFIG_OTA_MIN_HEAP_FOR_OTA` → reject, log `reason=low_heap`

After download:

7. **SHA-256 mismatch** — computed hash of downloaded binary != `manifest.sha256` → reject, roll back slot, log `reason=sha256_mismatch`

---

## OTA State Machine

```
IDLE
  │  (manual trigger or interval timer fires)
  ▼
CHECK
  │  fetch manifest → validate (rules 1-6 above)
  │  failure → IDLE (log rejection reason)
  ▼
DOWNLOAD
  │  write binary to inactive OTA slot via esp_ota_write()
  │  failure → IDLE (log error, do not switch slot)
  ▼
VERIFY
  │  compute SHA-256 of downloaded bytes (rule 7)
  │  mismatch → IDLE (log error, abort OTA handle)
  ▼
REBOOT
  │  esp_ota_set_boot_partition(new_slot)
  │  emit [X4] OTA_APPLY_OK slot=<new_slot>
  │  esp_restart()
  ▼
PENDING_VERIFY  (first boot in new slot)
  │  emit [X4] OTA_PENDING_VERIFY slot=<new_slot>
  │  run health check pipeline (see health-checks.md)
  │  all checks pass → esp_ota_mark_app_valid_cancel_rollback()
  │                     emit [X4] OTA_MARK_VALID
  │                     → IDLE
  │  any check fails → esp_ota_mark_app_invalid_rollback_and_reboot()
  │                     emit [X4] OTA_ROLLBACK_REQUESTED reason=<stage>
  ▼
VALID / ROLLBACK
```

---

## ESP-IDF Rollback API Usage

| Function | When called |
|----------|-------------|
| `esp_ota_begin()` | Start writing to inactive slot |
| `esp_ota_write()` | Write each chunk of downloaded data |
| `esp_ota_end()` | Finalize slot write |
| `esp_ota_set_boot_partition()` | Point bootloader to new slot before reboot |
| `esp_ota_get_running_partition()` | Get currently running slot |
| `esp_ota_get_state_partition()` | Check if current slot is `PENDING_VERIFY` |
| `esp_ota_mark_app_valid_cancel_rollback()` | Accept new firmware after health checks pass |
| `esp_ota_mark_app_invalid_rollback_and_reboot()` | Reject new firmware and reboot into previous slot |

If `esp_ota_mark_app_invalid_rollback_and_reboot()` is not available in the ESP-IDF
version in use, the firmware must:
1. Log the limitation clearly: `[X4] OTA_ROLLBACK_REQUESTED reason=api_unavailable`
2. Set a flag in NVS indicating the slot is invalid.
3. Call `esp_restart()` and rely on the bootloader's automatic rollback on pending state.
4. Document the limitation in `build-guide.md` under "Known Limitations".

---

## Pull-Based Check Flow

```
Device                          OTA Manifest Server
  │                                    │
  │── HTTPS GET manifest URL ─────────►│
  │◄─ 200 OK  {manifest JSON} ─────────│
  │                                    │
  │  [validate manifest locally]       │
  │                                    │
  │── HTTPS GET firmware.bin URL ──────►│
  │◄─ binary stream ───────────────────│
  │                                    │
  │  [write to inactive OTA slot]      │
  │  [verify SHA-256]                  │
  │  [reboot into new slot]            │
```

The device never accepts inbound connections for OTA. The manifest server is a static
file host (S3, GitHub Releases, any HTTPS server).

---

## Security Notes

- TLS certificate verification must be enabled. Set `CONFIG_OTA_SERVER_CERT_PEM` to the
  CA cert of the manifest and firmware server.
- Never fetch manifests over plain HTTP.
- The `url` field in the manifest must use HTTPS.
- Do not log full manifest URLs if they contain signed query parameters; truncate or
  redact the query string.

---

## Related Documents

- `partition-table.md` — slot layout and size requirements
- `health-checks.md` — health check pipeline that gates `OTA_MARK_VALID`
- `serial-log-markers.md` — OTA log markers reference
- `safety-rules.md` — OTA invariants
