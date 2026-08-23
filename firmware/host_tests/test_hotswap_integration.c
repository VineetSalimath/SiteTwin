#include <stdio.h>
#include <string.h>

#include "sitetwin/board_port_manager.h"
#include "sitetwin/hotswap_module_binding.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                         \
    } while (0)

/* A minimal scripted fake board (same style as test_board_port_manager.c)
 * standing in for the final PCB board_port_ops_t layer, so this test
 * exercises the REAL board_port_manager wired directly to the REAL
 * hotswap_module_binding -- neither side is mocked here, only the
 * lowest-level electrical HAL is. */

typedef struct {
    st_module_identity_t scripted[6];
    size_t scripted_count;
    size_t next_index;
} fake_port_script_t;

typedef struct {
    fake_port_script_t ports[ST_BOARD_PORT_MANAGER_MAX_PORTS];
    size_t port_count;
} fake_board_t;

static st_module_identity_t make_identity(st_module_id_status_t status, st_module_type_t type)
{
    st_module_identity_t identity;
    memset(&identity, 0, sizeof(identity));
    identity.status = status;
    identity.module_type = type;
    identity.sample_count = 3U;
    identity.voltage_calibrated = true;
    return identity;
}

static void script_push(fake_board_t *board, size_t port, st_module_identity_t identity)
{
    fake_port_script_t *script = &board->ports[port];
    if (script->scripted_count < (sizeof(script->scripted) / sizeof(script->scripted[0]))) {
        script->scripted[script->scripted_count++] = identity;
    }
}

static st_module_identity_t last_or_empty(const fake_port_script_t *script)
{
    if (script->scripted_count == 0U) {
        return make_identity(ST_MODULE_ID_EMPTY, ST_MODULE_TYPE_EMPTY);
    }
    return script->scripted[script->scripted_count - 1U];
}

static size_t fake_port_count(void *context)
{
    return ((fake_board_t *)context)->port_count;
}

static st_hal_result_t fake_read_module_id(void *context, size_t port_index,
                                           st_module_identity_t *identity)
{
    fake_board_t *board = (fake_board_t *)context;
    fake_port_script_t *script = &board->ports[port_index];

    *identity = (script->next_index < script->scripted_count)
                    ? script->scripted[script->next_index++]
                    : last_or_empty(script);
    return ST_HAL_OK;
}

