#include "sitetwin/board_port_manager.h"

#include <string.h>

static void reset_port_state(st_board_port_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->lifecycle = ST_MODULE_EMPTY;
    state->committed_type = ST_MODULE_TYPE_EMPTY;
    state->resolved_type = ST_MODULE_TYPE_EMPTY;
    state->pending_type = ST_MODULE_TYPE_EMPTY;
    state->fault_reason = ST_PORT_FAULT_NONE;
}

int st_board_port_manager_init(st_board_port_manager_t *manager,
                                const st_board_port_ops_t *ops,
                                const st_board_port_manager_callbacks_t *callbacks,
                                const st_board_port_manager_config_t *config)
{
    size_t port_count;
    size_t index;

    if (manager == NULL || ops == NULL || ops->port_count == NULL ||
        ops->port_read_module_id == NULL || config == NULL ||
        config->stable_scan_count == 0U) {
        return -1;
    }

    port_count = ops->port_count(ops->context);
    if (port_count == 0U || port_count > ST_BOARD_PORT_MANAGER_MAX_PORTS) {
        return -1;
    }

    memset(manager, 0, sizeof(*manager));
    manager->ops = ops;
    if (callbacks != NULL) {
        manager->callbacks = *callbacks;
    }
    manager->config = *config;
    manager->port_count = port_count;

    for (index = 0U; index < port_count; ++index) {
        reset_port_state(&manager->ports[index]);
    }
    return 0;
}

static void detach_if_committed(st_board_port_manager_t *manager, size_t index,
                                uint64_t now_ms)
{
    st_board_port_state_t *state = &manager->ports[index];

    if (state->committed_type != ST_MODULE_TYPE_EMPTY &&
        state->committed_type != ST_MODULE_TYPE_UNKNOWN) {
        if (manager->callbacks.detach != NULL) {
            manager->callbacks.detach(manager->callbacks.context, index, now_ms);
        }
    }
    state->committed_type = ST_MODULE_TYPE_EMPTY;
}

static void commit_observed_type(st_board_port_manager_t *manager, size_t index,
                                  st_module_type_t observed, uint64_t now_ms)
{
    st_board_port_state_t *state = &manager->ports[index];

    /* Record this reading as resolved *before* acting on it, so a
     * persistent fault (unclassified ID, bus mismatch, failed attach)
     * does not re-run attach()/bus_probe() on every subsequent poll --
     * only a genuine change in the stable reading reopens this path. */
    state->resolved_type = observed;

    /* Anything previously attached is torn down before we act on the new,
     * now-stable reading -- this covers plain removal, a straight swap to
     * a different module, and recovery out of a prior fault uniformly. */
    detach_if_committed(manager, index, now_ms);

    if (observed == ST_MODULE_TYPE_EMPTY) {
        state->lifecycle = ST_MODULE_EMPTY;
        state->fault_reason = ST_PORT_FAULT_NONE;
        return;
    }

    if (observed == ST_MODULE_TYPE_UNKNOWN) {
        /* Contract fault model: preserve the diagnostic reading, never
         * silently treat an out-of-band voltage as the nearest module. */
        state->lifecycle = ST_MODULE_FAULTED;
        state->fault_reason = ST_PORT_FAULT_UNCLASSIFIED_ID;
        return;
    }

    state->lifecycle = ST_MODULE_PROBING;
    {
        uint8_t expected_address = 0U;
        bool needs_probe = manager->callbacks.module_uses_bus_probe != NULL &&
                           manager->callbacks.module_uses_bus_probe(
                               manager->callbacks.context, observed, &expected_address);

        if (needs_probe) {
            st_hal_result_t probe_result = ST_HAL_OK;

            if (manager->ops->port_select_bus != NULL) {
                /* Best effort: if selection itself fails the probe below
                 * will very likely also fail and be caught there. */
                (void)manager->ops->port_select_bus(manager->ops->context, index);
            }
            probe_result = (manager->callbacks.bus_probe != NULL)
                               ? manager->callbacks.bus_probe(manager->callbacks.context,
                                                              index, expected_address)
                               : ST_HAL_OK;
            if (probe_result != ST_HAL_OK) {
                state->lifecycle = ST_MODULE_FAULTED;
                state->fault_reason = ST_PORT_FAULT_BUS_MISMATCH;
                return;
            }
        }
    }

    {
        int attach_result = (manager->callbacks.attach != NULL)
                                ? manager->callbacks.attach(manager->callbacks.context,
                                                            index, observed, now_ms)
                                : -1;
        if (attach_result != 0) {
            state->lifecycle = ST_MODULE_FAULTED;
            state->fault_reason = ST_PORT_FAULT_ATTACH_FAILED;
            return;
        }
    }

    state->lifecycle = ST_MODULE_ACTIVE;
    state->fault_reason = ST_PORT_FAULT_NONE;
    state->committed_type = observed;
}

static void poll_port(st_board_port_manager_t *manager, size_t index, uint64_t now_ms)
{
    st_board_port_state_t *state = &manager->ports[index];
    st_module_identity_t identity;
    st_hal_result_t result;

    result = manager->ops->port_read_module_id(manager->ops->context, index, &identity);
    if (result != ST_HAL_OK || identity.status == ST_MODULE_ID_MEASUREMENT_ERROR) {
        /* Transient HAL/measurement error: do not act on it, and do not
         * let it count toward (or break) an in-progress debounce run.
         * An already-ACTIVE port stays ACTIVE across a transient error. */
        state->pending_count = 0U;
        return;
    }

    state->last_identity = identity;

    if (identity.module_type == state->pending_type) {
        if (state->pending_count < UINT32_MAX) {
            state->pending_count++;
        }
    } else {
        state->pending_type = identity.module_type;
        state->pending_count = 1U;
    }

    if (state->pending_count < manager->config.stable_scan_count) {
        /* Not stable yet -- reflect the in-progress transition for
         * observability, but do not attach/detach/fault anything yet. */
        if (state->committed_type == ST_MODULE_TYPE_EMPTY) {
            state->lifecycle = (identity.module_type == ST_MODULE_TYPE_EMPTY)
                                   ? ST_MODULE_EMPTY
                                   : ST_MODULE_PRESENT_UNIDENTIFIED;
        }
        return;
    }

    if (identity.module_type == state->resolved_type) {
        return; /* stable and already handled (attached or faulted): nothing to do */
    }

    commit_observed_type(manager, index, identity.module_type, now_ms);
}

void st_board_port_manager_poll(st_board_port_manager_t *manager, uint64_t now_ms)
{
    size_t index;

    if (manager == NULL || manager->ops == NULL) {
        return;
    }
    for (index = 0U; index < manager->port_count; ++index) {
        poll_port(manager, index, now_ms);
    }
}

const st_board_port_state_t *st_board_port_manager_get_state(
    const st_board_port_manager_t *manager, size_t port_index)
{
    if (manager == NULL || port_index >= manager->port_count) {
        return NULL;
    }
    return &manager->ports[port_index];
}
