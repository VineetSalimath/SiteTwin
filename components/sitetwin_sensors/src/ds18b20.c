#include "sitetwin/ds18b20.h"

#include <stdio.h>
#include <string.h>

#define ST_DS18B20_FAMILY_CODE 0x28U
#define ST_ONEWIRE_READ_ROM 0x33U
#define ST_ONEWIRE_SKIP_ROM 0xCCU
#define ST_DS18B20_CONVERT_T 0x44U
#define ST_DS18B20_WRITE_SCRATCHPAD 0x4EU
#define ST_DS18B20_READ_SCRATCHPAD 0xBEU
#define ST_DS18B20_DEFAULT_CACHE_VALIDITY_MS 250U
#define ST_DS18B20_REPROBE_AFTER_FAILURES 3U
#define ST_DS18B20_MIN_TEMPERATURE_C (-55.0F)
#define ST_DS18B20_MAX_TEMPERATURE_C 125.0F

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

uint8_t st_ds18b20_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0U;
    size_t byte_index;

    if (data == NULL) {
        return 0U;
    }
    for (byte_index = 0U; byte_index < length; ++byte_index) {
        uint8_t current = data[byte_index];
        uint8_t bit_index;

        for (bit_index = 0U; bit_index < 8U; ++bit_index) {
            uint8_t mix = (uint8_t)((crc ^ current) & 0x01U);

            crc >>= 1U;
            if (mix != 0U) {
                crc ^= 0x8CU;
            }
            current >>= 1U;
        }
    }
    return crc;
}

uint32_t st_ds18b20_conversion_duration_ms(uint8_t resolution_bits)
{
    switch (resolution_bits) {
    case 9U:
        return 94U;
    case 10U:
        return 188U;
    case 11U:
        return 375U;
    case 12U:
        return 750U;
    default:
        return 0U;
    }
}

static uint8_t resolution_config_byte(uint8_t resolution_bits)
{
    return (uint8_t)(0x1FU | ((resolution_bits - 9U) << 5U));
}

