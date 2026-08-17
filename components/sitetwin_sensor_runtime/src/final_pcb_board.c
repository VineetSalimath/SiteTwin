#include "sitetwin/final_pcb_board.h"

#include <string.h>

#define ST_FINAL_PCB_ID_MIN_ACCEPTED_MV 298U
#define ST_FINAL_PCB_ID_SHT41_UPPER_MV 707U
#define ST_FINAL_PCB_ID_SCD41_UPPER_MV 937U
#define ST_FINAL_PCB_ID_PIR_UPPER_MV 1196U
#define ST_FINAL_PCB_ID_SGP40_UPPER_MV 1493U
#define ST_FINAL_PCB_ID_DS18B20_UPPER_MV 1815U
#define ST_FINAL_PCB_ID_BH1750_UPPER_MV 2125U
#define ST_FINAL_PCB_ID_REED_UPPER_MV 2448U
#define ST_FINAL_PCB_ID_ADXL345_UPPER_MV 2784U
#define ST_FINAL_PCB_ID_INA219_UPPER_MV 3121U
#define ST_FINAL_PCB_ID_EMPTY_UPPER_MV 3450U

static bool valid_port(size_t port_index)
{
    return port_index < ST_FINAL_PCB_PORT_COUNT;
}

st_hal_result_t st_final_pcb_port_select_bits(size_t port_index, bool *sel0,
                                               bool *sel1)
{
    if (!valid_port(port_index) || sel0 == NULL || sel1 == NULL) {
        return ST_HAL_IO_ERROR;
    }

    *sel0 = (port_index & 1U) != 0U;
    *sel1 = (port_index & 2U) != 0U;
    return ST_HAL_OK;
}

st_module_type_t st_final_pcb_classify_id_mv(uint16_t millivolts)
{
    /*
     * Provisional midpoint bands only. The outer guards deliberately leave
     * readings outside the characterised nominal range unclassified.
     */
    if (millivolts < ST_FINAL_PCB_ID_MIN_ACCEPTED_MV) {
        return ST_MODULE_TYPE_UNKNOWN;
    }
    if (millivolts < ST_FINAL_PCB_ID_SHT41_UPPER_MV) {
        return ST_MODULE_TYPE_SHT41;
    }
    if (millivolts < ST_FINAL_PCB_ID_SCD41_UPPER_MV) {
        return ST_MODULE_TYPE_SCD41;
    }
    if (millivolts < ST_FINAL_PCB_ID_PIR_UPPER_MV) {
        return ST_MODULE_TYPE_PIR;
    }
    if (millivolts < ST_FINAL_PCB_ID_SGP40_UPPER_MV) {
        return ST_MODULE_TYPE_SGP40;
    }
    if (millivolts < ST_FINAL_PCB_ID_DS18B20_UPPER_MV) {
        return ST_MODULE_TYPE_DS18B20;
    }
    if (millivolts < ST_FINAL_PCB_ID_BH1750_UPPER_MV) {
        return ST_MODULE_TYPE_BH1750;
    }
    if (millivolts < ST_FINAL_PCB_ID_REED_UPPER_MV) {
        return ST_MODULE_TYPE_REED;
    }
    if (millivolts < ST_FINAL_PCB_ID_ADXL345_UPPER_MV) {
        return ST_MODULE_TYPE_ADXL345;
    }
    if (millivolts < ST_FINAL_PCB_ID_INA219_UPPER_MV) {
        return ST_MODULE_TYPE_INA219;
    }
    if (millivolts <= ST_FINAL_PCB_ID_EMPTY_UPPER_MV) {
        return ST_MODULE_TYPE_EMPTY;
    }
    return ST_MODULE_TYPE_UNKNOWN;
}

bool st_final_pcb_module_uses_i2c(st_module_type_t module_type)
{
    return module_type == ST_MODULE_TYPE_SHT41 ||
           module_type == ST_MODULE_TYPE_SCD41 ||
           module_type == ST_MODULE_TYPE_SGP40 ||
           module_type == ST_MODULE_TYPE_BH1750 ||
           module_type == ST_MODULE_TYPE_ADXL345 ||
           module_type == ST_MODULE_TYPE_INA219;
}

