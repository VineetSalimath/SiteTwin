#include <stdio.h>
#include <string.h>

#include "sitetwin/ds18b20.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define TEST_READ_ROM 0x33U
#define TEST_SKIP_ROM 0xCCU
#define TEST_CONVERT_T 0x44U
#define TEST_WRITE_SCRATCHPAD 0x4EU
#define TEST_READ_SCRATCHPAD 0xBEU

typedef enum {
    FAKE_DS18_NONE = 0,
    FAKE_DS18_ROM,
    FAKE_DS18_SCRATCHPAD
} fake_ds18_read_mode_t;

typedef struct {
    uint8_t present;
    uint8_t corrupt_next_scratchpad;
    uint8_t rom[ST_DS18B20_ROM_SIZE];
    uint8_t scratchpad[ST_DS18B20_SCRATCHPAD_SIZE];
    fake_ds18_read_mode_t read_mode;
    uint32_t resets;
    uint32_t conversions;
    uint32_t scratchpad_reads;
} fake_ds18_bus_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_ds18b20_t sensor;
    st_module_instance_t module;
} ds18b20_fixture_t;

static float absolute_difference(float left, float right)
{
    float difference = left - right;

    return difference < 0.0F ? -difference : difference;
}

static void set_temperature_raw(fake_ds18_bus_t *bus, int16_t raw, uint8_t resolution_bits)
{
    memset(bus->scratchpad, 0, sizeof(bus->scratchpad));
    bus->scratchpad[0] = (uint8_t)raw;
    bus->scratchpad[1] = (uint8_t)((uint16_t)raw >> 8U);
    bus->scratchpad[2] = 0x4BU;
    bus->scratchpad[3] = 0x46U;
    bus->scratchpad[4] = (uint8_t)(0x1FU | ((resolution_bits - 9U) << 5U));
    bus->scratchpad[5] = 0xFFU;
    bus->scratchpad[6] = 0x0CU;
    bus->scratchpad[7] = 0x10U;
    bus->scratchpad[8] = st_ds18b20_crc8(bus->scratchpad, 8U);
}

static void fake_bus_init(fake_ds18_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->present = 1U;
    bus->rom[0] = 0x28U;
    bus->rom[1] = 0x01U;
    bus->rom[2] = 0x02U;
    bus->rom[3] = 0x03U;
    bus->rom[4] = 0x04U;
    bus->rom[5] = 0x05U;
    bus->rom[6] = 0x06U;
    bus->rom[7] = st_ds18b20_crc8(bus->rom, 7U);
    set_temperature_raw(bus, 0x0191, 12U); /* 25.0625 C */
}

static st_hal_result_t fake_reset(void *context)
{
    fake_ds18_bus_t *bus = (fake_ds18_bus_t *)context;

    if (bus == NULL) {
        return ST_HAL_IO_ERROR;
    }
    ++bus->resets;
    bus->read_mode = FAKE_DS18_NONE;
    return bus->present != 0U ? ST_HAL_OK : ST_HAL_NOT_PRESENT;
}

static st_hal_result_t fake_write(void *context, const uint8_t *data, size_t length)
{
    fake_ds18_bus_t *bus = (fake_ds18_bus_t *)context;

    if (bus == NULL || data == NULL || length == 0U) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    if (length == 1U && data[0] == TEST_READ_ROM) {
        bus->read_mode = FAKE_DS18_ROM;
        return ST_HAL_OK;
    }
    if (length == 5U && data[0] == TEST_SKIP_ROM &&
        data[1] == TEST_WRITE_SCRATCHPAD) {
        bus->scratchpad[2] = data[2];
        bus->scratchpad[3] = data[3];
        bus->scratchpad[4] = data[4];
        bus->scratchpad[8] = st_ds18b20_crc8(bus->scratchpad, 8U);
        return ST_HAL_OK;
    }
    if (length == 2U && data[0] == TEST_SKIP_ROM && data[1] == TEST_CONVERT_T) {
        ++bus->conversions;
        return ST_HAL_OK;
    }
    if (length == 2U && data[0] == TEST_SKIP_ROM &&
        data[1] == TEST_READ_SCRATCHPAD) {
        bus->read_mode = FAKE_DS18_SCRATCHPAD;
        return ST_HAL_OK;
    }
    return ST_HAL_UNSUPPORTED;
}

static st_hal_result_t fake_read(void *context, uint8_t *data, size_t length)
{
    fake_ds18_bus_t *bus = (fake_ds18_bus_t *)context;

    if (bus == NULL || data == NULL || bus->present == 0U) {
        return bus != NULL && bus->present == 0U ? ST_HAL_NOT_PRESENT
                                                 : ST_HAL_IO_ERROR;
    }
    if (bus->read_mode == FAKE_DS18_ROM && length == ST_DS18B20_ROM_SIZE) {
        memcpy(data, bus->rom, length);
        bus->read_mode = FAKE_DS18_NONE;
        return ST_HAL_OK;
    }
    if (bus->read_mode == FAKE_DS18_SCRATCHPAD &&
        length == ST_DS18B20_SCRATCHPAD_SIZE) {
        memcpy(data, bus->scratchpad, length);
        if (bus->corrupt_next_scratchpad != 0U) {
            data[8] ^= 0x01U;
            bus->corrupt_next_scratchpad = 0U;
        }
        ++bus->scratchpad_reads;
        bus->read_mode = FAKE_DS18_NONE;
        return ST_HAL_OK;
    }
    return ST_HAL_IO_ERROR;
}

