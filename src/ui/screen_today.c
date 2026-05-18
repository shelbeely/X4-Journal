#include "screen_today.h"
#include "components.h"
#include "display.h"
#include "metadata_index.h"
#include "rtc.h"
#include <stdio.h>

void screen_today_render(void)
{
    display_clear();
    ui_draw_header("Today");

    rtc_datetime_t dt;
    rtc_get_datetime(&dt);

    entry_meta_t entries[16];
    int count = 0;
    index_get_by_date(dt.year, dt.month, dt.day, entries, &count);

    if (count == 0) {
        ui_draw_centered_text(80, "No entries today.");
        ui_draw_centered_text(95, "Press CONFIRM to");
        ui_draw_centered_text(110, "write one.");
    } else {
        for (int i = 0; i < count && i < 6; i++) {
            char line[64];
            int h12 = 0, mi = 0;
            sscanf(entries[i].id, "%*4d-%*2d-%*2d-%2d%2d", &h12, &mi);
            int ampm = h12 >= 12;
            if (h12 == 0) h12 = 12; else if (h12 > 12) h12 -= 12;
            snprintf(line, sizeof(line), "%d:%02d%s  m:%d e:%d  %s",
                     h12, mi, ampm ? "pm" : "am",
                     entries[i].mood, entries[i].energy,
                     entries[i].preview[0] ? entries[i].preview : "");
            ui_draw_menu_item(20 + i * 24, line, false);
        }
    }

    display_full_refresh();
}
