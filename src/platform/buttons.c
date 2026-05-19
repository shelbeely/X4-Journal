#include "buttons.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "buttons";

/* GPIO assignments */
#define GPIO_LEFT     18
#define GPIO_RIGHT    19
#define GPIO_CONFIRM  20
#define GPIO_BACK     21
#define GPIO_VOL_UP    9
#define GPIO_VOL_DOWN 10
#define GPIO_POWER    11

#define DEBOUNCE_MS  50
#define HOLD_MS     800

static const struct { int gpio; button_id_t id; } BTN_MAP[] = {
    { GPIO_LEFT,     BTN_LEFT     },
    { GPIO_RIGHT,    BTN_RIGHT    },
    { GPIO_CONFIRM,  BTN_CONFIRM  },
    { GPIO_BACK,     BTN_BACK     },
    { GPIO_VOL_UP,   BTN_VOL_UP   },
    { GPIO_VOL_DOWN, BTN_VOL_DOWN },
    { GPIO_POWER,    BTN_POWER    },
};
#define BTN_COUNT (sizeof(BTN_MAP) / sizeof(BTN_MAP[0]))

static QueueHandle_t     s_gpio_evt_queue;
static button_callback_t s_cb;
static void             *s_cb_ctx;

typedef struct {
    int          gpio;
    uint32_t     press_tick;
    bool         pressed;
    bool         hold_fired;
} btn_state_t;

static btn_state_t s_state[BTN_COUNT];

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int gpio = (int)(intptr_t)arg;
    xQueueSendFromISR(s_gpio_evt_queue, &gpio, NULL);
}

static button_id_t gpio_to_id(int gpio)
{
    for (size_t i = 0; i < BTN_COUNT; i++) {
        if (BTN_MAP[i].gpio == gpio) return BTN_MAP[i].id;
    }
    return BTN_NONE;
}

static btn_state_t *gpio_to_state(int gpio)
{
    for (size_t i = 0; i < BTN_COUNT; i++) {
        if (BTN_MAP[i].gpio == gpio) return &s_state[i];
    }
    return NULL;
}

static void button_task(void *arg)
{
    int gpio;
    while (1) {
        /* poll for ISR notifications */
        if (xQueueReceive(s_gpio_evt_queue, &gpio, pdMS_TO_TICKS(20))) {
            vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
            int level = gpio_get_level(gpio);
            btn_state_t *st = gpio_to_state(gpio);
            if (!st) continue;
            button_id_t id = gpio_to_id(gpio);

            if (level == 0 && !st->pressed) {
                /* active-low: button down */
                st->pressed    = true;
                st->hold_fired = false;
                st->press_tick = xTaskGetTickCount();
                if (s_cb) {
                    button_event_t ev = { .id = id, .type = BTN_EVENT_PRESS, .duration_ms = 0 };
                    s_cb(ev, s_cb_ctx);
                }
            } else if (level == 1 && st->pressed) {
                /* button up */
                uint32_t dur = pdTICKS_TO_MS(xTaskGetTickCount() - st->press_tick);
                st->pressed = false;
                if (!st->hold_fired && s_cb) {
                    button_event_t ev = { .id = id, .type = BTN_EVENT_RELEASE, .duration_ms = dur };
                    s_cb(ev, s_cb_ctx);
                }
            }
        }

        /* check for hold events */
        uint32_t now = xTaskGetTickCount();
        for (size_t i = 0; i < BTN_COUNT; i++) {
            if (s_state[i].pressed && !s_state[i].hold_fired) {
                uint32_t held = pdTICKS_TO_MS(now - s_state[i].press_tick);
                if (held >= HOLD_MS) {
                    s_state[i].hold_fired = true;
                    if (s_cb) {
                        button_event_t ev = {
                            .id = BTN_MAP[i].id,
                            .type = BTN_EVENT_HOLD,
                            .duration_ms = held,
                        };
                        s_cb(ev, s_cb_ctx);
                    }
                }
            }
        }
    }
}

esp_err_t buttons_init(button_callback_t cb, void *ctx)
{
    s_cb     = cb;
    s_cb_ctx = ctx;
    memset(s_state, 0, sizeof(s_state));

    s_gpio_evt_queue = xQueueCreate(32, sizeof(int));
    if (!s_gpio_evt_queue) return ESP_ERR_NO_MEM;

    gpio_install_isr_service(0);

    for (size_t i = 0; i < BTN_COUNT; i++) {
        int g = BTN_MAP[i].gpio;
        gpio_config_t cfg = {
            .pin_bit_mask = (1ULL << g),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_ANYEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        gpio_isr_handler_add(g, gpio_isr_handler, (void *)(intptr_t)g);
    }

    xTaskCreate(button_task, "buttons", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "buttons_init ok");
    return ESP_OK;
}

button_id_t buttons_poll(void)
{
    int gpio;
    if (xQueueReceive(s_gpio_evt_queue, &gpio, pdMS_TO_TICKS(50))) {
        vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
        if (gpio_get_level(gpio) == 0) {
            return gpio_to_id(gpio);
        }
    }
    return BTN_NONE;
}

void buttons_deinit(void)
{
    for (size_t i = 0; i < BTN_COUNT; i++) {
        gpio_isr_handler_remove(BTN_MAP[i].gpio);
        gpio_reset_pin(BTN_MAP[i].gpio);
    }
    gpio_uninstall_isr_service();
    if (s_gpio_evt_queue) {
        vQueueDelete(s_gpio_evt_queue);
        s_gpio_evt_queue = NULL;
    }
}
