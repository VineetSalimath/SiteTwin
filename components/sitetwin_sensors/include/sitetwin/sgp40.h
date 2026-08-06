#ifndef SITETWIN_SGP40_H
#define SITETWIN_SGP40_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"
#include "sitetwin/voc_index_algorithm.h"

#define ST_SGP40_DEFAULT_ADDRESS 0x59U
#define ST_SGP40_CHANNEL_COUNT 1U
#define ST_SGP40_VOC_INDEX_CHANNEL 0U

typedef struct {
    float temperature_c;
    float humidity_percent;
    uint64_t acquired_at_ms;
} st_sgp40_compensation_t;

typedef int (*st_sgp40_compensation_provider_t)(
    void *context,
    uint64_t now_ms,
    st_sgp40_compensation_t *compensation);

typedef enum {
    ST_SGP40_UNINITIALISED = 0,
    ST_SGP40_PROBING,
    ST_SGP40_WAITING_COMPENSATION,
    ST_SGP40_MEASURING,
    ST_SGP40_ALGORITHM_WARMING,
    ST_SGP40_READY,
    ST_SGP40_COMPENSATION_UNAVAILABLE,
    ST_SGP40_FAULT_REPROBE,
    ST_SGP40_DETACHED
} st_sgp40_state_t;

typedef enum {
    ST_SGP40_PHASE_PROBE_SEND_SERIAL = 0,
    ST_SGP40_PHASE_PROBE_READ_SERIAL,
    ST_SGP40_PHASE_WAITING_SAMPLE,
    ST_SGP40_PHASE_MEASUREMENT_RESPONSE
} st_sgp40_phase_t;

typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    uint32_t algorithm_interval_ms;
    uint32_t compensation_maximum_age_ms;
    st_sgp40_compensation_provider_t compensation_provider;
    void *compensation_context;
    const char *voc_index_sensor_id;
} st_sgp40_config_t;

typedef struct {
    st_sgp40_config_t config;
    st_physical_module_metadata_t metadata;
    st_voc_index_algorithm_t algorithm;
    st_sgp40_state_t state;
    st_sgp40_phase_t phase;
    uint64_t measurement_ready_at_ms;
    uint64_t next_measurement_at_ms;
    uint64_t last_process_at_ms;
    uint64_t current_acquired_at_ms;
    uint64_t last_valid_at_ms;
    uint64_t last_attempt_at_ms;
    float current_voc_index;
    float last_valid_voc_index;
    uint16_t last_raw_signal;
    uint16_t last_humidity_ticks;
    uint16_t last_temperature_ticks;
    uint32_t current_quality_flags;
    st_driver_result_t current_failure_result;
    uint32_t measurement_commands;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t has_last_valid;
    uint8_t reprobe_pending;
} st_sgp40_t;

int st_sgp40_init(st_sgp40_t *sensor, const st_sgp40_config_t *config);
st_physical_module_driver_t st_sgp40_module_driver(st_sgp40_t *sensor);

uint8_t st_sgp40_crc8(const uint8_t data[2]);
int st_sgp40_humidity_ticks(float humidity_percent, uint16_t *ticks);
int st_sgp40_temperature_ticks(float temperature_c, uint16_t *ticks);
st_sgp40_state_t st_sgp40_state(const st_sgp40_t *sensor);
uint16_t st_sgp40_last_raw_signal(const st_sgp40_t *sensor);
uint32_t st_sgp40_measurement_command_count(const st_sgp40_t *sensor);
uint64_t st_sgp40_last_attempt_at_ms(const st_sgp40_t *sensor);

#endif
