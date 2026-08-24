#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "sitetwin/hotswap_module_binding.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                         \
    } while (0)

/* A trivial always-OK I2C bus: sufficient because every sensor's own
 * st_XXX_init() only checks that bus.write/read are non-NULL and does not
 * itself perform I2C traffic (transactions are deferred to the first
 * acquire()/probe() cycle, which is out of scope for this binding-layer
 * test -- each driver's own test file already covers protocol
 * correctness). */
static st_hal_result_t fake_i2c_write(void *context, uint8_t address, const uint8_t *data,
                                      size_t length)
{
    (void)context;
    (void)address;
    (void)data;
    (void)length;
    return ST_HAL_OK;
}

static st_hal_result_t fake_i2c_read(void *context, uint8_t address, uint8_t *data,
                                     size_t length)
{
    (void)context;
    (void)address;
    if (data != NULL && length > 0U) {
        memset(data, 0, length);
    }
    return ST_HAL_OK;
}

static st_hal_result_t fake_onewire_reset(void *context)
{
    (void)context;
    return ST_HAL_OK;
}
static st_hal_result_t fake_onewire_write(void *context, const uint8_t *data, size_t length)
{
    (void)context;
    (void)data;
    (void)length;
    return ST_HAL_OK;
}
static st_hal_result_t fake_onewire_read(void *context, uint8_t *data, size_t length)
{
    (void)context;
    if (data != NULL && length > 0U) {
        memset(data, 0, length);
    }
    return ST_HAL_OK;
}

/* One combined fixture is still fine to use for both callbacks in tests
 * (they just each get their own explicit context field pointing at the
 * same fixture) -- the point of the split in the real header is that
 * *production* callers aren't forced to share one context, not that
 * tests can't. */
typedef struct {
    size_t onewire_last_requested_port;
    int onewire_calls;
    int probe_calls;
    uint8_t probe_last_address;
    st_hal_result_t probe_result;
} fake_binding_fixture_t;

static st_hal_result_t fake_i2c_probe(void *context, uint8_t address)
{
    fake_binding_fixture_t *fixture = (fake_binding_fixture_t *)context;

    fixture->probe_calls++;
    fixture->probe_last_address = address;
    return fixture->probe_result;
}

static st_onewire_bus_t fake_data_common_bus_for_port(void *context, size_t port_index)
{
    fake_binding_fixture_t *fixture = (fake_binding_fixture_t *)context;
    st_onewire_bus_t bus;

    fixture->onewire_last_requested_port = port_index;
    fixture->onewire_calls++;
    memset(&bus, 0, sizeof(bus));
    bus.context = fixture;
    bus.reset = fake_onewire_reset;
    bus.write = fake_onewire_write;
    bus.read = fake_onewire_read;
    return bus;
}

static void make_binding(st_hotswap_module_binding_t *binding, st_sensor_registry_t *registry,
                         fake_binding_fixture_t *fixture)
{
    st_hotswap_binding_io_t io;

    memset(fixture, 0, sizeof(*fixture));
    fixture->probe_result = ST_HAL_OK; /* tests override this when they want a mismatch */

    memset(&io, 0, sizeof(io));
    io.i2c_bus.context = NULL;
    io.i2c_bus.write = fake_i2c_write;
    io.i2c_bus.read = fake_i2c_read;
    io.i2c_probe = fake_i2c_probe;
    io.i2c_probe_context = fixture;
    io.data_common_bus_for_port = fake_data_common_bus_for_port;
    io.data_common_context = fixture;

    st_sensor_registry_init(registry, "TEST_POD", 1U);
    if (st_hotswap_module_binding_init(binding, &io, registry, ST_HOTSWAP_BINDING_MAX_PORTS) !=
        0) {
        fprintf(stderr, "make_binding: init failed\n");
    }
}

static size_t count_attached_registry_slots(const st_sensor_registry_t *registry)
{
    size_t count = 0U;
    size_t i;

    for (i = 0U; i < ST_MAX_SENSOR_PORTS; ++i) {
        if (registry->ports[i].attached != 0U) {
            count++;
        }
    }
    return count;
}

