# Partition Table — OTA Slot Layout

## Overview

The Xteink X4 firmware uses the ESP-IDF **two-slot OTA partition layout**.
This layout allows the device to boot from one firmware image while writing a new image
into the other slot, then switching on reboot. If the new image fails its health checks,
the bootloader can fall back to the previously validated slot.

---

## Partition Layout

```
Label        Type   SubType   Offset    Size      Notes
-----------  -----  --------  --------  --------  ----------------------------
nvs          data   nvs       0x9000    0x5000    Non-volatile storage (config)
otadata      data   ota       0xe000    0x2000    OTA state (active slot pointer)
app0 (ota_0) app    ota_0     0x10000   0x1E0000  OTA slot 0 (~1.875 MB)
app1 (ota_1) app    ota_1     0x1F0000  0x1E0000  OTA slot 1 (~1.875 MB)
spiffs       data   spiffs    0x3D0000  0x30000   Filesystem / web assets
```

> **Note:** The exact offsets and sizes above are illustrative. Before the firmware
> project is created, the actual partition CSV must be read from the repository and
> this table updated to match. See the invariant below.

---

## Slot Behavior

### Normal boot (no pending OTA)
1. The bootloader reads `otadata` to determine which slot was last marked valid.
2. It boots the firmware from that slot.

### First boot after OTA
1. OTA manager writes the new image to the **inactive** slot.
2. OTA manager calls `esp_ota_set_boot_partition()` to point to the new slot.
3. Device reboots. Bootloader loads from the new slot.
4. The new firmware runs with **pending verification** state.
5. The firmware must call `esp_ota_mark_app_valid_cancel_rollback()` before the watchdog
   timer expires, or the bootloader will automatically revert to the previous slot on the
   next reset.

### Rollback
- If the firmware calls `esp_ota_mark_app_invalid_rollback_and_reboot()`, the current
  slot is invalidated and the device immediately reboots into the previous valid slot.
- If the firmware crashes or reboots before marking valid, the bootloader detects the
  pending state and boots the previous slot instead.
- The previously validated slot is **never overwritten** until the new slot has been
  explicitly accepted.

### Factory partition (if present)
- Some ESP32 boards include a factory partition as a last-resort recovery image.
- If present, it is used only when both OTA slots are invalid.
- The X4 firmware does not rely on a factory partition being present; safe mode and
  rollback cover the equivalent recovery path.

---

## Minimum Partition Size Requirements

Given the features added by this spec (OTA manager, health check subsystem, safe mode,
display diagnostics, dev diagnostics API, web server), each OTA partition must be
large enough to hold:

| Component | Estimated size |
|-----------|---------------|
| Base ESP-IDF runtime | ~700 KB |
| Application code | ~200 KB |
| OTA / health / safe mode subsystems | ~50 KB |
| Display driver + framebuffer | ~100 KB |
| Web server + API handlers | ~80 KB |
| Dev diagnostics (if compiled in) | ~30 KB |
| **Minimum recommended slot size** | **≥ 1.5 MB (0x180000)** |

If the current partition sizes are smaller than 1.5 MB per slot, the partition table
**must be reviewed** before adding these features. Any resize requires following the
partition table change invariant below.

---

## Invariant: Partition Table Must Not Change Without Documentation

> **This rule is a hard constraint. See `safety-rules.md` for the full policy.**

A partition table change is only permitted if:

1. A written justification is provided explaining why the change is necessary.
2. The change is confirmed not to invalidate or shrink either OTA slot.
3. The commit message references this document and the safety rules.
4. The change is reviewed separately from any functional firmware change.

If a proposed feature cannot fit within the existing partition layout, the feature
specification must be revised — not the partition table.

---

## `otadata` and OTA State

The `otadata` partition (2 KB) stores two copies of the OTA state structure, one per slot.
ESP-IDF manages this partition automatically via the OTA API. The firmware must never
write to `otadata` directly; only use the `esp_ota_*` API functions.

Key fields in the OTA state:

| Field | Meaning |
|-------|---------|
| `ota_seq` | Sequence counter; higher value = more recently written |
| `ota_state` | `ESP_OTA_IMG_NEW`, `ESP_OTA_IMG_PENDING_VERIFY`, `ESP_OTA_IMG_VALID`, `ESP_OTA_IMG_INVALID`, `ESP_OTA_IMG_ABORTED` |
| `crc32` | Integrity check for the state entry |

The firmware reads the current boot partition with `esp_ota_get_running_partition()` and
checks pending state with `esp_ota_get_state_partition()`.

---

## References

- ESP-IDF OTA API: `esp_ota_ops.h`
- ESP-IDF partition table docs: `docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html`
- Related specs: `ota.md`, `health-checks.md`, `safety-rules.md`
