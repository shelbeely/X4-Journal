#pragma once
#include <stdint.h>
#include <stdbool.h>

void ui_draw_header(const char *title);
void ui_draw_status_bar(uint8_t battery_pct, const char *last_entry);
void ui_draw_menu_item(int y, const char *label, bool selected);
void ui_draw_divider(int y);
void ui_draw_progress_bar(int x, int y, int w, int h, int value, int max_value);
void ui_draw_big_number(int x, int y, int value);
void ui_draw_tag(int x, int y, const char *tag);
void ui_draw_centered_text(int y, const char *text);
void ui_draw_wrapped_text(int x, int y, int max_width, const char *text);
