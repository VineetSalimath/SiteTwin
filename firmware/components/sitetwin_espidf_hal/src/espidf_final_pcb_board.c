#include "sitetwin/espidf_final_pcb_board.h"

#include <limits.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#ifdef CONFIG_SITETWIN_FINAL_PCB_PROFILE

static const char *TAG = "final_pcb_board";

enum {
    ST_ADC_CALI_NONE = 0,
    ST_ADC_CALI_CURVE = 1,
    ST_ADC_CALI_LINE = 2
};

static st_hal_result_t map_esp_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK:
        return ST_HAL_OK;
    case ESP_ERR_TIMEOUT:
        return ST_HAL_TIMEOUT;
    case ESP_ERR_NOT_FOUND:
        return ST_HAL_NOT_PRESENT;
    default:
        return ST_HAL_IO_ERROR;
    }
}

static st_hal_result_t final_gpio_write(void *context, int gpio, bool level)
{
    (void)context;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(gpio_set_level((gpio_num_t)gpio, level ? 1U : 0U));
}

static st_hal_result_t final_adc_read(void *context, int gpio, uint16_t *raw_adc,
                                      uint16_t *millivolts,
                                      bool *voltage_calibrated)
{
    st_espidf_final_pcb_board_t *board =
        (st_espidf_final_pcb_board_t *)context;
    int raw;
    int voltage;
    esp_err_t result;

    if (board == NULL || raw_adc == NULL || millivolts == NULL ||
        voltage_calibrated == NULL || !board->initialized ||
        gpio != CONFIG_SITETWIN_FINAL_PCB_ID_ADC_GPIO) {
        return ST_HAL_IO_ERROR;
    }

    result = adc_oneshot_read(board->adc_handle, board->adc_channel, &raw);
    if (result != ESP_OK || raw < 0 || raw > UINT16_MAX) {
        return map_esp_error(result == ESP_OK ? ESP_FAIL : result);
    }

    *raw_adc = (uint16_t)raw;
    if (board->calibration_active) {
        result = adc_cali_raw_to_voltage(board->adc_calibration, raw, &voltage);
        if (result != ESP_OK || voltage < 0 || voltage > UINT16_MAX) {
            return map_esp_error(result == ESP_OK ? ESP_FAIL : result);
        }
        *millivolts = (uint16_t)voltage;
        *voltage_calibrated = true;
    } else {
        uint32_t scaled = (uint32_t)raw *
                              CONFIG_SITETWIN_FINAL_PCB_ADC_FALLBACK_FULL_SCALE_MV +
                          CONFIG_SITETWIN_FINAL_PCB_ADC_FALLBACK_RAW_MAX / 2U;

        *millivolts = (uint16_t)(scaled /
                                 CONFIG_SITETWIN_FINAL_PCB_ADC_FALLBACK_RAW_MAX);
        *voltage_calibrated = false;
    }
    return ST_HAL_OK;
}

static void final_delay_us(void *context, uint32_t delay_us)
{
    (void)context;
    esp_rom_delay_us(delay_us);
}

static esp_err_t configure_gpio(void)
{
    gpio_config_t config;
    uint64_t output_mask;
    uint64_t input_mask;
    esp_err_t result;

    output_mask = (1ULL << CONFIG_SITETWIN_FINAL_PCB_ID_MUX_SEL0_GPIO) |
                  (1ULL << CONFIG_SITETWIN_FINAL_PCB_ID_MUX_SEL1_GPIO) |
                  (1ULL << CONFIG_SITETWIN_FINAL_PCB_DATA_MUX_SEL0_GPIO) |
                  (1ULL << CONFIG_SITETWIN_FINAL_PCB_DATA_MUX_SEL1_GPIO) |
                  (1ULL << CONFIG_SITETWIN_FINAL_PCB_ID_MOSFET_GATE_GPIO);
    memset(&config, 0, sizeof(config));
    config.pin_bit_mask = output_mask;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    result = gpio_config(&config);
    if (result != ESP_OK) {
        return result;
    }

    result = gpio_set_level((gpio_num_t)CONFIG_SITETWIN_FINAL_PCB_ID_MOSFET_GATE_GPIO,
                            1U);
    if (result != ESP_OK) {
        return result;
    }

    input_mask = (1ULL << CONFIG_SITETWIN_FINAL_PCB_HOTSWAP_WAKE_1_GPIO) |
                 (1ULL << CONFIG_SITETWIN_FINAL_PCB_HOTSWAP_WAKE_2_GPIO) |
                 (1ULL << CONFIG_SITETWIN_FINAL_PCB_HOTSWAP_WAKE_3_GPIO) |
                 (1ULL << CONFIG_SITETWIN_FINAL_PCB_HOTSWAP_WAKE_4_GPIO);
    memset(&config, 0, sizeof(config));
    config.pin_bit_mask = input_mask;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&config);
}