static int test_pir_attach_and_detach(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_pir_t *pir = NULL;
    const char *sensor_id = NULL;

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 1U, ST_MODULE_TYPE_PIR, 5000U) == 0);
    EXPECT(st_hotswap_binding_get_pir(&binding, 1U, &pir, &sensor_id) == 1);
    EXPECT(pir != NULL);
    EXPECT(strcmp(sensor_id, "pir_motion") == 0);
    EXPECT(count_attached_registry_slots(&registry) == 0U); /* PIR never touches the registry */

    st_hotswap_module_binding_detach(&binding, 1U, 6000U);
    EXPECT(st_hotswap_binding_get_pir(&binding, 1U, &pir, &sensor_id) == 0);

    return 0;
}

static int test_reed_attach_and_get_wrong_port(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_reed_debounce_t *reed = NULL;
    const char *sensor_id = NULL;

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 2U, ST_MODULE_TYPE_REED, 1000U) == 0);
    EXPECT(st_hotswap_binding_get_reed(&binding, 2U, &reed, &sensor_id) == 1);
    EXPECT(strcmp(sensor_id, "reed_contact") == 0);

    /* A different, never-attached port must report nothing. */
    EXPECT(st_hotswap_binding_get_reed(&binding, 0U, &reed, &sensor_id) == 0);
    EXPECT(st_hotswap_binding_get_pir(&binding, 2U, NULL, NULL) == 0); /* wrong kind, also NULL-safe */

    return 0;
}

static int test_registry_backed_type_attaches_and_detaches(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;

    make_binding(&binding, &registry, &fixture);

    EXPECT(count_attached_registry_slots(&registry) == 0U);
    EXPECT(st_hotswap_module_binding_attach(&binding, 0U, ST_MODULE_TYPE_BH1750, 1000U) == 0);
    EXPECT(count_attached_registry_slots(&registry) == 1U); /* BH1750 has 1 channel */

    EXPECT(st_hotswap_module_binding_attach(&binding, 1U, ST_MODULE_TYPE_SHT41, 1000U) == 0);
    EXPECT(count_attached_registry_slots(&registry) == 3U); /* + SHT41's 2 channels */

    st_hotswap_module_binding_detach(&binding, 0U, 2000U);
    EXPECT(count_attached_registry_slots(&registry) == 2U);

    st_hotswap_module_binding_detach(&binding, 1U, 2000U);
    EXPECT(count_attached_registry_slots(&registry) == 0U);

    return 0;
}

static int test_ds18b20_requests_onewire_bus_for_its_own_port(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 3U, ST_MODULE_TYPE_DS18B20, 1000U) == 0);
    EXPECT(fixture.onewire_calls == 1);
    EXPECT(fixture.onewire_last_requested_port == 3U);
    EXPECT(count_attached_registry_slots(&registry) == 1U);

    return 0;
}

static int test_ds18b20_fails_cleanly_without_onewire_provider(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_hotswap_binding_io_t io;

    memset(&fixture, 0, sizeof(fixture));
    fixture.probe_result = ST_HAL_OK;

    st_sensor_registry_init(&registry, "TEST_POD", 1U);
    memset(&io, 0, sizeof(io));
    io.i2c_bus.write = fake_i2c_write;
    io.i2c_bus.read = fake_i2c_read;
    io.i2c_probe = fake_i2c_probe;
    io.i2c_probe_context = &fixture;
    io.data_common_bus_for_port = NULL; /* deliberately not wired up */
    EXPECT(st_hotswap_module_binding_init(&binding, &io, &registry,
                                          ST_HOTSWAP_BINDING_MAX_PORTS) == 0);

    EXPECT(st_hotswap_module_binding_attach(&binding, 0U, ST_MODULE_TYPE_DS18B20, 1000U) != 0);
    EXPECT(count_attached_registry_slots(&registry) == 0U);

    return 0;
}

static int test_unknown_and_empty_never_attach(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 0U, ST_MODULE_TYPE_UNKNOWN, 1000U) != 0);
    EXPECT(st_hotswap_module_binding_attach(&binding, 0U, ST_MODULE_TYPE_EMPTY, 1000U) != 0);

    return 0;
}

