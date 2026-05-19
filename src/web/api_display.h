#pragma once
#include "esp_err.h"
#include <WebServer.h>

#ifdef __cplusplus
extern "C++" {
#endif

/* Register /api/display/* routes. */
void api_display_register(WebServer *server);

#ifdef __cplusplus
}
#endif
