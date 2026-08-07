#ifndef SITETWIN_INA219_H
#define SITETWIN_INA219_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"

#define ST_INA219_DEFAULT_ADDRESS 0x40U
#define ST_INA219_CHANNEL_COUNT 2U
#define ST_INA219_BUS_VOLTAGE_CHANNEL 0U
#define ST_INA219_CURRENT_CHANNEL 1U

/* This driver fixes PGA at /8 (±320 mV shunt full-scale, per TI datasheet
 * Table 4) and BRNG at 32 V bus range -- the same defaults Adafruit's own
 * library uses for their 0.1 ohm breakout. Given that fixed PGA, the
 * largest current the device can report is
 * 0.320 V / shunt_resistance_ohms, so max_expected_current_a must not
 * exceed that for the configured shunt (checked in st_ina219_init). Power
 * (power_mw) is intentionally not implemented -- ST_SENSOR_POWER_MW is not
 * yet an approved contract type; see the driver plan's shared-interface
 * proposal section. */
typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    float shunt_resistance_ohms;
    float max_expected_current_a;
    uint32_t sample_interval_ms;
    uint32_t cache_validity_ms;
    const char *bus_voltage_sensor_id;
    const char *current_sensor_id;
} st_ina219_config_t;

typedef enum {
    ST_INA219_IDLE = 0,
    ST_INA219_MEASURING,
    ST_INA219_CACHED_RESULT
} st_ina219_state_t;

typedef struct {
    st_ina219_config_t config;
    st_physical_module_metadata_t metadata;
    st_ina219_state_t state;
    uint64_t ready_at_ms;
    uint64_t cache_expires_at_ms;
    uint64_t current_acquired_at_ms;
    uint64_t last_valid_at_ms;
    uint64_t last_attempt_at_ms;
    float current_lsb_ma;
    uint16_t calibration_register;
    uint8_t cnvr_wait_pending;
    uint64_t cnvr_wait_at_ms;
    float current_values[ST_INA219_CHANNEL_COUNT];
    float last_valid_values[ST_INA219_CHANNEL_COUNT];
    uint32_t current_quality_flags;
    st_driver_result_t current_failure_result;
    uint32_t measurement_commands;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t has_last_valid;
    uint8_t consecutive_failures;
    uint8_t missing_reported;
} st_ina219_t;

int st_ina219_init(st_ina219_t *sensor, const st_ina219_config_t *config);
st_physical_module_driver_t st_ina219_module_driver(st_ina219_t *sensor);

/* Exposed for host tests and for callers that want the calibration math
 * without going through the driver. Matches TI datasheet Equation 1:
 * Cal = trunc(0.04096 / (Current_LSB_amps * R_SHUNT)). Current_LSB is
 * returned in mA (not A) since that is the unit the driver reports in. */
uint16_t st_ina219_calibration_register(float max_expected_current_a,
                                        float shunt_resistance_ohms,
                                        float *out_current_lsb_ma);

uint32_t st_ina219_measurement_command_count(const st_ina219_t *sensor);
uint64_t st_ina219_last_attempt_at_ms(const st_ina219_t *sensor);

#endif