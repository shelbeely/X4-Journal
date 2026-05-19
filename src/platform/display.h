#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define X4_DISPLAY_WIDTH  800
#define X4_DISPLAY_HEIGHT 480

/* ---- Display status object (mirrors display-diagnostics.md schema) -------- */
typedef struct {
    char     driver[32];
    int      width;
    int      height;
    int      rotation;
    size_t   framebuffer_size;
    uint32_t framebuffer_hash;         /* CRC32 of framebuffer contents */
    char     last_refresh_type[16];    /* "full", "partial", "none" */
    uint32_t last_refresh_duration_ms;
    uint32_t busy_pin_wait_ms;
    char     last_error[64];
    bool     init_ok;
    char     test_pattern_last[32];
    char     test_pattern_result[8];   /* "ok", "failed", "none" */
} display_status_t;

/* ---- Core driver API (existing) ------------------------------------------ */
esp_err_t display_init(void);
void      display_clear(void);
void      display_full_refresh(void);
void      display_partial_refresh(int x, int y, int w, int h);
void      display_set_pixel(int x, int y, uint8_t black);
void      display_draw_text(int x, int y, const char *text, int font_size);
void      display_draw_line(int x0, int y0, int x1, int y1);
void      display_draw_rect(int x, int y, int w, int h, int filled);
void      display_draw_image(int x, int y, int w, int h, const uint8_t *bitmap);
void      display_sleep(void);
void      display_wakeup(void);

/* ---- Diagnostics extensions ---------------------------------------------- */
bool      display_get_init_ok(void);
void      display_get_status(display_status_t *out);
uint8_t  *display_get_framebuffer(void);

/* Render a named test pattern.
   Names: "all_white", "all_black", "checkerboard", "border",
          "diagonal", "font_sample", "partial_rect", "rotation_test" */
esp_err_t display_render_test_pattern(const char *name);

/* Generate a 1-bit BMP image of the current framebuffer.
   Caller must free() *buf_out.  Returns ESP_ERR_NO_MEM if heap insufficient. */
esp_err_t display_screenshot_bmp(uint8_t **buf_out, size_t *len_out);

#ifdef __cplusplus
}
#endif