int st_ds18b20_decode_scratchpad(const uint8_t scratchpad[ST_DS18B20_SCRATCHPAD_SIZE],
                                 uint8_t expected_resolution_bits,
                                 float *temperature_c)
{
    uint8_t actual_resolution_bits;
    uint16_t unused_mask;
    int16_t raw;
    float decoded;

    if (scratchpad == NULL || temperature_c == NULL ||
        st_ds18b20_conversion_duration_ms(expected_resolution_bits) == 0U ||
        st_ds18b20_crc8(scratchpad, ST_DS18B20_SCRATCHPAD_SIZE - 1U) != scratchpad[8]) {
        return -1;
    }
    actual_resolution_bits = (uint8_t)(((scratchpad[4] >> 5U) & 0x03U) + 9U);
    if (actual_resolution_bits != expected_resolution_bits) {
        return -1;
    }

    raw = (int16_t)(((uint16_t)scratchpad[1] << 8U) | scratchpad[0]);
    switch (actual_resolution_bits) {
    case 9U:
        unused_mask = 0x0007U;
        break;
    case 10U:
        unused_mask = 0x0003U;
        break;
    case 11U:
        unused_mask = 0x0001U;
        break;
    default:
        unused_mask = 0U;
        break;
    }
    raw = (int16_t)((uint16_t)raw & (uint16_t)~unused_mask);
    decoded = (float)raw / 16.0F;
    if (decoded < ST_DS18B20_MIN_TEMPERATURE_C ||
        decoded > ST_DS18B20_MAX_TEMPERATURE_C) {
        return -1;
    }
    *temperature_c = decoded;
    return 0;
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

static void cache_failure(st_ds18b20_t *sensor, uint64_t now_ms,
                          uint32_t failure_flag, st_driver_result_t failure_result)
{
    int force_reprobe = 0;

    sensor->state = ST_DS18B20_CACHED_RESULT;
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
    if (sensor->consecutive_failures >= ST_DS18B20_REPROBE_AFTER_FAILURES) {
        sensor->probed = 0U;
        force_reprobe = 1;
    }
    if (sensor->has_last_valid != 0U && force_reprobe == 0) {
        sensor->current_temperature_c = sensor->last_valid_temperature_c;
        sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
        sensor->current_quality_flags |= ST_QUALITY_STALE;
        sensor->current_has_sample = 1U;
    }
}

static st_driver_result_t cached_result(const st_ds18b20_t *sensor)
{
    return sensor->current_has_sample != 0U ? ST_DRIVER_READY
                                            : sensor->current_failure_result;
}

static st_driver_result_t bus_sequence_result(st_hal_result_t result)
{
    return result == ST_HAL_OK ? ST_DRIVER_READY : map_hal_result(result);
}

static st_driver_result_t ds18b20_probe(void *context,
                                       st_physical_module_metadata_t *metadata)
{
    st_ds18b20_t *sensor = (st_ds18b20_t *)context;
    uint8_t read_rom = ST_ONEWIRE_READ_ROM;
    uint8_t configure[5];
    st_hal_result_t result;

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->probed != 0U) {
        *metadata = sensor->metadata;
        return ST_DRIVER_READY;
    }

    result = sensor->config.bus.reset(sensor->config.bus.context);
    if (result != ST_HAL_OK) {
        return map_hal_result(result);
    }
    result = sensor->config.bus.write(sensor->config.bus.context, &read_rom, 1U);
    if (result != ST_HAL_OK) {
        return map_hal_result(result);
    }
    result = sensor->config.bus.read(sensor->config.bus.context,
                                     sensor->rom, sizeof(sensor->rom));
    if (result != ST_HAL_OK) {
        return map_hal_result(result);
    }
    if (sensor->rom[0] != ST_DS18B20_FAMILY_CODE ||
        st_ds18b20_crc8(sensor->rom, ST_DS18B20_ROM_SIZE - 1U) != sensor->rom[7]) {
        return ST_DRIVER_ERROR;
    }

    configure[0] = ST_ONEWIRE_SKIP_ROM;
    configure[1] = ST_DS18B20_WRITE_SCRATCHPAD;
    configure[2] = 0x4BU;
    configure[3] = 0x46U;
    configure[4] = resolution_config_byte(sensor->config.resolution_bits);
    result = sensor->config.bus.reset(sensor->config.bus.context);
    if (result != ST_HAL_OK) {
        return map_hal_result(result);
    }
    result = sensor->config.bus.write(sensor->config.bus.context,
                                      configure, sizeof(configure));
    if (result != ST_HAL_OK) {
        return bus_sequence_result(result);
    }

    (void)snprintf(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid),
                   "ds18-%02x%02x%02x%02x%02x%02x%02x%02x",
                   sensor->rom[0], sensor->rom[1], sensor->rom[2], sensor->rom[3],
                   sensor->rom[4], sensor->rom[5], sensor->rom[6], sensor->rom[7]);
    sensor->probed = 1U;
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t finish_conversion(st_ds18b20_t *sensor, uint64_t now_ms)
{
    uint8_t command[2] = {ST_ONEWIRE_SKIP_ROM, ST_DS18B20_READ_SCRATCHPAD};
    uint8_t scratchpad[ST_DS18B20_SCRATCHPAD_SIZE];
    st_hal_result_t result;
    float temperature_c;

    result = sensor->config.bus.reset(sensor->config.bus.context);
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t driver_result = result == ST_HAL_NOT_PRESENT
                                               ? ST_DRIVER_NOT_PRESENT
                                               : ST_DRIVER_ERROR;

        cache_failure(sensor, now_ms, flag, driver_result);
        return cached_result(sensor);
    }
    result = sensor->config.bus.write(sensor->config.bus.context,
                                      command, sizeof(command));
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;

        cache_failure(sensor, now_ms, flag, map_hal_result(result));
        return cached_result(sensor);
    }
    result = sensor->config.bus.read(sensor->config.bus.context,
                                     scratchpad, sizeof(scratchpad));
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;

        cache_failure(sensor, now_ms, flag, map_hal_result(result));
        return cached_result(sensor);
    }
    if (st_ds18b20_crc8(scratchpad, ST_DS18B20_SCRATCHPAD_SIZE - 1U) != scratchpad[8]) {
        cache_failure(sensor, now_ms, ST_QUALITY_CRC_FAILED, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }
    if (st_ds18b20_decode_scratchpad(scratchpad, sensor->config.resolution_bits,
                                     &temperature_c) != 0) {
        cache_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE, ST_DRIVER_ERROR);
        return cached_result(sensor);
    }

    sensor->current_temperature_c = temperature_c;
    sensor->last_valid_temperature_c = temperature_c;
    sensor->current_acquired_at_ms = now_ms;
    sensor->last_valid_at_ms = now_ms;
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_READY;
    sensor->current_has_sample = 1U;
    sensor->has_last_valid = 1U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
    sensor->state = ST_DS18B20_CACHED_RESULT;
    sensor->cache_expires_at_ms = now_ms + sensor->config.cache_validity_ms;
    return ST_DRIVER_READY;
}

