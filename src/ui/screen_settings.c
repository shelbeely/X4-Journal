#include "screen_settings.h"
#include "components.h"
#include "display.h"

static const char *SETTINGS_ITEMS[] = {
    "Full Refresh",
    "Encryption",
    "Prompt Pack",
    "Export Journal",
    "Import from SD",
    "About",
};
#define SETTINGS_COUNT 6

void screen_settings_render(int selected_idx)
{
    display_clear();
    ui_draw_header("Settings");
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        ui_draw_menu_item(20 + i * 28, SETTINGS_ITEMS[i], i == selected_idx);
    }
    display_full_refresh();
}
