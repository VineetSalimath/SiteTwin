#include <stdio.h>
#include <string.h>

#include "sitetwin/adxl345.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "ADXL345 expectation failed: %s (%s:%d)\n", #condition, __FILE__,   \
                    __LINE__);                                                                   \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    int16_t samples[ST_ADXL345_FIFO_CAPACITY][3];
    uint8_t address;
    uint8_t register_pointer;
    uint8_t sample_count;
    uint8_t sample_index;
    uint8_t present;
    uint8_t registers[256];
    uint32_t data_reads;
} fake_adxl345_bus_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_adxl345_t sensor;
    st_module_instance_t module;
} adxl345_fixture_t;

static float absolute_difference(float left, float right)
{
    float result = left - right;
    return result < 0.0F ? -result : result;
}

static st_hal_result_t fake_bus_write(void *context, uint8_t address,
                                      const uint8_t *data, size_t length)
{
    fake_adxl345_bus_t *bus = (fake_adxl345_bus_t *)context;
    if (bus == NULL || data == NULL || address != bus->address ||
        (length != 1U && length != 2U)) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    bus->register_pointer = data[0];
    if (length == 2U) {
        bus->registers[data[0]] = data[1];
    }
    return ST_HAL_OK;
}

static st_hal_result_t fake_bus_read(void *context, uint8_t address,
                                     uint8_t *data, size_t length)
{
    fake_adxl345_bus_t *bus = (fake_adxl345_bus_t *)context;
    if (bus == NULL || data == NULL || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    if (bus->register_pointer == 0x00U && length == 1U) {
        data[0] = ST_ADXL345_DEVICE_ID;
        return ST_HAL_OK;
    }
    if (bus->register_pointer == 0x39U && length == 1U) {
        data[0] = (uint8_t)(bus->sample_count - bus->sample_index);
        return ST_HAL_OK;
    }
    if (bus->register_pointer == 0x32U && length == 6U &&
        bus->sample_index < bus->sample_count) {
        uint8_t axis;
        for (axis = 0U; axis < 3U; ++axis) {
            uint16_t raw = (uint16_t)bus->samples[bus->sample_index][axis];
            data[axis * 2U] = (uint8_t)raw;
            data[axis * 2U + 1U] = (uint8_t)(raw >> 8U);
        }
        ++bus->sample_index;
        ++bus->data_reads;
        return ST_HAL_OK;
    }
    return ST_HAL_IO_ERROR;
}

static void fake_bus_init(fake_adxl345_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->address = ST_ADXL345_DEFAULT_ADDRESS;
    bus->present = 1U;
}

static void set_alternating_window(fake_adxl345_bus_t *bus, uint8_t count,
                                   int16_t amplitude)
{
    uint8_t index;
    bus->sample_count = count;
    bus->sample_index = 0U;
    for (index = 0U; index < count; ++index) {
        bus->samples[index][0] = index % 2U == 0U ? amplitude : (int16_t)-amplitude;
        bus->samples[index][1] = 0;
        bus->samples[index][2] = 256; /* approximately +1 g static gravity */
    }
}

static int fixture_init(adxl345_fixture_t *fixture, fake_adxl345_bus_t *bus)
{
    const uint8_t slot = 0U;
    st_adxl345_config_t config;
    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    config.bus.context = bus;
    config.bus.write = fake_bus_write;
    config.bus.read = fake_bus_read;
    config.address = bus->address;
    config.range_g = 16U;
    config.rate_code = 0x0AU; /* 100 Hz */
    config.minimum_window_samples = 8U;
    config.sample_interval_ms = 5000U;
    config.vibration_sensor_id = "adxl345_vibration_rms";
    st_pod_runtime_init(&fixture->runtime, ST_POD_EQUIPMENT, "POD_3", 9U);
    if (st_adxl345_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_adxl345_module_driver(&fixture->sensor),
                                ST_ADXL345_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module, &fixture->runtime.registry,
                                     &slot, 1U);
}

static int test_adxl345_rms_math(void)
{
    const int16_t samples[] = {
        100, 0, 256,
        -100, 0, 256,
        100, 0, 256,
        -100, 0, 256,
    };
    float rms = st_adxl345_window_rms_g(samples, 4U,
                                        ST_ADXL345_DEFAULT_G_PER_LSB);
    EXPECT(absolute_difference(rms, 0.39F) < 0.0001F);
    EXPECT(st_adxl345_window_rms_g(samples, 1U,
                                   ST_ADXL345_DEFAULT_G_PER_LSB) == 0.0F);
    return 0;
}

static int test_adxl345_probe_window_and_feature_record(void)
{
    fake_adxl345_bus_t bus;
    adxl345_fixture_t fixture;
    st_telemetry_record_t record;
    fake_bus_init(&bus);
    set_alternating_window(&bus, 8U, 100);
    EXPECT(fixture_init(&fixture, &bus) == 0);

    st_pod_runtime_tick(&fixture.runtime, 0U); /* probe */
    EXPECT(bus.registers[0x2CU] == 0x0AU);
    EXPECT(bus.registers[0x31U] == 0x0BU);
    EXPECT(bus.registers[0x38U] == 0x88U);
    EXPECT(bus.registers[0x2DU] == 0x08U);
    st_pod_runtime_tick(&fixture.runtime, 1U); /* drain FIFO */
    EXPECT(bus.data_reads == 8U);
    EXPECT(st_adxl345_windows_completed(&fixture.sensor) == 1U);
    EXPECT(st_pod_runtime_next_telemetry(&fixture.runtime, &record) == 0);
    EXPECT(record.record_class == ST_RECORD_FEATURE);
    EXPECT(record.priority == ST_PRIORITY_FEATURE);
    EXPECT(record.reading.sensor_kind == ST_SENSOR_VIBRATION_RMS_G);
    EXPECT(record.reading.unit == ST_UNIT_G);
    EXPECT(absolute_difference(record.reading.value, 0.39F) < 0.0001F);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    return 0;
}

static int test_adxl345_minimum_window_clipping_and_missing(void)
{
    fake_adxl345_bus_t bus;
    adxl345_fixture_t fixture;
    st_telemetry_record_t record;
    fake_bus_init(&bus);
    set_alternating_window(&bus, 4U, 100);
    EXPECT(fixture_init(&fixture, &bus) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    set_alternating_window(&bus, 8U, 4095);
    st_pod_runtime_tick(&fixture.runtime, 101U);
    EXPECT(st_pod_runtime_next_telemetry(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_CLIPPED) != 0U);

    fake_bus_init(&bus);
    bus.present = 0U;
    EXPECT(fixture_init(&fixture, &bus) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    EXPECT(fixture.runtime.registry.ports[0].state == ST_PORT_PROBING);
    return 0;
}

static int test_adxl345_config_validation(void)
{
    fake_adxl345_bus_t bus;
    st_adxl345_t sensor;
    st_adxl345_config_t config;
    fake_bus_init(&bus);
    memset(&config, 0, sizeof(config));
    config.bus.context = &bus;
    config.bus.write = fake_bus_write;
    config.bus.read = fake_bus_read;
    config.address = 0x20U;
    config.range_g = 16U;
    config.rate_code = 0x0AU;
    config.minimum_window_samples = 8U;
    config.sample_interval_ms = 5000U;
    config.vibration_sensor_id = "vibration";
    EXPECT(st_adxl345_init(&sensor, &config) != 0);
    config.address = ST_ADXL345_DEFAULT_ADDRESS;
    config.range_g = 3U;
    EXPECT(st_adxl345_init(&sensor, &config) != 0);
    return 0;
}

int st_run_adxl345_tests(void)
{
    int failures = 0;
    failures += test_adxl345_rms_math();
    failures += test_adxl345_probe_window_and_feature_record();
    failures += test_adxl345_minimum_window_clipping_and_missing();
    failures += test_adxl345_config_validation();
    return failures;
}
