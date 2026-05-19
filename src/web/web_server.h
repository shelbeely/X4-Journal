#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t web_server_start(void);
void      web_server_stop(void);
bool      web_server_is_running(void);

#ifdef __cplusplus
}
#endif