static int test_bus_probe_addresses_match_contract(void)
{
    uint8_t address = 0U;

    EXPECT(st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_SHT41, &address));
    EXPECT(address == ST_SHT41_DEFAULT_ADDRESS);

    EXPECT(st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_SCD41, &address));
    EXPECT(address == ST_SCD41_DEFAULT_ADDRESS);

    EXPECT(st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_SGP40, &address));
    EXPECT(address == ST_SGP40_DEFAULT_ADDRESS);

    EXPECT(st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_BH1750, &address));
    EXPECT(address == ST_BH1750_DEFAULT_ADDRESS);

    EXPECT(st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_ADXL345, &address));
    EXPECT(address == ST_ADXL345_DEFAULT_ADDRESS);

    EXPECT(st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_INA219, &address));
    EXPECT(address == ST_INA219_DEFAULT_ADDRESS);

    /* Non-I2C types never request a bus cross-check. */
    EXPECT(!st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_DS18B20, &address));
    EXPECT(!st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_PIR, &address));
    EXPECT(!st_hotswap_module_binding_uses_bus_probe(NULL, ST_MODULE_TYPE_REED, &address));

    return 0;
}

static int test_bus_probe_delegates_to_i2c_probe_callback(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_hal_result_t result;

    make_binding(&binding, &registry, &fixture);

    result = st_hotswap_module_binding_bus_probe(&binding, 2U, ST_SHT41_DEFAULT_ADDRESS);
    EXPECT(result == ST_HAL_OK);
    EXPECT(fixture.probe_calls == 1);
    EXPECT(fixture.probe_last_address == ST_SHT41_DEFAULT_ADDRESS);

    return 0;
}

static int test_bus_probe_propagates_mismatch(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_hal_result_t result;

    make_binding(&binding, &registry, &fixture);
    fixture.probe_result = ST_HAL_NOT_PRESENT;

    result = st_hotswap_module_binding_bus_probe(&binding, 0U, ST_INA219_DEFAULT_ADDRESS);
    EXPECT(result == ST_HAL_NOT_PRESENT);

    return 0;
}

static int test_detach_reports_pir_capability(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_sensor_kind_t cleared[ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT];

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 1U, ST_MODULE_TYPE_PIR, 5000U) == 0);
    /* Nothing to drain before any detach has happened on this port. */
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 1U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 0U);

    st_hotswap_module_binding_detach(&binding, 1U, 6000U);
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 1U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 1U);
    EXPECT(cleared[0] == ST_SENSOR_MOTION);

    /* Draining is destructive -- a second drain without an intervening
     * detach returns nothing, so app_main.c's per-tick drain loop can't
     * double-clear the same capability on the next poll. */
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 1U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 0U);
    return 0;
}

static int test_detach_reports_reed_capability(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_sensor_kind_t cleared[ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT];

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 2U, ST_MODULE_TYPE_REED, 1000U) == 0);
    st_hotswap_module_binding_detach(&binding, 2U, 2000U);
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 2U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 1U);
    EXPECT(cleared[0] == ST_SENSOR_CONTACT);
    return 0;
}

