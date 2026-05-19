# Hardware Map — xteink X4

This document is the authoritative pin reference for all firmware modules.

## MCU

| Property | Value |
|---|---|
| Chip | ESP32-C3 (single-core RISC-V, 160 MHz) |
| RAM | 400 KB SRAM |
| Flash | 4 MB (internal), expandable via SD |
| Wireless | 802.11 b/g/n 2.4 GHz Wi-Fi + BLE 5.0 |
| SDK | ESP-IDF v5.x |

## E-Paper Display

| Property | Value |
|---|---|
| Model | GDEW0154M10 (or compatible) |
| Resolution | 200 × 200 pixels, 1-bit monochrome |
| Interface | 4-wire SPI |
| Refresh | Full refresh ~2 s; partial refresh ~0.3 s (use sparingly) |

### Display GPIO

| Signal | GPIO |
|---|---|
| MOSI (SDA) | 6 |
| CLK (SCL) | 7 |
| CS | 10 |
| DC (Data/Command) | 2 |
| RST | 3 |
| BUSY | 4 |

## SD Card

| Property | Value |
|---|---|
| Interface | SPI (shares MOSI/CLK with display, different CS) |
| Filesystem | FAT32 via ESP-IDF FATFS |
| Mount point | `/sdcard` |

### SD GPIO

| Signal | GPIO |
|---|---|
| MOSI | 6 (shared with display) |
| MISO | 5 |
| CLK | 7 (shared with display) |
| CS | 8 |

## Physical Buttons

All buttons are **active-low** with internal pull-ups enabled.
Hold detection threshold: **800 ms**.
Debounce: **50 ms**.

| Button | GPIO | Function |
|---|---|---|
| LEFT | 18 | Move selection left / previous |
| RIGHT | 19 | Move selection right / next |
| CONFIRM | 20 | Select / enter; hold = save/done |
| BACK | 21 | Go back / cancel |
| VOL+ | 9 | Scroll up / increase value |
| VOL− | 10 | Scroll down / decrease value |
| POWER | 11 | Sleep/wake; hold = power off |

## Battery

| Property | Value |
|---|---|
| Chemistry | 3.7 V LiPo |
| ADC pin | GPIO 1 (ADC1 channel 0) |
| Divider | 2:1 (actual voltage = adc_reading × 2) |
| Voltage range | 3.3 V (0%) → 4.2 V (100%) |

## RTC / Timekeeping

The ESP32-C3 does not have a dedicated hardware RTC that survives power cycles.
Timekeeping strategy (in priority order):

1. **SNTP** — sync from NTP on Wi-Fi connect; store result in NVS.
2. **NVS timestamp** — on wake from sleep, restore last-known Unix time from NVS.
3. **RTC coprocessor** — ESP32-C3 has an ultra-low-power RTC coprocessor; time is
   preserved through light sleep but lost on cold boot.
4. **External DS3231** (optional) — connect via I2C if battery-backed timekeeping is required.

### Optional External RTC (DS3231)

| Signal | GPIO |
|---|---|
| SDA | 0 |
| SCL | 1 |

## UART (Debug)

| Signal | GPIO |
|---|---|
| TX | 21 |
| RX | 20 |

> Note: UART TX/RX share GPIO with CONFIRM/BACK buttons. These GPIOs are reconfigured
> as GPIO inputs at runtime after boot (UART is only used for debug output via USB
> serial on the devkit; on production hardware UART is not connected to external pins).

## SPI Bus Sharing

The display and SD card share the MOSI and CLK lines. They are disambiguated by
their individual CS pins (GPIO 10 for display, GPIO 8 for SD). Both are driven
by the same ESP-IDF SPI host (SPI2 / HSPI). The display driver holds CS low only
during command/data transactions; the SD driver uses the FATFS SPI layer which
manages its own CS.

## E-Paper UX Constraints

- Full refresh required after every screen change to avoid ghosting.
- Partial refresh is available but must be tested for stability per display batch.
- Never animate; the EPD update latency (~2 s full, ~0.3 s partial) makes animations
  impossible and the attempt produces ugly artifacts.
- Always wait for BUSY pin LOW before sending subsequent commands.
- EPD deep-sleep mode draws ~0 µA; call `display_sleep()` before `power_sleep()`.
