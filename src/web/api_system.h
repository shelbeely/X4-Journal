#pragma once
#include "esp_err.h"
#include <WebServer.h>

#ifdef __cplusplus
extern "C++" {
#endif

/* Register /api/version, /api/health, /api/logs, /api/ota/* routes. */
void api_system_register(WebServer *server);

#ifdef __cplusplus
}
#endif