static st_driver_result_t ds18b20_acquire(void *context, uint64_t now_ms)
{
    st_ds18b20_t *sensor = (st_ds18b20_t *)context;
    uint8_t command[2] = {ST_ONEWIRE_SKIP_ROM, ST_DS18B20_CONVERT_T};
    st_hal_result_t result;

    if (sensor == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->state == ST_DS18B20_CACHED_RESULT) {
        if (now_ms <= sensor->cache_expires_at_ms) {
            return cached_result(sensor);
        }
        sensor->state = ST_DS18B20_IDLE;
    }
    if (sensor->state == ST_DS18B20_CONVERTING) {
        if (now_ms < sensor->ready_at_ms) {
            return ST_DRIVER_RETRY;
        }
        return finish_conversion(sensor, now_ms);
    }

    sensor->last_attempt_at_ms = now_ms;
    result = sensor->config.bus.reset(sensor->config.bus.context);
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;

        cache_failure(sensor, now_ms, flag, map_hal_result(result));
        return cached_result(sensor);
    }
    result = sensor->config.bus.write(sensor->config.bus.context,
                                      command, sizeof(command));
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;

        cache_failure(sensor, now_ms, flag, map_hal_result(result));
        return cached_result(sensor);
    }

    ++sensor->conversion_commands;
    sensor->state = ST_DS18B20_CONVERTING;
    sensor->ready_at_ms = now_ms +
                          st_ds18b20_conversion_duration_ms(sensor->config.resolution_bits);
    return ST_DRIVER_RETRY;
}

static st_driver_result_t ds18b20_read_channel(void *context, uint8_t channel_index,
                                               st_driver_sample_t *sample)
{
    st_ds18b20_t *sensor = (st_ds18b20_t *)context;

    if (sensor == NULL || sample == NULL ||
        channel_index != ST_DS18B20_TEMPERATURE_CHANNEL ||
        sensor->state != ST_DS18B20_CACHED_RESULT ||
        sensor->current_has_sample == 0U) {
        return ST_DRIVER_ERROR;
    }

    memset(sample, 0, sizeof(*sample));
    sample->value = sensor->current_temperature_c;
    sample->unit = sensor->metadata.channels[channel_index].unit;
    sample->quality_flags = sensor->current_quality_flags;
    sample->acquired_at_ms = sensor->current_acquired_at_ms;
    sample->acquired_at_valid = 1U;
    return ST_DRIVER_READY;
}

static void ds18b20_reset(void *context)
{
    st_ds18b20_t *sensor = (st_ds18b20_t *)context;

    if (sensor == NULL) {
        return;
    }
    sensor->state = ST_DS18B20_IDLE;
    memset(sensor->rom, 0, sizeof(sensor->rom));
    sensor->ready_at_ms = 0U;
    sensor->cache_expires_at_ms = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->last_valid_at_ms = 0U;
    sensor->last_attempt_at_ms = 0U;
    sensor->current_temperature_c = 0.0F;
    sensor->last_valid_temperature_c = 0.0F;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->has_last_valid = 0U;
    sensor->consecutive_failures = 0U;
    sensor->missing_reported = 0U;
}

int st_ds18b20_init(st_ds18b20_t *sensor, const st_ds18b20_config_t *config)
{
    if (sensor == NULL || config == NULL || config->bus.reset == NULL ||
        config->bus.write == NULL || config->bus.read == NULL ||
        st_ds18b20_conversion_duration_ms(config->resolution_bits) == 0U ||
        config->sample_interval_ms == 0U || config->temperature_sensor_id == NULL) {
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    if (sensor->config.cache_validity_ms == 0U) {
        sensor->config.cache_validity_ms = ST_DS18B20_DEFAULT_CACHE_VALIDITY_MS;
    }
    sensor->metadata.sample_interval_ms = config->sample_interval_ms;
    sensor->metadata.channel_count = ST_DS18B20_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[ST_DS18B20_TEMPERATURE_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_DS18B20_TEMPERATURE_CHANNEL].sensor_id),
                config->temperature_sensor_id);
    sensor->metadata.channels[ST_DS18B20_TEMPERATURE_CHANNEL].sensor_kind =
        ST_SENSOR_TEMPERATURE_C;
    sensor->metadata.channels[ST_DS18B20_TEMPERATURE_CHANNEL].unit = ST_UNIT_CELSIUS;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    return 0;
}

st_physical_module_driver_t st_ds18b20_module_driver(st_ds18b20_t *sensor)
{
    st_physical_module_driver_t driver;

    driver.context = sensor;
    driver.probe = ds18b20_probe;
    driver.acquire = ds18b20_acquire;
    driver.read_channel = ds18b20_read_channel;
    driver.reset = ds18b20_reset;
    return driver;
}

uint32_t st_ds18b20_conversion_command_count(const st_ds18b20_t *sensor)
{
    return sensor == NULL ? 0U : sensor->conversion_commands;
}

uint64_t st_ds18b20_last_attempt_at_ms(const st_ds18b20_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_attempt_at_ms;
}
