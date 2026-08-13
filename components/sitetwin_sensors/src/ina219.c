#include "sitetwin/ina219.h"

#include <stdio.h>
#include <string.h>

/* TI INA219 register map (SBOS448G Table 2). */
#define ST_INA219_REG_CONFIG 0x00U
#define ST_INA219_REG_BUS_VOLTAGE 0x02U
#define ST_INA219_REG_CURRENT 0x04U
#define ST_INA219_REG_CALIBRATION 0x05U

/* Configuration register value: BRNG=1 (32V bus range), PG=11 (/8 gain,
 * ±320mV shunt full-scale -- matches Adafruit's own 0.1 ohm breakout
 * defaults), BADC=SADC=0011 (12-bit, no averaging, 532us each -- same
 * resolution as the datasheet's own power-on default), MODE=011 (shunt
 * and bus, TRIGGERED rather than the power-on default's continuous mode).
 * This is deliberately "the datasheet's own default settings, but
 * triggered instead of continuous" so the driver only converts when asked,
 * matching the non-blocking trigger/wait/read shape used by every other
 * driver in this codebase rather than letting the ADC free-run. */
#define ST_INA219_CONFIG_TRIGGERED_VALUE 0x399BU

/* Two back-to-back 12-bit/no-averaging conversions (bus then shunt, or
 * vice versa -- the datasheet does not give a combined figure for
 * triggered "shunt and bus" mode) take up to ~532us + 532us = 1064us.
 * Rounded up with margin; the CNVR bit is also checked at read time as a
 * second line of defense rather than trusting this timer alone. */
#define ST_INA219_MEASUREMENT_DURATION_MS 2U

#define ST_INA219_DEFAULT_CACHE_VALIDITY_MS 250U
#define ST_INA219_REPROBE_AFTER_FAILURES 3U

#define ST_INA219_BUS_VOLTAGE_LSB_V 0.004F
#define ST_INA219_BUS_RAW_SHIFT 3U
#define ST_INA219_OVF_MASK 0x0001U
#define ST_INA219_CNVR_MASK 0x0002U

