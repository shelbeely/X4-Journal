#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    POWER_STATE_ACTIVE,
    POWER_STATE_SLEEP,
    POWER_STATE_OFF,
} power_state_t;

esp_err_t    power_init(void);
void         power_sleep(void);
void         power_off(void);
uint8_t      power_get_battery_pct(void);
float        power_get_battery_voltage(void);
power_state_t power_get_state(void);

#ifdef __cplusplus
}
#endif
