#include "display.h"
#include "hardware_pins.h"
#include <EInkDisplay.h>
#include "esp_log.h"
#include "esp_timer.h"
#include <new>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "display";

static EInkDisplay *s_eink = nullptr;

/* ---- Module-level status struct ------------------------------------------ */
static display_status_t s_status = {
    .driver                  = "SSD1677",
    .width                   = X4_DISPLAY_WIDTH,
    .height                  = X4_DISPLAY_HEIGHT,
    .rotation                = 0,
    .framebuffer_size        = 0,
    .framebuffer_hash        = 0,
    .last_refresh_type       = "none",
    .last_refresh_duration_ms = 0,
    .busy_pin_wait_ms        = 0,
    .last_error              = "",
    .init_ok                 = false,
    .test_pattern_last       = "none",
    .test_pattern_result     = "none",
};

/* ---- CRC32 helper (table-free, used for framebuffer hash) ----------------- */
static uint32_t crc32_byte(uint32_t crc, uint8_t b)
{
    crc ^= (uint32_t)b;
    for (int i = 0; i < 8; i++)
        crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0u);
    return crc;
}

static uint32_t crc32_buf(const uint8_t *buf, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) crc = crc32_byte(crc, buf[i]);
    return ~crc;
}

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

/* Set a pixel directly in the EInkDisplay framebuffer.
   EInkDisplay convention: 0 bit = black, 1 bit = white (row-major, MSB first).
   The 'black' parameter is 1 to draw black (clears the bit) and 0 for white (sets the bit). */
static void fb_set_pixel(uint8_t *fb, int x, int y, uint8_t black)
{
    if (x < 0 || x >= X4_DISPLAY_WIDTH || y < 0 || y >= X4_DISPLAY_HEIGHT) return;
    int stride    = X4_DISPLAY_WIDTH / 8;
    int byte_idx  = y * stride + x / 8;
    int bit_idx   = 7 - (x % 8);
    if (black) {
        fb[byte_idx] &= ~(1 << bit_idx);
    } else {
        fb[byte_idx] |=  (1 << bit_idx);
    }
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "[X4] DISPLAY_INIT_START");
    if (!s_eink) {
        size_t fb_size = (X4_DISPLAY_WIDTH / 8) * X4_DISPLAY_HEIGHT;
        s_eink = new (std::nothrow) EInkDisplay(X4_SPI_SCLK, X4_SPI_MOSI, X4_EPD_CS,
                                                 X4_EPD_DC, X4_EPD_RST, X4_EPD_BUSY);
        if (!s_eink) {
            size_t heap = esp_get_free_heap_size();
            ESP_LOGE(TAG, "[X4] DISPLAY_FRAMEBUFFER_ALLOC_FAILED size_requested=%zu heap_free=%zu",
                     fb_size, heap);
            strncpy(s_status.last_error, "alloc_failed", sizeof(s_status.last_error)-1);
            ESP_LOGE(TAG, "[X4] DISPLAY_INIT_FAILED error=alloc_failed");
            return ESP_ERR_NO_MEM;
        }
        s_status.framebuffer_size = fb_size;
        ESP_LOGI(TAG, "[X4] DISPLAY_FRAMEBUFFER_ALLOC_OK size=%zu", fb_size);
    }
    s_eink->begin();
    s_eink->clearScreen(0xFF);
    s_status.init_ok = true;
    s_status.last_error[0] = '\0';
    ESP_LOGI(TAG, "[X4] DISPLAY_INIT_OK driver=%s rotation=%d width=%d height=%d",
             s_status.driver, s_status.rotation, X4_DISPLAY_WIDTH, X4_DISPLAY_HEIGHT);
    return ESP_OK;
}

void display_clear(void)
{
    if (!s_eink) return;
    s_eink->clearScreen(0xFF); /* 0xFF = all white */
}

void display_full_refresh(void)
{
    if (!s_eink) return;
    ESP_LOGI(TAG, "[X4] DISPLAY_FULL_REFRESH_START");
    int64_t t0 = esp_timer_get_time();
    s_eink->displayBuffer(EInkDisplay::FULL_REFRESH);
    uint32_t dur = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    s_status.last_refresh_duration_ms = dur;
    strncpy(s_status.last_refresh_type, "full", sizeof(s_status.last_refresh_type)-1);
    /* Approximate busy-pin wait as the total refresh time for the status object */
    s_status.busy_pin_wait_ms = dur;
    /* Update framebuffer hash after refresh */
    uint8_t *fb = s_eink->getFrameBuffer();
    if (fb) s_status.framebuffer_hash = crc32_buf(fb, s_status.framebuffer_size);
    ESP_LOGI(TAG, "[X4] DISPLAY_FULL_REFRESH_OK duration_ms=%lu", (unsigned long)dur);
}

