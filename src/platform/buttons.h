#pragma once
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_LEFT,
    BTN_RIGHT,
    BTN_CONFIRM,
    BTN_BACK,
    BTN_VOL_UP,
    BTN_VOL_DOWN,
    BTN_POWER,
    BTN_NONE,
} button_id_t;

typedef enum {
    BTN_EVENT_PRESS,
    BTN_EVENT_RELEASE,
    BTN_EVENT_HOLD,
} button_event_type_t;

typedef struct {
    button_id_t        id;
    button_event_type_t type;
    uint32_t           duration_ms;
} button_event_t;

typedef void (*button_callback_t)(button_event_t event, void *ctx);

esp_err_t  buttons_init(button_callback_t cb, void *ctx);
button_id_t buttons_poll(void);   /* blocking poll, 50 ms debounce */
void        buttons_deinit(void);
bool        buttons_is_init(void);
button_id_t buttons_last_event_id(void); /* returns BTN_NONE if no event yet */

#ifdef __cplusplus
}
#endif