static st_hal_result_t fake_select_bus(void *context, size_t port_index)
{
    (void)context;
    (void)port_index;
    return ST_HAL_OK;
}

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
static st_hal_result_t fake_i2c_probe(void *context, uint8_t address)
{
    (void)context;
    (void)address;
    return ST_HAL_OK; /* everything ACKs in this test */
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

static int test_full_stack_insert_then_remove(void)
{
    fake_board_t board;
    st_board_port_ops_t ops;
    st_sensor_registry_t registry;
    st_hotswap_module_binding_t binding;
    st_hotswap_binding_io_t io;
    st_board_port_manager_t manager;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_pir_t *pir = NULL;
    const char *pir_sensor_id = NULL;

    memset(&board, 0, sizeof(board));
    board.port_count = 4U;

    /* Port 0: SHT41 inserted, stays. Port 1: PIR inserted, stays. */
    script_push(&board, 0U, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SHT41));
    script_push(&board, 0U, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SHT41));
    script_push(&board, 1U, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_PIR));
    script_push(&board, 1U, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_PIR));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    st_sensor_registry_init(&registry, "TEST_POD", 1U);
    memset(&io, 0, sizeof(io));
    io.i2c_bus.write = fake_i2c_write;
    io.i2c_bus.read = fake_i2c_read;
    io.i2c_probe = fake_i2c_probe;
    io.i2c_probe_context = NULL;
    io.data_common_bus_for_port = NULL; /* no DS18B20 exercised in this test */
    EXPECT(st_hotswap_module_binding_init(&binding, &io, &registry,
                                          ST_HOTSWAP_BINDING_MAX_PORTS) == 0);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &binding;
    callbacks.attach = st_hotswap_module_binding_attach;
    callbacks.detach = st_hotswap_module_binding_detach;
    callbacks.module_uses_bus_probe = st_hotswap_module_binding_uses_bus_probe;
    callbacks.bus_probe = st_hotswap_module_binding_bus_probe;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    /* Two polls to satisfy the stable-scan requirement for both ports. */
    st_board_port_manager_poll(&manager, 10000U);
    st_board_port_manager_poll(&manager, 10050U);

    EXPECT(st_board_port_manager_get_state(&manager, 0U)->lifecycle == ST_MODULE_ACTIVE);
    EXPECT(count_attached_registry_slots(&registry) == 2U); /* SHT41's temperature + humidity */

    EXPECT(st_board_port_manager_get_state(&manager, 1U)->lifecycle == ST_MODULE_ACTIVE);
    EXPECT(st_hotswap_binding_get_pir(&binding, 1U, &pir, &pir_sensor_id) == 1);
    EXPECT(pir != NULL);
    EXPECT(strcmp(pir_sensor_id, "pir_motion") == 0);

    /* Now remove the SHT41 (port 0 goes empty) and confirm the registry
     * slots are actually torn down through the full manager -> binding ->
     * module_instance -> registry chain, not just locally in one layer. */
    script_push(&board, 0U, make_identity(ST_MODULE_ID_EMPTY, ST_MODULE_TYPE_EMPTY));
    script_push(&board, 0U, make_identity(ST_MODULE_ID_EMPTY, ST_MODULE_TYPE_EMPTY));
    st_board_port_manager_poll(&manager, 10100U);
    st_board_port_manager_poll(&manager, 10150U);

    EXPECT(st_board_port_manager_get_state(&manager, 0U)->lifecycle == ST_MODULE_EMPTY);
    EXPECT(count_attached_registry_slots(&registry) == 0U); /* SHT41's 2 slots freed */
    /* Port 1's PIR must be completely unaffected by port 0's removal. */
    EXPECT(st_hotswap_binding_get_pir(&binding, 1U, &pir, &pir_sensor_id) == 1);

    return 0;
}

static int test_unclassified_reading_never_reaches_the_registry(void)
{
    fake_board_t board;
    st_board_port_ops_t ops;
    st_sensor_registry_t registry;
    st_hotswap_module_binding_t binding;
    st_hotswap_binding_io_t io;
    st_board_port_manager_t manager;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;

    memset(&board, 0, sizeof(board));
    board.port_count = 4U;
    script_push(&board, 2U, make_identity(ST_MODULE_ID_UNCLASSIFIED, ST_MODULE_TYPE_UNKNOWN));
    script_push(&board, 2U, make_identity(ST_MODULE_ID_UNCLASSIFIED, ST_MODULE_TYPE_UNKNOWN));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    st_sensor_registry_init(&registry, "TEST_POD", 1U);
    memset(&io, 0, sizeof(io));
    io.i2c_bus.write = fake_i2c_write;
    io.i2c_bus.read = fake_i2c_read;
    io.i2c_probe = fake_i2c_probe;
    EXPECT(st_hotswap_module_binding_init(&binding, &io, &registry,
                                          ST_HOTSWAP_BINDING_MAX_PORTS) == 0);

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &binding;
    callbacks.attach = st_hotswap_module_binding_attach;
    callbacks.detach = st_hotswap_module_binding_detach;
    callbacks.module_uses_bus_probe = st_hotswap_module_binding_uses_bus_probe;
    callbacks.bus_probe = st_hotswap_module_binding_bus_probe;
    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager, 1000U);
    st_board_port_manager_poll(&manager, 1050U);

    EXPECT(st_board_port_manager_get_state(&manager, 2U)->lifecycle == ST_MODULE_FAULTED);
    EXPECT(count_attached_registry_slots(&registry) == 0U);

    return 0;
}

int st_run_hotswap_integration_tests(void)
{
    int failures = 0;

    failures += test_full_stack_insert_then_remove();
    failures += test_unclassified_reading_never_reaches_the_registry();

    if (failures == 0) {
        printf("test_hotswap_integration: all tests passed\n");
    }
    return failures;
}
