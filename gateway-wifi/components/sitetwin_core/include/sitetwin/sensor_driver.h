#ifndef SITETWIN_SENSOR_DRIVER_H
#define SITETWIN_SENSOR_DRIVER_H

#include <stdint.h>

#include "sitetwin/contracts.h"

typedef enum {
    ST_DRIVER_READY = 0,
    ST_DRIVER_NOT_PRESENT,
    ST_DRIVER_RETRY,
    ST_DRIVER_ERROR
} st_driver_result_t;

typedef struct {
    float value;
    st_unit_t unit;
    uint32_t quality_flags;
} st_driver_sample_t;

typedef struct {
    void *context;
    st_driver_result_t (*probe)(void *context, st_module_metadata_t *metadata);
    st_driver_result_t (*sample)(void *context, uint64_t now_ms, st_driver_sample_t *sample);
} st_sensor_driver_t;

#endif
