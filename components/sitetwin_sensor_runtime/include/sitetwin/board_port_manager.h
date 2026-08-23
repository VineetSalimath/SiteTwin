#ifndef SITETWIN_BOARD_PORT_MANAGER_H
#define SITETWIN_BOARD_PORT_MANAGER_H

#include "sitetwin/board_port.h"

#define ST_BOARD_PORT_MANAGER_MAX_PORTS 4U

/*
 * This layer turns a raw st_board_port_ops_t (single-shot ID measurement)
 * into the stable-commit hot-swap behaviour described in the final PCB
 * contract: debounce/settle, classify, optional bus cross-check, and only
 * then attach or detach a logical module -- never on a single noisy
 * reading. It is deliberately independent of any concrete sensor driver;
 * callers supply attach/detach/bus_probe callbacks.
 */

typedef enum {
    ST_PORT_FAULT_NONE = 0,
    ST_PORT_FAULT_UNCLASSIFIED_ID,   /* voltage outside every known band */
    ST_PORT_FAULT_BUS_MISMATCH,      /* classified, but bus cross-check failed */
    ST_PORT_FAULT_ATTACH_FAILED,     /* classified and (if applicable) bus-checked, but the caller's attach() rejected it */
    ST_PORT_FAULT_HAL_ERROR          /* transient board-port HAL error; does not by itself detach an already-active port */
} st_port_fault_reason_t;

typedef struct {
    void *context;

    /*
     * Called once a module type has been stably identified and, if
     * applicable, bus-cross-checked. Return 0 on success. A non-zero
     * return marks the port FAULTED (ST_PORT_FAULT_ATTACH_FAILED) instead
     * of ACTIVE and the manager will retry attach again only after the
     * reading changes and re-stabilises (it will not hammer a failing
     * attach() every poll).
     */
    int (*attach)(void *context, size_t port_index, st_module_type_t module_type);

    /*
     * Called whenever a previously committed (non-EMPTY, non-UNKNOWN)
     * port type is about to change, including on physical removal. Must
     * be safe to call even if attach() was never actually reached for
     * this port.
     */
    void (*detach)(void *context, size_t port_index);

    /*
     * Optional. If non-NULL and it returns true for a newly classified
     * module_type, bus_probe() is called before attach() and a non-OK
     * result marks the port FAULTED (ST_PORT_FAULT_BUS_MISMATCH) instead
     * of attaching. Leave both NULL to skip cross-checking entirely.
     */
    bool (*module_uses_bus_probe)(void *context, st_module_type_t module_type,
                                   uint8_t *expected_address);
    st_hal_result_t (*bus_probe)(void *context, size_t port_index,
                                  uint8_t expected_address);
} st_board_port_manager_callbacks_t;

typedef struct {
    /* Consecutive identical scans required before a reading is acted on.
     * 2 is the contract's stated minimum ("stable double-scan commit"). */
    uint32_t stable_scan_count;
} st_board_port_manager_config_t;

typedef struct {
    st_module_lifecycle_state_t lifecycle;
    st_module_type_t committed_type;   /* ST_MODULE_TYPE_EMPTY unless a module is actually attached right now */
    st_module_type_t resolved_type;    /* last stable reading already acted on (attached, faulted, or empty) -- distinct from committed_type so a persistent fault does not retry attach()/bus_probe() every poll */
    st_module_type_t pending_type;
    uint32_t pending_count;
    st_port_fault_reason_t fault_reason;
    st_module_identity_t last_identity;
} st_board_port_state_t;

typedef struct {
    const st_board_port_ops_t *ops;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t config;
    st_board_port_state_t ports[ST_BOARD_PORT_MANAGER_MAX_PORTS];
    size_t port_count;
} st_board_port_manager_t;

/* Returns 0 on success. Fails if ops is NULL, ops->port_count() exceeds
 * ST_BOARD_PORT_MANAGER_MAX_PORTS, or stable_scan_count is 0. */
int st_board_port_manager_init(st_board_port_manager_t *manager,
                                const st_board_port_ops_t *ops,
                                const st_board_port_manager_callbacks_t *callbacks,
                                const st_board_port_manager_config_t *config);

/*
 * Polls every port once via ops->port_read_module_id, runs the result
 * through the debounce/stable-commit state machine, and calls
 * attach/detach as needed. Call this on whatever cadence the caller's
 * main loop uses (e.g. every SENSOR_INTERVAL_MS); the manager does not
 * sleep, block, or own any timer beyond the per-poll scan counters.
 */
void st_board_port_manager_poll(st_board_port_manager_t *manager);

const st_board_port_state_t *st_board_port_manager_get_state(
    const st_board_port_manager_t *manager, size_t port_index);

#endif
