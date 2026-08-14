#ifndef SITETWIN_SCD41_H
#define SITETWIN_SCD41_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"

#define ST_SCD41_DEFAULT_ADDRESS 0x62U
#define ST_SCD41_CHANNEL_COUNT 1U
#define ST_SCD41_CO2_CHANNEL 0U

typedef enum {
    ST_SCD41_MODE_PERIODIC = 0,
    ST_SCD41_MODE_LOW_POWER_PERIODIC
} st_scd41_measurement_mode_t;

typedef enum {
    ST_SCD41_UNINITIALISED = 0,
    ST_SCD41_PROBING,
    ST_SCD41_STARTING,
    ST_SCD41_WARMING_UP,
    ST_SCD41_WAITING_DATA_READY,
    ST_SCD41_READING,
    ST_SCD41_READY,
    ST_SCD41_FAULT_REPROBE,
    ST_SCD41_DETACHED
} st_scd41_state_t;

typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    st_scd41_measurement_mode_t measurement_mode;
    uint32_t poll_interval_ms;
    const char *co2_sensor_id;
} st_scd41_config_t;

typedef enum {
    ST_SCD41_PHASE_PROBE_SEND_SERIAL = 0,
    ST_SCD41_PHASE_PROBE_READ_SERIAL,
    ST_SCD41_PHASE_PROBE_AFTER_STOP,
    ST_SCD41_PHASE_STATUS_COMMAND,
    ST_SCD41_PHASE_STATUS_RESPONSE,
    ST_SCD41_PHASE_MEASUREMENT_RESPONSE
} st_scd41_phase_t;

typedef struct {
    st_scd41_config_t config;
    st_physical_module_metadata_t metadata;
    st_scd41_state_t state;
    st_scd41_phase_t phase;
    uint64_t next_action_at_ms;
    uint64_t current_acquired_at_ms;
    uint64_t last_valid_at_ms;
    uint64_t last_attempt_at_ms;
    float current_co2_ppm;
    float last_valid_co2_ppm;
    uint32_t current_quality_flags;
    st_driver_result_t current_failure_result;
    uint32_t start_commands;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t has_last_valid;
    uint8_t reprobe_pending;
} st_scd41_t;

int st_scd41_init(st_scd41_t *sensor, const st_scd41_config_t *config);
st_physical_module_driver_t st_scd41_module_driver(st_scd41_t *sensor);

uint8_t st_scd41_crc8(const uint8_t data[2]);
float st_scd41_raw_co2_ppm(uint16_t raw);
st_scd41_state_t st_scd41_state(const st_scd41_t *sensor);
uint32_t st_scd41_start_command_count(const st_scd41_t *sensor);
uint64_t st_scd41_last_attempt_at_ms(const st_scd41_t *sensor);

#endif
