#include "sitetwin/sht41.h"

#include <stdio.h>
#include <string.h>

#define ST_SHT41_COMMAND_MEASURE_HIGH_PRECISION 0xFDU
#define ST_SHT41_COMMAND_READ_SERIAL 0x89U
#define ST_SHT41_HIGH_PRECISION_DURATION_MS 9U
#define ST_SHT41_DEFAULT_CACHE_VALIDITY_MS 250U
#define ST_SHT41_REPROBE_AFTER_FAILURES 3U
#define ST_SHT41_MIN_TEMPERATURE_C (-40.0F)
#define ST_SHT41_MAX_TEMPERATURE_C 125.0F
#define ST_SHT41_MIN_HUMIDITY_PERCENT 0.0F
#define ST_SHT41_MAX_HUMIDITY_PERCENT 100.0F

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

uint8_t st_sht41_crc8(const uint8_t data[2])
{
    uint8_t crc = 0xFFU;
    uint8_t byte_index;

    if (data == NULL) {
        return 0U;
    }
    for (byte_index = 0U; byte_index < 2U; ++byte_index) {
        uint8_t bit_index;

        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; ++bit_index) {
            crc = (crc & 0x80U) != 0U ? (uint8_t)((crc << 1U) ^ 0x31U)
                                       : (uint8_t)(crc << 1U);
        }
    }
    return crc;
}

float st_sht41_raw_temperature_c(uint16_t raw)
{
    return -45.0F + (175.0F * (float)raw / 65535.0F);
}