void display_partial_refresh(int x, int y, int w, int h)
{
    if (!s_eink) return;
    ESP_LOGI(TAG, "[X4] DISPLAY_PARTIAL_REFRESH_START x=%d y=%d w=%d h=%d", x, y, w, h);
    int64_t t0 = esp_timer_get_time();
    /* Fall back to full refresh if the driver does not expose partial mode */
    s_eink->displayBuffer(EInkDisplay::FULL_REFRESH);
    uint32_t dur = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    s_status.last_refresh_duration_ms = dur;
    strncpy(s_status.last_refresh_type, "partial", sizeof(s_status.last_refresh_type)-1);
    s_status.busy_pin_wait_ms = dur;
    uint8_t *fb = s_eink->getFrameBuffer();
    if (fb) s_status.framebuffer_hash = crc32_buf(fb, s_status.framebuffer_size);
    ESP_LOGI(TAG, "[X4] DISPLAY_PARTIAL_REFRESH_OK duration_ms=%lu", (unsigned long)dur);
}

void display_set_pixel(int x, int y, uint8_t black)
{
    fb_set_pixel(s_eink->getFrameBuffer(), x, y, black);
}

void display_draw_text(int x, int y, const char *text, int font_size)
{
    if (!text) return;
    uint8_t *fb  = s_eink->getFrameBuffer();
    int scale    = (font_size < 2) ? 1 : 2;
    int cx       = x;
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) { cx += (5 + 1) * scale; continue; }
        const uint8_t *col = FONT5x7[c - 32];
        for (int col_i = 0; col_i < 5; col_i++) {
            for (int row_i = 0; row_i < 7; row_i++) {
                uint8_t bit = (col[col_i] >> row_i) & 1;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        fb_set_pixel(fb, cx + col_i * scale + sx,
                                     y + row_i * scale + sy, bit);
                    }
                }
            }
        }
        cx += (5 + 1) * scale;
    }
}

void display_draw_line(int x0, int y0, int x1, int y1)
{
    uint8_t *fb = s_eink->getFrameBuffer();
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        fb_set_pixel(fb, x0, y0, 1);
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
    if (!bitmap || !s_eink) return;
    uint8_t *fb = s_eink->getFrameBuffer();
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int     bit_pos = row * w + col;
            uint8_t byte    = bitmap[bit_pos / 8];
            uint8_t bit     = (byte >> (7 - (bit_pos % 8))) & 1;
            fb_set_pixel(fb, x + col, y + row, bit);
        }
    }
}

void display_sleep(void)
{
    if (!s_eink) return;
    s_eink->deepSleep();
    ESP_LOGI(TAG, "[X4] DISPLAY_SLEEP_OK");
}

void display_wakeup(void)
{
    if (!s_eink) return;
    s_eink->begin();
    s_eink->clearScreen(0xFF);
    ESP_LOGI(TAG, "[X4] DISPLAY_WAKE_OK");
}

/* ---- Diagnostics extensions ---------------------------------------------- */

bool display_get_init_ok(void)
{
    return s_status.init_ok;
}

void display_get_status(display_status_t *out)
{
    if (!out) return;
    /* Update live framebuffer hash */
    if (s_eink && s_status.init_ok) {
        uint8_t *fb = s_eink->getFrameBuffer();
        if (fb) s_status.framebuffer_hash = crc32_buf(fb, s_status.framebuffer_size);
    }
    *out = s_status;
}

uint8_t *display_get_framebuffer(void)
{
    if (!s_eink || !s_status.init_ok) return nullptr;
    return s_eink->getFrameBuffer();
}

/* ---- Test patterns ------------------------------------------------------- */

esp_err_t display_render_test_pattern(const char *name)
{
    if (!s_eink || !s_status.init_ok || !name) return ESP_ERR_INVALID_STATE;

    uint8_t *fb = s_eink->getFrameBuffer();
    if (!fb) return ESP_ERR_NO_MEM;

    strncpy(s_status.test_pattern_last, name, sizeof(s_status.test_pattern_last)-1);
    strncpy(s_status.test_pattern_result, "failed", sizeof(s_status.test_pattern_result)-1);

    int W = X4_DISPLAY_WIDTH;
    int H = X4_DISPLAY_HEIGHT;
    size_t fb_bytes = (W / 8) * H;

    if (strcmp(name, "all_white") == 0) {
        memset(fb, 0xFF, fb_bytes);
    } else if (strcmp(name, "all_black") == 0) {
        memset(fb, 0x00, fb_bytes);
    } else if (strcmp(name, "checkerboard") == 0) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                fb_set_pixel(fb, x, y, ((x / 8 + y / 8) % 2 == 0) ? 1 : 0);
            }
        }
    } else if (strcmp(name, "border") == 0) {
        memset(fb, 0xFF, fb_bytes);          /* white fill */
        display_draw_rect(0, 0, W, H, 0);   /* 1-pixel black border */
        display_draw_text(2,  2,       "TL", 1);
        display_draw_text(W-14, 2,     "TR", 1);
        display_draw_text(2,  H-10,    "BL", 1);
        display_draw_text(W-14, H-10,  "BR", 1);
    } else if (strcmp(name, "diagonal") == 0) {
        memset(fb, 0xFF, fb_bytes);
        display_draw_line(0, 0, W-1, H-1);
    } else if (strcmp(name, "font_sample") == 0) {
        memset(fb, 0xFF, fb_bytes);
        display_draw_text(10, 10,  "X4 DIAG 0123456789", 2);
        display_draw_text(10, 40,  "abcdefghijklmnopqrstuvwxyz", 1);
        display_draw_text(10, 55,  "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 1);
    } else if (strcmp(name, "partial_rect") == 0) {
        memset(fb, 0xFF, fb_bytes);
        int rx = (W - 64) / 2, ry = (H - 32) / 2;
        display_draw_rect(rx, ry, 64, 32, 1);
    } else if (strcmp(name, "rotation_test") == 0) {
        memset(fb, 0xFF, fb_bytes);
        display_draw_text(W/2 - 20, 4,     "TOP",    2);
        display_draw_text(W/2 - 24, H-22,  "BOTTOM", 1);
        display_draw_text(2,  H/2 - 5,     "LEFT",   1);
        display_draw_text(W-38, H/2 - 5,   "RIGHT",  1);
    } else {
        ESP_LOGW(TAG, "Unknown test pattern: %s", name);
        return ESP_ERR_INVALID_ARG;
    }

    display_full_refresh();
    strncpy(s_status.test_pattern_result, "ok", sizeof(s_status.test_pattern_result)-1);
    ESP_LOGI(TAG, "Test pattern '%s' rendered OK", name);
    return ESP_OK;
}

