# X4-Journal — PocketShrine

A privacy-first, offline reflective journal firmware for the **xteink X4** — an ESP32-C3 device with an e-paper display, physical buttons, SD card, and Wi-Fi.

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32-C3 (RISC-V, 160 MHz, 400 KB SRAM, 4 MB flash) |
| Display | E-paper 800×480 (SSD1677-based), SPI |
| Storage | MicroSD via SPI, FAT32 |
| Buttons | 7 buttons: ADC resistor-ladder (GPIO1 + GPIO2) + power button (GPIO3) |
| Wireless | Wi-Fi 2.4 GHz (SoftAP mode for local web editor) |
| Battery | 3.7 V LiPo, ADC monitoring |

See [`docs/hardware.md`](docs/hardware.md) for full pin assignments.

## Features

- **Plain Markdown on SD card** — human-readable without any special software
- **On-device quick check-in** — mood, energy, anxiety (1–5) + canned-fragment note composer
- **Timeline view** — scroll through past entries by date
- **Daily prompts** — 10 built-in prompt packs (daily check-in, dysphoria support, anxiety grounding, gratitude, tiny wins, memory capture, dream log, media diary, ritual/witchy, debug-my-brain)
- **Web editor** — connect phone/browser over Wi-Fi AP; write full Markdown entries
- **ZIP export** — download all entries as a ZIP via the web UI
- **Encryption** (Phase 4) — AES-256-GCM vault; passphrase via web UI; BIP39 recovery phrase
- **Offline-first** — no cloud account required, ever

## Build

### Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html) or PlatformIO IDE
- ESP-IDF toolchain (installed automatically by PlatformIO)

### Compile & Flash

```bash
pio run -t upload
pio device monitor
```

## SD Card Setup

Format SD card as FAT32. The firmware creates required directories automatically on first boot. Optionally pre-populate:

```
/prompts/default.json   ← prompt packs (see sdcard/prompts/default.json)
/config/journal.toml    ← device config (see sdcard/config/journal.toml)
```

## Web Editor

1. Navigate to **Sync** on the home screen.
2. Connect your phone/browser to Wi-Fi SSID `PocketShrine-XXXX` (shown on screen).
3. Open `http://192.168.4.1` in your browser.
4. Write, browse, and export entries.

## Project Structure

```
src/
  app/       — journal_app, journal_routes, prompt_engine, timeline_view, entry_editor
  storage/   — journal_fs, markdown_entry, metadata_index, export_zip
  crypto/    — vault, key_derivation
  ui/        — components, screen_home, screen_today, screen_timeline,
               screen_prompt, screen_settings, screen_sync
  web/       — web_server, api_entries, api_prompts, api_export,
               static_editor/index.html
  platform/  — buttons, display, power, wifi, rtc, sdcard
  main.cpp

docs/
  hardware.md      — pin map, display driver, button layout
  architecture.md  — layer diagram and data-flow

sdcard/
  prompts/default.json   — template prompt packs for SD card
  config/journal.toml    — template device config for SD card
```

## Phases

| Phase | Status | Description |
|---|---|---|
| 0 | ✅ Scaffold | Hardware docs, project structure, platform drivers |
| 1 | ✅ MVP | Storage, home screen, quick check-in, timeline, prompts |
| 2 | ✅ Web | Wi-Fi AP, HTTP server, REST API, web editor SPA |
| 3 | 🔲 Polish | Favorites, streaks, sleep screen, UX tuning |
| 4 | 🔲 Privacy | Encrypted vault, lock screen, recovery phrase |
| 5 | 🔲 Export | Android PWA, GitHub backup, Obsidian export |

## License

MIT
