#pragma once
#include "esp_err.h"

typedef enum {
    ROUTE_HOME,
    ROUTE_TODAY,
    ROUTE_TIMELINE,
    ROUTE_TIMELINE_ENTRY,
    ROUTE_PROMPTS,
    ROUTE_CHECKIN,
    ROUTE_SYNC,
    ROUTE_SETTINGS,
    ROUTE_SLEEP,
} app_route_t;

esp_err_t   routes_init(void);
void        routes_navigate(app_route_t route);
app_route_t routes_current(void);
void        routes_back(void);
