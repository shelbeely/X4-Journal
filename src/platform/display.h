#pragma once
#include "esp_err.h"
#include <stdint.h>

#define DISPLAY_WIDTH  200
#define DISPLAY_HEIGHT 200

esp_err_t display_init(void);
void      display_clear(void);
void      display_full_refresh(void);
void      display_set_pixel(int x, int y, uint8_t black);
void      display_draw_text(int x, int y, const char *text, int font_size);
void      display_draw_line(int x0, int y0, int x1, int y1);
void      display_draw_rect(int x, int y, int w, int h, int filled);
void      display_draw_image(int x, int y, int w, int h, const uint8_t *bitmap);
void      display_sleep(void);
void      display_wakeup(void);
