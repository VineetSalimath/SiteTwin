#include <stdio.h>
#include <string.h>

#include "sitetwin/bh1750.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define TEST_BH1750_COMMAND_POWER_ON 0x01U
#define TEST_BH1750_COMMAND_MEASURE 0x20U

typedef struct {
    uint8_t address;
    uint8_t last_command;
    uint8_t measurement_response[2];
    st_hal_result_t next_measurement_read_result;
    uint32_t power_on_commands;
    uint32_t measurement_commands;
    uint32_t measurement_reads;
    uint8_t present;
} fake_bh1750_bus_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_bh1750_t sensor;
    st_module_instance_t module;
} bh1750_fixture_t;

static float absolute_difference(float left, float right)
{
    float difference = left - right;

    return difference < 0.0F ? -difference : difference;
}

static void set_measurement_response(fake_bh1750_bus_t *bus, uint16_t raw)
{
    bus->measurement_response[0] = (uint8_t)(raw >> 8U);
    bus->measurement_response[1] = (uint8_t)raw;
}

static st_hal_result_t fake_bus_write(void *context, uint8_t address,
                                      const uint8_t *data, size_t length)
{
    fake_bh1750_bus_t *bus = (fake_bh1750_bus_t *)context;

    if (bus == NULL || data == NULL || length != 1U || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    bus->last_command = data[0];
    if (data[0] == TEST_BH1750_COMMAND_POWER_ON) {
        ++bus->power_on_commands;
    } else if (data[0] == TEST_BH1750_COMMAND_MEASURE) {
        ++bus->measurement_commands;
    } else {
        return ST_HAL_UNSUPPORTED;
    }
    return ST_HAL_OK;
}

static st_hal_result_t fake_bus_read(void *context, uint8_t address,
                                     uint8_t *data, size_t length)
{
    fake_bh1750_bus_t *bus = (fake_bh1750_bus_t *)context;

    if (bus == NULL || data == NULL || length != 2U || address != bus->address) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }
    if (bus->last_command == TEST_BH1750_COMMAND_MEASURE) {
        st_hal_result_t result = bus->next_measurement_read_result;

        ++bus->measurement_reads;
        bus->next_measurement_read_result = ST_HAL_OK;
        if (result != ST_HAL_OK) {
            return result;
        }
        memcpy(data, bus->measurement_response, length);
        return ST_HAL_OK;
    }
    /* No read is ever issued directly after Power On in this driver --
     * probe() only writes. A read here means the driver's sequencing broke. */
    return ST_HAL_IO_ERROR;
}

static void fake_bus_init(fake_bh1750_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->address = ST_BH1750_DEFAULT_ADDRESS;
    bus->present = 1U;
    /* Datasheet worked example ex1: High byte 1000_0011, low byte 1001_0000
     * -> raw 0x8390 (33680) -> approx. 28067 lx. */
    set_measurement_response(bus, 0x8390U);
}

static int fixture_init(bh1750_fixture_t *fixture, fake_bh1750_bus_t *bus,
                        uint32_t sample_interval_ms, uint32_t cache_validity_ms)
{
    static const uint8_t slots[ST_BH1750_CHANNEL_COUNT] = {0U};
    st_bh1750_config_t config;
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
    config.illuminance_sensor_id = "bh1750_illuminance";
    st_pod_runtime_init(&fixture->runtime, ST_POD_ACTIVITY_ACCESS, "POD_2", 7U);
    if (st_bh1750_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_bh1750_module_driver(&fixture->sensor),
                                ST_BH1750_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module, &fixture->runtime.registry,
                                     slots, ST_BH1750_CHANNEL_COUNT);
}

static int pop_one_record(st_pod_runtime_t *runtime, st_telemetry_record_t *record)
{
    return st_pod_runtime_next_telemetry(runtime, record) == 0 &&
                   record->reading.sensor_kind == ST_SENSOR_ILLUMINANCE_LUX
               ? 0
               : -1;
}

static int test_bh1750_protocol_helpers(void)
{
    /* Worked examples straight from the ROHM datasheet's "Measurement
     * sequence example" section, so the tolerance only needs to cover the
     * datasheet's own rounding, not driver behaviour. */
    EXPECT(absolute_difference(st_bh1750_raw_to_lux(0x8390U), 28066.67F) < 0.5F);
    EXPECT(absolute_difference(st_bh1750_raw_to_lux(0x0110U), 226.67F) < 0.5F);
    EXPECT(absolute_difference(st_bh1750_raw_to_lux(0U), 0.0F) < 0.001F);
    /* Max raw value (0xFFFF) is the boundary this driver treats as
     * out-of-range -- see ST_BH1750_MAX_LUX in bh1750.c. */
    EXPECT(absolute_difference(st_bh1750_raw_to_lux(0xFFFFU), 54612.5F) < 0.5F);
    return 0;
}