static int test_detach_reports_both_channels_of_multichannel_module(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_sensor_kind_t cleared[ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT];
    uint8_t count;
    int saw_temperature = 0;
    int saw_humidity = 0;
    uint8_t i;

    make_binding(&binding, &registry, &fixture);

    /* SHT41 reports two capabilities (temperature + humidity) off one
     * physical port -- confirms detach() walks every attached channel,
     * not just the first, when reverse-mapping port -> capabilities.
     *
     * st_sensor_registry_port_sensor_kind() reads port->metadata, which
     * st_sensor_registry_attach() deliberately leaves empty until a real
     * probe cycle completes (see test_detach_before_first_probe_reports_
     * no_capabilities below for that exact edge case). Driving that via
     * st_sensor_registry_tick() would require this file's fake I2C bus to
     * pass SHT41's real CRC-validated serial-read protocol, which it
     * isn't built to do (it returns all-zero reads) and isn't what this
     * test exists to exercise -- SHT41's own driver tests already cover
     * that protocol. Seeding metadata directly reaches the same state a
     * real successful probe would have left, without coupling this test
     * to unrelated wire-protocol detail. */
    EXPECT(st_hotswap_module_binding_attach(&binding, 0U, ST_MODULE_TYPE_SHT41, 1000U) == 0);
    strcpy(registry.ports[0].metadata.sensor_id, "sht41_temperature");
    registry.ports[0].metadata.sensor_kind = ST_SENSOR_TEMPERATURE_C;
    strcpy(registry.ports[1].metadata.sensor_id, "sht41_humidity");
    registry.ports[1].metadata.sensor_kind = ST_SENSOR_RELATIVE_HUMIDITY_PERCENT;

    st_hotswap_module_binding_detach(&binding, 0U, 2000U);
    count = st_hotswap_module_binding_take_cleared_capabilities(
        &binding, 0U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT);
    EXPECT(count == 2U);
    for (i = 0U; i < count; ++i) {
        if (cleared[i] == ST_SENSOR_TEMPERATURE_C) {
            saw_temperature = 1;
        }
        if (cleared[i] == ST_SENSOR_RELATIVE_HUMIDITY_PERCENT) {
            saw_humidity = 1;
        }
    }
    EXPECT(saw_temperature == 1);
    EXPECT(saw_humidity == 1);
    return 0;
}

static int test_detach_before_first_probe_reports_no_capabilities(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_sensor_kind_t cleared[ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT];

    make_binding(&binding, &registry, &fixture);

    /* Edge case: a module unplugged before its very first probe cycle
     * ever completes has no populated metadata to reverse-map, so
     * detach() reports 0 capabilities to suspend. This is harmless in
     * practice -- no reading ever reached the alarm engine for it either,
     * so there is nothing active to leave stale -- but is worth having
     * pinned down explicitly rather than discovered by surprise. */
    EXPECT(st_hotswap_module_binding_attach(&binding, 0U, ST_MODULE_TYPE_SHT41, 1000U) == 0);
    st_hotswap_module_binding_detach(&binding, 0U, 1500U);
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 0U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 0U);
    return 0;
}

static int test_take_cleared_capabilities_rejects_invalid_args(void)
{
    st_hotswap_module_binding_t binding;
    st_sensor_registry_t registry;
    fake_binding_fixture_t fixture;
    st_sensor_kind_t cleared[ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT];

    make_binding(&binding, &registry, &fixture);

    EXPECT(st_hotswap_module_binding_attach(&binding, 1U, ST_MODULE_TYPE_PIR, 1000U) == 0);
    st_hotswap_module_binding_detach(&binding, 1U, 2000U);

    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               NULL, 1U, cleared, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 0U);
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, ST_HOTSWAP_BINDING_MAX_PORTS, cleared,
               ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 0U);
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 1U, NULL, ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT) == 0U);
    EXPECT(st_hotswap_module_binding_take_cleared_capabilities(
               &binding, 1U, cleared, 0U) == 0U);
    return 0;
}

int st_run_hotswap_module_binding_tests(void)
{
    int failures = 0;

    failures += test_pir_attach_and_detach();
    failures += test_reed_attach_and_get_wrong_port();
    failures += test_registry_backed_type_attaches_and_detaches();
    failures += test_ds18b20_requests_onewire_bus_for_its_own_port();
    failures += test_ds18b20_fails_cleanly_without_onewire_provider();
    failures += test_unknown_and_empty_never_attach();
    failures += test_bus_probe_addresses_match_contract();
    failures += test_bus_probe_delegates_to_i2c_probe_callback();
    failures += test_bus_probe_propagates_mismatch();
    failures += test_detach_reports_pir_capability();
    failures += test_detach_reports_reed_capability();
    failures += test_detach_reports_both_channels_of_multichannel_module();
    failures += test_detach_before_first_probe_reports_no_capabilities();
    failures += test_take_cleared_capabilities_rejects_invalid_args();

    if (failures == 0) {
        printf("test_hotswap_module_binding: all tests passed\n");
    }
    return failures;
}
