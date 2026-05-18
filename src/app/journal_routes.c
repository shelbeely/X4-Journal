#include "journal_routes.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "routes";

#define STACK_DEPTH 8

static app_route_t s_stack[STACK_DEPTH];
static int         s_top = -1;

esp_err_t routes_init(void)
{
    s_top      = 0;
    s_stack[0] = ROUTE_HOME;
    ESP_LOGI(TAG, "routes_init ok");
    return ESP_OK;
}

void routes_navigate(app_route_t route)
{
    if (s_top < STACK_DEPTH - 1) {
        s_stack[++s_top] = route;
    } else {
        /* stack full: replace top */
        s_stack[s_top] = route;
    }
    ESP_LOGD(TAG, "navigate → %d (depth %d)", route, s_top);
}

app_route_t routes_current(void)
{
    if (s_top < 0) return ROUTE_HOME;
    return s_stack[s_top];
}

void routes_back(void)
{
    if (s_top > 0) {
        s_top--;
        ESP_LOGD(TAG, "back → %d", s_stack[s_top]);
    }
}
