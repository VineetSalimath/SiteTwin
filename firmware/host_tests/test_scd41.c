#include <stdio.h>
#include <string.h>

#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"
#include "sitetwin/scd41.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define TEST_SCD41_START_PERIODIC 0x21B1U
#define TEST_SCD41_START_LOW_POWER 0x21ACU
#define TEST_SCD41_READ_MEASUREMENT 0xEC05U
#define TEST_SCD41_STOP_PERIODIC 0x3F86U
#define TEST_SCD41_GET_DATA_READY 0xE4B8U
#define TEST_SCD41_GET_SERIAL 0x3682U

typedef struct {
    uint8_t address;
    uint8_t present;
    uint8_t periodic_running;
    uint8_t data_ready;
    uint8_t corrupt_status_crc;
    uint16_t last_command;
    uint8_t serial_response[9];
    uint8_t measurement_response[9];
    st_hal_result_t next_read_result;
    st_hal_result_t next_measurement_command_result;
    uint32_t serial_commands;
    uint32_t stop_commands;
    uint32_t standard_start_commands;
    uint32_t low_power_start_commands;
    uint32_t status_commands;
    uint32_t measurement_commands;
    uint32_t measurement_reads;
} fake_scd41_bus_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_scd41_t sensor;
    st_module_instance_t module;
} scd41_fixture_t;

static uint16_t decode_command(const uint8_t data[2])
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static void encode_word(uint16_t value, uint8_t output[3])
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
    output[2] = st_scd41_crc8(output);
}

static void set_serial_response(fake_scd41_bus_t *bus,
                                uint16_t first, uint16_t second, uint16_t third)
{
    encode_word(first, &bus->serial_response[0]);
    encode_word(second, &bus->serial_response[3]);
    encode_word(third, &bus->serial_response[6]);
}

static void set_measurement_response(fake_scd41_bus_t *bus, uint16_t co2_ppm)
{
    encode_word(co2_ppm, &bus->measurement_response[0]);
    encode_word(0x6667U, &bus->measurement_response[3]);
    encode_word(0x5EB9U, &bus->measurement_response[6]);
}

static st_hal_result_t fake_write(void *context, uint8_t address,
                                  const uint8_t *data, size_t length)
{
    fake_scd41_bus_t *bus = (fake_scd41_bus_t *)context;
    uint16_t command;

    if (bus == NULL || data == NULL || length != 2U || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }

    command = decode_command(data);
    switch (command) {
    case TEST_SCD41_GET_SERIAL:
        if (bus->periodic_running != 0U) {
            return ST_HAL_IO_ERROR;
        }
        ++bus->serial_commands;
        break;
    case TEST_SCD41_STOP_PERIODIC:
        if (bus->periodic_running == 0U) {
            return ST_HAL_IO_ERROR;
        }
        bus->periodic_running = 0U;
        ++bus->stop_commands;
        break;
    case TEST_SCD41_START_PERIODIC:
        if (bus->periodic_running != 0U) {
            return ST_HAL_IO_ERROR;
        }
        bus->periodic_running = 1U;
        ++bus->standard_start_commands;
        break;
    case TEST_SCD41_START_LOW_POWER:
        if (bus->periodic_running != 0U) {
            return ST_HAL_IO_ERROR;
        }
        bus->periodic_running = 1U;
        ++bus->low_power_start_commands;
        break;
    case TEST_SCD41_GET_DATA_READY:
        if (bus->periodic_running == 0U) {
            return ST_HAL_IO_ERROR;
        }
        ++bus->status_commands;
        break;
    case TEST_SCD41_READ_MEASUREMENT:
        if (bus->periodic_running == 0U || bus->data_ready == 0U) {
            return ST_HAL_IO_ERROR;
        }
        if (bus->next_measurement_command_result != ST_HAL_OK) {
            st_hal_result_t result = bus->next_measurement_command_result;

            bus->next_measurement_command_result = ST_HAL_OK;
            return result;
        }
        ++bus->measurement_commands;
        break;
    default:
        return ST_HAL_UNSUPPORTED;
    }

    bus->last_command = command;
    return ST_HAL_OK;
}

