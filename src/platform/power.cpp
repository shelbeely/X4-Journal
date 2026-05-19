#include "power.h"
#include <BatteryMonitor.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static const char *TAG = "power";

/* Battery ADC pin (GPIO1 = ADC channel 0 on ESP32-C3) and 2:1 voltage divider */
#define BAT_ADC_PIN      1
#define BAT_DIVIDER      2.0f

/* GPIO used to wake from light sleep (power button) */
#define GPIO_POWER_WAKE 3  /* InputManager::POWER_BUTTON_PIN */

static BatteryMonitor s_bat(BAT_ADC_PIN, BAT_DIVIDER);
static power_state_t  s_state = POWER_STATE_ACTIVE;

esp_err_t power_init(void)
{
    s_state = POWER_STATE_ACTIVE;
    ESP_LOGI(TAG, "power_init ok");
    return ESP_OK;
}

float power_get_battery_voltage(void)
{
    return (float)s_bat.readVolts();
}

uint8_t power_get_battery_pct(void)
{
    return (uint8_t)s_bat.readPercentage();
}

power_state_t power_get_state(void)
{
    return s_state;
}

void power_sleep(void)
{
    s_state = POWER_STATE_SLEEP;
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)GPIO_POWER_WAKE, GPIO_INTR_LOW_LEVEL);
    esp_light_sleep_start();
    /* execution resumes here on wake */
    s_state = POWER_STATE_ACTIVE;
}

void power_off(void)
{
    s_state = POWER_STATE_OFF;
    esp_deep_sleep_start();
}
