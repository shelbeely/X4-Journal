# Partition Table

## Overview

The Xteink X4 firmware currently uses a **single factory application partition** layout.
There are no OTA slots yet — over-the-air update capability is planned for a future phase.
The full partition layout is defined in `partitions.csv` at the repository root.

---

## Partition Layout

```
Label     Type   SubType   Offset    Size      Notes
--------  -----  --------  --------  --------  ----------------------------
nvs       data   nvs       0x9000    0x6000    Non-volatile storage (config, RTC)
phy_init  data   phy       0xf000    0x1000    Wi-Fi/BLE radio calibration data
factory   app    factory   0x10000   0x300000  Application firmware (~3 MB)
spiffs    data   spiffs    0x310000  0xF0000   Filesystem / web assets (~960 KB)
```

> This table is derived directly from `partitions.csv` in the repository root.
> Update both files together whenever the partition layout changes.

---

## Current Slot Behavior

Because there is only a `factory` partition and no `otadata` partition, the bootloader
always loads the factory image. The OTA APIs (`esp_ota_*`) are not active in this layout.

OTA dual-slot support (with `ota_0`, `ota_1`, and `otadata` partitions) is planned for
Phase 4 / 5. When that work begins, this document and `partitions.csv` must be updated
together, following the invariant below.

---

## Application Partition Size

The `factory` partition is 3 MB (0x300000), which is sufficient for the current firmware
including the display driver, storage layer, web server, and all app modules.

When OTA dual-slot support is added in a future phase, the partition table will need to
be restructured to accommodate two OTA slots. Any such change must follow the invariant
below.

---

## Invariant: Partition Table Must Not Change Without Documentation

> **This rule is a hard constraint. See `safety-rules.md` for the full policy.**

A partition table change is only permitted if:

1. A written justification is provided explaining why the change is necessary.
2. The change is confirmed not to break the existing flash layout.
3. The commit message references this document and the safety rules.
4. The change is reviewed separately from any functional firmware change.

If a proposed feature cannot fit within the existing partition layout, the feature
specification must be revised — not the partition table.

---

## References

- ESP-IDF partition table docs: `docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html`
- Related specs: `ota.md`, `safety-rules.md`