/* ---- BMP screenshot ------------------------------------------------------- */

esp_err_t display_screenshot_bmp(uint8_t **buf_out, size_t *len_out)
{
    if (!buf_out || !len_out) return ESP_ERR_INVALID_ARG;
    if (!s_eink || !s_status.init_ok) return ESP_ERR_INVALID_STATE;

    uint8_t *fb = s_eink->getFrameBuffer();
    if (!fb) return ESP_ERR_INVALID_STATE;

    /* BMP 1-bit layout:
       Row stride = ceil(width/32)*4 bytes.  For 800 px: 800/8 = 100 = 4-byte aligned. */
    int W = X4_DISPLAY_WIDTH;
    int H = X4_DISPLAY_HEIGHT;
    int row_stride = ((W + 31) / 32) * 4;     /* 100 bytes for 800 px */
    size_t pixel_data_size = (size_t)row_stride * H;
    /* BMP header = 14 + DIB header = 40 + color table = 8 */
    size_t header_size = 14 + 40 + 8;
    size_t total = header_size + pixel_data_size;

    uint8_t *bmp = (uint8_t *)malloc(total);
    if (!bmp) return ESP_ERR_NO_MEM;
    memset(bmp, 0, total);

    uint8_t *p = bmp;

    /* --- BMP File Header (14 bytes) --- */
    *p++ = 'B'; *p++ = 'M';
    uint32_t file_sz = (uint32_t)total;
    memcpy(p, &file_sz, 4); p += 4;
    p += 4;  /* reserved */
    uint32_t px_offset = (uint32_t)header_size;
    memcpy(p, &px_offset, 4); p += 4;

    /* --- DIB Header BITMAPINFOHEADER (40 bytes) --- */
    uint32_t dib_sz   = 40;
    int32_t  bmp_w    = W;
    int32_t  bmp_h    = -H;   /* negative = top-down (matches our framebuffer order) */
    uint16_t planes   = 1;
    uint16_t bpp      = 1;
    uint32_t compress = 0;
    uint32_t img_sz   = (uint32_t)pixel_data_size;
    int32_t  xpels    = 2835, ypels = 2835;
    uint32_t clr_used = 2, clr_imp = 2;
    memcpy(p, &dib_sz,   4); p += 4;
    memcpy(p, &bmp_w,    4); p += 4;
    memcpy(p, &bmp_h,    4); p += 4;
    memcpy(p, &planes,   2); p += 2;
    memcpy(p, &bpp,      2); p += 2;
    memcpy(p, &compress, 4); p += 4;
    memcpy(p, &img_sz,   4); p += 4;
    memcpy(p, &xpels,    4); p += 4;
    memcpy(p, &ypels,    4); p += 4;
    memcpy(p, &clr_used, 4); p += 4;
    memcpy(p, &clr_imp,  4); p += 4;

    /* --- Color table: index 0 = black, index 1 = white --- */
    /* RGBQUAD: blue, green, red, reserved */
    /* index 0: black */
    *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x00;
    /* index 1: white */
    *p++ = 0xFF; *p++ = 0xFF; *p++ = 0xFF; *p++ = 0x00;

    /* --- Pixel data: copy framebuffer rows (top-down because bmp_h < 0) ---
       e-paper FB: 0=black, 1=white  →  BMP palette 0=black, 1=white  → copy directly */
    int src_stride = W / 8;
    for (int row = 0; row < H; row++) {
        memcpy(p, fb + row * src_stride, src_stride);
        p += row_stride;   /* dst stride may be >= src_stride (both 100 for 800px) */
    }

    *buf_out = bmp;
    *len_out = total;
    return ESP_OK;
}
