#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include "esp_err.h"

esp_err_t journal_app_init(void);
void      journal_app_task(void *arg);

#ifdef __cplusplus
}
#endif
