#include "sitetwin/espidf_actuation.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "nvs.h"

#define ST_ACTUATION_LEDC_MODE LEDC_LOW_SPEED_MODE
#define ST_ACTUATION_LEDC_TIMER LEDC_TIMER_0
#define ST_ACTUATION_LEDC_CHANNEL LEDC_CHANNEL_0
#define ST_ACTUATION_LEDC_DUTY 512U
#define ST_COMMAND_NVS_NAMESPACE "st_command"
#define ST_COMMAND_NVS_KEY "state"

static int command_state_load(void *context, st_command_persistent_state_t *state)
{
    nvs_handle_t handle;
    size_t size = sizeof(*state);
    esp_err_t result;
    (void)context;
    result = nvs_open(ST_COMMAND_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result != ESP_OK) {
        return -1;
    }
    result = nvs_get_blob(handle, ST_COMMAND_NVS_KEY, state, &size);
    nvs_close(handle);
    return result == ESP_OK && size == sizeof(*state) ? 0 : -1;
}

static int command_state_save(void *context, const st_command_persistent_state_t *state)
{
    nvs_handle_t handle;
    esp_err_t result;
    (void)context;
    result = nvs_open(ST_COMMAND_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result != ESP_OK) {
        return -1;
    }
    result = nvs_set_blob(handle, ST_COMMAND_NVS_KEY, state, sizeof(*state));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result == ESP_OK ? 0 : -1;
}

st_command_persistence_t st_espidf_command_persistence(void)
{
    st_command_persistence_t persistence = {
        .context = NULL,
        .load = command_state_load,
        .save = command_state_save,
    };
    return persistence;
}

esp_err_t st_espidf_actuation_init(st_espidf_actuation_t *service, int led_gpio,
                                  int buzzer_gpio, uint32_t buzzer_frequency_hz)
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
        .freq_hz = buzzer_frequency_hz,
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

    if (service == NULL || led_gpio < 0 || buzzer_gpio < 0 || buzzer_frequency_hz == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(service, 0, sizeof(*service));
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
    service->led_gpio = led_gpio;
    service->buzzer_gpio = buzzer_gpio;
    service->buzzer_frequency_hz = buzzer_frequency_hz;
    service->initialised = 1U;
    return ESP_OK;
}

esp_err_t st_espidf_actuation_apply(st_espidf_actuation_t *service,
                                   st_local_actuation_state_t requested)
{
    esp_err_t result;
    if (service == NULL || service->initialised == 0U) {
        return ESP_ERR_INVALID_STATE;
    }
    if (requested.led_active != service->applied.led_active) {
        result = gpio_set_level((gpio_num_t)service->led_gpio, requested.led_active != 0U);
        if (result != ESP_OK) {
            return result;
        }
    }
    if (requested.buzzer_active != service->applied.buzzer_active) {
        result = ledc_set_duty(ST_ACTUATION_LEDC_MODE, ST_ACTUATION_LEDC_CHANNEL,
                               requested.buzzer_active != 0U ? ST_ACTUATION_LEDC_DUTY : 0U);
        if (result != ESP_OK) {
            return result;
        }
        result = ledc_update_duty(ST_ACTUATION_LEDC_MODE, ST_ACTUATION_LEDC_CHANNEL);
        if (result != ESP_OK) {
            return result;
        }
    }
    service->applied = requested;
    return ESP_OK;
}
