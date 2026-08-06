#ifndef SITETWIN_PHYSICAL_MODULE_H
#define SITETWIN_PHYSICAL_MODULE_H

#include <stdint.h>

#include "sitetwin/contracts.h"
#include "sitetwin/sensor_driver.h"

#define ST_MODULE_MAX_CHANNELS 4U

typedef struct {
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    st_sensor_kind_t sensor_kind;
    st_unit_t unit;
} st_logical_channel_metadata_t;

typedef struct {
    char module_uid[ST_MODULE_UID_MAX_LEN];
    uint32_t sample_interval_ms;
    uint8_t channel_count;
    st_logical_channel_metadata_t channels[ST_MODULE_MAX_CHANNELS];
} st_physical_module_metadata_t;

/*
 * The owner must serialize probe/acquire/read/reset calls. SiteTwin's pod
 * acquisition task is the single owner in the ESP-IDF composition. A future
 * multi-task scheduler must place the same lock around the whole operation,
 * not only around individual I2C calls.
 */
typedef struct {
    void *context;
    st_driver_result_t (*probe)(void *context, st_physical_module_metadata_t *metadata);
    st_driver_result_t (*acquire)(void *context, uint64_t now_ms);
    st_driver_result_t (*read_channel)(void *context, uint8_t channel_index,
                                       st_driver_sample_t *sample);
    void (*reset)(void *context);
} st_physical_module_driver_t;

#endif
