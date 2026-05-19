#include "screen_home.h"
#include "components.h"
#include "display.h"
#include "power.h"
#include "prompt_engine.h"
#include "journal_fs.h"
#include "rtc.h"
#include <stdio.h>

static const char *MENU_ITEMS[] = {
    "New Entry", "Today", "Timeline", "Prompts", "Sync", "Settings",
};
#define MENU_COUNT 6

void screen_home_render(int selected_idx)
{
    display_clear();

    /* date */
    rtc_datetime_t dt;
    rtc_get_datetime(&dt);
    char date_buf[48];
    rtc_format_date(&dt, date_buf, sizeof(date_buf));
    display_draw_text(4, 2, date_buf, 2);

    ui_draw_divider(18);

    /* daily prompt */
    const char *prompt = prompt_get_daily();
    ui_draw_wrapped_text(4, 22, X4_DISPLAY_WIDTH - 8, prompt);

    ui_draw_divider(52);

    /* menu items */
    for (int i = 0; i < MENU_COUNT; i++) {
        ui_draw_menu_item(54 + i * 22, MENU_ITEMS[i], i == selected_idx);
    }

    ui_draw_divider(X4_DISPLAY_HEIGHT - 18);

    /* status bar */
    char last[32];
    journal_fs_get_last_entry_time(last, sizeof(last));
    ui_draw_status_bar(power_get_battery_pct(), last);

    display_full_refresh();
}
