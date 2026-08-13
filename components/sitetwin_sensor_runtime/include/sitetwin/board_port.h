#ifndef SITETWIN_BOARD_PORT_H
#define SITETWIN_BOARD_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sitetwin/sensor_hal.h"

#define ST_MODULE_ID_DATA_MAX 16U

/*
 * Board-intent boundary only: I1 does not provide an implementation of these
 * operations. The final board contract uses four universal ports, a
 * CD74HC4052M96 DATA mux, and DATA_COMMON on ESP32-C6 GPIO3. Identification,
 * mux selection, insertion/removal handling, and hot-swap sequencing remain
 * feature-gated until their electrical validation is complete.
 */

typedef struct {
    uint8_t data[ST_MODULE_ID_DATA_MAX];
    uint8_t length;
} st_module_identity_t;

typedef struct {
    void *context;
    size_t (*port_count)(void *context);
    st_hal_result_t (*port_detect_present)(void *context, size_t port_index, bool *present);
    st_hal_result_t (*port_power_set)(void *context, size_t port_index, bool enabled);
    st_hal_result_t (*port_read_module_id)(void *context, size_t port_index,
                                           st_module_identity_t *identity);
    st_hal_result_t (*port_select_bus)(void *context, size_t port_index);
    st_hal_result_t (*port_enable_bus)(void *context, size_t port_index);
    st_hal_result_t (*port_disable_bus)(void *context, size_t port_index);
    st_hal_result_t (*port_clear_fault)(void *context, size_t port_index);
} st_board_port_ops_t;

typedef enum {
    ST_MODULE_EMPTY = 0,
    ST_MODULE_PRESENT_UNIDENTIFIED,
    ST_MODULE_POWER_SETTLING,
    ST_MODULE_IDENTIFYING,
    ST_MODULE_PROBING,
    ST_MODULE_WARMING_UP,
    ST_MODULE_ACTIVE,
    ST_MODULE_FAULTED,
    ST_MODULE_REMOVED
} st_module_lifecycle_state_t;

#endif
