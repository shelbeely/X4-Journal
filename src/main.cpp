#include <Arduino.h>
#include <EInkDisplay.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <SDCardManager.h>

// ── Display SPI pins (Xteink X4) ─────────────────────────────────────────────
#define EPD_SCLK  8
#define EPD_MOSI 10
#define EPD_CS   21
#define EPD_DC    4
#define EPD_RST   5
#define EPD_BUSY  6

// ── Battery ADC pin ───────────────────────────────────────────────────────────
#define BATTERY_ADC_PIN 7

// ── Peripheral instances ──────────────────────────────────────────────────────
EInkDisplay display(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY);
BatteryMonitor battery(BATTERY_ADC_PIN);
InputManager input;

void setup() {
    Serial.begin(115200);

    // Initialise display
    display.begin();

    // Initialise SD card (optional – continues if no card is present)
    if (!SdMan.begin()) {
        Serial.println("SD card not found – continuing without it.");
    }

    // Initialise button inputs
    input.begin();

    // Draw initial screen
    display.clearScreen();
    display.displayBuffer(EInkDisplay::FULL_REFRESH);

    Serial.println("X4-Journal ready.");
    Serial.printf("Battery: %u%%\n", battery.readPercentage());
}

void loop() {
    input.update();

    if (input.wasAnyPressed()) {
        Serial.printf("Button pressed – battery: %u%%\n", battery.readPercentage());
    }
}