uint8_t st_final_pcb_expected_i2c_address(st_module_type_t module_type)
{
    switch (module_type) {
    case ST_MODULE_TYPE_SHT41:
        return 0x44U;
    case ST_MODULE_TYPE_SCD41:
        return 0x62U;
    case ST_MODULE_TYPE_SGP40:
        return 0x59U;
    case ST_MODULE_TYPE_BH1750:
        return 0x23U;
    case ST_MODULE_TYPE_ADXL345:
        return 0x53U;
    case ST_MODULE_TYPE_INA219:
        return 0x40U;
    default:
        return 0U;
    }
}

static uint16_t median_u16(uint16_t *values, size_t count)
{
    size_t index;

    for (index = 1U; index < count; ++index) {
        uint16_t value = values[index];
        size_t position = index;

        while (position > 0U && values[position - 1U] > value) {
            values[position] = values[position - 1U];
            --position;
        }
        values[position] = value;
    }
    return values[count / 2U];
}

static st_hal_result_t select_mux(st_final_pcb_board_t *board, size_t port_index,
                                  int sel0_gpio, int sel1_gpio)
{
    bool sel0;
    bool sel1;
    st_hal_result_t result;

    result = st_final_pcb_port_select_bits(port_index, &sel0, &sel1);
    if (result != ST_HAL_OK) {
        return result;
    }
    result = board->io.gpio_write(board->io.context, sel0_gpio, sel0);
    if (result != ST_HAL_OK) {
        return result;
    }
    result = board->io.gpio_write(board->io.context, sel1_gpio, sel1);
    if (result != ST_HAL_OK) {
        return result;
    }
    board->io.delay_us(board->io.context, board->config.mux_settle_us);
    return ST_HAL_OK;
}

static size_t final_port_count(void *context)
{
    (void)context;
    return ST_FINAL_PCB_PORT_COUNT;
}

static st_hal_result_t final_read_module_id(void *context, size_t port_index,
                                            st_module_identity_t *identity)
{
    st_final_pcb_board_t *board = (st_final_pcb_board_t *)context;
    uint16_t raw_samples[ST_FINAL_PCB_MAX_ID_SAMPLES];
    uint16_t voltage_samples[ST_FINAL_PCB_MAX_ID_SAMPLES];
    uint8_t index;
    bool calibrated = true;
    st_hal_result_t result;
    st_hal_result_t disable_result;

    if (board == NULL || identity == NULL || !valid_port(port_index)) {
        return ST_HAL_IO_ERROR;
    }
    memset(identity, 0, sizeof(*identity));
    identity->status = ST_MODULE_ID_MEASUREMENT_ERROR;

    result = select_mux(board, port_index, board->config.id_mux_sel0_gpio,
                        board->config.id_mux_sel1_gpio);
    if (result != ST_HAL_OK) {
        return result;
    }

    result = board->io.gpio_write(board->io.context,
                                  board->config.id_mosfet_gate_gpio,
                                  board->config.id_pullup_enable_level);
    if (result != ST_HAL_OK) {
        (void)board->io.gpio_write(board->io.context,
                                   board->config.id_mosfet_gate_gpio,
                                   board->config.id_pullup_disable_level);
        return result;
    }
    board->io.delay_us(board->io.context, board->config.id_settle_us);

    for (index = 0U; index < board->config.id_sample_count; ++index) {
        bool sample_calibrated = false;

        result = board->io.adc_read(board->io.context, board->config.id_adc_gpio,
                                    &raw_samples[index], &voltage_samples[index],
                                    &sample_calibrated);
        calibrated = calibrated && sample_calibrated;
        if (result != ST_HAL_OK) {
            break;
        }
    }

    disable_result = board->io.gpio_write(board->io.context,
                                          board->config.id_mosfet_gate_gpio,
                                          board->config.id_pullup_disable_level);
    if (result != ST_HAL_OK) {
        return result;
    }
    if (disable_result != ST_HAL_OK) {
        return disable_result;
    }

    identity->raw_adc = median_u16(raw_samples, board->config.id_sample_count);
    identity->millivolts = median_u16(voltage_samples,
                                     board->config.id_sample_count);
    identity->sample_count = board->config.id_sample_count;
    identity->voltage_calibrated = calibrated;
    identity->module_type = st_final_pcb_classify_id_mv(identity->millivolts);
    if (identity->module_type == ST_MODULE_TYPE_UNKNOWN) {
        identity->status = ST_MODULE_ID_UNCLASSIFIED;
    } else if (identity->module_type == ST_MODULE_TYPE_EMPTY) {
        identity->status = ST_MODULE_ID_EMPTY;
    } else {
        identity->status = ST_MODULE_ID_PROVISIONAL_MATCH;
    }
    return ST_HAL_OK;
}