static st_hal_result_t fake_read(void *context, uint8_t address,
                                 uint8_t *data, size_t length)
{
    fake_scd41_bus_t *bus = (fake_scd41_bus_t *)context;
    st_hal_result_t result;

    if (bus == NULL || data == NULL || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }

    result = bus->next_read_result;
    bus->next_read_result = ST_HAL_OK;
    if (result != ST_HAL_OK) {
        return result;
    }

    if (bus->last_command == TEST_SCD41_GET_SERIAL && length == 9U) {
        memcpy(data, bus->serial_response, length);
        return ST_HAL_OK;
    }
    if (bus->last_command == TEST_SCD41_GET_DATA_READY && length == 3U) {
        encode_word(bus->data_ready != 0U ? 0x0001U : 0x8000U, data);
        if (bus->corrupt_status_crc != 0U) {
            data[2] ^= 0x01U;
            bus->corrupt_status_crc = 0U;
        }
        return ST_HAL_OK;
    }
    if (bus->last_command == TEST_SCD41_READ_MEASUREMENT && length == 9U) {
        memcpy(data, bus->measurement_response, length);
        bus->data_ready = 0U;
        ++bus->measurement_reads;
        return ST_HAL_OK;
    }
    return ST_HAL_IO_ERROR;
}

static void fake_bus_init(fake_scd41_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->address = ST_SCD41_DEFAULT_ADDRESS;
    bus->present = 1U;
    set_serial_response(bus, 0xF896U, 0x9F07U, 0x3BBEU);
    set_measurement_response(bus, 500U);
}

static int fixture_init(scd41_fixture_t *fixture, fake_scd41_bus_t *bus,
                        st_scd41_measurement_mode_t mode, uint32_t poll_interval_ms)
{
    static const uint8_t slots[ST_SCD41_CHANNEL_COUNT] = {2U};
    st_scd41_config_t config;
    st_i2c_bus_t i2c_bus;

    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    i2c_bus.context = bus;
    i2c_bus.write = fake_write;
    i2c_bus.read = fake_read;
    config.bus = i2c_bus;
    config.address = bus->address;
    config.measurement_mode = mode;
    config.poll_interval_ms = poll_interval_ms;
    config.co2_sensor_id = "scd41_co2";
    st_pod_runtime_init(&fixture->runtime, ST_POD_ENVIRONMENT, "ENV_01", 77U);
    if (st_scd41_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_scd41_module_driver(&fixture->sensor),
                                ST_SCD41_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module, &fixture->runtime.registry,
                                     slots, ST_SCD41_CHANNEL_COUNT);
}

static int pop_co2(st_pod_runtime_t *runtime, st_telemetry_record_t *record)
{
    if (st_pod_runtime_next_telemetry(runtime, record) != 0) {
        return -1;
    }
    return record->reading.sensor_kind == ST_SENSOR_CO2_PPM &&
                   record->reading.unit == ST_UNIT_PPM
               ? 0
               : -1;
}

static void complete_initial_probe(scd41_fixture_t *fixture)
{
    st_pod_runtime_tick(&fixture->runtime, 0U);
    st_pod_runtime_tick(&fixture->runtime, 1000U);
}

static void complete_measurement(scd41_fixture_t *fixture, uint64_t start_ms)
{
    st_pod_runtime_tick(&fixture->runtime, start_ms);
    st_pod_runtime_tick(&fixture->runtime, start_ms + 100U);
    st_pod_runtime_tick(&fixture->runtime, start_ms + 200U);
}

static int test_protocol_and_operating_modes(void)
{
    const uint8_t crc_example[2] = {0xBEU, 0xEFU};
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;

    EXPECT(st_scd41_crc8(crc_example) == 0x92U);
    EXPECT(st_scd41_raw_co2_ppm(500U) == 500.0F);

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_PERIODIC, 200U) == 0);
    complete_initial_probe(&fixture);
    EXPECT(bus.standard_start_commands == 1U);
    EXPECT(bus.low_power_start_commands == 0U);
    EXPECT(st_scd41_start_command_count(&fixture.sensor) == 1U);
    return 0;
}

