#include "sitetwin/espidf_actuation.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"

#define ST_ACTUATION_LEDC_MODE LEDC_LOW_SPEED_MODE
#define ST_ACTUATION_LEDC_TIMER LEDC_TIMER_0
#define ST_ACTUATION_LEDC_CHANNEL LEDC_CHANNEL_0
/* 10-bit resolution (0-1023); 512 is ~50% duty, a moderate volume starting
 * point -- not yet tuned against the real buzzer's acceptable drive level. */
#define ST_ACTUATION_LEDC_DUTY 512U

esp_err_t st_espidf_local_output_init(st_espidf_local_output_t *driver, int led_gpio,
                                       int buzzer_gpio, uint32_t default_buzzer_frequency_hz)
{
    gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << (uint32_t)led_gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ledc_timer_config_t timer_config = {
        .speed_mode = ST_ACTUATION_LEDC_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = ST_ACTUATION_LEDC_TIMER,
        .freq_hz = default_buzzer_frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_channel_config_t channel_config = {
        .gpio_num = buzzer_gpio,
        .speed_mode = ST_ACTUATION_LEDC_MODE,
        .channel = ST_ACTUATION_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = ST_ACTUATION_LEDC_TIMER,
        .duty = 0U,
        .hpoint = 0U,
    };
    esp_err_t result;

    if (driver == NULL || led_gpio < 0 || buzzer_gpio < 0 || default_buzzer_frequency_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(driver, 0, sizeof(*driver));

    result = gpio_config(&led_config);
    if (result != ESP_OK) {
        return result;
    }
    result = gpio_set_level((gpio_num_t)led_gpio, 0U);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_timer_config(&timer_config);
    if (result != ESP_OK) {
        return result;
    }
    result = ledc_channel_config(&channel_config);
    if (result != ESP_OK) {
        return result;
    }

    driver->led_gpio = led_gpio;
    driver->buzzer_gpio = buzzer_gpio;
    driver->buzzer_frequency_hz = default_buzzer_frequency_hz;
    driver->initialised = 1U;
    return ESP_OK;
}

static int local_output_submit(void *context, const st_local_output_request_t *request)
{
    st_espidf_local_output_t *driver = (st_espidf_local_output_t *)context;
    esp_err_t result;

    if (driver == NULL || driver->initialised == 0U || request == NULL) {
        return -1;
    }

    switch (request->kind) {
    case ST_LOCAL_OUTPUT_LED:
        if (request->active != driver->led_active) {
            result = gpio_set_level((gpio_num_t)driver->led_gpio, request->active != 0U);
            if (result != ESP_OK) {
                return -1;
            }
            driver->led_active = request->active;
        }
        return 0;

    case ST_LOCAL_OUTPUT_BUZZER: {
        uint32_t freq = request->frequency_hz != 0U ? request->frequency_hz
                                                      : driver->buzzer_frequency_hz;
        if (request->active != driver->buzzer_active || freq != driver->buzzer_frequency_hz) {
            if (freq != driver->buzzer_frequency_hz) {
                result = ledc_set_freq(ST_ACTUATION_LEDC_MODE, ST_ACTUATION_LEDC_TIMER, freq);
                if (result != ESP_OK) {
                    return -1;
                }
                driver->buzzer_frequency_hz = freq;
            }
            result = ledc_set_duty(ST_ACTUATION_LEDC_MODE, ST_ACTUATION_LEDC_CHANNEL,
                                    request->active != 0U ? ST_ACTUATION_LEDC_DUTY : 0U);
            if (result != ESP_OK) {
                return -1;
            }
            result = ledc_update_duty(ST_ACTUATION_LEDC_MODE, ST_ACTUATION_LEDC_CHANNEL);
            if (result != ESP_OK) {
                return -1;
            }
            driver->buzzer_active = request->active;
        }
        return 0;
    }

    default:
        return -1;
    }
}

st_local_output_service_t st_espidf_local_output_service(st_espidf_local_output_t *driver)
{
    st_local_output_service_t service = {
        .context = driver,
        .submit = local_output_submit,
    };
    return service;
}