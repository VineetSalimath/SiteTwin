#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sitetwin/board_port_manager.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                         \
    } while (0)

/* --- fake board ops: caller pushes a scripted sequence of identities per port --- */

typedef struct {
    st_module_identity_t scripted[8];
    size_t scripted_count;
    size_t next_index;
} fake_port_script_t;

typedef struct {
    fake_port_script_t ports[ST_BOARD_PORT_MANAGER_MAX_PORTS];
    size_t port_count;
    size_t select_bus_calls[ST_BOARD_PORT_MANAGER_MAX_PORTS];
} fake_board_t;

static st_module_identity_t make_identity(st_module_id_status_t status,
                                          st_module_type_t type, uint16_t mv)
{
    st_module_identity_t identity;
    memset(&identity, 0, sizeof(identity));
    identity.status = status;
    identity.module_type = type;
    identity.millivolts = mv;
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

/* Fills the remaining unused slots by repeating the last pushed reading,
 * so poll() beyond the scripted sequence keeps returning a steady value. */
static st_module_identity_t last_or_empty(const fake_port_script_t *script)
{
    if (script->scripted_count == 0U) {
        return make_identity(ST_MODULE_ID_EMPTY, ST_MODULE_TYPE_EMPTY, 3300U);
    }
    return script->scripted[script->scripted_count - 1U];
}

static size_t fake_port_count(void *context)
{
    fake_board_t *board = (fake_board_t *)context;
    return board->port_count;
}

static st_hal_result_t fake_read_module_id(void *context, size_t port_index,
                                           st_module_identity_t *identity)
{
    fake_board_t *board = (fake_board_t *)context;
    fake_port_script_t *script = &board->ports[port_index];

    if (script->next_index < script->scripted_count) {
        *identity = script->scripted[script->next_index++];
    } else {
        *identity = last_or_empty(script);
    }
    return ST_HAL_OK;
}

static st_hal_result_t fake_select_bus(void *context, size_t port_index)
{
    fake_board_t *board = (fake_board_t *)context;
    board->select_bus_calls[port_index]++;
    return ST_HAL_OK;
}

static st_hal_result_t fake_detect_present(void *context, size_t port_index, bool *present)
{
    (void)context;
    (void)port_index;
    *present = true;
    return ST_HAL_OK;
}

/* --- fake callbacks: record attach/detach/probe invocations --- */

typedef struct {
    int attach_calls;
    int detach_calls;
    int probe_calls;
    size_t last_attach_port;
    st_module_type_t last_attach_type;
    size_t last_detach_port;
    int attach_should_fail;
    st_hal_result_t probe_result;
} fake_callback_log_t;

static int fake_attach(void *context, size_t port_index, st_module_type_t module_type)
{
    fake_callback_log_t *log = (fake_callback_log_t *)context;
    log->attach_calls++;
    log->last_attach_port = port_index;
    log->last_attach_type = module_type;
    return log->attach_should_fail ? -1 : 0;
}

static void fake_detach(void *context, size_t port_index)
{
    fake_callback_log_t *log = (fake_callback_log_t *)context;
    log->detach_calls++;
    log->last_detach_port = port_index;
}

static bool fake_uses_bus_probe(void *context, st_module_type_t module_type, uint8_t *address)
{
    (void)context;
    if (module_type == ST_MODULE_TYPE_SHT41) {
        *address = 0x44U;
        return true;
    }
    return false;
}

static st_hal_result_t fake_bus_probe(void *context, size_t port_index, uint8_t expected_address)
{
    fake_callback_log_t *log = (fake_callback_log_t *)context;
    (void)port_index;
    (void)expected_address;
    log->probe_calls++;
    return log->probe_result;
}

/* --- tests --- */

static int test_insert_requires_stable_scans(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));

    /* One noisy UNKNOWN reading, then two consistent SHT41 readings. With
     * stable_scan_count = 2, only the second SHT41 reading should attach. */
    script_push(&board, 0, make_identity(ST_MODULE_ID_UNCLASSIFIED, ST_MODULE_TYPE_UNKNOWN, 1900U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SHT41, 620U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SHT41, 622U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;
    ops.port_detect_present = fake_detect_present;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;

    config.stable_scan_count = 2U;

    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager); /* UNKNOWN reading #1 -> not stable yet, no attach */
    EXPECT(log.attach_calls == 0);
    st_board_port_manager_poll(&manager); /* SHT41 reading #1 -> pending resets, count=1 */
    EXPECT(log.attach_calls == 0);
    st_board_port_manager_poll(&manager); /* SHT41 reading #2 -> stable, should attach now */
    EXPECT(log.attach_calls == 1);
    EXPECT(log.last_attach_type == ST_MODULE_TYPE_SHT41);
    EXPECT(log.last_attach_port == 0U);

    const st_board_port_state_t *state = st_board_port_manager_get_state(&manager, 0U);
    EXPECT(state != NULL);
    EXPECT(state->lifecycle == ST_MODULE_ACTIVE);
    EXPECT(state->committed_type == ST_MODULE_TYPE_SHT41);

    return 0;
}

