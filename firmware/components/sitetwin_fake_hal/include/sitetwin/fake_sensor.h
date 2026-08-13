#ifndef SITETWIN_FAKE_SENSOR_H
#define SITETWIN_FAKE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "sitetwin/sensor_driver.h"

typedef struct {
    bool present;
    bool fail_next_sample;
    uint64_t warm_until_ms;
    float value;
    float increment_per_sample;
    uint32_t quality_flags;
    st_module_metadata_t metadata;
} st_fake_sensor_t;

void st_fake_sensor_init(st_fake_sensor_t *sensor, const char *sensor_id, const char *module_uid,
                         st_sensor_kind_t kind, st_unit_t unit, uint32_t sample_interval_ms,
                         float initial_value);
st_sensor_driver_t st_fake_sensor_driver(st_fake_sensor_t *sensor);

#endif