static int test_startup_not_ready_and_first_valid(void)
{
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_LOW_POWER_PERIODIC, 200U) == 0);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_UNINITIALISED);

    st_pod_runtime_tick(&fixture.runtime, 0U);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_PROBING);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    st_pod_runtime_tick(&fixture.runtime, 1000U);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_WARMING_UP);
    EXPECT(bus.low_power_start_commands == 1U);
    EXPECT(fixture.runtime.registry.ports[2].state == ST_PORT_WARMING_UP);

    st_pod_runtime_tick(&fixture.runtime, 1001U);
    EXPECT(bus.status_commands == 1U);
    bus.next_read_result = ST_HAL_BUSY;
    st_pod_runtime_tick(&fixture.runtime, 1101U);
    EXPECT(fixture.runtime.registry.ports[2].state == ST_PORT_WARMING_UP);
    st_pod_runtime_tick(&fixture.runtime, 1201U);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_WARMING_UP);
    EXPECT(bus.measurement_commands == 0U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    bus.data_ready = 1U;
    bus.next_measurement_command_result = ST_HAL_BUSY;
    st_pod_runtime_tick(&fixture.runtime, 1401U);
    st_pod_runtime_tick(&fixture.runtime, 1501U);
    EXPECT(bus.measurement_commands == 0U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    st_pod_runtime_tick(&fixture.runtime, 1701U);
    st_pod_runtime_tick(&fixture.runtime, 1801U);
    st_pod_runtime_tick(&fixture.runtime, 1901U);
    EXPECT(bus.measurement_commands == 1U);
    EXPECT(bus.measurement_reads == 1U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 500.0F);
    EXPECT(record.reading.uptime_ms == 1901U);
    EXPECT(record.reading.sequence == 1U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_WARMING_UP) == 0U);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_READY);
    return 0;
}

static int test_first_failure_then_recovery(void)
{
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    bus.corrupt_status_crc = 1U;
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_LOW_POWER_PERIODIC, 200U) == 0);
    complete_initial_probe(&fixture);
    bus.data_ready = 1U;
    complete_measurement(&fixture, 1001U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    EXPECT(fixture.runtime.registry.ports[2].state == ST_PORT_FAULTED);

    set_measurement_response(&bus, 650U);
    bus.data_ready = 1U;
    st_pod_runtime_tick(&fixture.runtime, 2201U);
    EXPECT(bus.stop_commands == 1U);
    st_pod_runtime_tick(&fixture.runtime, 3201U);
    st_pod_runtime_tick(&fixture.runtime, 4201U);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_WARMING_UP);
    complete_measurement(&fixture, 4202U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 650.0F);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT(record.reading.sequence == 1U);
    return 0;
}

static int test_stale_crc_quality_transition_and_recovery(void)
{
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_LOW_POWER_PERIODIC, 200U) == 0);
    complete_initial_probe(&fixture);
    bus.data_ready = 1U;
    complete_measurement(&fixture, 1001U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.uptime_ms == 1201U);

    bus.measurement_response[8] ^= 0x01U;
    bus.data_ready = 1U;
    complete_measurement(&fixture, 1401U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 500.0F);
    EXPECT(record.reading.uptime_ms == 1201U);
    EXPECT(st_scd41_last_attempt_at_ms(&fixture.sensor) == 1601U);
    EXPECT((record.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED)) ==
           (ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED));
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) == 0U);

    st_pod_runtime_tick(&fixture.runtime, 1801U);
    EXPECT(fixture.runtime.registry.ports[2].state == ST_PORT_FAULTED);
    set_measurement_response(&bus, 700U);
    bus.data_ready = 1U;
    st_pod_runtime_tick(&fixture.runtime, 2801U);
    st_pod_runtime_tick(&fixture.runtime, 3801U);
    st_pod_runtime_tick(&fixture.runtime, 4801U);
    complete_measurement(&fixture, 4802U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 700.0F);
    EXPECT(record.reading.uptime_ms == 5002U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((record.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED)) == 0U);
    EXPECT(fixture.runtime.reporting.readings_reported == 3U);
    return 0;
}