static esp_err_t calibration_init(st_espidf_final_pcb_board_t *board)
{
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t curve_config = {
        .unit_id = board->adc_unit,
        .chan = board->adc_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    result = adc_cali_create_scheme_curve_fitting(&curve_config,
                                                   &board->adc_calibration);
    if (result == ESP_OK) {
        board->calibration_active = true;
        board->calibration_scheme = ST_ADC_CALI_CURVE;
        return ESP_OK;
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t line_config = {
        .unit_id = board->adc_unit,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    result = adc_cali_create_scheme_line_fitting(&line_config,
                                                  &board->adc_calibration);
    if (result == ESP_OK) {
        board->calibration_active = true;
        board->calibration_scheme = ST_ADC_CALI_LINE;
        return ESP_OK;
    }
#endif

    board->adc_calibration = NULL;
    board->calibration_active = false;
    board->calibration_scheme = ST_ADC_CALI_NONE;
    return result;
}

static void calibration_deinit(st_espidf_final_pcb_board_t *board)
{
    if (board->adc_calibration == NULL) {
        return;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (board->calibration_scheme == ST_ADC_CALI_CURVE) {
        (void)adc_cali_delete_scheme_curve_fitting(board->adc_calibration);
    }
#endif
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (board->calibration_scheme == ST_ADC_CALI_LINE) {
        (void)adc_cali_delete_scheme_line_fitting(board->adc_calibration);
    }
#endif
    board->adc_calibration = NULL;
    board->calibration_active = false;
    board->calibration_scheme = ST_ADC_CALI_NONE;
}

#endif

esp_err_t st_espidf_final_pcb_board_init(st_espidf_final_pcb_board_t *board)
{
#ifndef CONFIG_SITETWIN_FINAL_PCB_PROFILE
    (void)board;
    return ESP_ERR_NOT_SUPPORTED;
#else
    adc_oneshot_unit_init_cfg_t unit_config;
    adc_oneshot_chan_cfg_t channel_config;
    st_final_pcb_board_config_t portable_config;
    st_final_pcb_io_t io;
    esp_err_t result;

    if (board == NULL || CONFIG_SITETWIN_FINAL_PCB_PORT_COUNT !=
                             ST_FINAL_PCB_PORT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(board, 0, sizeof(*board));

    result = configure_gpio();
    if (result != ESP_OK) {
        return result;
    }
    result = adc_oneshot_io_to_channel(CONFIG_SITETWIN_FINAL_PCB_ID_ADC_GPIO,
                                       &board->adc_unit, &board->adc_channel);
    if (result != ESP_OK) {
        return result;
    }

    memset(&unit_config, 0, sizeof(unit_config));
    unit_config.unit_id = board->adc_unit;
    result = adc_oneshot_new_unit(&unit_config, &board->adc_handle);
    if (result != ESP_OK) {
        return result;
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.atten = ADC_ATTEN_DB_12;
    channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;
    result = adc_oneshot_config_channel(board->adc_handle, board->adc_channel,
                                        &channel_config);
    if (result != ESP_OK) {
        st_espidf_final_pcb_board_deinit(board);
        return result;
    }

    result = calibration_init(board);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "ADC calibration unavailable; raw-derived voltage is diagnostic only");
    }

    memset(&portable_config, 0, sizeof(portable_config));
    portable_config.id_adc_gpio = CONFIG_SITETWIN_FINAL_PCB_ID_ADC_GPIO;
    portable_config.id_mux_sel0_gpio = CONFIG_SITETWIN_FINAL_PCB_ID_MUX_SEL0_GPIO;
    portable_config.id_mux_sel1_gpio = CONFIG_SITETWIN_FINAL_PCB_ID_MUX_SEL1_GPIO;
    portable_config.data_mux_sel0_gpio =
        CONFIG_SITETWIN_FINAL_PCB_DATA_MUX_SEL0_GPIO;
    portable_config.data_mux_sel1_gpio =
        CONFIG_SITETWIN_FINAL_PCB_DATA_MUX_SEL1_GPIO;
    portable_config.id_mosfet_gate_gpio =
        CONFIG_SITETWIN_FINAL_PCB_ID_MOSFET_GATE_GPIO;
    portable_config.id_pullup_enable_level = false;
    portable_config.id_pullup_disable_level = true;
    portable_config.mux_settle_us = CONFIG_SITETWIN_FINAL_PCB_MUX_SETTLE_US;
    portable_config.id_settle_us = CONFIG_SITETWIN_FINAL_PCB_ID_SETTLE_US;
    portable_config.id_sample_count = CONFIG_SITETWIN_FINAL_PCB_ID_SAMPLE_COUNT;

    memset(&io, 0, sizeof(io));
    io.context = board;
    io.gpio_write = final_gpio_write;
    io.adc_read = final_adc_read;
    io.delay_us = final_delay_us;
    board->initialized = true;
    if (st_final_pcb_board_init(&board->portable_board, &portable_config, &io) != 0) {
        st_espidf_final_pcb_board_deinit(board);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG,
             "Final PCB H2 board-port ready; ID bands are provisional and hot-swap wake is disabled");
    return ESP_OK;
#endif
}

void st_espidf_final_pcb_board_deinit(st_espidf_final_pcb_board_t *board)
{
    if (board == NULL) {
        return;
    }
#ifdef CONFIG_SITETWIN_FINAL_PCB_PROFILE
    (void)gpio_set_level((gpio_num_t)CONFIG_SITETWIN_FINAL_PCB_ID_MOSFET_GATE_GPIO,
                         1U);
    calibration_deinit(board);
    if (board->adc_handle != NULL) {
        (void)adc_oneshot_del_unit(board->adc_handle);
    }
#endif
    memset(board, 0, sizeof(*board));
}

const st_board_port_ops_t *st_espidf_final_pcb_board_ops(
    st_espidf_final_pcb_board_t *board)
{
    if (board == NULL || !board->initialized) {
        return NULL;
    }
    return st_final_pcb_board_ops(&board->portable_board);
}
