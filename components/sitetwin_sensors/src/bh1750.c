#include "sitetwin/bh1750.h"

#include <stdio.h>
#include <string.h>

/* ROHM BH1750FVI instruction set (see datasheet "Instruction Set
 * Architecture" table). Only the subset this driver uses is defined here. */
#define ST_BH1750_COMMAND_POWER_ON 0x01U
#define ST_BH1750_COMMAND_ONE_TIME_H_RESOLUTION 0x20U

/* Datasheet: One Time H-Resolution Mode measurement time is typ. 120 ms,
 * max. 180 ms. Using the max bound so the state machine never reads before
 * the device has actually finished converting. */
#define ST_BH1750_MEASUREMENT_DURATION_MS 180U

#define ST_BH1750_DEFAULT_CACHE_VALIDITY_MS 250U
#define ST_BH1750_REPROBE_AFTER_FAILURES 3U

/* Default MTreg sensitivity (69 decimal): lux = raw / 1.2. */
#define ST_BH1750_MIN_LUX 0.0F

/* Saturation is checked on the RAW 16-bit value, not on the converted lux
 * float. Comparing a derived float (raw / 1.2) against a float threshold is
 * precision-sensitive -- 65535.0f / 1.2f evaluates to ~54612.496, which is
 * *below* the mathematically-exact 54612.5, so a float-based ">=" check
 * would silently accept the sensor's own maximum possible output as a
 * normal in-range reading. Comparing the raw integer instead sidesteps
 * that entirely. */
#define ST_BH1750_SATURATED_RAW 0xFFFFU

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

float st_bh1750_raw_to_lux(uint16_t raw)
{
    return (float)raw / 1.2F;
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

static void cache_failure(st_bh1750_t *sensor, uint64_t now_ms, uint32_t failure_flag,
                          st_driver_result_t failure_result)
{
    int force_reprobe = 0;

    sensor->state = ST_BH1750_CACHED_RESULT;
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
    if (sensor->consecutive_failures >= ST_BH1750_REPROBE_AFTER_FAILURES) {
        sensor->probed = 0U;
        force_reprobe = 1;
    }

    if (sensor->has_last_valid != 0U && force_reprobe == 0) {
        sensor->current_value = sensor->last_valid_value;
        sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
        sensor->current_quality_flags |= ST_QUALITY_STALE;
        sensor->current_has_sample = 1U;
    }
}

static st_driver_result_t cached_result(const st_bh1750_t *sensor)
{
    return sensor->current_has_sample != 0U ? ST_DRIVER_READY
                                            : sensor->current_failure_result;
}

/* BH1750 has no readable serial number, so probe cannot fingerprint a
 * specific unit the way SHT41's probe does. Presence is instead confirmed
 * by successfully sending Power On -- a safe, side-effect-free command that
 * does not start a measurement and is harmless to repeat. module_uid is
 * derived from the configured address since no per-unit identity exists. */
static st_driver_result_t bh1750_probe(void *context, st_physical_module_metadata_t *metadata)
{
    st_bh1750_t *sensor = (st_bh1750_t *)context;
    uint8_t command = ST_BH1750_COMMAND_POWER_ON;
    st_hal_result_t hal_result;

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->probed != 0U) {
        *metadata = sensor->metadata;
        return ST_DRIVER_READY;
    }

    hal_result = sensor->config.bus.write(sensor->config.bus.context,
                                          sensor->config.address, &command, 1U);
    if (hal_result != ST_HAL_OK) {
        return map_hal_result(hal_result);
    }

    (void)snprintf(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid),
                   "bh1750-%02x", sensor->config.address);
    sensor->probed = 1U;
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t finish_measurement(st_bh1750_t *sensor, uint64_t now_ms)
{
    uint8_t response[2];
    st_hal_result_t hal_result;
    uint16_t raw;
    float lux;

    hal_result = sensor->config.bus.read(sensor->config.bus.context,
                                         sensor->config.address, response, sizeof(response));
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

    /* Read Format per datasheet: high byte first, low byte second. No CRC
     * is defined for this device -- saturation/range checking below is the
     * only validity signal available. */
    raw = (uint16_t)(((uint16_t)response[0] << 8U) | response[1]);
    if (raw >= ST_BH1750_SATURATED_RAW) {
        cache_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }
    lux = st_bh1750_raw_to_lux(raw);
    if (lux < ST_BH1750_MIN_LUX) {
        cache_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }

    sensor->current_value = lux;
    sensor->last_valid_value = lux;
    sensor->current_acquired_at_ms = now_ms;
    sensor->last_valid_at_ms = now_ms;
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_READY;
    sensor->current_has_sample = 1U;
    sensor->has_last_valid = 1U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
    sensor->state = ST_BH1750_CACHED_RESULT;
    sensor->cache_expires_at_ms = now_ms + sensor->config.cache_validity_ms;
    return ST_DRIVER_READY;
}