static int test_missing_sensor_and_recovery(void)
{
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_LOW_POWER_PERIODIC, 200U) == 0);
    complete_initial_probe(&fixture);
    bus.data_ready = 1U;
    complete_measurement(&fixture, 1001U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);

    bus.present = 0U;
    st_pod_runtime_tick(&fixture.runtime, 1401U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 500.0F);
    EXPECT(record.reading.uptime_ms == 1201U);
    EXPECT((record.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING)) ==
           (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING));
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) == 0U);

    st_pod_runtime_tick(&fixture.runtime, 1601U);
    EXPECT(fixture.runtime.registry.ports[2].state == ST_PORT_PROBING);
    bus.present = 1U;
    bus.periodic_running = 0U;
    set_measurement_response(&bus, 800U);
    bus.data_ready = 1U;
    st_pod_runtime_tick(&fixture.runtime, 2601U);
    st_pod_runtime_tick(&fixture.runtime, 3601U);
    complete_measurement(&fixture, 3602U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 800.0F);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_SENSOR_MISSING) == 0U);
    return 0;
}

static int test_out_of_range_stale(void)
{
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_LOW_POWER_PERIODIC, 200U) == 0);
    complete_initial_probe(&fixture);
    bus.data_ready = 1U;
    complete_measurement(&fixture, 1001U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);

    set_measurement_response(&bus, 0U);
    bus.data_ready = 1U;
    complete_measurement(&fixture, 1401U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value == 500.0F);
    EXPECT((record.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_OUT_OF_RANGE)) ==
           (ST_QUALITY_STALE | ST_QUALITY_OUT_OF_RANGE));
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    return 0;
}

static int test_detach_reattach_lifetime(void)
{
    static const uint8_t slots[ST_SCD41_CHANNEL_COUNT] = {2U};
    fake_scd41_bus_t bus;
    scd41_fixture_t fixture;
    st_sensor_driver_t stale_driver;
    st_driver_sample_t sample;
    st_telemetry_record_t record;
    size_t cycle;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, ST_SCD41_MODE_LOW_POWER_PERIODIC, 200U) == 0);
    stale_driver = fixture.runtime.registry.ports[2].driver;
    st_module_instance_detach(&fixture.module, &fixture.runtime.registry, 10U);
    EXPECT(fixture.runtime.registry.ports[2].attached == 0U);
    EXPECT(st_scd41_state(&fixture.sensor) == ST_SCD41_DETACHED);
    EXPECT(stale_driver.sample(stale_driver.context, 11U, &sample) == ST_DRIVER_NOT_PRESENT);

    for (cycle = 0U; cycle < 32U; ++cycle) {
        EXPECT(st_module_instance_attach(&fixture.module, &fixture.runtime.registry,
                                         slots, ST_SCD41_CHANNEL_COUNT) == 0);
        EXPECT(fixture.runtime.registry.ports[2].driver.context ==
               &fixture.module.adapters[0]);
        st_module_instance_detach(&fixture.module, &fixture.runtime.registry,
                                  (uint64_t)cycle + 20U);
        EXPECT(fixture.runtime.registry.ports[2].attached == 0U);
    }

    EXPECT(st_module_instance_attach(&fixture.module, &fixture.runtime.registry,
                                     slots, ST_SCD41_CHANNEL_COUNT) == 0);
    st_pod_runtime_tick(&fixture.runtime, 1000U);
    st_pod_runtime_tick(&fixture.runtime, 2000U);
    bus.data_ready = 1U;
    complete_measurement(&fixture, 2001U);
    EXPECT(pop_co2(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    return 0;
}

int st_run_scd41_tests(void)
{
    int failures = 0;

    failures += test_protocol_and_operating_modes();
    failures += test_startup_not_ready_and_first_valid();
    failures += test_first_failure_then_recovery();
    failures += test_stale_crc_quality_transition_and_recovery();
    failures += test_missing_sensor_and_recovery();
    failures += test_out_of_range_stale();
    failures += test_detach_reattach_lifetime();
    return failures;
}
