#ifndef SITETWIN_DS18B20_H
#define SITETWIN_DS18B20_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"

#define ST_DS18B20_CHANNEL_COUNT 1U
#define ST_DS18B20_TEMPERATURE_CHANNEL 0U
#define ST_DS18B20_SCRATCHPAD_SIZE 9U
#define ST_DS18B20_ROM_SIZE 8U

typedef struct {
    st_onewire_bus_t bus;
    uint8_t resolution_bits;
    uint32_t sample_interval_ms;
    uint32_t cache_validity_ms;
    const char *temperature_sensor_id;
} st_ds18b20_config_t;

typedef enum {
    ST_DS18B20_IDLE = 0,
    ST_DS18B20_CONVERTING,
    ST_DS18B20_CACHED_RESULT
} st_ds18b20_state_t;

typedef struct {
    st_ds18b20_config_t config;
    st_physical_module_metadata_t metadata;
    st_ds18b20_state_t state;
    uint8_t rom[ST_DS18B20_ROM_SIZE];
    uint64_t ready_at_ms;
    uint64_t cache_expires_at_ms;
    uint64_t current_acquired_at_ms;
    uint64_t last_valid_at_ms;
    uint64_t last_attempt_at_ms;
    float current_temperature_c;
    float last_valid_temperature_c;
    uint32_t current_quality_flags;
    st_driver_result_t current_failure_result;
    uint32_t conversion_commands;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t has_last_valid;
    uint8_t consecutive_failures;
    uint8_t missing_reported;
} st_ds18b20_t;

int st_ds18b20_init(st_ds18b20_t *sensor, const st_ds18b20_config_t *config);
st_physical_module_driver_t st_ds18b20_module_driver(st_ds18b20_t *sensor);

uint8_t st_ds18b20_crc8(const uint8_t *data, size_t length);
uint32_t st_ds18b20_conversion_duration_ms(uint8_t resolution_bits);
int st_ds18b20_decode_scratchpad(const uint8_t scratchpad[ST_DS18B20_SCRATCHPAD_SIZE],
                                 uint8_t expected_resolution_bits,
                                 float *temperature_c);
uint32_t st_ds18b20_conversion_command_count(const st_ds18b20_t *sensor);
uint64_t st_ds18b20_last_attempt_at_ms(const st_ds18b20_t *sensor);

#endif