static int test_removal_detaches(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));

    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_BH1750, 1975U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_BH1750, 1978U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_EMPTY, ST_MODULE_TYPE_EMPTY, 3300U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_EMPTY, ST_MODULE_TYPE_EMPTY, 3300U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);
    EXPECT(log.attach_calls == 1);
    EXPECT(log.detach_calls == 0);

    st_board_port_manager_poll(&manager); /* EMPTY #1 -> not stable yet */
    EXPECT(log.detach_calls == 0);
    st_board_port_manager_poll(&manager); /* EMPTY #2 -> stable -> detach */
    EXPECT(log.detach_calls == 1);
    EXPECT(log.last_detach_port == 0U);

    const st_board_port_state_t *state = st_board_port_manager_get_state(&manager, 0U);
    EXPECT(state->lifecycle == ST_MODULE_EMPTY);
    EXPECT(state->committed_type == ST_MODULE_TYPE_EMPTY);

    return 0;
}

static int test_unknown_id_never_attaches(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));

    script_push(&board, 0, make_identity(ST_MODULE_ID_UNCLASSIFIED, ST_MODULE_TYPE_UNKNOWN, 2050U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_UNCLASSIFIED, ST_MODULE_TYPE_UNKNOWN, 2051U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_UNCLASSIFIED, ST_MODULE_TYPE_UNKNOWN, 2049U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);

    EXPECT(log.attach_calls == 0);
    const st_board_port_state_t *state = st_board_port_manager_get_state(&manager, 0U);
    EXPECT(state->lifecycle == ST_MODULE_FAULTED);
    EXPECT(state->fault_reason == ST_PORT_FAULT_UNCLASSIFIED_ID);
    EXPECT(state->committed_type == ST_MODULE_TYPE_EMPTY);

    return 0;
}

static int test_bus_mismatch_faults_instead_of_attaching(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));
    log.probe_result = ST_HAL_NOT_PRESENT; /* simulate I2C NACK */

    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SHT41, 618U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SHT41, 621U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;
    callbacks.module_uses_bus_probe = fake_uses_bus_probe;
    callbacks.bus_probe = fake_bus_probe;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);

    EXPECT(log.probe_calls == 1);
    EXPECT(log.attach_calls == 0);
    EXPECT(board.select_bus_calls[0] == 1U);

    const st_board_port_state_t *state = st_board_port_manager_get_state(&manager, 0U);
    EXPECT(state->lifecycle == ST_MODULE_FAULTED);
    EXPECT(state->fault_reason == ST_PORT_FAULT_BUS_MISMATCH);

    return 0;
}

