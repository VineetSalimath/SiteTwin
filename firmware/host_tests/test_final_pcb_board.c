#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sitetwin/final_pcb_board.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef enum {
    FAKE_EVENT_WRITE = 0,
    FAKE_EVENT_DELAY,
    FAKE_EVENT_ADC
} fake_event_type_t;

typedef struct {
    fake_event_type_t type;
    int gpio;
    uint32_t value;
} fake_event_t;

typedef struct {
    fake_event_t events[32];
    size_t event_count;
    uint16_t raw_samples[ST_FINAL_PCB_MAX_ID_SAMPLES];
    uint16_t voltage_samples[ST_FINAL_PCB_MAX_ID_SAMPLES];
    size_t adc_index;
    size_t fail_adc_at;
    bool calibrated;
} fake_board_io_t;

static void add_event(fake_board_io_t *fake, fake_event_type_t type, int gpio,
                      uint32_t value)
{
    if (fake->event_count < sizeof(fake->events) / sizeof(fake->events[0])) {
        fake->events[fake->event_count].type = type;
        fake->events[fake->event_count].gpio = gpio;
        fake->events[fake->event_count].value = value;
        fake->event_count++;
    }
}

static st_hal_result_t fake_gpio_write(void *context, int gpio, bool level)
{
    fake_board_io_t *fake = (fake_board_io_t *)context;

    add_event(fake, FAKE_EVENT_WRITE, gpio, level ? 1U : 0U);
    return ST_HAL_OK;
}

static st_hal_result_t fake_adc_read(void *context, int gpio, uint16_t *raw_adc,
                                     uint16_t *millivolts,
                                     bool *voltage_calibrated)
{
    fake_board_io_t *fake = (fake_board_io_t *)context;
    size_t index = fake->adc_index++;

    add_event(fake, FAKE_EVENT_ADC, gpio, (uint32_t)index);
    if (index == fake->fail_adc_at) {
        return ST_HAL_IO_ERROR;
    }
    *raw_adc = fake->raw_samples[index];
    *millivolts = fake->voltage_samples[index];
    *voltage_calibrated = fake->calibrated;
    return ST_HAL_OK;
}

static void fake_delay_us(void *context, uint32_t delay_us)
{
    fake_board_io_t *fake = (fake_board_io_t *)context;

    add_event(fake, FAKE_EVENT_DELAY, -1, delay_us);
}

static int init_fixture(st_final_pcb_board_t *board, fake_board_io_t *fake,
                        uint8_t sample_count)
{
    st_final_pcb_board_config_t config = {
        .id_adc_gpio = 0,
        .id_mux_sel0_gpio = 1,
        .id_mux_sel1_gpio = 2,
        .data_mux_sel0_gpio = 21,
        .data_mux_sel1_gpio = 20,
        .id_mosfet_gate_gpio = 18,
        .id_pullup_enable_level = false,
        .id_pullup_disable_level = true,
        .mux_settle_us = 2000U,
        .id_settle_us = 3000U,
        .id_sample_count = sample_count,
    };
    st_final_pcb_io_t io = {
        .context = fake,
        .gpio_write = fake_gpio_write,
        .adc_read = fake_adc_read,
        .delay_us = fake_delay_us,
    };

    memset(board, 0, sizeof(*board));
    fake->fail_adc_at = SIZE_MAX;
    return st_final_pcb_board_init(board, &config, &io);
}

static int test_port_select_mapping(void)
{
    const bool expected_sel0[ST_FINAL_PCB_PORT_COUNT] = {false, true, false, true};
    const bool expected_sel1[ST_FINAL_PCB_PORT_COUNT] = {false, false, true, true};
    size_t port;

    for (port = 0U; port < ST_FINAL_PCB_PORT_COUNT; ++port) {
        bool sel0 = false;
        bool sel1 = false;

        EXPECT(st_final_pcb_port_select_bits(port, &sel0, &sel1) == ST_HAL_OK);
        EXPECT(sel0 == expected_sel0[port]);
        EXPECT(sel1 == expected_sel1[port]);
    }
    EXPECT(st_final_pcb_port_select_bits(ST_FINAL_PCB_PORT_COUNT, NULL, NULL) ==
           ST_HAL_IO_ERROR);
    return 0;
}