static int fixture_init(ds18b20_fixture_t *fixture, fake_ds18_bus_t *bus)
{
    static const uint8_t slots[ST_DS18B20_CHANNEL_COUNT] = {3U};
    st_ds18b20_config_t config;

    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    config.bus.context = bus;
    config.bus.reset = fake_reset;
    config.bus.write = fake_write;
    config.bus.read = fake_read;
    config.resolution_bits = 12U;
    config.sample_interval_ms = 1000U;
    config.cache_validity_ms = 50U;
    config.temperature_sensor_id = "ds18b20_temperature";
    st_pod_runtime_init(&fixture->runtime, ST_POD_EQUIPMENT, "POD_3", 3U);
    if (st_ds18b20_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_ds18b20_module_driver(&fixture->sensor),
                                ST_DS18B20_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module, &fixture->runtime.registry,
                                     slots, ST_DS18B20_CHANNEL_COUNT);
}

static int pop_temperature(st_pod_runtime_t *runtime, st_telemetry_record_t *record)
{
    return st_pod_runtime_next_telemetry(runtime, record) == 0 &&
                   strcmp(record->reading.sensor_id, "ds18b20_temperature") == 0 &&
                   record->reading.sensor_kind == ST_SENSOR_TEMPERATURE_C
               ? 0
               : -1;
}

static int test_crc_timing_and_signed_decode(void)
{
    uint8_t known_scratchpad[ST_DS18B20_SCRATCHPAD_SIZE] = {
        0x50U, 0x05U, 0x4BU, 0x46U, 0x7FU, 0xFFU, 0x0CU, 0x10U, 0x1CU
    };
    uint8_t negative[ST_DS18B20_SCRATCHPAD_SIZE];
    float temperature_c = 0.0F;

    EXPECT(st_ds18b20_crc8(known_scratchpad, 8U) == 0x1CU);
    EXPECT(st_ds18b20_decode_scratchpad(known_scratchpad, 12U, &temperature_c) == 0);
    EXPECT(absolute_difference(temperature_c, 85.0F) < 0.001F);
    memcpy(negative, known_scratchpad, sizeof(negative));
    negative[0] = 0x5EU;
    negative[1] = 0xFFU;
    negative[8] = st_ds18b20_crc8(negative, 8U);
    EXPECT(st_ds18b20_decode_scratchpad(negative, 12U, &temperature_c) == 0);
    EXPECT(absolute_difference(temperature_c, -10.125F) < 0.001F);
    EXPECT(st_ds18b20_conversion_duration_ms(9U) == 94U);
    EXPECT(st_ds18b20_conversion_duration_ms(10U) == 188U);
    EXPECT(st_ds18b20_conversion_duration_ms(11U) == 375U);
    EXPECT(st_ds18b20_conversion_duration_ms(12U) == 750U);
    return 0;
}

static int test_nonblocking_conversion_and_crc_stale_fallback(void)
{
    fake_ds18_bus_t bus;
    ds18b20_fixture_t fixture;
    st_telemetry_record_t record;
    st_sensor_driver_t driver;
    st_driver_sample_t pending;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U); /* ROM probe + resolution setup */
    EXPECT(fixture.sensor.probed != 0U);
    EXPECT(strncmp(fixture.sensor.metadata.module_uid, "ds18-28", 7U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 1U); /* start conversion */
    EXPECT(bus.conversions == 1U);
    driver = fixture.runtime.registry.ports[3].driver;
    EXPECT(driver.sample(driver.context, 500U, &pending) == ST_DRIVER_RETRY);
    EXPECT(bus.scratchpad_reads == 0U);
    st_pod_runtime_tick(&fixture.runtime, 751U);
    EXPECT(bus.scratchpad_reads == 1U);
    EXPECT(pop_temperature(&fixture.runtime, &record) == 0);
    EXPECT(absolute_difference(record.reading.value, 25.0625F) < 0.001F);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);

    bus.corrupt_next_scratchpad = 1U;
    st_pod_runtime_tick(&fixture.runtime, 1751U);
    st_pod_runtime_tick(&fixture.runtime, 2501U);
    EXPECT(pop_temperature(&fixture.runtime, &record) == 0);
    EXPECT(absolute_difference(record.reading.value, 25.0625F) < 0.001F);
    EXPECT((record.reading.quality_flags & ST_QUALITY_CRC_FAILED) != 0U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    return 0;
}

static int test_missing_sensor_reprobe(void)
{
    fake_ds18_bus_t bus;
    ds18b20_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    st_pod_runtime_tick(&fixture.runtime, 751U);
    EXPECT(pop_temperature(&fixture.runtime, &record) == 0);

    bus.present = 0U;
    st_pod_runtime_tick(&fixture.runtime, 1751U);
    EXPECT(pop_temperature(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags &
            (ST_QUALITY_SENSOR_MISSING | ST_QUALITY_STALE)) ==
           (ST_QUALITY_SENSOR_MISSING | ST_QUALITY_STALE));
    st_pod_runtime_tick(&fixture.runtime, 2751U);
    st_pod_runtime_tick(&fixture.runtime, 3751U);
    EXPECT(fixture.runtime.registry.ports[3].state == ST_PORT_PROBING);

    bus.present = 1U;
    st_pod_runtime_tick(&fixture.runtime, 4751U);
    st_pod_runtime_tick(&fixture.runtime, 4752U);
    st_pod_runtime_tick(&fixture.runtime, 5502U);
    EXPECT(pop_temperature(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    return 0;
}

int st_run_ds18b20_tests(void)
{
    int failures = 0;

    failures += test_crc_timing_and_signed_decode();
    failures += test_nonblocking_conversion_and_crc_stale_fallback();
    failures += test_missing_sensor_reprobe();
    return failures;
}
