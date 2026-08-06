#include <stdio.h>
#include <string.h>

#include "sitetwin/fake_sensor.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"
#include "sitetwin/sht41.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define TEST_SHT41_COMMAND_MEASURE 0xFDU
#define TEST_SHT41_COMMAND_SERIAL 0x89U

typedef struct {
    uint8_t address;
    uint8_t last_command;
    uint8_t serial_response[6];
    uint8_t measurement_response[6];
    st_hal_result_t next_measurement_read_result;
    uint32_t serial_commands;
    uint32_t measurement_commands;
    uint32_t measurement_reads;
    uint8_t present;
} fake_sht41_bus_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_sht41_t sensor;
    st_module_instance_t module;
} sht41_fixture_t;

static float absolute_difference(float left, float right)
{
    float difference = left - right;

    return difference < 0.0F ? -difference : difference;
}

static void encode_word(uint16_t raw, uint8_t output[3])
{
    output[0] = (uint8_t)(raw >> 8U);
    output[1] = (uint8_t)raw;
    output[2] = st_sht41_crc8(output);
}

static void set_serial_response(fake_sht41_bus_t *bus, uint16_t high, uint16_t low)
{
    encode_word(high, &bus->serial_response[0]);
    encode_word(low, &bus->serial_response[3]);
}

static void set_measurement_response(fake_sht41_bus_t *bus,
                                     uint16_t raw_temperature, uint16_t raw_humidity)
{
    encode_word(raw_temperature, &bus->measurement_response[0]);
    encode_word(raw_humidity, &bus->measurement_response[3]);
}

