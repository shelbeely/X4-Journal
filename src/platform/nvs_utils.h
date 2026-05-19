#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NVS namespace used by this module: "x4sys" */

/* Initialise NVS flash; handles erase-and-reinit if partition is corrupted. */
esp_err_t nvs_utils_init(void);

/* Boot-attempt counter — increment at the very start of every boot;
   reset only after BOOT_OK is reached.  Used for crash-loop detection. */
void     nvs_utils_boot_count_increment(void);
void     nvs_utils_boot_count_reset(void);
uint32_t nvs_utils_boot_count_get(void);

/* Last-crash flag — set if previous boot ended before BOOT_OK. */
void nvs_utils_set_crashed(bool crashed);
bool nvs_utils_was_crashed(void);

/* Safe-mode flag — set when safe mode is entered so remote diagnostics
   can know it occurred even after a normal reboot. */
void nvs_utils_set_safe_mode_entered(bool entered);
bool nvs_utils_safe_mode_was_entered(void);

#ifdef __cplusplus
}
#endif
