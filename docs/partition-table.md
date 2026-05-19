# Partition Table

## Overview

The Xteink X4 firmware uses a **dual-OTA application partition** layout to support
remote over-the-air firmware updates.  Two equal-sized OTA slots allow the previous
firmware to be recovered automatically via ESP-IDF rollback if the new firmware fails
its health checks.

The full partition layout is defined in `partitions.csv` at the repository root.

---

## Partition Layout

```
Label     Type   SubType   Offset    Size      Notes
--------  -----  --------  --------  --------  ----------------------------
nvs       data   nvs       0x9000    0x6000    Non-volatile storage (config, RTC, OTA state)
phy_init  data   phy       0xf000    0x1000    Wi-Fi/BLE radio calibration data
otadata   data   ota       0x10000   0x2000    OTA selection / rollback data
ota_0     app    ota_0     0x12000   0x17F000  OTA slot 0 (~1.5 MB)
ota_1     app    ota_1     0x191000  0x17F000  OTA slot 1 (~1.5 MB)
spiffs    data   spiffs    0x310000  0xF0000   Filesystem / web assets (~960 KB)
```

> This table is derived directly from `partitions.csv` in the repository root.
> Update both files together whenever the partition layout changes.
> Any partition table change requires the documented justification from safety-rules.md §3.

---

## Justification for the Dual-OTA Layout

The factory-only layout was replaced in this commit to enable remote-safe OTA firmware
updates as specified in the OTA design document (`docs/ota.md`).  The justification
satisfies the invariant in `safety-rules.md §3`:

1. **Why the change is necessary**: OTA dual-slot support requires `otadata` + `ota_0`
   + `ota_1`.  Without these partitions the `esp_ota_*` APIs are not active and remote
   firmware updates are impossible.

2. **Both OTA slots still fit**: `ota_0` occupies `0x12000–0x190FFF` (1.5 MB) and
   `ota_1` occupies `0x191000–0x30FFFF` (1.5 MB).  They are contiguous and end exactly
   where `spiffs` begins at `0x310000`.

3. **Commit reference**: this document and `partitions.csv` are updated together in the
   same commit; see the commit message referencing `safety-rules.md §3`.

---

## Flash Requirements

This partition layout **requires a 4 MB flash chip**.  The total addressable range is
`0x000000–0x400000` (4 MB):
- `nvs` through `ota_1` occupies `0x9000–0x30FFFF`
- `spiffs` occupies `0x310000–0x3FFFFF`

---

## OTA Slot Behavior

| Situation | Active slot | Notes |
|-----------|-------------|-------|
| First flash (USB) | ota_0 | Initial firmware goes to ota_0 |
| After OTA update | ota_1 (or whichever was inactive) | Device boots PENDING_VERIFY |
| Health checks pass | same slot, marked VALID | ota_mark_valid() called |
| Health checks fail | previous slot | ota_rollback() triggers reboot |

The `otadata` partition stores which slot is active and whether it is
`PENDING_VERIFY`, `VALID`, or `INVALID`.  The ESP-IDF second-stage bootloader
reads `otadata` at every boot.

---

## Important: Partition Table Cannot Be Changed Via OTA

OTA updates only replace the application binary in one of the OTA slots.  The partition
table itself is stored in flash at offset `0x8000` and can **only** be changed by a
full USB flash.  Devices already running the factory layout must be reflashed via USB
before they can use the OTA dual-slot system.

---

## Invariant: Partition Table Must Not Change Without Documentation

> **This rule is a hard constraint. See `safety-rules.md` for the full policy.**

A partition table change is only permitted if:

1. A written justification is provided explaining why the change is necessary.
2. The change is confirmed not to break the existing flash layout.
3. The commit message references this document and the safety rules.
4. The change is reviewed separately from any functional firmware change.

---

## References

- ESP-IDF partition table docs: `docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html`
- Related specs: `ota.md`, `safety-rules.md`
