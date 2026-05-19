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
| Controller | SSD1677 |
| Resolution | 800 × 480 pixels, 1-bit monochrome (greyscale via LUT) |
| Interface | 4-wire SPI |
| Refresh | Full refresh ~2 s; fast/partial refresh ~0.3 s (use sparingly) |

### Display GPIO

| Signal | GPIO |
|---|---|
| MOSI (SDA) | 10 |
| CLK (SCL) | 8 |
| CS | 21 |
| DC (Data/Command) | 4 |
| RST | 5 |
| BUSY | 6 |

## SD Card

| Property | Value |
|---|---|
| Interface | SPI (shares MOSI/CLK with display, different CS) |
| Filesystem | FAT32 via ESP-IDF FATFS |
| Mount point | `/sdcard` |

### SD GPIO

| Signal | GPIO |
|---|---|
| MOSI | 10 (shared with display) |
| MISO | 5 |
| CLK | 8 (shared with display) |
| CS | 12 |

## Physical Buttons

The X4 uses an **ADC resistor-ladder** scheme rather than individual GPIO pins for most
buttons. Two ADC pins each serve a group of buttons, and the power button is a direct
digital GPIO.

| Group | GPIO | Buttons |
|---|---|---|
| ADC bus 1 | 1 | BACK, CONFIRM, LEFT, RIGHT (resistor ladder) |
| ADC bus 2 | 2 | UP (VOL+), DOWN (VOL−) (resistor ladder) |
| Power (digital) | 3 | POWER (active LOW, internal pull-up) |

Button indices used by `InputManager` (community-sdk):

| Index | Name | Function |
|---|---|---|
| 0 | BACK | Go back / cancel |
| 1 | CONFIRM | Select / enter; hold = save/done |
| 2 | LEFT | Move selection left / previous |
| 3 | RIGHT | Move selection right / next |
| 4 | UP | Scroll up / increase value |
| 5 | DOWN | Scroll down / decrease value |
| 6 | POWER | Sleep/wake; hold = power off |

Hold detection threshold: **800 ms** (firmware-side, in `src/platform/buttons.cpp`).
Debounce: **5 ms** (InputManager SDK default).

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

> Note: GPIO 21 is also used as the EPD chip-select line (`X4_EPD_CS`). UART0 output
> is only relevant before the display driver asserts CS. In practice, debug output is
> accessed through the USB CDC serial port provided by the ESP32-C3 USB interface;
> use `pio device monitor` or a serial terminal at 115200 baud.

## SPI Bus Sharing

The display and SD card share the MOSI and CLK lines. They are disambiguated by
their individual CS pins (GPIO 21 for display, GPIO 12 for SD). Both are driven
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
