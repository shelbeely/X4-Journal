#include "screen_timeline.h"
#include "components.h"
#include "display.h"
#include "rtc.h"
#include <stdio.h>
#include <string.h>

#define VISIBLE_ROWS 6
#define ROW_HEIGHT   28

void screen_timeline_render(const timeline_state_t *ts)
{
    display_clear();
    ui_draw_header("Timeline");

    if (!ts || ts->count == 0) {
        ui_draw_centered_text(80, "No entries yet.");
        display_full_refresh();
        return;
    }

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = ts->scroll_offset + i;
        if (idx >= ts->count) break;
        int y = 20 + i * ROW_HEIGHT;
        bool sel = (idx == ts->selected_idx);

        /* draw date + mood bar */
        char date_str[12];
        strncpy(date_str, ts->entries[idx].id, 10);
        date_str[10] = '\0';
        display_draw_text(sel ? 14 : 4, y + 2, date_str, 1);

        char info[32];
        snprintf(info, sizeof(info), "M:%d E:%d A:%d",
                 ts->entries[idx].mood,
                 ts->entries[idx].energy,
                 ts->entries[idx].anxiety);
        display_draw_text(sel ? 14 : 4, y + 12, info, 1);

        if (ts->entries[idx].preview[0]) {
            display_draw_text(90, y + 2, ts->entries[idx].preview, 1);
        }
        if (sel) display_draw_text(2, y + 6, ">", 1);
        ui_draw_divider(y + ROW_HEIGHT - 1);
    }

    display_full_refresh();
}

void screen_timeline_entry_render(const journal_entry_t *e)
{
    if (!e) return;
    display_clear();

    char header[32];
    strncpy(header, e->id, 10);
    header[10] = '\0';
    ui_draw_header(header);

    int y = 22;
    char info[48];
    snprintf(info, sizeof(info), "Mood:%d  Energy:%d  Anxiety:%d",
             e->mood, e->energy, e->anxiety);
    display_draw_text(2, y, info, 1);
    y += 12;

    if (e->body_feeling[0]) {
        char feel[32];
        snprintf(feel, sizeof(feel), "Feeling: %s", e->body_feeling);
        display_draw_text(2, y, feel, 1);
        y += 12;
    }

    ui_draw_divider(y);
    y += 4;
    ui_draw_wrapped_text(2, y, DISPLAY_WIDTH - 4, e->body);

    display_full_refresh();
}