static int test_provisional_classifier(void)
{
    EXPECT(st_final_pcb_classify_id_mv(297U) == ST_MODULE_TYPE_UNKNOWN);
    EXPECT(st_final_pcb_classify_id_mv(298U) == ST_MODULE_TYPE_SHT41);
    EXPECT(st_final_pcb_classify_id_mv(706U) == ST_MODULE_TYPE_SHT41);
    EXPECT(st_final_pcb_classify_id_mv(707U) == ST_MODULE_TYPE_SCD41);
    EXPECT(st_final_pcb_classify_id_mv(1055U) == ST_MODULE_TYPE_PIR);
    EXPECT(st_final_pcb_classify_id_mv(1336U) == ST_MODULE_TYPE_SGP40);
    EXPECT(st_final_pcb_classify_id_mv(1650U) == ST_MODULE_TYPE_DS18B20);
    EXPECT(st_final_pcb_classify_id_mv(1980U) == ST_MODULE_TYPE_BH1750);
    EXPECT(st_final_pcb_classify_id_mv(2269U) == ST_MODULE_TYPE_REED);
    EXPECT(st_final_pcb_classify_id_mv(2627U) == ST_MODULE_TYPE_ADXL345);
    EXPECT(st_final_pcb_classify_id_mv(2941U) == ST_MODULE_TYPE_INA219);
    EXPECT(st_final_pcb_classify_id_mv(3300U) == ST_MODULE_TYPE_EMPTY);
    EXPECT(st_final_pcb_classify_id_mv(3451U) == ST_MODULE_TYPE_UNKNOWN);
    EXPECT(st_final_pcb_module_uses_i2c(ST_MODULE_TYPE_SHT41));
    EXPECT(!st_final_pcb_module_uses_i2c(ST_MODULE_TYPE_DS18B20));
    EXPECT(st_final_pcb_expected_i2c_address(ST_MODULE_TYPE_SHT41) == 0x44U);
    EXPECT(st_final_pcb_expected_i2c_address(ST_MODULE_TYPE_DS18B20) == 0U);
    return 0;
}

static int test_id_read_and_pullup_sequence(void)
{
    st_final_pcb_board_t board;
    fake_board_io_t fake;
    const st_board_port_ops_t *ops;
    st_module_identity_t identity;
    const uint16_t raw_samples[5] = {500U, 100U, 300U, 400U, 200U};
    const uint16_t voltage_samples[5] = {1655U, 1648U, 1650U, 1649U, 1652U};

    memset(&fake, 0, sizeof(fake));
    memcpy(fake.raw_samples, raw_samples, sizeof(raw_samples));
    memcpy(fake.voltage_samples, voltage_samples, sizeof(voltage_samples));
    fake.calibrated = true;
    EXPECT(init_fixture(&board, &fake, 5U) == 0);
    ops = st_final_pcb_board_ops(&board);
    EXPECT(ops != NULL);
    EXPECT(ops->port_count(ops->context) == 4U);
    EXPECT(ops->port_read_module_id(ops->context, 2U, &identity) == ST_HAL_OK);
    EXPECT(identity.status == ST_MODULE_ID_PROVISIONAL_MATCH);
    EXPECT(identity.module_type == ST_MODULE_TYPE_DS18B20);
    EXPECT(identity.raw_adc == 300U);
    EXPECT(identity.millivolts == 1650U);
    EXPECT(identity.sample_count == 5U);
    EXPECT(identity.voltage_calibrated);

    EXPECT(fake.event_count == 11U);
    EXPECT(fake.events[0].type == FAKE_EVENT_WRITE && fake.events[0].gpio == 1 &&
           fake.events[0].value == 0U);
    EXPECT(fake.events[1].type == FAKE_EVENT_WRITE && fake.events[1].gpio == 2 &&
           fake.events[1].value == 1U);
    EXPECT(fake.events[2].type == FAKE_EVENT_DELAY && fake.events[2].value == 2000U);
    EXPECT(fake.events[3].type == FAKE_EVENT_WRITE && fake.events[3].gpio == 18 &&
           fake.events[3].value == 0U);
    EXPECT(fake.events[4].type == FAKE_EVENT_DELAY && fake.events[4].value == 3000U);
    EXPECT(fake.events[10].type == FAKE_EVENT_WRITE && fake.events[10].gpio == 18 &&
           fake.events[10].value == 1U);
    return 0;
}

