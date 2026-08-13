#include <stdio.h>
#include <string.h>

#include "sitetwin/ina219.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

#define TEST_INA219_REG_CONFIG 0x00U
#define TEST_INA219_REG_BUS_VOLTAGE 0x02U
#define TEST_INA219_REG_CURRENT 0x04U
#define TEST_INA219_REG_CALIBRATION 0x05U

typedef struct {
    uint8_t address;
    uint8_t last_reg_pointer;
    uint16_t config_register;
    uint16_t calibration_register;
    uint16_t bus_voltage_register;
    uint8_t current_response[2];
    st_hal_result_t next_bus_voltage_read_result;
    uint32_t config_writes;
    uint32_t calibration_writes;
    uint32_t bus_voltage_reads;
    uint32_t current_reads;
    uint8_t present;
} fake_ina219_bus_t;

typedef struct {
    st_pod_runtime_t runtime;
    st_ina219_t sensor;
    st_module_instance_t module;
} ina219_fixture_t;

static float absolute_difference(float left, float right)
{
    float difference = left - right;

    return difference < 0.0F ? -difference : difference;
}

static void set_bus_voltage(fake_ina219_bus_t *bus, uint16_t raw_bd, uint8_t cnvr, uint8_t ovf)
{
    bus->bus_voltage_register =
        (uint16_t)((raw_bd << 3U) | (cnvr != 0U ? 0x02U : 0U) | (ovf != 0U ? 0x01U : 0U));
}

static void set_current(fake_ina219_bus_t *bus, int16_t raw)
{
    bus->current_response[0] = (uint8_t)(((uint16_t)raw) >> 8U);
    bus->current_response[1] = (uint8_t)((uint16_t)raw);
}

static st_hal_result_t fake_bus_write(void *context, uint8_t address,
                                      const uint8_t *data, size_t length)
{
    fake_ina219_bus_t *bus = (fake_ina219_bus_t *)context;

    if (bus == NULL || data == NULL || address != bus->address ||
        (length != 1U && length != 3U)) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }

    bus->last_reg_pointer = data[0];
    if (length == 3U) {
        uint16_t value = (uint16_t)(((uint16_t)data[1] << 8U) | data[2]);

        if (data[0] == TEST_INA219_REG_CONFIG) {
            bus->config_register = value;
            ++bus->config_writes;
        } else if (data[0] == TEST_INA219_REG_CALIBRATION) {
            bus->calibration_register = value;
            ++bus->calibration_writes;
        } else {
            return ST_HAL_UNSUPPORTED;
        }
    }
    return ST_HAL_OK;
}

static st_hal_result_t fake_bus_read(void *context, uint8_t address,
                                     uint8_t *data, size_t length)
{
    fake_ina219_bus_t *bus = (fake_ina219_bus_t *)context;

    if (bus == NULL || data == NULL || address != bus->address || length != 2U) {
        return ST_HAL_IO_ERROR;
    }
    if (bus->present == 0U) {
        return ST_HAL_NOT_PRESENT;
    }

    if (bus->last_reg_pointer == TEST_INA219_REG_CONFIG) {
        data[0] = (uint8_t)(bus->config_register >> 8U);
        data[1] = (uint8_t)bus->config_register;
        return ST_HAL_OK;
    }
    if (bus->last_reg_pointer == TEST_INA219_REG_BUS_VOLTAGE) {
        st_hal_result_t result = bus->next_bus_voltage_read_result;

        ++bus->bus_voltage_reads;
        bus->next_bus_voltage_read_result = ST_HAL_OK;
        if (result != ST_HAL_OK) {
            return result;
        }
        data[0] = (uint8_t)(bus->bus_voltage_register >> 8U);
        data[1] = (uint8_t)bus->bus_voltage_register;
        return ST_HAL_OK;
    }
    if (bus->last_reg_pointer == TEST_INA219_REG_CURRENT) {
        ++bus->current_reads;
        data[0] = bus->current_response[0];
        data[1] = bus->current_response[1];
        return ST_HAL_OK;
    }
    return ST_HAL_IO_ERROR;
}

static void fake_bus_init(fake_ina219_bus_t *bus)
{
    memset(bus, 0, sizeof(*bus));
    bus->address = ST_INA219_DEFAULT_ADDRESS;
    bus->present = 1U;
    /* raw_bd=3000 -> 3000 * 4mV = 12.000 V. CNVR=1 (ready), OVF=0. */
    set_bus_voltage(bus, 3000U, 1U, 0U);
    /* raw=10000 -> 10000 * 0.09765625 mA/bit = 976.5625 mA, matching the
     * calibration for max_expected_current_a=3.2, shunt=0.1 used below. */
    set_current(bus, 10000);
}