static void copy_string(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

uint16_t st_ina219_calibration_register(float max_expected_current_a,
                                        float shunt_resistance_ohms,
                                        float *out_current_lsb_ma)
{
    /* TI datasheet Equation 1/2:
     *   Current_LSB = MaxExpectedCurrent / 2^15
     *   Cal = trunc(0.04096 / (Current_LSB * R_SHUNT))
     * Current_LSB is computed in amps per the formula, then reported to
     * the caller in mA since that is the unit this driver's readings use. */
    float current_lsb_a = max_expected_current_a / 32768.0F;
    float calibration_f;

    if (out_current_lsb_ma != NULL) {
        *out_current_lsb_ma = current_lsb_a * 1000.0F;
    }
    if (current_lsb_a <= 0.0F || shunt_resistance_ohms <= 0.0F) {
        return 0U;
    }
    calibration_f = 0.04096F / (current_lsb_a * shunt_resistance_ohms);
    if (calibration_f < 0.0F) {
        return 0U;
    }
    if (calibration_f > 65535.0F) {
        calibration_f = 65535.0F;
    }
    return (uint16_t)calibration_f; /* trunc(), matching the datasheet's own notation */
}

static st_driver_result_t map_hal_result(st_hal_result_t result)
{
    switch (result) {
    case ST_HAL_OK:
        return ST_DRIVER_READY;
    case ST_HAL_NOT_PRESENT:
        return ST_DRIVER_NOT_PRESENT;
    case ST_HAL_BUSY:
    case ST_HAL_TIMEOUT:
        return ST_DRIVER_RETRY;
    default:
        return ST_DRIVER_ERROR;
    }
}

static st_hal_result_t read_register(st_ina219_t *sensor, uint8_t reg, uint8_t *data, size_t length)
{
    st_hal_result_t result;

    result = sensor->config.bus.write(sensor->config.bus.context, sensor->config.address, &reg, 1U);
    if (result != ST_HAL_OK) {
        return result;
    }
    return sensor->config.bus.read(sensor->config.bus.context, sensor->config.address, data, length);
}

static st_hal_result_t write_register(st_ina219_t *sensor, uint8_t reg, uint16_t value)
{
    uint8_t payload[3];

    payload[0] = reg;
    payload[1] = (uint8_t)(value >> 8U);
    payload[2] = (uint8_t)value;
    return sensor->config.bus.write(sensor->config.bus.context, sensor->config.address, payload,
                                    sizeof(payload));
}

static void cache_failure(st_ina219_t *sensor, uint64_t now_ms, uint32_t failure_flag,
                          st_driver_result_t failure_result)
{
    int force_reprobe = 0;

    sensor->state = ST_INA219_CACHED_RESULT;
    sensor->cache_expires_at_ms = now_ms + sensor->config.cache_validity_ms;
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_failure_result = failure_result;
    sensor->current_has_sample = 0U;
    sensor->current_quality_flags = failure_flag;
    if (sensor->consecutive_failures < UINT8_MAX) {
        ++sensor->consecutive_failures;
    }

    if (failure_flag == ST_QUALITY_SENSOR_MISSING) {
        sensor->probed = 0U;
        if (sensor->has_last_valid != 0U && sensor->missing_reported == 0U) {
            sensor->missing_reported = 1U;
        } else if (sensor->missing_reported != 0U) {
            sensor->has_last_valid = 0U;
        }
    }
    if (sensor->consecutive_failures >= ST_INA219_REPROBE_AFTER_FAILURES) {
        sensor->probed = 0U;
        force_reprobe = 1;
    }

    if (sensor->has_last_valid != 0U && force_reprobe == 0) {
        sensor->current_values[ST_INA219_BUS_VOLTAGE_CHANNEL] =
            sensor->last_valid_values[ST_INA219_BUS_VOLTAGE_CHANNEL];
        sensor->current_values[ST_INA219_CURRENT_CHANNEL] =
            sensor->last_valid_values[ST_INA219_CURRENT_CHANNEL];
        sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
        sensor->current_quality_flags |= ST_QUALITY_STALE;
        sensor->current_has_sample = 1U;
    }
}

static st_driver_result_t cached_result(const st_ina219_t *sensor)
{
    return sensor->current_has_sample != 0U ? ST_DRIVER_READY
                                            : sensor->current_failure_result;
}

/* INA219 has no serial number either. Presence is confirmed by writing the
 * Calibration register (a real, necessary, idempotent one-time setup step
 * -- Current/Power registers stay zero until this is done, per datasheet
 * section 8.5.3) followed by a harmless read of the Configuration
 * register. module_uid is address-derived since no per-unit identity
 * exists on this device either. */
static st_driver_result_t ina219_probe(void *context, st_physical_module_metadata_t *metadata)
{
    st_ina219_t *sensor = (st_ina219_t *)context;
    uint8_t response[2];
    st_hal_result_t hal_result;

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->probed != 0U) {
        *metadata = sensor->metadata;
        return ST_DRIVER_READY;
    }

    hal_result = write_register(sensor, ST_INA219_REG_CALIBRATION, sensor->calibration_register);
    if (hal_result != ST_HAL_OK) {
        return map_hal_result(hal_result);
    }
    hal_result = read_register(sensor, ST_INA219_REG_CONFIG, response, sizeof(response));
    if (hal_result != ST_HAL_OK) {
        return map_hal_result(hal_result);
    }

    (void)snprintf(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid),
                   "ina219-%02x", sensor->config.address);
    sensor->probed = 1U;
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t finish_measurement(st_ina219_t *sensor, uint64_t now_ms)
{
    uint8_t bus_response[2];
    uint8_t current_response[2];
    st_hal_result_t hal_result;
    uint16_t bus_register;
    uint16_t bus_raw;
    int16_t current_raw;
    float bus_voltage_v;
    float current_ma;

    if (sensor->cnvr_wait_pending != 0U && sensor->cnvr_wait_at_ms == now_ms) {
        /* A different channel already checked CNVR at this exact now_ms
         * within the same tick and found the conversion still in
         * progress -- do not issue a second physical read for the same
         * answer. */
        return ST_DRIVER_RETRY;
    }

    hal_result = read_register(sensor, ST_INA219_REG_BUS_VOLTAGE, bus_response, sizeof(bus_response));
    if (hal_result == ST_HAL_BUSY) {
        return ST_DRIVER_RETRY;
    }
    if (hal_result != ST_HAL_OK) {
        uint32_t flag = hal_result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t result = hal_result == ST_HAL_NOT_PRESENT ? ST_DRIVER_NOT_PRESENT
                                                                     : ST_DRIVER_ERROR;

        cache_failure(sensor, now_ms, flag, result);
        return cached_result(sensor);
    }

    bus_register = (uint16_t)(((uint16_t)bus_response[0] << 8U) | bus_response[1]);
    if ((bus_register & ST_INA219_CNVR_MASK) == 0U) {
        /* Conversion genuinely not finished despite our timing margin --
         * ask again next tick rather than trust a stale/incomplete value.
         * State stays MEASURING so the next acquire() call retries this
         * same read. Remember which now_ms we already checked so that a
         * second channel's acquire() call within THIS SAME tick (both
         * channels share this context) does not re-issue the physical
         * I2C read only to observe the identical not-ready result. */
        sensor->cnvr_wait_pending = 1U;
        sensor->cnvr_wait_at_ms = now_ms;
        return ST_DRIVER_RETRY;
    }
    sensor->cnvr_wait_pending = 0U;
    if ((bus_register & ST_INA219_OVF_MASK) != 0U) {
        /* Math overflow: shunt or bus voltage exceeded the configured
         * range. The datasheet notes this does not catch ADC-level
         * overflow directly, only the internal current/power calculation
         * overflow -- documented here rather than silently treated as
         * more precise than it is. */
        cache_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }

    /* Bus Voltage register bits 15:3 are BD12..BD0; shift right 3 before
     * applying the 4 mV LSB (see datasheet Figure 24 / section 8.6.3.2). */
    bus_raw = (uint16_t)(bus_register >> ST_INA219_BUS_RAW_SHIFT);
    bus_voltage_v = (float)bus_raw * ST_INA219_BUS_VOLTAGE_LSB_V;

    hal_result = read_register(sensor, ST_INA219_REG_CURRENT, current_response,
                               sizeof(current_response));
    if (hal_result == ST_HAL_BUSY) {
        return ST_DRIVER_RETRY;
    }
    if (hal_result != ST_HAL_OK) {
        uint32_t flag = hal_result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t result = hal_result == ST_HAL_NOT_PRESENT ? ST_DRIVER_NOT_PRESENT
                                                                     : ST_DRIVER_ERROR;

        cache_failure(sensor, now_ms, flag, result);
        return cached_result(sensor);
    }
    /* Current register is two's-complement signed (datasheet 8.6.3.4). */
    current_raw = (int16_t)(((uint16_t)current_response[0] << 8U) | current_response[1]);
    current_ma = (float)current_raw * sensor->current_lsb_ma;

    sensor->current_values[ST_INA219_BUS_VOLTAGE_CHANNEL] = bus_voltage_v;
    sensor->current_values[ST_INA219_CURRENT_CHANNEL] = current_ma;
    sensor->last_valid_values[ST_INA219_BUS_VOLTAGE_CHANNEL] = bus_voltage_v;
    sensor->last_valid_values[ST_INA219_CURRENT_CHANNEL] = current_ma;
    sensor->current_acquired_at_ms = now_ms;
    sensor->last_valid_at_ms = now_ms;
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_READY;
    sensor->current_has_sample = 1U;
    sensor->has_last_valid = 1U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
    sensor->state = ST_INA219_CACHED_RESULT;
    sensor->cache_expires_at_ms = now_ms + sensor->config.cache_validity_ms;
    return ST_DRIVER_READY;
}

