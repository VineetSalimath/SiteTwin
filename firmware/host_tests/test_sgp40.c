#include <stdio.h>
#include <string.h>

#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"
#include "sitetwin/sgp40.h"
#include "sitetwin/sht41.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define TEST_SGP40_MEASURE_RAW 0x260FU
#define TEST_SGP40_HEATER_OFF 0x3615U
#define TEST_SGP40_GET_SERIAL 0x3682U

typedef struct {
    uint8_t address;
    uint8_t present;
    uint8_t corrupt_raw_crc;
    uint16_t last_command;
    uint8_t last_write[8];
    size_t last_write_length;
    uint8_t serial_response[9];
    uint8_t raw_response[3];
    st_hal_result_t next_write_result;
    st_hal_result_t next_read_result;
    uint32_t serial_commands;
    uint32_t measurement_commands;
    uint32_t measurement_reads;
    uint32_t heater_off_commands;
} fake_sgp40_bus_t;

typedef struct {
    uint8_t available;
    uint8_t refresh_timestamp;
    float temperature_c;
    float humidity_percent;
    uint64_t acquired_at_ms;
} fake_compensation_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_sgp40_t sensor;
    st_module_instance_t module;
} sgp40_fixture_t;

static uint16_t decode_command(const uint8_t data[2])
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

static void encode_word(uint16_t value, uint8_t output[3])
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
    output[2] = st_sgp40_crc8(output);
}

static st_hal_result_t fake_write(void *context,
                                  uint8_t address,
                                  const uint8_t *data,
                                  size_t length)
{
    fake_sgp40_bus_t *bus = (fake_sgp40_bus_t *)context;
    st_hal_result_t injected;
    uint16_t command;

    if (bus == NULL || data == NULL || address != bus->address ||
        (length != 2U && length != 8U)) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    injected = bus->next_write_result;
    bus->next_write_result = ST_HAL_OK;
    if (injected != ST_HAL_OK) {
        return injected;
    }

    command = decode_command(data);
    if (command == TEST_SGP40_GET_SERIAL && length == 2U) {
        ++bus->serial_commands;
    } else if (command == TEST_SGP40_MEASURE_RAW && length == 8U) {
        ++bus->measurement_commands;
    } else if (command == TEST_SGP40_HEATER_OFF && length == 2U) {
        ++bus->heater_off_commands;
    } else {
        return ST_HAL_UNSUPPORTED;
    }

    memcpy(bus->last_write, data, length);
    bus->last_write_length = length;
    bus->last_command = command;
    return ST_HAL_OK;
}