static int test_bh1750_shared_acquisition_and_stale_recovery(void)
{
    fake_bh1750_bus_t bus;
    bh1750_fixture_t fixture;
    st_telemetry_record_t record;
    float last_value;

    fake_bus_init(&bus);
    /* sample_interval_ms=400 so cache expiry (below) drives re-acquisition
     * rather than the registry's own scheduling interval. */
    EXPECT(fixture_init(&fixture, &bus, 400U, 50U) == 0);

    st_pod_runtime_tick(&fixture.runtime, 0U);
    EXPECT(bus.power_on_commands == 1U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    EXPECT(bus.measurement_commands == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    {
        /* Before the 180 ms measurement window elapses, sample() must
         * return RETRY without issuing a read -- this is the non-blocking
         * state-machine contract, not a real time delay in this test. */
        st_driver_sample_t pending_sample;
        st_sensor_driver_t driver = fixture.runtime.registry.ports[0].driver;

        EXPECT(driver.sample(driver.context, 100U, &pending_sample) == ST_DRIVER_RETRY);
        EXPECT(bus.measurement_commands == 1U);
        EXPECT(bus.measurement_reads == 0U);
    }

    /* ready_at_ms = 1 + 180 = 181. */
    st_pod_runtime_tick(&fixture.runtime, 181U);
    EXPECT(bus.measurement_commands == 1U);
    EXPECT(bus.measurement_reads == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 1U);
    EXPECT(pop_one_record(&fixture.runtime, &record) == 0);
    EXPECT(record.reading.uptime_ms == 181U);
    EXPECT(record.reading.sequence == 1U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    last_value = record.reading.value;

    /* Cache (50 ms) expires well before the next scheduled tick, so this
     * re-triggers a fresh one-time measurement rather than reusing the
     * cached value. */
    st_pod_runtime_tick(&fixture.runtime, 581U);
    EXPECT(bus.measurement_commands == 2U);
    st_pod_runtime_tick(&fixture.runtime, 761U);
    EXPECT(bus.measurement_reads == 2U);
    /* Same raw value as before -- routine/STATE reporting suppresses the
     * unchanged reading rather than re-queuing it. */
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    /* Force a bus-level failure on the next measurement read and confirm
     * the driver falls back to the last valid value marked STALE, per the
     * cache_failure() path in bh1750.c. */
    bus.next_measurement_read_result = ST_HAL_IO_ERROR;
    st_pod_runtime_tick(&fixture.runtime, 1161U);
    EXPECT(bus.measurement_commands == 3U);
    st_pod_runtime_tick(&fixture.runtime, 1341U);
    EXPECT(bus.measurement_reads == 3U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 1U);
    EXPECT(pop_one_record(&fixture.runtime, &record) == 0);
    EXPECT(st_bh1750_last_attempt_at_ms(&fixture.sensor) == 1341U);
    EXPECT(record.reading.value == last_value);
    EXPECT((record.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    return 0;
}

static int test_bh1750_missing_sensor_stale_then_reprobe(void)
{
    fake_bh1750_bus_t bus;
    bh1750_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, 400U, 50U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    st_pod_runtime_tick(&fixture.runtime, 181U);
    EXPECT(pop_one_record(&fixture.runtime, &record) == 0);

    bus.present = 0U;
    st_pod_runtime_tick(&fixture.runtime, 581U);
    EXPECT(pop_one_record(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags &
            (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING)) ==
           (ST_QUALITY_STALE | ST_QUALITY_SENSOR_MISSING));
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) == 0U);

    /* Three consecutive failures (per ST_BH1750_REPROBE_AFTER_FAILURES)
     * force the port back into PROBING before it will try again. */
    st_pod_runtime_tick(&fixture.runtime, 981U);
    st_pod_runtime_tick(&fixture.runtime, 1381U);
    EXPECT(fixture.runtime.registry.ports[0].state == ST_PORT_PROBING);

    bus.present = 1U;
    st_pod_runtime_tick(&fixture.runtime, 2381U);
    st_pod_runtime_tick(&fixture.runtime, 2382U);
    st_pod_runtime_tick(&fixture.runtime, 2562U);
    EXPECT(pop_one_record(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT(bus.power_on_commands == 2U);
    return 0;
}

static int test_bh1750_out_of_range(void)
{
    fake_bh1750_bus_t bus;
    bh1750_fixture_t fixture;
    st_telemetry_record_t record;

    fake_bus_init(&bus);
    /* Maximum possible raw reading (0xFFFF) is beyond what this driver
     * treats as trustworthy without an MTreg adjustment it does not yet
     * make -- see ST_BH1750_MAX_LUX. This is a real reachable device
     * output, not an artificially corrupted one. */
    set_measurement_response(&bus, 0xFFFFU);
    EXPECT(fixture_init(&fixture, &bus, 400U, 50U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);
    st_pod_runtime_tick(&fixture.runtime, 181U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    EXPECT(fixture.runtime.registry.ports[0].state == ST_PORT_FAULTED);

    set_measurement_response(&bus, 0x8390U);
    st_pod_runtime_tick(&fixture.runtime, 1181U);
    st_pod_runtime_tick(&fixture.runtime, 1182U);
    st_pod_runtime_tick(&fixture.runtime, 1362U);
    EXPECT(pop_one_record(&fixture.runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    return 0;
}

int st_run_bh1750_tests(void)
{
    int failures = 0;

    failures += test_bh1750_protocol_helpers();
    failures += test_bh1750_shared_acquisition_and_stale_recovery();
    failures += test_bh1750_missing_sensor_stale_then_reprobe();
    failures += test_bh1750_out_of_range();
    return failures;
}