static st_driver_result_t ina219_acquire(void *context, uint64_t now_ms)
{
    st_ina219_t *sensor = (st_ina219_t *)context;
    st_hal_result_t hal_result;

    if (sensor == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->state == ST_INA219_CACHED_RESULT) {
        if (now_ms <= sensor->cache_expires_at_ms) {
            return cached_result(sensor);
        }
        sensor->state = ST_INA219_IDLE;
    }
    if (sensor->state == ST_INA219_MEASURING) {
        if (now_ms < sensor->ready_at_ms) {
            return ST_DRIVER_RETRY;
        }
        return finish_measurement(sensor, now_ms);
    }

    /* Writing a triggered MODE value into the Configuration register
     * starts a new conversion even if that same value was already
     * programmed (datasheet 8.5.1) -- this is the trigger step, analogous
     * to BH1750's one-time measurement opcode. */
    sensor->last_attempt_at_ms = now_ms;
    hal_result = write_register(sensor, ST_INA219_REG_CONFIG, ST_INA219_CONFIG_TRIGGERED_VALUE);
    if (hal_result == ST_HAL_BUSY || hal_result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (hal_result != ST_HAL_OK) {
        uint32_t flag = hal_result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t result = hal_result == ST_HAL_NOT_PRESENT ? ST_DRIVER_NOT_PRESENT
                                                                     : ST_DRIVER_ERROR;

        cache_failure(sensor, now_ms, flag, result);
        return cached_result(sensor);
    }

    ++sensor->measurement_commands;
    sensor->state = ST_INA219_MEASURING;
    sensor->ready_at_ms = now_ms + ST_INA219_MEASUREMENT_DURATION_MS;
    return ST_DRIVER_RETRY;
}

static st_driver_result_t ina219_read_channel(void *context, uint8_t channel_index,
                                              st_driver_sample_t *sample)
{
    st_ina219_t *sensor = (st_ina219_t *)context;

    if (sensor == NULL || sample == NULL || channel_index >= ST_INA219_CHANNEL_COUNT ||
        sensor->state != ST_INA219_CACHED_RESULT || sensor->current_has_sample == 0U) {
        return ST_DRIVER_ERROR;
    }

    memset(sample, 0, sizeof(*sample));
    sample->value = sensor->current_values[channel_index];
    sample->unit = sensor->metadata.channels[channel_index].unit;
    sample->quality_flags = sensor->current_quality_flags;
    sample->acquired_at_ms = sensor->current_acquired_at_ms;
    sample->acquired_at_valid = 1U;
    return ST_DRIVER_READY;
}

static void ina219_reset(void *context)
{
    st_ina219_t *sensor = (st_ina219_t *)context;

    if (sensor == NULL) {
        return;
    }
    sensor->state = ST_INA219_IDLE;
    sensor->ready_at_ms = 0U;
    sensor->cache_expires_at_ms = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->last_valid_at_ms = 0U;
    sensor->last_attempt_at_ms = 0U;
    sensor->current_values[ST_INA219_BUS_VOLTAGE_CHANNEL] = 0.0F;
    sensor->current_values[ST_INA219_CURRENT_CHANNEL] = 0.0F;
    sensor->last_valid_values[ST_INA219_BUS_VOLTAGE_CHANNEL] = 0.0F;
    sensor->last_valid_values[ST_INA219_CURRENT_CHANNEL] = 0.0F;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->has_last_valid = 0U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
    sensor->cnvr_wait_pending = 0U;
    sensor->cnvr_wait_at_ms = 0U;
    /* calibration_register / current_lsb_ma are derived purely from
     * config and do not change across resets -- intentionally preserved. */
}

int st_ina219_init(st_ina219_t *sensor, const st_ina219_config_t *config)
{
    float max_measurable_current_a;

    if (sensor == NULL || config == NULL || config->bus.write == NULL ||
        config->bus.read == NULL || config->address > 0x7FU ||
        config->sample_interval_ms == 0U || config->bus_voltage_sensor_id == NULL ||
        config->current_sensor_id == NULL || config->shunt_resistance_ohms <= 0.0F ||
        config->max_expected_current_a <= 0.0F) {
        return -1;
    }

    /* PGA is fixed at /8 (see ST_INA219_CONFIG_TRIGGERED_VALUE), giving a
     * ±320 mV shunt full-scale. The caller's requested current range must
     * fit inside what that shunt can actually represent at that PGA
     * setting -- otherwise Current_LSB would be computed for a range the
     * hardware configuration can never reach.
     *
     * A small relative tolerance is applied before rejecting: computing
     * 0.320F / shunt_resistance_ohms and comparing directly against a
     * caller-supplied value that is meant to represent exactly that same
     * boundary (e.g. 3.2F for a 0.1 ohm shunt) can fail due to float
     * rounding alone -- 0.320f/0.1f evaluates to ~3.1999998, not exactly
     * 3.2f -- which would otherwise reject a configuration that is
     * actually valid. */
    max_measurable_current_a = 0.320F / config->shunt_resistance_ohms;
    if (config->max_expected_current_a > max_measurable_current_a * 1.0005F) {
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    if (sensor->config.cache_validity_ms == 0U) {
        sensor->config.cache_validity_ms = ST_INA219_DEFAULT_CACHE_VALIDITY_MS;
    }
    sensor->calibration_register = st_ina219_calibration_register(
        config->max_expected_current_a, config->shunt_resistance_ohms, &sensor->current_lsb_ma);
    if (sensor->calibration_register == 0U) {
        return -1;
    }

    sensor->metadata.sample_interval_ms = config->sample_interval_ms;
    sensor->metadata.channel_count = ST_INA219_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[ST_INA219_BUS_VOLTAGE_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_INA219_BUS_VOLTAGE_CHANNEL].sensor_id),
                config->bus_voltage_sensor_id);
    sensor->metadata.channels[ST_INA219_BUS_VOLTAGE_CHANNEL].sensor_kind = ST_SENSOR_VOLTAGE_V;
    sensor->metadata.channels[ST_INA219_BUS_VOLTAGE_CHANNEL].unit = ST_UNIT_VOLT;
    copy_string(sensor->metadata.channels[ST_INA219_CURRENT_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_INA219_CURRENT_CHANNEL].sensor_id),
                config->current_sensor_id);
    sensor->metadata.channels[ST_INA219_CURRENT_CHANNEL].sensor_kind = ST_SENSOR_CURRENT_MA;
    sensor->metadata.channels[ST_INA219_CURRENT_CHANNEL].unit = ST_UNIT_MILLIAMP;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    return 0;
}

st_physical_module_driver_t st_ina219_module_driver(st_ina219_t *sensor)
{
    st_physical_module_driver_t driver;

    driver.context = sensor;
    driver.probe = ina219_probe;
    driver.acquire = ina219_acquire;
    driver.read_channel = ina219_read_channel;
    driver.reset = ina219_reset;
    return driver;
}

uint32_t st_ina219_measurement_command_count(const st_ina219_t *sensor)
{
    return sensor == NULL ? 0U : sensor->measurement_commands;
}

uint64_t st_ina219_last_attempt_at_ms(const st_ina219_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_attempt_at_ms;
}