static st_hal_result_t fake_read(void *context,
                                 uint8_t address,
                                 uint8_t *data,
                                 size_t length)
{
    fake_sgp40_bus_t *bus = (fake_sgp40_bus_t *)context;
    st_hal_result_t injected;

    if (bus == NULL || data == NULL || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    injected = bus->next_read_result;
    bus->next_read_result = ST_HAL_OK;
    if (injected != ST_HAL_OK) {
        return injected;
    }

    if (bus->last_command == TEST_SGP40_GET_SERIAL && length == 9U) {
        memcpy(data, bus->serial_response, length);
        return ST_HAL_OK;
    }
    if (bus->last_command == TEST_SGP40_MEASURE_RAW && length == 3U) {
        memcpy(data, bus->raw_response, length);
        if (bus->corrupt_raw_crc != 0U) {
            data[2] ^= 0x01U;
            bus->corrupt_raw_crc = 0U;
        }
        ++bus->measurement_reads;
        return ST_HAL_OK;
    }
    return ST_HAL_IO_ERROR;
}

static int fake_compensation_provider(void *context,
                                      uint64_t now_ms,
                                      st_sgp40_compensation_t *compensation)
{
    fake_compensation_t *source = (fake_compensation_t *)context;

    if (source == NULL || compensation == NULL || source->available == 0U) {
        return -1;
    }
    compensation->temperature_c = source->temperature_c;
    compensation->humidity_percent = source->humidity_percent;
    compensation->acquired_at_ms = source->refresh_timestamp != 0U
                                        ? now_ms
                                        : source->acquired_at_ms;
    return 0;
}

static void fake_bus_init(fake_sgp40_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->address = ST_SGP40_DEFAULT_ADDRESS;
    bus->present = 1U;
    encode_word(0x0123U, &bus->serial_response[0]);
    encode_word(0x4567U, &bus->serial_response[3]);
    encode_word(0x89ABU, &bus->serial_response[6]);
    encode_word(25000U, bus->raw_response);
}

static void fake_compensation_init(fake_compensation_t *compensation)
{
    memset(compensation, 0, sizeof(*compensation));
    compensation->available = 1U;
    compensation->refresh_timestamp = 1U;
    compensation->temperature_c = 21.3F;
    compensation->humidity_percent = 42.5F;
}

static int fixture_init(sgp40_fixture_t *fixture,
                        fake_sgp40_bus_t *bus,
                        fake_compensation_t *compensation)
{
    static const uint8_t slots[ST_SGP40_CHANNEL_COUNT] = {3U};
    st_sgp40_config_t config;
    st_i2c_bus_t i2c_bus;

    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    i2c_bus.context = bus;
    i2c_bus.write = fake_write;
    i2c_bus.read = fake_read;
    config.bus = i2c_bus;
    config.address = bus->address;
    config.algorithm_interval_ms = 1000U;
    config.compensation_maximum_age_ms = 2000U;
    config.compensation_provider = fake_compensation_provider;
    config.compensation_context = compensation;
    config.voc_index_sensor_id = "sgp40_voc_index";
    st_pod_runtime_init(&fixture->runtime, ST_POD_ENVIRONMENT, "ENV_01", 88U);
    if (st_sgp40_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_sgp40_module_driver(&fixture->sensor),
                                ST_SGP40_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module,
                                     &fixture->runtime.registry,
                                     slots,
                                     ST_SGP40_CHANNEL_COUNT);
}

static void complete_probe(sgp40_fixture_t *fixture)
{
    st_pod_runtime_tick(&fixture->runtime, 0U);
    st_pod_runtime_tick(&fixture->runtime, 1000U);
}

static void complete_raw_measurement(sgp40_fixture_t *fixture, uint64_t start_ms)
{
    st_pod_runtime_tick(&fixture->runtime, start_ms);
    st_pod_runtime_tick(&fixture->runtime, start_ms + 100U);
}

static void complete_algorithm_blackout(sgp40_fixture_t *fixture,
                                        uint64_t first_start_ms)
{
    uint32_t sample;

    for (sample = 0U; sample < 47U; ++sample) {
        complete_raw_measurement(fixture,
                                 first_start_ms + (uint64_t)sample * 1000U);
    }
}

static int pop_voc(st_pod_runtime_t *runtime, st_telemetry_record_t *record)
{
    if (st_pod_runtime_next_telemetry(runtime, record) != 0) {
        return -1;
    }
    return record->reading.sensor_kind == ST_SENSOR_VOC_INDEX &&
                   record->reading.unit == ST_UNIT_INDEX
               ? 0
               : -1;
}

static int test_protocol_conversions_and_algorithm(void)
{
    const uint8_t crc_example[2] = {0xBEU, 0xEFU};
    st_voc_index_algorithm_t algorithm;
    uint16_t ticks;
    int32_t index = 0;
    uint32_t sample;

    EXPECT(st_sgp40_crc8(crc_example) == 0x92U);
    EXPECT(st_sgp40_humidity_ticks(50.0F, &ticks) == 0);
    EXPECT(ticks == 0x8000U);
    EXPECT(st_sgp40_temperature_ticks(25.0F, &ticks) == 0);
    EXPECT(ticks == 0x6666U);
    EXPECT(st_sgp40_humidity_ticks(-0.1F, &ticks) != 0);
    EXPECT(st_sgp40_humidity_ticks(100.1F, &ticks) != 0);
    EXPECT(st_sgp40_temperature_ticks(-45.1F, &ticks) != 0);
    EXPECT(st_sgp40_temperature_ticks(130.1F, &ticks) != 0);

    EXPECT(st_voc_index_algorithm_init(&algorithm, 1000U) == 0);
    for (sample = 0U; sample < 46U; ++sample) {
        EXPECT(st_voc_index_algorithm_process(&algorithm, 25000U, &index) ==
               ST_VOC_ALGORITHM_WARMING_UP);
        EXPECT(index == 0);
    }
    EXPECT(st_voc_index_algorithm_process(&algorithm, 25000U, &index) ==
           ST_VOC_ALGORITHM_READY);
    EXPECT(index >= 1 && index <= 500);
    return 0;
}

static int test_sht41_compensation_accessor(void)
{
    st_sht41_t sensor;
    st_sht41_environment_sample_t sample;

    memset(&sensor, 0, sizeof(sensor));
    sensor.current_values[ST_SHT41_TEMPERATURE_CHANNEL] = 22.5F;
    sensor.current_values[ST_SHT41_HUMIDITY_CHANNEL] = 48.0F;
    sensor.current_acquired_at_ms = 1000U;
    sensor.last_valid_at_ms = 1000U;
    sensor.has_last_valid = 1U;
    sensor.current_has_sample = 1U;
    EXPECT(st_sht41_get_valid_environment(&sensor, 2000U, 1500U, &sample) == 0);
    EXPECT(sample.temperature_c == 22.5F);
    EXPECT(sample.humidity_percent == 48.0F);
    EXPECT(sample.acquired_at_ms == 1000U);

    sensor.current_quality_flags = ST_QUALITY_STALE;
    EXPECT(st_sht41_get_valid_environment(&sensor, 2000U, 1500U, &sample) != 0);
    sensor.current_quality_flags = ST_QUALITY_CRC_FAILED;
    EXPECT(st_sht41_get_valid_environment(&sensor, 2000U, 1500U, &sample) != 0);
    sensor.current_quality_flags = 0U;
    EXPECT(st_sht41_get_valid_environment(&sensor, 3000U, 1500U, &sample) != 0);
    return 0;
}

static int test_compensation_command_blackout_and_first_valid(void)
{
    fake_sgp40_bus_t bus;
    fake_compensation_t compensation;
    sgp40_fixture_t fixture;
    st_telemetry_record_t record;
    uint16_t expected_humidity;
    uint16_t expected_temperature;

    fake_bus_init(&bus);
    fake_compensation_init(&compensation);
    compensation.available = 0U;
    EXPECT(fixture_init(&fixture, &bus, &compensation) == 0);
    complete_probe(&fixture);
    st_pod_runtime_tick(&fixture.runtime, 1001U);
    EXPECT(st_sgp40_state(&fixture.sensor) == ST_SGP40_COMPENSATION_UNAVAILABLE);
    EXPECT(bus.measurement_commands == 0U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    compensation.available = 1U;
    compensation.refresh_timestamp = 0U;
    compensation.acquired_at_ms = 0U;
    st_pod_runtime_tick(&fixture.runtime, 2001U);
    EXPECT(bus.measurement_commands == 0U);
    EXPECT(st_sgp40_state(&fixture.sensor) == ST_SGP40_COMPENSATION_UNAVAILABLE);
    compensation.refresh_timestamp = 1U;
    st_pod_runtime_tick(&fixture.runtime, 3001U);
    EXPECT(bus.measurement_commands == 1U);
    EXPECT(bus.last_write_length == 8U);
    EXPECT(decode_command(bus.last_write) == TEST_SGP40_MEASURE_RAW);
    EXPECT(st_sgp40_humidity_ticks(compensation.humidity_percent,
                                   &expected_humidity) == 0);
    EXPECT(st_sgp40_temperature_ticks(compensation.temperature_c,
                                      &expected_temperature) == 0);
    EXPECT(bus.last_write[2] == (uint8_t)(expected_humidity >> 8U));
    EXPECT(bus.last_write[3] == (uint8_t)expected_humidity);
    EXPECT(bus.last_write[4] == st_sgp40_crc8(&bus.last_write[2]));
    EXPECT(bus.last_write[5] == (uint8_t)(expected_temperature >> 8U));
    EXPECT(bus.last_write[6] == (uint8_t)expected_temperature);
    EXPECT(bus.last_write[7] == st_sgp40_crc8(&bus.last_write[5]));
    EXPECT(!(bus.last_write[2] == 0x80U && bus.last_write[3] == 0x00U &&
             bus.last_write[5] == 0x66U && bus.last_write[6] == 0x66U));
    st_pod_runtime_tick(&fixture.runtime, 3101U);
    EXPECT(st_sgp40_state(&fixture.sensor) == ST_SGP40_ALGORITHM_WARMING);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    st_pod_runtime_tick(&fixture.runtime, 3901U);
    EXPECT(bus.measurement_commands == 1U);

    complete_algorithm_blackout(&fixture, 4001U);
    EXPECT(pop_voc(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.value >= 1.0F && record.reading.value <= 500.0F);
    EXPECT(record.reading.uptime_ms == 49101U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT(st_sgp40_last_raw_signal(&fixture.sensor) == 25000U);
    EXPECT(record.reading.value != (float)st_sgp40_last_raw_signal(&fixture.sensor));
    EXPECT(fixture.sensor.algorithm.samples_processed == 48U);
    EXPECT(st_sgp40_state(&fixture.sensor) == ST_SGP40_READY);
    return 0;
}

static int test_quality_transition_and_recovery(void)
{
    fake_sgp40_bus_t bus;
    fake_compensation_t compensation;
    sgp40_fixture_t fixture;
    st_telemetry_record_t first;
    st_telemetry_record_t transition;
    uint32_t reset_count;

    fake_bus_init(&bus);
    fake_compensation_init(&compensation);
    EXPECT(fixture_init(&fixture, &bus, &compensation) == 0);
    complete_probe(&fixture);
    complete_algorithm_blackout(&fixture, 1001U);
    EXPECT(pop_voc(&fixture.runtime, &first) == 0);

    compensation.available = 0U;
    st_pod_runtime_tick(&fixture.runtime, 48001U);
    EXPECT(pop_voc(&fixture.runtime, &transition) == 0);
    EXPECT(transition.reading.value == first.reading.value);
    EXPECT(transition.reading.uptime_ms == first.reading.uptime_ms);
    EXPECT((transition.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_COMPENSATION_UNAVAILABLE)) ==
           (ST_QUALITY_STALE | ST_QUALITY_COMPENSATION_UNAVAILABLE));
    EXPECT((transition.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    reset_count = fixture.sensor.algorithm.reset_count;

    compensation.available = 1U;
    complete_raw_measurement(&fixture, 49001U);
    EXPECT(pop_voc(&fixture.runtime, &transition) == 0);
    EXPECT((transition.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_WARMING_UP)) ==
           (ST_QUALITY_STALE | ST_QUALITY_WARMING_UP));
    EXPECT(fixture.sensor.algorithm.reset_count == reset_count);

    complete_algorithm_blackout(&fixture, 50001U);
    EXPECT(pop_voc(&fixture.runtime, &transition) == 0);
    EXPECT((transition.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((transition.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_COMPENSATION_UNAVAILABLE |
             ST_QUALITY_WARMING_UP)) == 0U);

    bus.corrupt_raw_crc = 1U;
    complete_raw_measurement(&fixture, 97001U);
    EXPECT(pop_voc(&fixture.runtime, &transition) == 0);
    EXPECT(transition.reading.uptime_ms == 96101U);
    EXPECT((transition.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED)) ==
           (ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED));
    EXPECT((transition.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    st_pod_runtime_tick(&fixture.runtime, 98001U);
    EXPECT(fixture.runtime.registry.ports[3].state == ST_PORT_FAULTED);
    return 0;
}

static int test_failures_detach_and_reattach(void)
{
    static const uint8_t slots[ST_SGP40_CHANNEL_COUNT] = {3U};
    fake_sgp40_bus_t bus;
    fake_compensation_t compensation;
    sgp40_fixture_t fixture;
    st_telemetry_record_t record;
    st_sensor_driver_t stale_driver;
    st_driver_sample_t sample;
    size_t cycle;

    fake_bus_init(&bus);
    fake_compensation_init(&compensation);
    bus.corrupt_raw_crc = 1U;
    EXPECT(fixture_init(&fixture, &bus, &compensation) == 0);
    complete_probe(&fixture);
    complete_raw_measurement(&fixture, 1001U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    EXPECT(fixture.runtime.registry.ports[3].state == ST_PORT_FAULTED);

    bus.present = 0U;
    st_pod_runtime_tick(&fixture.runtime, 2201U);
    EXPECT(fixture.runtime.registry.ports[3].state == ST_PORT_PROBING);
    bus.present = 1U;
    st_pod_runtime_tick(&fixture.runtime, 3201U);
    st_pod_runtime_tick(&fixture.runtime, 4201U);
    EXPECT(st_sgp40_state(&fixture.sensor) == ST_SGP40_WAITING_COMPENSATION);

    stale_driver = fixture.runtime.registry.ports[3].driver;
    st_module_instance_detach(&fixture.module, &fixture.runtime.registry, 4300U);
    EXPECT(bus.heater_off_commands == 1U);
    EXPECT(st_sgp40_state(&fixture.sensor) == ST_SGP40_DETACHED);
    EXPECT(stale_driver.sample(stale_driver.context, 4301U, &sample) ==
           ST_DRIVER_NOT_PRESENT);

    for (cycle = 0U; cycle < 32U; ++cycle) {
        EXPECT(st_module_instance_attach(&fixture.module,
                                         &fixture.runtime.registry,
                                         slots,
                                         ST_SGP40_CHANNEL_COUNT) == 0);
        st_module_instance_detach(&fixture.module,
                                  &fixture.runtime.registry,
                                  5000U + (uint64_t)cycle);
    }
    EXPECT(st_module_instance_attach(&fixture.module,
                                     &fixture.runtime.registry,
                                     slots,
                                     ST_SGP40_CHANNEL_COUNT) == 0);
    st_pod_runtime_tick(&fixture.runtime, 6000U);
    st_pod_runtime_tick(&fixture.runtime, 7000U);
    complete_algorithm_blackout(&fixture, 7001U);
    EXPECT(pop_voc(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    return 0;
}

int st_run_sgp40_tests(void)
{
    int failures = 0;

    failures += test_protocol_conversions_and_algorithm();
    failures += test_sht41_compensation_accessor();
    failures += test_compensation_command_blackout_and_first_valid();
    failures += test_quality_transition_and_recovery();
    failures += test_failures_detach_and_reattach();
    return failures;
}
