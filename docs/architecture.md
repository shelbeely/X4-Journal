# Architecture — X4-Journal / PocketShrine

## Layer Diagram

```
┌─────────────────────────────────────────────────────────┐
│                        src/main.c                        │
│              (app_main — initialises all layers)         │
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

## Web API Surface

| Method | Path | Description |
|---|---|---|
| GET | `/` | Serve web editor SPA |
| GET | `/api/entries` | List entry metadata (optionally filter by `?date=YYYY-MM-DD`) |
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