static st_hal_result_t fake_bus_write(void *context, uint8_t address,
                                      const uint8_t *data, size_t length)
{
    fake_sht41_bus_t *bus = (fake_sht41_bus_t *)context;

    if (bus == NULL || data == NULL || length != 1U || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    bus->last_command = data[0];
    if (data[0] == TEST_SHT41_COMMAND_SERIAL) {
        ++bus->serial_commands;
    } else if (data[0] == TEST_SHT41_COMMAND_MEASURE) {
        ++bus->measurement_commands;
    } else {
        return ST_HAL_UNSUPPORTED;
    }
    return ST_HAL_OK;
}

static st_hal_result_t fake_bus_read(void *context, uint8_t address,
                                     uint8_t *data, size_t length)
{
    fake_sht41_bus_t *bus = (fake_sht41_bus_t *)context;

    if (bus == NULL || data == NULL || length != 6U || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    if (bus->last_command == TEST_SHT41_COMMAND_SERIAL) {
        memcpy(data, bus->serial_response, length);
        return ST_HAL_OK;
    }
    if (bus->last_command == TEST_SHT41_COMMAND_MEASURE) {
        st_hal_result_t result = bus->next_measurement_read_result;

        ++bus->measurement_reads;
        bus->next_measurement_read_result = ST_HAL_OK;
        if (result != ST_HAL_OK) {
            return result;
        }
        memcpy(data, bus->measurement_response, length);
        return ST_HAL_OK;
    }
    return ST_HAL_IO_ERROR;
}

static void fake_bus_init(fake_sht41_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->address = ST_SHT41_DEFAULT_ADDRESS;
    bus->present = 1U;
    set_serial_response(bus, 0x1234U, 0x5678U);
    set_measurement_response(bus, 0x62B0U, 0x72B0U);
}

static int fixture_init(sht41_fixture_t *fixture, fake_sht41_bus_t *bus,
                        uint32_t sample_interval_ms, uint32_t cache_validity_ms)
{
    static const uint8_t slots[ST_SHT41_CHANNEL_COUNT] = {0U, 1U};
    st_sht41_config_t config;
    st_i2c_bus_t i2c_bus;

    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    i2c_bus.context = bus;
    i2c_bus.write = fake_bus_write;
    i2c_bus.read = fake_bus_read;
    config.bus = i2c_bus;
    config.address = bus->address;
    config.sample_interval_ms = sample_interval_ms;
    config.cache_validity_ms = cache_validity_ms;
    config.temperature_sensor_id = "sht41_temperature";
    config.humidity_sensor_id = "sht41_humidity";
    st_pod_runtime_init(&fixture->runtime, ST_POD_ENVIRONMENT, "ENV_01", 42U);
    if (st_sht41_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_sht41_module_driver(&fixture->sensor),
                                ST_SHT41_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module, &fixture->runtime.registry,
                                     slots, ST_SHT41_CHANNEL_COUNT);
}

static int pop_two_records(st_pod_runtime_t *runtime,
                           st_telemetry_record_t *temperature,
                           st_telemetry_record_t *humidity)
{
    st_telemetry_record_t first;
    st_telemetry_record_t second;

    if (st_pod_runtime_next_telemetry(runtime, &first) != 0 ||
        st_pod_runtime_next_telemetry(runtime, &second) != 0) {
        return -1;
    }
    if (first.reading.sensor_kind == ST_SENSOR_TEMPERATURE_C) {
        *temperature = first;
        *humidity = second;
    } else {
        *temperature = second;
        *humidity = first;
    }
    return temperature->reading.sensor_kind == ST_SENSOR_TEMPERATURE_C &&
                   humidity->reading.sensor_kind == ST_SENSOR_RELATIVE_HUMIDITY_PERCENT
               ? 0
               : -1;
}

static int test_sht41_protocol_helpers(void)
{
    const uint8_t crc_example[2] = {0xBEU, 0xEFU};

    EXPECT(st_sht41_crc8(crc_example) == 0x92U);
    EXPECT(absolute_difference(st_sht41_raw_temperature_c(0U), -45.0F) < 0.001F);
    EXPECT(absolute_difference(st_sht41_raw_temperature_c(65535U), 130.0F) < 0.001F);
    EXPECT(absolute_difference(st_sht41_raw_humidity_percent(0U), -6.0F) < 0.001F);
    EXPECT(absolute_difference(st_sht41_raw_humidity_percent(65535U), 119.0F) < 0.001F);
    return 0;
}

static int test_validity_normalization(void)
{
    const uint32_t invalid_flags[] = {
        ST_QUALITY_CRC_FAILED,
        ST_QUALITY_OUT_OF_RANGE,
        ST_QUALITY_SENSOR_MISSING,
        ST_QUALITY_STALE,
    };
    size_t index;

    for (index = 0U; index < sizeof(invalid_flags) / sizeof(invalid_flags[0]); ++index) {
        st_sensor_registry_t registry;
        st_fake_sensor_t sensor;
        st_sensor_reading_t reading;

        st_sensor_registry_init(&registry, "ENV_01", 1U);
        st_fake_sensor_init(&sensor, "quality_test", "module-quality",
                            ST_SENSOR_TEMPERATURE_C, ST_UNIT_CELSIUS, 1000U, 20.0F);
        sensor.quality_flags = invalid_flags[index] | ST_QUALITY_VALID | ST_QUALITY_BATTERY_LOW;
        EXPECT(st_sensor_registry_attach(&registry, 0U, st_fake_sensor_driver(&sensor)) == 0);
        EXPECT(st_sensor_registry_tick(&registry, 0U, &reading, 1U) == 0U);
        EXPECT(st_sensor_registry_tick(&registry, 1U, &reading, 1U) == 1U);
        EXPECT((reading.quality_flags & invalid_flags[index]) != 0U);
        EXPECT((reading.quality_flags & ST_QUALITY_VALID) == 0U);
        EXPECT((reading.quality_flags & ST_QUALITY_BATTERY_LOW) != 0U);
    }

    {
        st_sensor_registry_t registry;
        st_fake_sensor_t sensor;
        st_sensor_reading_t reading;

        st_sensor_registry_init(&registry, "ENV_01", 1U);
        st_fake_sensor_init(&sensor, "warming_test", "module-warming",
                            ST_SENSOR_TEMPERATURE_C, ST_UNIT_CELSIUS, 1000U, 20.0F);
        sensor.quality_flags = ST_QUALITY_BATTERY_LOW | ST_QUALITY_MOUNTING_CHANGED;
        sensor.warm_until_ms = 100U;
        EXPECT(st_sensor_registry_attach(&registry, 0U, st_fake_sensor_driver(&sensor)) == 0);
        EXPECT(st_sensor_registry_tick(&registry, 0U, &reading, 1U) == 0U);
        EXPECT(st_sensor_registry_tick(&registry, 1U, &reading, 1U) == 1U);
        EXPECT((reading.quality_flags & ST_QUALITY_VALID) != 0U);
        EXPECT((reading.quality_flags & ST_QUALITY_WARMING_UP) != 0U);
        EXPECT((reading.quality_flags & ST_QUALITY_BATTERY_LOW) != 0U);
        EXPECT((reading.quality_flags & ST_QUALITY_MOUNTING_CHANGED) != 0U);
    }
    return 0;
}

static int test_shared_acquisition_and_stale_recovery(void)
{
    fake_sht41_bus_t bus;
    sht41_fixture_t fixture;
    st_telemetry_record_t temperature;
    st_telemetry_record_t humidity;
    float last_temperature;
    float last_humidity;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, 200U, 50U) == 0);

    st_pod_runtime_tick(&fixture.runtime, 0U);
    EXPECT(bus.serial_commands == 1U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    EXPECT(bus.measurement_commands == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    {
        st_driver_sample_t pending_sample;
        st_sensor_driver_t temperature_driver = fixture.runtime.registry.ports[0].driver;
        st_sensor_driver_t humidity_driver = fixture.runtime.registry.ports[1].driver;

        EXPECT(temperature_driver.sample(temperature_driver.context, 2U, &pending_sample) ==
               ST_DRIVER_RETRY);
        EXPECT(humidity_driver.sample(humidity_driver.context, 2U, &pending_sample) ==
               ST_DRIVER_RETRY);
        EXPECT(bus.measurement_commands == 1U);
        EXPECT(bus.measurement_reads == 0U);
    }
    st_pod_runtime_tick(&fixture.runtime, 101U);
    EXPECT(bus.measurement_commands == 1U);
    EXPECT(bus.measurement_reads == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT(temperature.reading.uptime_ms == 101U);
    EXPECT(humidity.reading.uptime_ms == 101U);
    EXPECT(temperature.reading.sequence == 1U);
    EXPECT(humidity.reading.sequence == 1U);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    last_temperature = temperature.reading.value;
    last_humidity = humidity.reading.value;

    st_pod_runtime_tick(&fixture.runtime, 301U);
    EXPECT(bus.measurement_commands == 2U);
    st_pod_runtime_tick(&fixture.runtime, 401U);
    EXPECT(bus.measurement_reads == 2U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    bus.measurement_response[2] ^= 0x01U;
    st_pod_runtime_tick(&fixture.runtime, 601U);
    EXPECT(bus.measurement_commands == 3U);
    st_pod_runtime_tick(&fixture.runtime, 701U);
    EXPECT(bus.measurement_reads == 3U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT(temperature.reading.uptime_ms == 401U);
    EXPECT(humidity.reading.uptime_ms == 401U);
    EXPECT(st_sht41_last_attempt_at_ms(&fixture.sensor) == 701U);
    EXPECT(temperature.reading.value == last_temperature);
    EXPECT(humidity.reading.value == last_humidity);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_CRC_FAILED) != 0U);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_CRC_FAILED) != 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_VALID) == 0U);

    set_measurement_response(&bus, 0x6300U, 0x7300U);
    st_pod_runtime_tick(&fixture.runtime, 901U);
    st_pod_runtime_tick(&fixture.runtime, 1001U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT(temperature.reading.uptime_ms == 1001U);
    EXPECT(humidity.reading.uptime_ms == 1001U);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((temperature.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED)) == 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT(fixture.runtime.reporting.readings_reported == 6U);
    return 0;
}

static int test_missing_sensor_stale_then_reprobe(void)
{
    fake_sht41_bus_t bus;
    sht41_fixture_t fixture;
    st_telemetry_record_t temperature;
    st_telemetry_record_t humidity;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, 200U, 50U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    st_pod_runtime_tick(&fixture.runtime, 101U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);

    bus.present = 0U;
    st_pod_runtime_tick(&fixture.runtime, 301U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT(temperature.reading.uptime_ms == 101U);
    EXPECT(humidity.reading.uptime_ms == 101U);
    EXPECT((temperature.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING)) ==
           (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING));
    EXPECT((humidity.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING)) ==
           (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING));
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) == 0U);

    st_pod_runtime_tick(&fixture.runtime, 501U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    EXPECT(fixture.runtime.registry.ports[0].state == ST_PORT_PROBING);
    EXPECT(fixture.runtime.registry.ports[1].state == ST_PORT_PROBING);

    bus.present = 1U;
    st_pod_runtime_tick(&fixture.runtime, 1501U);
    st_pod_runtime_tick(&fixture.runtime, 1502U);
    st_pod_runtime_tick(&fixture.runtime, 1602U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT(bus.serial_commands == 2U);
    return 0;
}

static int test_first_failure_and_out_of_range(void)
{
    fake_sht41_bus_t bus;
    sht41_fixture_t fixture;
    st_telemetry_record_t temperature;
    st_telemetry_record_t humidity;

    fake_bus_init(&bus);
    bus.measurement_response[2] ^= 0x01U;
    EXPECT(fixture_init(&fixture, &bus, 200U, 50U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    st_pod_runtime_tick(&fixture.runtime, 101U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    EXPECT(fixture.runtime.registry.ports[0].state == ST_PORT_FAULTED);
    EXPECT(fixture.runtime.registry.ports[1].state == ST_PORT_FAULTED);

    set_measurement_response(&bus, 0x62B0U, 0x72B0U);
    st_pod_runtime_tick(&fixture.runtime, 1101U);
    st_pod_runtime_tick(&fixture.runtime, 1102U);
    st_pod_runtime_tick(&fixture.runtime, 1202U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_VALID) != 0U);

    set_measurement_response(&bus, 0x62B0U, 0x0000U);
    st_pod_runtime_tick(&fixture.runtime, 1402U);
    st_pod_runtime_tick(&fixture.runtime, 1502U);
    EXPECT(pop_two_records(&fixture.runtime, &temperature, &humidity) == 0);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_OUT_OF_RANGE) != 0U);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((temperature.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_OUT_OF_RANGE) != 0U);
    EXPECT((humidity.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    return 0;
}

static int test_detach_and_reattach_lifetime(void)
{
    static const uint8_t slots[ST_SHT41_CHANNEL_COUNT] = {0U, 1U};
    fake_sht41_bus_t bus;
    sht41_fixture_t fixture;
    st_sensor_driver_t stale_driver;
    st_driver_sample_t sample;
    size_t cycle;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, 200U, 50U) == 0);
    stale_driver = fixture.runtime.registry.ports[0].driver;
    st_module_instance_detach(&fixture.module, &fixture.runtime.registry, 10U);
    EXPECT(fixture.runtime.registry.ports[0].attached == 0U);
    EXPECT(fixture.runtime.registry.ports[1].attached == 0U);
    EXPECT(stale_driver.sample(stale_driver.context, 11U, &sample) == ST_DRIVER_NOT_PRESENT);

    {
        st_fake_sensor_t occupied_sensor;

        st_fake_sensor_init(&occupied_sensor, "occupied", "module-occupied",
                            ST_SENSOR_TEMPERATURE_C, ST_UNIT_CELSIUS, 1000U, 20.0F);
        EXPECT(st_sensor_registry_attach(&fixture.runtime.registry, 1U,
                                         st_fake_sensor_driver(&occupied_sensor)) == 0);
        EXPECT(st_module_instance_attach(&fixture.module, &fixture.runtime.registry,
                                         slots, ST_SHT41_CHANNEL_COUNT) != 0);
        EXPECT(fixture.runtime.registry.ports[0].attached == 0U);
        EXPECT(fixture.runtime.registry.ports[1].attached != 0U);
        EXPECT(fixture.module.attached == 0U);
        st_sensor_registry_detach(&fixture.runtime.registry, 1U, 12U);
    }

    for (cycle = 0U; cycle < 32U; ++cycle) {
        EXPECT(st_module_instance_attach(&fixture.module, &fixture.runtime.registry,
                                         slots, ST_SHT41_CHANNEL_COUNT) == 0);
        EXPECT(fixture.runtime.registry.ports[0].driver.context ==
               &fixture.module.adapters[0]);
        EXPECT(fixture.runtime.registry.ports[1].driver.context ==
               &fixture.module.adapters[1]);
        st_module_instance_detach(&fixture.module, &fixture.runtime.registry,
                                  (uint64_t)cycle + 20U);
        EXPECT(fixture.runtime.registry.ports[0].attached == 0U);
        EXPECT(fixture.runtime.registry.ports[1].attached == 0U);
    }

    EXPECT(st_module_instance_attach(&fixture.module, &fixture.runtime.registry,
                                     slots, ST_SHT41_CHANNEL_COUNT) == 0);
    st_pod_runtime_tick(&fixture.runtime, 1000U);
    st_pod_runtime_tick(&fixture.runtime, 1001U);
    st_pod_runtime_tick(&fixture.runtime, 1101U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    return 0;
}

int st_run_sensor_foundation_tests(void)
{
    int failures = 0;

    failures += test_sht41_protocol_helpers();
    failures += test_validity_normalization();
    failures += test_shared_acquisition_and_stale_recovery();
    failures += test_first_failure_and_out_of_range();
    failures += test_missing_sensor_stale_then_reprobe();
    failures += test_detach_and_reattach_lifetime();
    return failures;
}
