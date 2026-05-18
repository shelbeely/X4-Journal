#include "components.h"
#include "display.h"
#include <stdio.h>
#include <string.h>

#define FONT_SM 1
#define FONT_MD 2

void ui_draw_header(const char *title)
{
    display_draw_rect(0, 0, DISPLAY_WIDTH, 18, 1);  /* black background */
    /* invert: draw white text on black — simple approach: draw text then invert region
       For simplicity draw with XOR trick: draw text in black on a separate pass.
       Since we can't XOR easily, just draw rect then draw text normally (black on black)
       and rely on caller to invert. Instead, leave rect white and draw border: */
    display_draw_rect(0, 0, DISPLAY_WIDTH, 18, 0);
    display_draw_line(0, 17, DISPLAY_WIDTH - 1, 17);
    if (title) {
        display_draw_text(4, 4, title, FONT_MD);
    }
}

void ui_draw_status_bar(uint8_t battery_pct, const char *last_entry)
{
    int y = DISPLAY_HEIGHT - 18;
    display_draw_line(0, y, DISPLAY_WIDTH - 1, y);
    char buf[48];
    snprintf(buf, sizeof(buf), "Bat:%d%%", battery_pct);
    display_draw_text(2, y + 5, buf, FONT_SM);
    if (last_entry) {
        display_draw_text(70, y + 5, last_entry, FONT_SM);
    }
}

void ui_draw_menu_item(int y, const char *label, bool selected)
{
    if (selected) {
        display_draw_rect(0, y, DISPLAY_WIDTH, 22, 1);
        /* draw inverted text: since display_draw_text draws black pixels on white bg,
           we draw the rect first then text — pixels that match will cancel.
           For true invert we'd need per-pixel XOR; instead draw white text effect
           by drawing rect then text — text pixels set to black over black = invisible.
           Workaround: draw selection indicator with arrow instead */
        display_draw_rect(0, y, DISPLAY_WIDTH, 22, 0);  /* outline */
        display_draw_text(2, y + 7, ">", FONT_SM);
        display_draw_text(12, y + 7, label ? label : "", FONT_SM);
    } else {
        display_draw_text(12, y + 7, label ? label : "", FONT_SM);
    }
}

void ui_draw_divider(int y)
{
    display_draw_line(0, y, DISPLAY_WIDTH - 1, y);
}

void ui_draw_progress_bar(int x, int y, int w, int h, int value, int max_value)
{
    display_draw_rect(x, y, w, h, 0);
    if (max_value > 0 && value > 0) {
        int filled = (value * (w - 2)) / max_value;
        if (filled > w - 2) filled = w - 2;
        display_draw_rect(x + 1, y + 1, filled, h - 2, 1);
    }
}

void ui_draw_big_number(int x, int y, int value)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    display_draw_text(x, y, buf, FONT_MD);
}

void ui_draw_tag(int x, int y, const char *tag)
{
    if (!tag) return;
    int w = strlen(tag) * 7 + 6;
    display_draw_rect(x, y, w, 12, 0);
    display_draw_text(x + 2, y + 2, tag, FONT_SM);
}

void ui_draw_centered_text(int y, const char *text)
{
    if (!text) return;
    int len = strlen(text);
    int x = (DISPLAY_WIDTH - len * 7) / 2;
    if (x < 0) x = 0;
    display_draw_text(x, y, text, FONT_SM);
}

void ui_draw_wrapped_text(int x, int y, int max_width, const char *text)
{
    if (!text) return;
    char line[64];
    int  chars_per_line = max_width / 7;  /* 7px per char at FONT_SM */
    if (chars_per_line < 1) chars_per_line = 1;
    int cy = y;
    const char *p = text;

    while (*p && cy < DISPLAY_HEIGHT - 20) {
        int n = 0;
        /* find last space before chars_per_line */
        const char *end = p;
        while (*end && n < chars_per_line) { end++; n++; }
        if (*end) {
            const char *sp = end;
            while (sp > p && *sp != ' ') sp--;
            if (sp > p) end = sp;
        }
        n = end - p;
        if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        display_draw_text(x, cy, line, FONT_SM);
        cy += 10;
        p = end;
        if (*p == ' ') p++;
    }
}
