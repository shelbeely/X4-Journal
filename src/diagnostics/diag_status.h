#pragma once

/* Compile only when CONFIG_X4_DEV_DIAGNOSTICS is enabled. */
#ifdef CONFIG_X4_DEV_DIAGNOSTICS

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Populate json_out (len bytes) with the full diagnostics object.
   Returns ESP_OK on success, ESP_ERR_NO_MEM if the buffer is too small. */
esp_err_t diag_get_status(char *json_out, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_X4_DEV_DIAGNOSTICS */
