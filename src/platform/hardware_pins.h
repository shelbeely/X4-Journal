#pragma once

/* Shared SPI bus (SPI2) pin assignments for the Xteink X4 hardware.
   Both the e-paper display and the SD card share this bus with
   separate chip-select lines. */
#define X4_SPI_SCLK  8
#define X4_SPI_MOSI 10
#define X4_SPI_MISO  5

/* E-paper display control pins */
#define X4_EPD_CS   21
#define X4_EPD_DC    4
#define X4_EPD_RST   5
#define X4_EPD_BUSY  6

/* SD card chip select */
#define X4_SD_CS    12

/* Battery ADC */
#define X4_BAT_ADC_PIN  1   /* GPIO1 = ADC channel 0 on ESP32-C3 */
#define X4_BAT_DIVIDER  2.0f

/* Power button (also used as wake-from-sleep source) */
#define X4_POWER_BTN_PIN  3
