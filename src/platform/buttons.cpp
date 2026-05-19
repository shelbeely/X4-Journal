#include "buttons.h"
#include <InputManager.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "buttons";

#define HOLD_MS 800U

static InputManager      s_input;
static button_callback_t s_cb      = nullptr;
static void             *s_cb_ctx  = nullptr;
static QueueHandle_t     s_poll_q;  /* used by buttons_poll() — PRESS events only */
static TaskHandle_t      s_task    = nullptr;

/* Maps InputManager button index → button_id_t */
static const button_id_t ID_MAP[] = {
    BTN_BACK,     /* InputManager::BTN_BACK    = 0 */
    BTN_CONFIRM,  /* InputManager::BTN_CONFIRM = 1 */
    BTN_LEFT,     /* InputManager::BTN_LEFT    = 2 */
    BTN_RIGHT,    /* InputManager::BTN_RIGHT   = 3 */
    BTN_VOL_UP,   /* InputManager::BTN_UP      = 4 */
    BTN_VOL_DOWN, /* InputManager::BTN_DOWN    = 5 */
    BTN_POWER,    /* InputManager::BTN_POWER   = 6 */
};
static const int ID_MAP_COUNT = (int)(sizeof(ID_MAP) / sizeof(ID_MAP[0]));

static uint32_t s_hold_start[7]  = {0};
static bool     s_hold_fired[7]  = {false};

static void button_task(void *arg)
{
    s_input.begin();

    while (1) {
        s_input.update();
        uint32_t now = (uint32_t)millis();

        for (int i = 0; i < ID_MAP_COUNT; i++) {
            button_id_t id = ID_MAP[i];

            if (s_input.wasPressed((uint8_t)i)) {
                s_hold_start[i] = now;
                s_hold_fired[i] = false;
                button_event_t ev = { id, BTN_EVENT_PRESS, 0 };
                if (s_cb) s_cb(ev, s_cb_ctx);
                /* Only PRESS events go into the poll queue */
                xQueueSend(s_poll_q, &ev, 0);
            }

            if (s_input.wasReleased((uint8_t)i)) {
                uint32_t dur = now - s_hold_start[i];
                s_hold_fired[i] = false;
                button_event_t ev = { id, BTN_EVENT_RELEASE, dur };
                if (s_cb) s_cb(ev, s_cb_ctx);
                /* RELEASE events are only delivered via callback, not the poll queue */
            }

            /* Hold detection */
            if (s_input.isPressed((uint8_t)i) && !s_hold_fired[i]) {
                uint32_t held = now - s_hold_start[i];
                if (held >= HOLD_MS) {
                    s_hold_fired[i] = true;
                    button_event_t ev = { id, BTN_EVENT_HOLD, held };
                    if (s_cb) s_cb(ev, s_cb_ctx);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t buttons_init(button_callback_t cb, void *ctx)
{
    s_cb     = cb;
    s_cb_ctx = ctx;

    for (int i = 0; i < ID_MAP_COUNT; i++) {
        s_hold_start[i] = 0;
        s_hold_fired[i] = false;
    }

    s_poll_q = xQueueCreate(16, sizeof(button_event_t));
    if (!s_poll_q) return ESP_ERR_NO_MEM;

    xTaskCreate(button_task, "buttons", 4096, NULL, 5, &s_task);
    ESP_LOGI(TAG, "buttons_init ok");
    return ESP_OK;
}

button_id_t buttons_poll(void)
{
    button_event_t ev;
    if (xQueueReceive(s_poll_q, &ev, pdMS_TO_TICKS(50))) {
        return ev.id;
    }
    return BTN_NONE;
}

void buttons_deinit(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = nullptr;
    }
    s_cb     = nullptr;
    s_cb_ctx = nullptr;
    if (s_poll_q) {
        vQueueDelete(s_poll_q);
        s_poll_q = nullptr;
    }
}

