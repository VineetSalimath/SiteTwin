#ifndef SITETWIN_BH1750_H
#define SITETWIN_BH1750_H

#include <stdint.h>

#include "sitetwin/physical_module.h"
#include "sitetwin/sensor_hal.h"

/* Default I2C address when the ADDR pin is pulled low (or floating with the
 * Adafruit breakout's onboard pulldown). Pulling ADDR high changes the
 * address to ST_BH1750_ALT_ADDRESS -- exposed here so board configs can
 * select it explicitly rather than assuming one or the other. */
#define ST_BH1750_DEFAULT_ADDRESS 0x23U
#define ST_BH1750_ALT_ADDRESS 0x5CU

#define ST_BH1750_CHANNEL_COUNT 1U
#define ST_BH1750_ILLUMINANCE_CHANNEL 0U

typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    uint32_t sample_interval_ms;
    uint32_t cache_validity_ms;
    const char *illuminance_sensor_id;
} st_bh1750_config_t;

typedef enum {
    ST_BH1750_IDLE = 0,
    ST_BH1750_MEASURING,
    ST_BH1750_CACHED_RESULT
} st_bh1750_state_t;

typedef struct {
    st_bh1750_config_t config;
    st_physical_module_metadata_t metadata;
    st_bh1750_state_t state;
    uint64_t ready_at_ms;
    uint64_t cache_expires_at_ms;
    uint64_t current_acquired_at_ms;
    uint64_t last_valid_at_ms;
    uint64_t last_attempt_at_ms;
    float current_value;
    float last_valid_value;
    uint32_t current_quality_flags;
    st_driver_result_t current_failure_result;
    uint32_t measurement_commands;
    uint8_t probed;
    uint8_t current_has_sample;
    uint8_t has_last_valid;
    uint8_t consecutive_failures;
    uint8_t missing_reported;
} st_bh1750_t;

int st_bh1750_init(st_bh1750_t *sensor, const st_bh1750_config_t *config);
st_physical_module_driver_t st_bh1750_module_driver(st_bh1750_t *sensor);

/* Exposed for host tests to build expected values and for callers that want
 * the raw-to-lux conversion without going through the driver. Uses the
 * default MTreg sensitivity (69 decimal) documented for One Time
 * H-Resolution Mode: lux = raw / 1.2. */
float st_bh1750_raw_to_lux(uint16_t raw);

uint32_t st_bh1750_measurement_command_count(const st_bh1750_t *sensor);
uint64_t st_bh1750_last_attempt_at_ms(const st_bh1750_t *sensor);

#endif