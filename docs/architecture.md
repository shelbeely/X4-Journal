# Architecture — X4-Journal / PocketShrine

## Hardware Target

**Device:** Xteink X4 (PocketShrine)
**MCU:** ESP32-C3 (RISC-V single-core, 160 MHz)
**Display:** SPI EPD 800×480 framebuffer (SSD1677 driver via `EInkDisplay` SDK lib, wrapped in `src/platform/display`)
**Storage:** SD card via SdFat (`/sdcard/` VFS prefix); NVS for config state
**Input:** Buttons via ADC resistor-ladder (GPIO1/GPIO2) + power button (GPIO3); events through FreeRTOS queue (see `src/platform/buttons`)
**Connectivity:** 802.11 b/g/n Wi-Fi (SoftAP mode; SSID `PocketShrine-XXXX`)
**Build system:** PlatformIO (`platformio.ini`)

---

## Firmware Source Layer Diagram

```
┌─────────────────────────────────────────────────────────┐
│                       src/main.cpp                       │
│              (setup() — initialises all layers)          │
└──────────────┬──────────────────────────┬───────────────┘
               │                          │
┌──────────────▼──────────────┐  ┌────────▼──────────────┐
│         src/app/            │  │       src/web/         │
│  journal_app (FreeRTOS task)│  │  web_server            │
│  journal_routes (screen nav)│  │  api_entries           │
│  prompt_engine              │  │  api_prompts           │
│  timeline_view              │  │  api_export            │
│  entry_editor               │  │  static_editor/        │
└──────────────┬──────────────┘  └────────┬──────────────┘
               │                          │
┌──────────────▼──────────────────────────▼───────────────┐
│                       src/ui/                            │
│  components  screen_home  screen_today  screen_timeline  │
│  screen_prompt  screen_settings  screen_sync             │
└──────────────┬──────────────────────────────────────────┘
               │
┌──────────────▼──────────────────────────────────────────┐
│                     src/storage/                         │
│  journal_fs      markdown_entry                          │
│  metadata_index  export_zip                              │
└──────────────┬──────────────────────────────────────────┘
               │
┌──────────────▼──────────────┐  ┌────────────────────────┐
│       src/crypto/           │  │     src/platform/       │
│  vault  key_derivation      │  │  buttons  display       │
│  (Phase 4; stubs only now)  │  │  power    wifi          │
└─────────────────────────────┘  │  rtc      sdcard        │
                                 └────────────────────────┘
```

---

## Boot & OTA Layer Diagram

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

## Module Responsibilities

### `src/platform/`
Direct hardware abstraction. No business logic. Each module:
- `buttons` — GPIO ISR + FreeRTOS queue; emits `button_event_t` with id, type (press/release/hold), duration
- `display` — SPI EPD driver; 200×200 framebuffer; `display_set_pixel` / `display_draw_text` / `display_full_refresh`
- `sdcard` — FATFS mount + helpers: mkdir_p, read, write, list, delete
- `wifi` — SoftAP lifecycle; SSID = `PocketShrine-XXXX` (last 4 of MAC)
- `rtc` — SNTP + NVS timestamp; datetime formatting helpers
- `power` — light/deep sleep via `esp_sleep`; battery ADC percentage

### `src/storage/`
- `journal_fs` — path resolution; directory creation; entry enumeration by date/week
- `markdown_entry` — serialize/deserialize `.md` files with YAML front matter (no external lib)
- `metadata_index` — in-memory index of ≤512 entries rebuilt by scanning front matter; query by date/tag/favorite
- `export_zip` — store-only ZIP writer (no external lib); outputs to `/sdcard/export/`

### `src/crypto/`
Plaintext pass-through by default (`vault_is_enabled()` = false). Wired but inactive until Phase 4.
- `vault` — AES-256-GCM via mbedTLS; format: [12B IV][16B tag][ciphertext]; salt + enabled flag in NVS
- `key_derivation` — PBKDF2-SHA256 via mbedTLS; 12-word BIP39-style recovery phrase

### `src/app/`
- `journal_app` — main FreeRTOS task; translates button events → route transitions → screen renders
- `journal_routes` — screen router with depth-8 navigation stack; `routes_back()` pops
- `prompt_engine` — cJSON parser for `default.json`; daily prompt = `prompts[day_of_year % count]`
- `timeline_view` — scrollable entry list state backed by `metadata_index`
- `entry_editor` — three modes: CHECKIN, FREEWRITE, PROMPTED; on-device fragment composer

### `src/ui/`
All screen functions: clear framebuffer → draw content → `display_full_refresh()`.
E-paper rules: no animations, full-screen state changes only, large selectable rows (≥20 px).