static int fixture_init(ina219_fixture_t *fixture, fake_ina219_bus_t *bus,
                        uint32_t sample_interval_ms, uint32_t cache_validity_ms)
{
    static const uint8_t slots[ST_INA219_CHANNEL_COUNT] = {0U, 1U};
    st_ina219_config_t config;
    st_i2c_bus_t i2c_bus;

    memset(fixture, 0, sizeof(*fixture));
    memset(&config, 0, sizeof(config));
    i2c_bus.context = bus;
    i2c_bus.write = fake_bus_write;
    i2c_bus.read = fake_bus_read;
    config.bus = i2c_bus;
    config.address = bus->address;
    config.shunt_resistance_ohms = 0.1F;
    config.max_expected_current_a = 3.2F;
    config.sample_interval_ms = sample_interval_ms;
    config.cache_validity_ms = cache_validity_ms;
    config.bus_voltage_sensor_id = "ina219_bus_voltage";
    config.current_sensor_id = "ina219_current";
    st_pod_runtime_init(&fixture->runtime, ST_POD_EQUIPMENT, "POD_3", 3U);
    if (st_ina219_init(&fixture->sensor, &config) != 0 ||
        st_module_instance_init(&fixture->module,
                                st_ina219_module_driver(&fixture->sensor),
                                ST_INA219_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return st_module_instance_attach(&fixture->module, &fixture->runtime.registry,
                                     slots, ST_INA219_CHANNEL_COUNT);
}

static int pop_two_records(st_pod_runtime_t *runtime, st_telemetry_record_t *voltage,
                           st_telemetry_record_t *current)
{
    st_telemetry_record_t first;
    st_telemetry_record_t second;

    if (st_pod_runtime_next_telemetry(runtime, &first) != 0 ||
        st_pod_runtime_next_telemetry(runtime, &second) != 0) {
        return -1;
    }
    if (first.reading.sensor_kind == ST_SENSOR_VOLTAGE_V) {
        *voltage = first;
        *current = second;
    } else {
        *voltage = second;
        *current = first;
    }
    return voltage->reading.sensor_kind == ST_SENSOR_VOLTAGE_V &&
                   current->reading.sensor_kind == ST_SENSOR_CURRENT_MA
               ? 0
               : -1;
}

static int test_ina219_calibration_math(void)
{
    float current_lsb_ma = 0.0F;
    uint16_t calibration = st_ina219_calibration_register(3.2F, 0.1F, &current_lsb_ma);

    /* Verified independently: Current_LSB = 3.2/32768 A = 0.09765625 mA/bit;
     * Cal = trunc(0.04096 / (Current_LSB_A * 0.1)) = trunc(4194.3037) = 4194. */
    EXPECT(calibration == 4194U);
    EXPECT(absolute_difference(current_lsb_ma, 0.09765625F) < 0.0001F);

    /* Rejects a shunt/current combination outside the fixed PGA's ±320 mV
     * range (see st_ina219_init's max_measurable_current_a check). */
    EXPECT(st_ina219_calibration_register(0.0F, 0.1F, NULL) == 0U);
    EXPECT(st_ina219_calibration_register(3.2F, 0.0F, NULL) == 0U);
    return 0;
}

static int test_ina219_shared_acquisition_and_stale_recovery(void)
{
    fake_ina219_bus_t bus;
    ina219_fixture_t fixture;
    st_telemetry_record_t voltage;
    st_telemetry_record_t current;
    float last_voltage;
    float last_current;

    fake_bus_init(&bus);
    EXPECT(fixture_init(&fixture, &bus, 400U, 50U) == 0);

    st_pod_runtime_tick(&fixture.runtime, 0U);
    EXPECT(bus.calibration_writes == 1U);
    /* Triggers the conversion; both channel ports get ST_DRIVER_RETRY from
     * the shared acquire() (driver ready_at_ms=1+2=3 hasn't elapsed yet),
     * which the registry itself backs off by +100 ms per port
     * (sensor_registry.c: `next_sample_at_ms = now_ms + 100U` on RETRY) --
     * a second, independent timing layer on top of the driver's own
     * state machine. Both ports' next_sample_at_ms become 1+100=101. */
    st_pod_runtime_tick(&fixture.runtime, 1U);
    EXPECT(bus.config_writes == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    /* First tick at/after 101 where the registry will actually call
     * sample() on either port again; by then the driver's own 2 ms timer
     * is long past, so this completes the measurement. */
    st_pod_runtime_tick(&fixture.runtime, 101U);
    EXPECT(bus.bus_voltage_reads == 1U);
    EXPECT(bus.current_reads == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    EXPECT(pop_two_records(&fixture.runtime, &voltage, &current) == 0);
    EXPECT(voltage.reading.uptime_ms == 101U);
    EXPECT(current.reading.uptime_ms == 101U);
    EXPECT(absolute_difference(voltage.reading.value, 12.0F) < 0.001F);
    EXPECT(absolute_difference(current.reading.value, 976.5625F) < 0.001F);
    EXPECT((voltage.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((current.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    last_voltage = voltage.reading.value;
    last_current = current.reading.value;

    /* Registry scheduled the next sample at 101 + sample_interval_ms(400)
     * = 501 for both ports. Cache (50 ms) is long expired by then, so this
     * tick re-triggers a fresh conversion. */
    st_pod_runtime_tick(&fixture.runtime, 501U);
    EXPECT(bus.config_writes == 2U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    /* Same +100 ms registry backoff applies again: 501+100=601. */
    st_pod_runtime_tick(&fixture.runtime, 601U);
    EXPECT(bus.bus_voltage_reads == 2U);
    /* Same readings as before -- routine/STATE suppression, no re-queue. */
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    /* Next scheduled sample: 601 + 400 = 1001. Force a bus failure on that
     * cycle's Bus Voltage read and confirm both channels fall back to
     * their last valid values marked STALE. */
    bus.next_bus_voltage_read_result = ST_HAL_IO_ERROR;
    st_pod_runtime_tick(&fixture.runtime, 1001U);
    EXPECT(bus.config_writes == 3U);
    st_pod_runtime_tick(&fixture.runtime, 1101U);
    EXPECT(bus.bus_voltage_reads == 3U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    EXPECT(pop_two_records(&fixture.runtime, &voltage, &current) == 0);
    EXPECT(st_ina219_last_attempt_at_ms(&fixture.sensor) == 1101U);
    EXPECT(voltage.reading.value == last_voltage);
    EXPECT(current.reading.value == last_current);
    EXPECT((voltage.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((voltage.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    EXPECT((current.reading.quality_flags & ST_QUALITY_STALE) != 0U);
    EXPECT((current.reading.quality_flags & ST_QUALITY_VALID) == 0U);
    return 0;
}

static int test_ina219_conversion_not_ready_retries(void)
{
    fake_ina219_bus_t bus;
    ina219_fixture_t fixture;

    fake_bus_init(&bus);
    /* CNVR=0: device claims the previous conversion hasn't finished yet. */
    set_bus_voltage(&bus, 3000U, 0U, 0U);
    EXPECT(fixture_init(&fixture, &bus, 400U, 50U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U); /* trigger; both ports RETRY -> backoff to 101 */
    st_pod_runtime_tick(&fixture.runtime, 101U);
    EXPECT(bus.bus_voltage_reads == 1U);
    EXPECT(bus.current_reads == 0U); /* never reached -- CNVR gated the read */
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);

    /* finish_measurement's CNVR-gated RETURN counts as ST_DRIVER_RETRY at
     * the registry level too, so both ports back off another +100 ms from
     * this tick: 101+100=201. Device catches up before then. */
    set_bus_voltage(&bus, 3000U, 1U, 0U);
    st_pod_runtime_tick(&fixture.runtime, 201U);
    EXPECT(bus.bus_voltage_reads == 2U);
    EXPECT(bus.current_reads == 1U);
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 2U);
    return 0;
}

static int test_ina219_overflow_then_recovery(void)
{
    fake_ina219_bus_t bus;
    ina219_fixture_t fixture;
    st_telemetry_record_t voltage;
    st_telemetry_record_t current;

    fake_bus_init(&bus);
    set_bus_voltage(&bus, 3000U, 1U, 1U); /* OVF=1 */
    EXPECT(fixture_init(&fixture, &bus, 400U, 50U) == 0);
    st_pod_runtime_tick(&fixture.runtime, 0U);
    st_pod_runtime_tick(&fixture.runtime, 1U);   /* trigger; RETRY -> backoff to 101 */
    st_pod_runtime_tick(&fixture.runtime, 101U); /* OVF detected -> ERROR -> FAULTED */
    EXPECT(st_telemetry_queue_count(&fixture.runtime.outbound) == 0U);
    EXPECT(fixture.runtime.registry.ports[0].state == ST_PORT_FAULTED);
    EXPECT(fixture.runtime.registry.ports[1].state == ST_PORT_FAULTED);

    /* sensor_registry.c backs FAULTED ports off by exactly 1000 ms
     * (verified directly against sensor_registry.c) before it will
     * reprobe. Reprobing (state -> WARMING_UP) consumes one tick on its
     * own before sample() is called again, and that first sample() call
     * re-triggers the conversion (RETRY, +100 ms backoff) before a
     * further tick actually completes the read -- the same two-layer
     * timing this test's first cycle already exercised. */
    set_bus_voltage(&bus, 3000U, 1U, 0U); /* OVF cleared before recovery */
    st_pod_runtime_tick(&fixture.runtime, 1101U); /* FAULTED -> WARMING_UP */
    st_pod_runtime_tick(&fixture.runtime, 1102U); /* re-trigger; RETRY -> backoff to 1202 */
    st_pod_runtime_tick(&fixture.runtime, 1202U); /* completes */
    EXPECT(pop_two_records(&fixture.runtime, &voltage, &current) == 0);
    EXPECT((voltage.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    EXPECT((current.reading.quality_flags & ST_QUALITY_VALID) != 0U);
    return 0;
}

int st_run_ina219_tests(void)
{
    int failures = 0;

    failures += test_ina219_calibration_math();
    failures += test_ina219_shared_acquisition_and_stale_recovery();
    failures += test_ina219_conversion_not_ready_retries();
    failures += test_ina219_overflow_then_recovery();
    return failures;
}