static st_driver_result_t bh1750_acquire(void *context, uint64_t now_ms)
{
    st_bh1750_t *sensor = (st_bh1750_t *)context;
    uint8_t command = ST_BH1750_COMMAND_ONE_TIME_H_RESOLUTION;
    st_hal_result_t hal_result;

    if (sensor == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->state == ST_BH1750_CACHED_RESULT) {
        if (now_ms <= sensor->cache_expires_at_ms) {
            return cached_result(sensor);
        }
        sensor->state = ST_BH1750_IDLE;
    }
    if (sensor->state == ST_BH1750_MEASURING) {
        if (now_ms < sensor->ready_at_ms) {
            return ST_DRIVER_RETRY;
        }
        return finish_measurement(sensor, now_ms);
    }

    /* One Time H-Resolution Mode: device measures once, then automatically
     * returns to Power Down. This single write is its own I2C transaction
     * (STOP is issued at the end of bus.write()), matching the datasheet's
     * "insert STOP after every opcode" requirement -- no mode-change and
     * measurement-trigger are ever chained without one. */
    sensor->last_attempt_at_ms = now_ms;
    hal_result = sensor->config.bus.write(sensor->config.bus.context,
                                          sensor->config.address, &command, 1U);
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
    sensor->state = ST_BH1750_MEASURING;
    sensor->ready_at_ms = now_ms + ST_BH1750_MEASUREMENT_DURATION_MS;
    return ST_DRIVER_RETRY;
}

static st_driver_result_t bh1750_read_channel(void *context, uint8_t channel_index,
                                              st_driver_sample_t *sample)
{
    st_bh1750_t *sensor = (st_bh1750_t *)context;

    if (sensor == NULL || sample == NULL || channel_index >= ST_BH1750_CHANNEL_COUNT ||
        sensor->state != ST_BH1750_CACHED_RESULT || sensor->current_has_sample == 0U) {
        return ST_DRIVER_ERROR;
    }

    memset(sample, 0, sizeof(*sample));
    sample->value = sensor->current_value;
    sample->unit = sensor->metadata.channels[channel_index].unit;
    sample->quality_flags = sensor->current_quality_flags;
    sample->acquired_at_ms = sensor->current_acquired_at_ms;
    sample->acquired_at_valid = 1U;
    return ST_DRIVER_READY;
}

static void bh1750_reset(void *context)
{
    st_bh1750_t *sensor = (st_bh1750_t *)context;

    if (sensor == NULL) {
        return;
    }
    sensor->state = ST_BH1750_IDLE;
    sensor->ready_at_ms = 0U;
    sensor->cache_expires_at_ms = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->last_valid_at_ms = 0U;
    sensor->last_attempt_at_ms = 0U;
    sensor->current_value = 0.0F;
    sensor->last_valid_value = 0.0F;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->has_last_valid = 0U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
}

int st_bh1750_init(st_bh1750_t *sensor, const st_bh1750_config_t *config)
{
    if (sensor == NULL || config == NULL || config->bus.write == NULL ||
        config->bus.read == NULL || config->address > 0x7FU ||
        config->sample_interval_ms == 0U || config->illuminance_sensor_id == NULL) {
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    if (sensor->config.cache_validity_ms == 0U) {
        sensor->config.cache_validity_ms = ST_BH1750_DEFAULT_CACHE_VALIDITY_MS;
    }
    sensor->metadata.sample_interval_ms = config->sample_interval_ms;
    sensor->metadata.channel_count = ST_BH1750_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[ST_BH1750_ILLUMINANCE_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_BH1750_ILLUMINANCE_CHANNEL].sensor_id),
                config->illuminance_sensor_id);
    sensor->metadata.channels[ST_BH1750_ILLUMINANCE_CHANNEL].sensor_kind =
        ST_SENSOR_ILLUMINANCE_LUX;
    sensor->metadata.channels[ST_BH1750_ILLUMINANCE_CHANNEL].unit = ST_UNIT_LUX;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    return 0;
}

st_physical_module_driver_t st_bh1750_module_driver(st_bh1750_t *sensor)
{
    st_physical_module_driver_t driver;

    driver.context = sensor;
    driver.probe = bh1750_probe;
    driver.acquire = bh1750_acquire;
    driver.read_channel = bh1750_read_channel;
    driver.reset = bh1750_reset;
    return driver;
}

uint32_t st_bh1750_measurement_command_count(const st_bh1750_t *sensor)
{
    return sensor == NULL ? 0U : sensor->measurement_commands;
}

uint64_t st_bh1750_last_attempt_at_ms(const st_bh1750_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_attempt_at_ms;
}