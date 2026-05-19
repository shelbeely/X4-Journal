# X4-Journal

A PlatformIO firmware project for the **Xteink X4** e-ink device, built on top of the
[OpenX4 E-Paper Community SDK](https://github.com/open-x4-epaper/community-sdk).

## Requirements

- [PlatformIO](https://platformio.org/install) (CLI or IDE extension)
- Xteink X4 hardware

## Getting started

```bash
# 1. Clone with submodules
git clone --recurse-submodules https://github.com/shelbeely/X4-Journal.git
cd X4-Journal

# 2. (If you already cloned without --recurse-submodules)
git submodule update --init --recursive

# 3. Build
pio run

# 4. Flash
pio run --target upload
```

## Project structure

```
X4-Journal/
├── open-x4-sdk/        # Community SDK (git submodule)
│   ├── libs/
│   │   ├── display/EInkDisplay/
│   │   ├── hardware/BatteryMonitor/
│   │   ├── hardware/InputManager/
│   │   └── hardware/SDCardManager/
│   └── tools/
├── src/
│   └── main.cpp        # Firmware entry point
└── platformio.ini      # PlatformIO build configuration
```

## SDK libraries used

| Library | Path | Description |
|---|---|---|
| `EInkDisplay` | `open-x4-sdk/libs/display/EInkDisplay` | Low-level e-ink driver with grayscale support |
| `BatteryMonitor` | `open-x4-sdk/libs/hardware/BatteryMonitor` | Battery voltage & percentage |
| `InputManager` | `open-x4-sdk/libs/hardware/InputManager` | Button input handling |
| `SDCardManager` | `open-x4-sdk/libs/hardware/SDCardManager` | SD card file system utilities |