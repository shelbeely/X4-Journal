#include "screen_sync.h"
#include "components.h"
#include "display.h"
#include <stdio.h>

void screen_sync_render(const char *ssid, const char *ip)
{
    display_clear();
    ui_draw_header("Web Editor");

    ui_draw_centered_text(30, "Connect to Wi-Fi:");
    ui_draw_centered_text(44, ssid ? ssid : "");
    ui_draw_divider(58);
    ui_draw_centered_text(64, "Open browser:");

    char url[48];
    snprintf(url, sizeof(url), "http://%s", ip ? ip : "192.168.4.1");
    ui_draw_centered_text(78, url);

    ui_draw_divider(DISPLAY_HEIGHT - 20);
    ui_draw_centered_text(DISPLAY_HEIGHT - 16, "BACK to stop server");

    display_full_refresh();
}