### `src/web/`
- `web_server` — `esp_http_server`; registers URI handlers
- `api_entries` — full CRUD + favorite toggle; cJSON serialization
- `api_prompts` — list packs, today's prompt, upload new pack
- `api_export` — streams ZIP; handles `.md` import
- `static_editor/index.html` — self-contained SPA; zero CDN dependencies; vanilla JS

---

## setup() Call Order

```
setup()  [Arduino entry point in src/main.cpp]
  │
  ├─► sdcard_init()              — must come before any filesystem access
  │
  ├─► display_init()             — EInkDisplay heap allocation + SPI init
  │
  ├─► display splash screen      — "PocketShrine vX.Y.Z" drawn and refreshed
  │
  ├─► rtcdrv_init()              — restores last-known time from NVS (Preferences)
  │
  ├─► power_init()               — battery ADC + sleep configuration
  │
  ├─► vault_init()               — crypto vault (pass-through until Phase 4)
  │
  ├─► [if sdcard mounted] journal_fs_init()
  │
  ├─► [if sdcard mounted] index_rebuild()   — scans front matter into metadata index
  │
  ├─► [if sdcard mounted] prompt_engine_init()
  │
  ├─► entry_editor_init()
  │
  ├─► buttons_init()             — starts FreeRTOS button_task polling InputManager
  │
  └─► journal_app_init()
      └─► xTaskCreate(journal_app_task, ...)

loop()  [Arduino main loop]
  └─► vTaskDelay(1000 ms)        — all logic runs inside journal_app_task
```

---

## Data Flow: Button Press → Save Entry

```
[Physical Button]
      │ GPIO ISR
      ▼
[buttons queue]  →  [journal_app task]
                           │ button_event_t
                           ▼
                    [journal_routes]  →  current route determines handler
                           │
                    [entry_editor]   →  accumulates mood/energy/anxiety/fragments
                           │
                    [markdown_entry] →  serialize to .md with YAML front matter
                           │
                    [journal_fs]     →  write to /sdcard/journal/YYYY/MM/YYYY-MM-DD_HH-MM.md
                           │
                    [metadata_index] →  append to in-memory index
                           │
                    [screen_home]    →  full EPD refresh
```

---

## SD Card File Layout

```
/sdcard/
  journal/
    2026/
      05/
        2026-05-17_21-04.md   ← plaintext Markdown entry
        2026-05-18_09-30.md
      import/                 ← drop .md files here for ingestion
  prompts/
    default.json              ← built-in prompt packs
    custom-pack.json          ← user-added packs
  config/
    journal.toml              ← device configuration
  export/
    journal-export-2026-05-18.zip
  web/
    index.html                ← optional: override embedded SPA from SD
```

---

## Entry File Format

```markdown
---
id: 2026-05-17-2104
created: 2026-05-17T21:04:00
mood: 3
energy: 2
anxiety: 4
body_feeling: neutral
tags:
  - tired
  - tiny-win
source: device
encrypted: false
favorite: false
---

Today I feel: heavy, scattered. One thing I need: rest. Tiny win: I kept going.
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
| `buttons` | `journal_app` | FreeRTOS queue of `button_event_t` |
| `metadata_index` | `timeline_view`, `api_entries` | In-memory index queried by date/tag/favorite |

---

## Web API Surface

| Method | Path | Description |
|---|---|---|
| GET | `/` | Serve web editor SPA |
| GET | `/api/entries` | List entry metadata |
| GET | `/api/entries/:id` | Full entry JSON |
| POST | `/api/entries` | Create entry |
| PUT | `/api/entries/:id` | Update entry |
| DELETE | `/api/entries/:id` | Delete entry |
| POST | `/api/entries/:id/favorite` | Toggle favorite |
| GET | `/api/prompts` | List packs + today's prompt |
| GET | `/api/prompts/daily` | Today's prompt text only |
| POST | `/api/prompts/upload` | Upload new prompt pack JSON |
| GET | `/api/export` | Download ZIP of all entries |
| POST | `/api/import` | Upload `.md` files for import |
| GET | `/api/version` | Firmware version + OTA slot |
| GET | `/api/health` | Health check status |
| GET | `/api/logs` | Recent serial log buffer |
| POST | `/api/ota/check` | Trigger manifest check |
| POST | `/api/ota/apply` | Apply validated OTA update |
| POST | `/api/ota/rollback` | Roll back to previous slot |
| GET | `/api/display/status` | Display driver status |
| POST | `/api/display/test-pattern` | Render a named test pattern |

See `api.md` for the complete endpoint catalogue including display and dev endpoints.

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

- `partition-table.md` — flash layout and OTA slot spec
- `ota.md` — OTA state machine
- `health-checks.md` — health check pipeline
- `safe-mode.md` — safe mode boot path
- `display-diagnostics.md` — display subsystem detail
- `dev-diagnostics.md` — diagnostics flags and endpoints
- `api.md` — full HTTP API spec
- `safety-rules.md` — invariants
- `build-guide.md` — build and flash instructions