static int test_adc_error_still_disables_pullup(void)
{
    st_final_pcb_board_t board;
    fake_board_io_t fake;
    const st_board_port_ops_t *ops;
    st_module_identity_t identity;

    memset(&fake, 0, sizeof(fake));
    EXPECT(init_fixture(&board, &fake, 5U) == 0);
    fake.fail_adc_at = 2U;
    ops = st_final_pcb_board_ops(&board);
    EXPECT(ops->port_read_module_id(ops->context, 0U, &identity) == ST_HAL_IO_ERROR);
    EXPECT(identity.status == ST_MODULE_ID_MEASUREMENT_ERROR);
    EXPECT(fake.event_count > 0U);
    EXPECT(fake.events[fake.event_count - 1U].type == FAKE_EVENT_WRITE);
    EXPECT(fake.events[fake.event_count - 1U].gpio == 18);
    EXPECT(fake.events[fake.event_count - 1U].value == 1U);
    return 0;
}

static int test_data_mux_and_unsupported_operations(void)
{
    st_final_pcb_board_t board;
    fake_board_io_t fake;
    const st_board_port_ops_t *ops;

    memset(&fake, 0, sizeof(fake));
    EXPECT(init_fixture(&board, &fake, 5U) == 0);
    ops = st_final_pcb_board_ops(&board);
    EXPECT(ops->port_select_bus(ops->context, 3U) == ST_HAL_OK);
    EXPECT(fake.event_count == 3U);
    EXPECT(fake.events[0].gpio == 21 && fake.events[0].value == 1U);
    EXPECT(fake.events[1].gpio == 20 && fake.events[1].value == 1U);
    EXPECT(ops->port_select_bus(ops->context, 4U) == ST_HAL_IO_ERROR);
    EXPECT(ops->port_power_set(ops->context, 0U, true) == ST_HAL_UNSUPPORTED);
    EXPECT(ops->port_enable_bus(ops->context, 0U) == ST_HAL_UNSUPPORTED);
    EXPECT(ops->port_disable_bus(ops->context, 0U) == ST_HAL_UNSUPPORTED);
    EXPECT(ops->port_clear_fault(ops->context, 0U) == ST_HAL_OK);
    EXPECT(ops->port_clear_fault(ops->context, 4U) == ST_HAL_IO_ERROR);
    EXPECT(init_fixture(&board, &fake, 4U) != 0);
    return 0;
}

static int test_unknown_and_empty_presence(void)
{
    st_final_pcb_board_t board;
    fake_board_io_t fake;
    const st_board_port_ops_t *ops;
    st_module_identity_t identity;
    bool present = false;
    size_t index;

    memset(&fake, 0, sizeof(fake));
    for (index = 0U; index < 5U; ++index) {
        fake.raw_samples[index] = 4095U;
        fake.voltage_samples[index] = 3300U;
    }
    EXPECT(init_fixture(&board, &fake, 5U) == 0);
    ops = st_final_pcb_board_ops(&board);
    EXPECT(ops->port_read_module_id(ops->context, 0U, &identity) == ST_HAL_OK);
    EXPECT(identity.status == ST_MODULE_ID_EMPTY);

    fake.event_count = 0U;
    fake.adc_index = 0U;
    EXPECT(ops->port_detect_present(ops->context, 0U, &present) == ST_HAL_OK);
    EXPECT(!present);

    for (index = 0U; index < 5U; ++index) {
        fake.voltage_samples[index] = 100U;
    }
    fake.event_count = 0U;
    fake.adc_index = 0U;
    EXPECT(ops->port_detect_present(ops->context, 0U, &present) == ST_HAL_OK);
    EXPECT(present);
    return 0;
}

int st_run_final_pcb_board_tests(void)
{
    int failures = 0;

    failures += test_port_select_mapping();
    failures += test_provisional_classifier();
    failures += test_id_read_and_pullup_sequence();
    failures += test_adc_error_still_disables_pullup();
    failures += test_data_mux_and_unsupported_operations();
    failures += test_unknown_and_empty_presence();
    return failures;
}