static int test_type_swap_detaches_old_before_attaching_new(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));

    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SCD41, 840U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_SCD41, 843U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_INA219, 2930U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_INA219, 2933U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);
    EXPECT(log.attach_calls == 1);
    EXPECT(log.last_attach_type == ST_MODULE_TYPE_SCD41);
    EXPECT(log.detach_calls == 0);

    st_board_port_manager_poll(&manager); /* INA219 #1 -> not stable yet, SCD41 stays committed */
    EXPECT(log.detach_calls == 0);
    st_board_port_manager_poll(&manager); /* INA219 #2 -> stable -> detach SCD41, attach INA219 */
    EXPECT(log.detach_calls == 1);
    EXPECT(log.attach_calls == 2);
    EXPECT(log.last_attach_type == ST_MODULE_TYPE_INA219);

    return 0;
}

static int test_attach_failure_marks_faulted_without_retry_storm(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));
    log.attach_should_fail = 1;

    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_REED, 2260U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_REED, 2262U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);
    EXPECT(log.attach_calls == 1);

    /* Reading stays REED and already-committed (well, faulted) -- must not
     * call attach() again every poll once the identity stops changing. */
    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);
    EXPECT(log.attach_calls == 1);

    const st_board_port_state_t *state = st_board_port_manager_get_state(&manager, 0U);
    EXPECT(state->lifecycle == ST_MODULE_FAULTED);
    EXPECT(state->fault_reason == ST_PORT_FAULT_ATTACH_FAILED);

    return 0;
}

static int test_transient_hal_error_does_not_drop_active_port(void)
{
    fake_board_t board;
    fake_callback_log_t log;
    st_board_port_ops_t ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_manager_t manager;

    memset(&board, 0, sizeof(board));
    board.port_count = 1U;
    memset(&log, 0, sizeof(log));

    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_ADXL345, 2620U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_ADXL345, 2623U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_MEASUREMENT_ERROR, ST_MODULE_TYPE_UNKNOWN, 0U));
    script_push(&board, 0, make_identity(ST_MODULE_ID_PROVISIONAL_MATCH, ST_MODULE_TYPE_ADXL345, 2621U));

    memset(&ops, 0, sizeof(ops));
    ops.context = &board;
    ops.port_count = fake_port_count;
    ops.port_read_module_id = fake_read_module_id;
    ops.port_select_bus = fake_select_bus;

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &log;
    callbacks.attach = fake_attach;
    callbacks.detach = fake_detach;

    config.stable_scan_count = 2U;
    EXPECT(st_board_port_manager_init(&manager, &ops, &callbacks, &config) == 0);

    st_board_port_manager_poll(&manager);
    st_board_port_manager_poll(&manager);
    EXPECT(log.attach_calls == 1);

    st_board_port_manager_poll(&manager); /* MEASUREMENT_ERROR -> ignored, no detach */
    EXPECT(log.detach_calls == 0);
    const st_board_port_state_t *state = st_board_port_manager_get_state(&manager, 0U);
    EXPECT(state->lifecycle == ST_MODULE_ACTIVE);
    EXPECT(state->committed_type == ST_MODULE_TYPE_ADXL345);

    st_board_port_manager_poll(&manager); /* back to ADXL345 -> still just 1 stable count, stays active */
    EXPECT(log.detach_calls == 0);
    EXPECT(log.attach_calls == 1);

    return 0;
}

int st_run_board_port_manager_tests(void)
{
    int failures = 0;

    failures += test_insert_requires_stable_scans();
    failures += test_removal_detaches();
    failures += test_unknown_id_never_attaches();
    failures += test_bus_mismatch_faults_instead_of_attaching();
    failures += test_type_swap_detaches_old_before_attaching_new();
    failures += test_attach_failure_marks_faulted_without_retry_storm();
    failures += test_transient_hal_error_does_not_drop_active_port();

    if (failures == 0) {
        printf("test_board_port_manager: all tests passed\n");
    }
    return failures;
}
