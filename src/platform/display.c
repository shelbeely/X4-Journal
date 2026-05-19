#include "display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "display";

/* SPI GPIO */
#define PIN_MOSI  6
#define PIN_CLK   7
#define PIN_CS   10
#define PIN_DC    2
#define PIN_RST   3
#define PIN_BUSY  4

#define EPD_WIDTH  DISPLAY_WIDTH
#define EPD_HEIGHT DISPLAY_HEIGHT
#define FB_BYTES   (EPD_WIDTH * EPD_HEIGHT / 8)

static spi_device_handle_t s_spi;
static uint8_t s_fb[FB_BYTES];  /* 1-bit framebuffer: 1=black, 0=white */

/* ---- minimal 5x7 ASCII font (printable chars 32–126) ---- */
static const uint8_t FONT5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x08,0x08,0x2A,0x1C,0x08}, /* '~' */
};

/* ---- SPI helpers ---- */
static void spi_write_byte(uint8_t b)
{
    spi_transaction_t t = {
        .length    = 8,
        .tx_buffer = &b,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void epd_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_DC, 0);
    gpio_set_level(PIN_CS, 0);
    spi_write_byte(cmd);
    gpio_set_level(PIN_CS, 1);
}

static void epd_data(uint8_t data)
{
    gpio_set_level(PIN_DC, 1);
    gpio_set_level(PIN_CS, 0);
    spi_write_byte(data);
    gpio_set_level(PIN_CS, 1);
}

static void epd_wait_busy(void)
{
    while (gpio_get_level(PIN_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void epd_hw_reset(void)
{
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* ---- EPD init sequence (GDEW0154M10-style) ---- */
static void epd_init_sequence(void)
{
    epd_hw_reset();
    epd_wait_busy();

    epd_cmd(0x12);   /* soft reset */
    epd_wait_busy();

    epd_cmd(0x01);   /* driver output control */
    epd_data(0xC7);
    epd_data(0x00);
    epd_data(0x01);

    epd_cmd(0x11);   /* data entry mode: X inc, Y inc, addr updated in X */
    epd_data(0x01);

    epd_cmd(0x44);   /* set RAM X address start/end (byte units, 0–24 = 25 bytes = 200 px) */
    epd_data(0x00);
    epd_data(0x18);

    epd_cmd(0x45);   /* set RAM Y address start/end */
    epd_data(0xC7);
    epd_data(0x00);
    epd_data(0x00);
    epd_data(0x00);

    epd_cmd(0x3C);   /* border waveform */
    epd_data(0x05);

    epd_cmd(0x18);   /* read built-in temperature sensor */
    epd_data(0x80);

    epd_cmd(0x4E);   /* set RAM X address counter */
    epd_data(0x00);
    epd_cmd(0x4F);   /* set RAM Y address counter */
    epd_data(0xC7);
    epd_data(0x00);

    epd_wait_busy();
}

esp_err_t display_init(void)
{
    /* configure non-SPI pins */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << PIN_CS),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_config_t bi = {
        .pin_bit_mask = (1ULL << PIN_BUSY),
        .mode         = GPIO_MODE_INPUT,
    };
    gpio_config(&bi);

    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = -1,
        .sclk_io_num   = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FB_BYTES + 8,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 2000000,
        .mode           = 0,
        .spics_io_num   = -1,  /* manual CS */
        .queue_size     = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi));

    memset(s_fb, 0xFF, sizeof(s_fb)); /* all white */
    epd_init_sequence();
    ESP_LOGI(TAG, "display_init ok");
    return ESP_OK;
}

void display_clear(void)
{
    memset(s_fb, 0xFF, sizeof(s_fb));
}

void display_set_pixel(int x, int y, uint8_t black)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    int byte_idx = (y * EPD_WIDTH + x) / 8;
    int bit_idx  = 7 - ((y * EPD_WIDTH + x) % 8);
    if (black) {
        s_fb[byte_idx] &= ~(1 << bit_idx);
    } else {
        s_fb[byte_idx] |=  (1 << bit_idx);
    }
}

void display_full_refresh(void)
{
    /* write framebuffer to EPD RAM */
    epd_cmd(0x24);  /* write RAM (BW) */
    gpio_set_level(PIN_DC, 1);
    gpio_set_level(PIN_CS, 0);
    spi_transaction_t t = {
        .length    = FB_BYTES * 8,
        .tx_buffer = s_fb,
    };
    spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(PIN_CS, 1);

    epd_cmd(0x22);  /* display update sequence: load LUT, enable clock, display */
    epd_data(0xF7);
    epd_cmd(0x20);  /* master activation */
    epd_wait_busy();
}

void display_draw_text(int x, int y, const char *text, int font_size)
{
    if (!text) return;
    int scale = (font_size < 2) ? 1 : 2;  /* simple 1x or 2x scaling */
    int cx = x;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) { cx += (5 + 1) * scale; continue; }
        const uint8_t *col = FONT5x7[c - 32];
        for (int col_i = 0; col_i < 5; col_i++) {
            for (int row_i = 0; row_i < 7; row_i++) {
                uint8_t bit = (col[col_i] >> row_i) & 1;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        display_set_pixel(cx + col_i * scale + sx,
                                          y  + row_i * scale + sy, bit);
                    }
                }
            }
        }
        cx += (5 + 1) * scale;
    }
}

void display_draw_line(int x0, int y0, int x1, int y1)
{
    /* Bresenham */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        display_set_pixel(x0, y0, 1);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void display_draw_rect(int x, int y, int w, int h, int filled)
{
    if (filled) {
        for (int row = y; row < y + h; row++) {
            display_draw_line(x, row, x + w - 1, row);
        }
    } else {
        display_draw_line(x,         y,         x + w - 1, y        );
        display_draw_line(x,         y + h - 1, x + w - 1, y + h - 1);
        display_draw_line(x,         y,         x,         y + h - 1);
        display_draw_line(x + w - 1, y,         x + w - 1, y + h - 1);
    }
}

void display_draw_image(int x, int y, int w, int h, const uint8_t *bitmap)
{
    if (!bitmap) return;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int bit_pos  = row * w + col;
            uint8_t byte = bitmap[bit_pos / 8];
            uint8_t bit  = (byte >> (7 - (bit_pos % 8))) & 1;
            display_set_pixel(x + col, y + row, bit);
        }
    }
}

void display_sleep(void)
{
    epd_cmd(0x10);
    epd_data(0x01);
    vTaskDelay(pdMS_TO_TICKS(100));
}

void display_wakeup(void)
{
    epd_init_sequence();
}