static st_hal_result_t final_detect_present(void *context, size_t port_index,
                                            bool *present)
{
    st_module_identity_t identity;
    st_hal_result_t result;

    if (present == NULL) {
        return ST_HAL_IO_ERROR;
    }
    result = final_read_module_id(context, port_index, &identity);
    if (result != ST_HAL_OK) {
        return result;
    }

    /* Comparator polarity is not assumed. An unknown measurement remains
     * present-for-diagnosis rather than being silently treated as empty. */
    *present = identity.status != ST_MODULE_ID_EMPTY;
    return ST_HAL_OK;
}

static st_hal_result_t final_power_set(void *context, size_t port_index,
                                       bool enabled)
{
    (void)context;
    (void)enabled;
    return valid_port(port_index) ? ST_HAL_UNSUPPORTED : ST_HAL_IO_ERROR;
}

static st_hal_result_t final_select_bus(void *context, size_t port_index)
{
    st_final_pcb_board_t *board = (st_final_pcb_board_t *)context;

    if (board == NULL) {
        return ST_HAL_IO_ERROR;
    }
    return select_mux(board, port_index, board->config.data_mux_sel0_gpio,
                      board->config.data_mux_sel1_gpio);
}

static st_hal_result_t final_bus_enable(void *context, size_t port_index)
{
    (void)context;
    return valid_port(port_index) ? ST_HAL_UNSUPPORTED : ST_HAL_IO_ERROR;
}

static st_hal_result_t final_bus_disable(void *context, size_t port_index)
{
    (void)context;
    return valid_port(port_index) ? ST_HAL_UNSUPPORTED : ST_HAL_IO_ERROR;
}

static st_hal_result_t final_clear_fault(void *context, size_t port_index)
{
    return context != NULL && valid_port(port_index) ? ST_HAL_OK : ST_HAL_IO_ERROR;
}

int st_final_pcb_board_init(st_final_pcb_board_t *board,
                            const st_final_pcb_board_config_t *config,
                            const st_final_pcb_io_t *io)
{
    if (board == NULL || config == NULL || io == NULL || io->gpio_write == NULL ||
        io->adc_read == NULL || io->delay_us == NULL ||
        config->id_sample_count == 0U ||
        config->id_sample_count > ST_FINAL_PCB_MAX_ID_SAMPLES ||
        (config->id_sample_count & 1U) == 0U) {
        return -1;
    }

    memset(board, 0, sizeof(*board));
    board->config = *config;
    board->io = *io;
    board->ops.context = board;
    board->ops.port_count = final_port_count;
    board->ops.port_detect_present = final_detect_present;
    board->ops.port_power_set = final_power_set;
    board->ops.port_read_module_id = final_read_module_id;
    board->ops.port_select_bus = final_select_bus;
    board->ops.port_enable_bus = final_bus_enable;
    board->ops.port_disable_bus = final_bus_disable;
    board->ops.port_clear_fault = final_clear_fault;
    return 0;
}

const st_board_port_ops_t *st_final_pcb_board_ops(st_final_pcb_board_t *board)
{
    return board == NULL ? NULL : &board->ops;
}
