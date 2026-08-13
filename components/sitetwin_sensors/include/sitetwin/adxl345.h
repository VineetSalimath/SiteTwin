#ifndef SITETWIN_ADXL345_H
#define SITETWIN_ADXL345_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"

#define ST_ADXL345_DEFAULT_ADDRESS 0x53U
#define ST_ADXL345_ALT_ADDRESS 0x1DU
#define ST_ADXL345_DEVICE_ID 0xE5U
#define ST_ADXL345_FIFO_CAPACITY 32U
#define ST_ADXL345_CHANNEL_COUNT 1U
#define ST_ADXL345_VIBRATION_CHANNEL 0U
#define ST_ADXL345_DEFAULT_G_PER_LSB 0.0039F

typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    uint8_t range_g;
    uint8_t rate_code;
    uint8_t minimum_window_samples;
    uint32_t sample_interval_ms;
    float g_per_lsb;
    const char *vibration_sensor_id;
} st_adxl345_config_t;

typedef struct {
    st_adxl345_config_t config;
    st_physical_module_metadata_t metadata;
    float current_vibration_rms_g;
    uint32_t current_quality_flags;
    uint64_t current_acquired_at_ms;
    uint32_t windows_completed;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t current_sample_count;
} st_adxl345_t;

int st_adxl345_init(st_adxl345_t *sensor, const st_adxl345_config_t *config);
st_physical_module_driver_t st_adxl345_module_driver(st_adxl345_t *sensor);

/* Computes orientation-independent vector RMS after removing the per-axis
 * mean (and therefore static gravity/mounting orientation) from a window. */
float st_adxl345_window_rms_g(const int16_t *xyz, uint8_t sample_count,
                              float g_per_lsb);
uint32_t st_adxl345_windows_completed(const st_adxl345_t *sensor);

#endif
