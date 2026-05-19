#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Safe-mode GPIO and hold time.  Defaults to X4_POWER_BTN_PIN (GPIO3). */
#ifndef CONFIG_SAFE_MODE_GPIO
#define CONFIG_SAFE_MODE_GPIO  3   /* GPIO3 = POWER button (active LOW, pullup) */
#endif

#ifndef CONFIG_SAFE_MODE_HOLD_MS
#define CONFIG_SAFE_MODE_HOLD_MS  3000
#endif

/* Call as the very first thing in setup(), before any other init.
   Reads GPIO state; if the safe-mode button is held for CONFIG_SAFE_MODE_HOLD_MS,
   sets the internal flag and writes the NVS flag. */
void safe_mode_check_entry(void);

/* Returns true if safe mode was triggered this boot. */
bool safe_mode_is_active(void);

/* Run the minimal safe-mode boot path.  Blocks in an idle loop.
   Should be called from setup() immediately after safe_mode_check_entry()
   when safe_mode_is_active() is true. */
void safe_mode_run(void);

#ifdef __cplusplus
}
#endif
