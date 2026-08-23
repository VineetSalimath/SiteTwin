#ifndef SITETWIN_BOARD_PORT_H
#define SITETWIN_BOARD_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sitetwin/sensor_hal.h"

/*
 * Physical module identity reported by a board-port implementation. A
 * provisional match is useful for development diagnostics, but it is not a
 * calibrated final-PCB classification claim.
 */
typedef enum {
    ST_MODULE_TYPE_UNKNOWN = 0,
    ST_MODULE_TYPE_EMPTY,
    ST_MODULE_TYPE_SHT41,
    ST_MODULE_TYPE_SCD41,
    ST_MODULE_TYPE_PIR,
    ST_MODULE_TYPE_SGP40,
    ST_MODULE_TYPE_DS18B20,
    ST_MODULE_TYPE_BH1750,
    ST_MODULE_TYPE_REED,
    ST_MODULE_TYPE_ADXL345,
    ST_MODULE_TYPE_INA219
} st_module_type_t;

typedef enum {
    ST_MODULE_ID_UNCLASSIFIED = 0,
    ST_MODULE_ID_PROVISIONAL_MATCH,
    ST_MODULE_ID_EMPTY,
    ST_MODULE_ID_MEASUREMENT_ERROR
} st_module_id_status_t;

typedef struct {
    st_module_id_status_t status;
    st_module_type_t module_type;
    uint16_t raw_adc;
    uint16_t millivolts;
    uint8_t sample_count;
    bool voltage_calibrated;
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
