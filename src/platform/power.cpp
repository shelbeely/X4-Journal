#include "power.h"
#include "hardware_pins.h"
#include <BatteryMonitor.h>
#include <InputManager.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static const char *TAG = "power";

static BatteryMonitor s_bat(X4_BAT_ADC_PIN, X4_BAT_DIVIDER);
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
    gpio_wakeup_enable((gpio_num_t)InputManager::POWER_BUTTON_PIN, GPIO_INTR_LOW_LEVEL);
    esp_light_sleep_start();
    /* execution resumes here on wake */
    s_state = POWER_STATE_ACTIVE;
}

void power_off(void)
{
    s_state = POWER_STATE_OFF;
    esp_deep_sleep_start();
}
