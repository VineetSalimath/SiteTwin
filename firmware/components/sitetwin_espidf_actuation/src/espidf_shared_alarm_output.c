#include "sitetwin/espidf_shared_alarm_output.h"

#include <string.h>

#include "driver/ledc.h"

/* Distinct timer/channel numbers from ST_ACTUATION_LEDC_TIMER/CHANNEL
 * (espidf_actuation.c, used by the fixed pod profiles' two-GPIO buzzer)
 * -- the two drivers never coexist in one build today (final_pcb never
 * calls st_espidf_local_output_init), but keeping the numbers distinct
 * costs nothing and avoids a landmine if that ever changes. */
#define ST_SHARED_ALARM_LEDC_MODE LEDC_LOW_SPEED_MODE
#define ST_SHARED_ALARM_LEDC_TIMER LEDC_TIMER_1
#define ST_SHARED_ALARM_LEDC_CHANNEL LEDC_CHANNEL_1

esp_err_t st_espidf_shared_alarm_output_init(st_espidf_shared_alarm_output_t *driver, int gpio,
                                             uint32_t frequency_hz)
{
    ledc_timer_config_t timer_config = {
        .speed_mode = ST_SHARED_ALARM_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = ST_SHARED_ALARM_LEDC_TIMER,
        .freq_hz = frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel_config = {
        .gpio_num = gpio,
        .speed_mode = ST_SHARED_ALARM_LEDC_MODE,
        .channel = ST_SHARED_ALARM_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = ST_SHARED_ALARM_LEDC_TIMER,
        .duty = 0U,
        .hpoint = 0U,
    };
    esp_err_t result;

    if (driver == NULL || gpio < 0 || frequency_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(driver, 0, sizeof(*driver));

    result = ledc_timer_config(&timer_config);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_channel_config(&channel_config);
    if (result != ESP_OK) {
        return result;
    }

    driver->gpio = gpio;
    driver->frequency_hz = frequency_hz;
    driver->initialised = 1U;
    st_shared_alarm_indicator_init(&driver->indicator);
    return ESP_OK;
}

esp_err_t st_espidf_shared_alarm_output_tick(st_espidf_shared_alarm_output_t *driver,
                                             uint8_t desired_active, uint64_t now_ms)
{
    uint8_t duty;
    esp_err_t result;

    if (driver == NULL || driver->initialised == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    duty = st_shared_alarm_indicator_tick(&driver->indicator, desired_active, now_ms);
    if (duty == driver->last_duty) {
        return ESP_OK;
    }
    result = ledc_set_duty(ST_SHARED_ALARM_LEDC_MODE, ST_SHARED_ALARM_LEDC_CHANNEL, duty);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_update_duty(ST_SHARED_ALARM_LEDC_MODE, ST_SHARED_ALARM_LEDC_CHANNEL);
    if (result != ESP_OK) {
        return result;
    }
    driver->last_duty = duty;
    return ESP_OK;
}