float st_sht41_raw_humidity_percent(uint16_t raw)
{
    return -6.0F + (125.0F * (float)raw / 65535.0F);
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

static void cache_failure(st_sht41_t *sensor, uint64_t now_ms, uint32_t failure_flag,
                          st_driver_result_t failure_result)
{
    int force_reprobe = 0;

    sensor->state = ST_SHT41_CACHED_RESULT;
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
    if (sensor->consecutive_failures >= ST_SHT41_REPROBE_AFTER_FAILURES) {
        sensor->probed = 0U;
        force_reprobe = 1;
    }

    if (sensor->has_last_valid != 0U && force_reprobe == 0) {
        sensor->current_values[ST_SHT41_TEMPERATURE_CHANNEL] =
            sensor->last_valid_values[ST_SHT41_TEMPERATURE_CHANNEL];
        sensor->current_values[ST_SHT41_HUMIDITY_CHANNEL] =
            sensor->last_valid_values[ST_SHT41_HUMIDITY_CHANNEL];
        sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
        sensor->current_quality_flags |= ST_QUALITY_STALE;
        sensor->current_has_sample = 1U;
    }
}

static st_driver_result_t cached_result(const st_sht41_t *sensor)
{
    return sensor->current_has_sample != 0U ? ST_DRIVER_READY
                                            : sensor->current_failure_result;
}

static st_driver_result_t sht41_probe(void *context, st_physical_module_metadata_t *metadata)
{
    st_sht41_t *sensor = (st_sht41_t *)context;
    uint8_t command = ST_SHT41_COMMAND_READ_SERIAL;
    uint8_t response[6];
    st_hal_result_t hal_result;
    uint32_t serial;

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
    hal_result = sensor->config.bus.read(sensor->config.bus.context,
                                         sensor->config.address, response, sizeof(response));
    if (hal_result != ST_HAL_OK) {
        return map_hal_result(hal_result);
    }
    if (st_sht41_crc8(&response[0]) != response[2] ||
        st_sht41_crc8(&response[3]) != response[5]) {
        return ST_DRIVER_ERROR;
    }

    serial = ((uint32_t)response[0] << 24U) | ((uint32_t)response[1] << 16U) |
             ((uint32_t)response[3] << 8U) | (uint32_t)response[4];
    (void)snprintf(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid),
                   "sht41-%08lx", (unsigned long)serial);
    sensor->probed = 1U;
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t finish_measurement(st_sht41_t *sensor, uint64_t now_ms)
{
    uint8_t response[6];
    st_hal_result_t hal_result;
    uint16_t raw_temperature;
    uint16_t raw_humidity;
    float temperature;
    float humidity;

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
    if (st_sht41_crc8(&response[0]) != response[2] ||
        st_sht41_crc8(&response[3]) != response[5]) {
        cache_failure(sensor, now_ms, ST_QUALITY_CRC_FAILED, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }

    raw_temperature = (uint16_t)(((uint16_t)response[0] << 8U) | response[1]);
    raw_humidity = (uint16_t)(((uint16_t)response[3] << 8U) | response[4]);
    temperature = st_sht41_raw_temperature_c(raw_temperature);
    humidity = st_sht41_raw_humidity_percent(raw_humidity);
    if (temperature < ST_SHT41_MIN_TEMPERATURE_C ||
        temperature > ST_SHT41_MAX_TEMPERATURE_C ||
        humidity < ST_SHT41_MIN_HUMIDITY_PERCENT ||
        humidity > ST_SHT41_MAX_HUMIDITY_PERCENT) {
        cache_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }

    sensor->current_values[ST_SHT41_TEMPERATURE_CHANNEL] = temperature;
    sensor->current_values[ST_SHT41_HUMIDITY_CHANNEL] = humidity;
    sensor->last_valid_values[ST_SHT41_TEMPERATURE_CHANNEL] = temperature;
    sensor->last_valid_values[ST_SHT41_HUMIDITY_CHANNEL] = humidity;
    sensor->current_acquired_at_ms = now_ms;
    sensor->last_valid_at_ms = now_ms;
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_READY;
    sensor->current_has_sample = 1U;
    sensor->has_last_valid = 1U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
    sensor->state = ST_SHT41_CACHED_RESULT;
    sensor->cache_expires_at_ms = now_ms + sensor->config.cache_validity_ms;
    return ST_DRIVER_READY;
}

static st_driver_result_t sht41_acquire(void *context, uint64_t now_ms)
{
    st_sht41_t *sensor = (st_sht41_t *)context;
    uint8_t command = ST_SHT41_COMMAND_MEASURE_HIGH_PRECISION;
    st_hal_result_t hal_result;

    if (sensor == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->state == ST_SHT41_CACHED_RESULT) {
        if (now_ms <= sensor->cache_expires_at_ms) {
            return cached_result(sensor);
        }
        sensor->state = ST_SHT41_IDLE;
    }
    if (sensor->state == ST_SHT41_MEASURING) {
        if (now_ms < sensor->ready_at_ms) {
            return ST_DRIVER_RETRY;
        }
        return finish_measurement(sensor, now_ms);
    }

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
    sensor->state = ST_SHT41_MEASURING;
    sensor->ready_at_ms = now_ms + ST_SHT41_HIGH_PRECISION_DURATION_MS;
    return ST_DRIVER_RETRY;
}

static st_driver_result_t sht41_read_channel(void *context, uint8_t channel_index,
                                              st_driver_sample_t *sample)
{
    st_sht41_t *sensor = (st_sht41_t *)context;

    if (sensor == NULL || sample == NULL || channel_index >= ST_SHT41_CHANNEL_COUNT ||
        sensor->state != ST_SHT41_CACHED_RESULT || sensor->current_has_sample == 0U) {
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

static void sht41_reset(void *context)
{
    st_sht41_t *sensor = (st_sht41_t *)context;

    if (sensor == NULL) {
        return;
    }
    sensor->state = ST_SHT41_IDLE;
    sensor->ready_at_ms = 0U;
    sensor->cache_expires_at_ms = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->last_valid_at_ms = 0U;
    sensor->last_attempt_at_ms = 0U;
    memset(sensor->current_values, 0, sizeof(sensor->current_values));
    memset(sensor->last_valid_values, 0, sizeof(sensor->last_valid_values));
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->has_last_valid = 0U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
}

int st_sht41_init(st_sht41_t *sensor, const st_sht41_config_t *config)
{
    if (sensor == NULL || config == NULL || config->bus.write == NULL ||
        config->bus.read == NULL || config->address > 0x7FU ||
        config->sample_interval_ms == 0U || config->temperature_sensor_id == NULL ||
        config->humidity_sensor_id == NULL) {
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    if (sensor->config.cache_validity_ms == 0U) {
        sensor->config.cache_validity_ms = ST_SHT41_DEFAULT_CACHE_VALIDITY_MS;
    }
    sensor->metadata.sample_interval_ms = config->sample_interval_ms;
    sensor->metadata.channel_count = ST_SHT41_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[ST_SHT41_TEMPERATURE_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_SHT41_TEMPERATURE_CHANNEL].sensor_id),
                config->temperature_sensor_id);
    sensor->metadata.channels[ST_SHT41_TEMPERATURE_CHANNEL].sensor_kind =
        ST_SENSOR_TEMPERATURE_C;
    sensor->metadata.channels[ST_SHT41_TEMPERATURE_CHANNEL].unit = ST_UNIT_CELSIUS;
    copy_string(sensor->metadata.channels[ST_SHT41_HUMIDITY_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_SHT41_HUMIDITY_CHANNEL].sensor_id),
                config->humidity_sensor_id);
    sensor->metadata.channels[ST_SHT41_HUMIDITY_CHANNEL].sensor_kind =
        ST_SENSOR_RELATIVE_HUMIDITY_PERCENT;
    sensor->metadata.channels[ST_SHT41_HUMIDITY_CHANNEL].unit = ST_UNIT_PERCENT;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    return 0;
}

st_physical_module_driver_t st_sht41_module_driver(st_sht41_t *sensor)
{
    st_physical_module_driver_t driver;

    driver.context = sensor;
    driver.probe = sht41_probe;
    driver.acquire = sht41_acquire;
    driver.read_channel = sht41_read_channel;
    driver.reset = sht41_reset;
    return driver;
}

uint32_t st_sht41_measurement_command_count(const st_sht41_t *sensor)
{
    return sensor == NULL ? 0U : sensor->measurement_commands;
}

uint64_t st_sht41_last_attempt_at_ms(const st_sht41_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_attempt_at_ms;
}
