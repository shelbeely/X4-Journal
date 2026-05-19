#include "power.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static const char *TAG = "power";

#define BAT_ADC_CHANNEL  ADC1_CHANNEL_0   /* GPIO1 */
#define BAT_ADC_ATTEN    ADC_ATTEN_DB_11
#define BAT_VREF_MV      1100
#define BAT_DIVIDER      2                /* voltage divider 2:1 */
#define BAT_MV_MIN       3300
#define BAT_MV_MAX       4200

#define GPIO_POWER_WAKE  11

static power_state_t  s_state = POWER_STATE_ACTIVE;
static esp_adc_cal_characteristics_t s_adc_chars;

esp_err_t power_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BAT_ADC_CHANNEL, BAT_ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_1, BAT_ADC_ATTEN, ADC_WIDTH_BIT_12,
                             BAT_VREF_MV, &s_adc_chars);
    s_state = POWER_STATE_ACTIVE;
    ESP_LOGI(TAG, "power_init ok");
    return ESP_OK;
}

float power_get_battery_voltage(void)
{
    uint32_t raw = adc1_get_raw(BAT_ADC_CHANNEL);
    uint32_t mv  = esp_adc_cal_raw_to_voltage(raw, &s_adc_chars);
    return (float)(mv * BAT_DIVIDER) / 1000.0f;
}

uint8_t power_get_battery_pct(void)
{
    float v  = power_get_battery_voltage();
    float mv = v * 1000.0f;
    if (mv <= BAT_MV_MIN) return 0;
    if (mv >= BAT_MV_MAX) return 100;
    return (uint8_t)(((mv - BAT_MV_MIN) * 100.0f) / (BAT_MV_MAX - BAT_MV_MIN));
}

power_state_t power_get_state(void)
{
    return s_state;
}

void power_sleep(void)
{
    s_state = POWER_STATE_SLEEP;
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable(GPIO_POWER_WAKE, GPIO_INTR_LOW_LEVEL);
    esp_light_sleep_start();
    /* execution resumes here on wake */
    s_state = POWER_STATE_ACTIVE;
}

void power_off(void)
{
    s_state = POWER_STATE_OFF;
    esp_deep_sleep_start();
}
