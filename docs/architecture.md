# Architecture — System Overview

## Hardware Target

**Device:** Xteink X4
**MCU:** ESP32 (Xtensa LX6 dual-core, 240 MHz)
**Display:** E-paper (model TBD; update once firmware project is inspected)
**Storage:** SPI flash (minimum 4 MB); two OTA partitions + NVS + SPIFFS
**Input:** Physical buttons including a boot/recovery button (GPIO0 or board-defined)
**Connectivity:** 802.11 b/g/n Wi-Fi (2.4 GHz)
**Build system:** ESP-IDF (CMake/idf.py)

---

## Firmware Layer Diagram

```
┌─────────────────────────────────────────────────────────┐
│                      ESP32 Hardware                      │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│                 ESP-IDF Bootloader                        │
│  • Reads otadata partition                               │
│  • Selects active OTA slot (ota_0 or ota_1)             │
│  • Auto-rolls back PENDING_VERIFY slot on crash         │
│  !! Never modified by this project !!                    │
└────────────────────────┬────────────────────────────────┘
                         │
┌────────────────────────▼────────────────────────────────┐
│              app_main / Boot Sequence                    │
│  1. Emit BOOT_START                                      │
│  2. Detect safe mode button hold                        │
│  3. Init NVS / storage                                   │
│  4. Connect Wi-Fi                                        │
│  5. Run health check pipeline (if PENDING_VERIFY)       │
│  6. Emit BOOT_OK                                         │
└──────┬──────────────────────────┬───────────────────────┘
       │ normal boot              │ safe mode (button held)
┌──────▼──────────────┐   ┌──────▼──────────────────────┐
│   Application Layer  │   │        Safe Mode             │
│  • Full UI           │   │  • Minimal init only         │
│  • All features      │   │  • Wi-Fi (saved creds)       │
│  • Periodic OTA poll │   │  • OTA subsystem             │
└──────┬──────────────┘   │  • Recovery display screen   │
       │                   │  • Serial log output         │
       │                   └──────────────────────────────┘
       │
┌──────▼──────────────────────────────────────────────────┐
│                   OTA Subsystem                          │
│  • Manifest fetch (HTTPS pull)                          │
│  • Manifest validation                                   │
│  • Binary download + SHA-256 verify                     │
│  • esp_ota_* API calls                                  │
│  • Rollback gate (calls health checker result)          │
└──────┬──────────────────────────────────────────────────┘
       │
┌──────▼──────────────────────────────────────────────────┐
│                 Health Check Pipeline                    │
│  boot → reset_reason → storage → wifi → internet →      │
│  ota → display → input → heap → logs                    │
│  • Pass: mark firmware valid                            │
│  • Fail: trigger rollback                               │
└──────┬──────────────────────────────────────────────────┘
       │
┌──────▼──────────────────────────────────────────────────┐
│            Display Diagnostics Subsystem                │
│  • Structured log markers                               │
│  • Test patterns                                        │
│  • Status object                                        │
│  • Framebuffer screenshot                               │
└──────┬──────────────────────────────────────────────────┘
       │
┌──────▼──────────────────────────────────────────────────┐
│              Web API (if web server present)            │
│  /api/version  /api/health  /api/logs                   │
│  /api/ota/*    /api/display/*                           │
│  /api/dev/*  (CONFIG_X4_DIAG_HTTP_API=y only)          │
└─────────────────────────────────────────────────────────┘
```

---

## Dependency Map: Call Order

The following describes which subsystem calls which, and in what order during a normal boot:

```
app_main()
  │
  ├─► safe_mode_detect()          — reads GPIO before any other init
  │
  ├─► nvs_init()                  — required by: wifi, ota, safe mode, crash counter
  │
  ├─► display_init()              — called early; result stored for health check
  │   └─► emit DISPLAY_INIT_START / DISPLAY_INIT_OK / DISPLAY_INIT_FAILED
  │
  ├─► wifi_init()                 — uses NVS credentials
  │   └─► emit WIFI_OK / WIFI_FAILED
  │
  ├─► input_init()                — initializes button subsystem
  │   └─► emit INPUT_OK / INPUT_FAILED
  │
  ├─► [if PENDING_VERIFY] health_check_run()
  │   ├─► check boot / reset_reason / storage / wifi / internet / ota
  │   ├─► check display (render all_white test pattern)
  │   ├─► check input
  │   ├─► check heap
  │   ├─► check logs
  │   ├─► [all pass] → esp_ota_mark_app_valid_cancel_rollback()
  │   │                emit OTA_MARK_VALID
  │   └─► [any fail] → emit OTA_ROLLBACK_REQUESTED
  │                    esp_ota_mark_app_invalid_rollback_and_reboot()
  │
  ├─► [if web server] http_server_start()
  │   ├─► register /api/* handlers
  │   └─► [if CONFIG_X4_DIAG_HTTP_API] register /api/dev/* handlers
  │
  ├─► ota_scheduler_start()       — starts background manifest poll timer
  │
  └─► [safe mode] safe_mode_loop() | [normal] app_loop()
```

---

## Key Interfaces Between Subsystems

| Producer | Consumer | Interface |
|----------|----------|-----------|
| `health_check_run()` | OTA rollback gate | Returns `health_status_t` struct; OTA gate reads overall `status` field |
| `display_init()` | Health check stage 7 | Returns `esp_err_t`; health check reads it directly |
| `ota_subsystem` | Health check stage 6 | Health check calls `ota_is_reachable()` |
| `wifi_init()` | Health check stages 4 & 5 | Health check reads `wifi_get_state()` |
| `nvs_init()` | All subsystems | Shared NVS handle passed to each subsystem init |
| `http_server` | Web API handlers | Handlers call into OTA, health, display, diagnostics subsystems |

---

## Non-Goals

The following are **explicitly out of scope** for this project. No code, configuration,
or build change may touch these areas:

| Area | Reason |
|------|--------|
| Bootloader source modification | ESP-IDF bootloader is pre-built; modification requires secure boot re-signing |
| eFuse programming | Irreversible hardware change |
| Secure boot configuration | Requires matched key provisioning; out of scope for dev OTA |
| Flash encryption | Irreversible if misapplied; out of scope |
| Inbound port exposure | Security constraint; OTA is pull-based only |
| Partition table changes without justification | Invariant; see `partition-table.md` |

---

## Related Documents

- `partition-table.md` — flash layout
- `ota.md` — OTA state machine
- `health-checks.md` — health check pipeline
- `safe-mode.md` — safe mode boot path
- `display-diagnostics.md` — display subsystem detail
- `dev-diagnostics.md` — diagnostics flags and endpoints
- `api.md` — full HTTP API spec
- `safety-rules.md` — invariants
