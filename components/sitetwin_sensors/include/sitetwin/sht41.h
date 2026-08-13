#ifndef SITETWIN_SHT41_H
#define SITETWIN_SHT41_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"

#define ST_SHT41_DEFAULT_ADDRESS 0x44U
#define ST_SHT41_CHANNEL_COUNT 2U
#define ST_SHT41_TEMPERATURE_CHANNEL 0U
#define ST_SHT41_HUMIDITY_CHANNEL 1U

typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    uint32_t sample_interval_ms;
    uint32_t cache_validity_ms;
    const char *temperature_sensor_id;
    const char *humidity_sensor_id;
} st_sht41_config_t;

typedef enum {
    ST_SHT41_IDLE = 0,
    ST_SHT41_MEASURING,
    ST_SHT41_CACHED_RESULT
} st_sht41_state_t;

typedef struct {
    st_sht41_config_t config;
    st_physical_module_metadata_t metadata;
    st_sht41_state_t state;
    uint64_t ready_at_ms;
    uint64_t cache_expires_at_ms;
    uint64_t current_acquired_at_ms;
    uint64_t last_valid_at_ms;
    uint64_t last_attempt_at_ms;
    float current_values[ST_SHT41_CHANNEL_COUNT];
    float last_valid_values[ST_SHT41_CHANNEL_COUNT];
    uint32_t current_quality_flags;
    st_driver_result_t current_failure_result;
    uint32_t measurement_commands;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t has_last_valid;
    uint8_t consecutive_failures;
    uint8_t missing_reported;
} st_sht41_t;

int st_sht41_init(st_sht41_t *sensor, const st_sht41_config_t *config);
st_physical_module_driver_t st_sht41_module_driver(st_sht41_t *sensor);

uint8_t st_sht41_crc8(const uint8_t data[2]);
float st_sht41_raw_temperature_c(uint16_t raw);
float st_sht41_raw_humidity_percent(uint16_t raw);
uint32_t st_sht41_measurement_command_count(const st_sht41_t *sensor);
uint64_t st_sht41_last_attempt_at_ms(const st_sht41_t *sensor);

#endif
