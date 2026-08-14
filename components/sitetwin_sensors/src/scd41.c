#include "sitetwin/scd41.h"

#include <stdio.h>
#include <string.h>

#define ST_SCD41_COMMAND_START_PERIODIC 0x21B1U
#define ST_SCD41_COMMAND_START_LOW_POWER_PERIODIC 0x21ACU
#define ST_SCD41_COMMAND_READ_MEASUREMENT 0xEC05U
#define ST_SCD41_COMMAND_STOP_PERIODIC 0x3F86U
#define ST_SCD41_COMMAND_GET_DATA_READY 0xE4B8U
#define ST_SCD41_COMMAND_GET_SERIAL 0x3682U
#define ST_SCD41_COMMAND_RESPONSE_DELAY_MS 1U
#define ST_SCD41_MIN_CO2_PPM 1U
#define ST_SCD41_MAX_CO2_PPM 40000U

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

static void encode_command(uint16_t command, uint8_t output[2])
{
    output[0] = (uint8_t)(command >> 8U);
    output[1] = (uint8_t)command;
}

static uint16_t decode_word(const uint8_t input[2])
{
    return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

uint8_t st_scd41_crc8(const uint8_t data[2])
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

float st_scd41_raw_co2_ppm(uint16_t raw)
{
    return (float)raw;
}

static st_hal_result_t write_command(st_scd41_t *sensor, uint16_t command)
{
    uint8_t bytes[2];

    encode_command(command, bytes);
    return sensor->config.bus.write(sensor->config.bus.context,
                                    sensor->config.address, bytes, sizeof(bytes));
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

static int response_crc_valid(const uint8_t *response, size_t word_count)
{
    size_t index;

    for (index = 0U; index < word_count; ++index) {
        const uint8_t *word = &response[index * 3U];

        if (st_scd41_crc8(word) != word[2]) {
            return 0;
        }
    }
    return 1;
}

static void prepare_reprobe(st_scd41_t *sensor)
{
    sensor->probed = 0U;
    sensor->phase = ST_SCD41_PHASE_PROBE_SEND_SERIAL;
    sensor->state = ST_SCD41_FAULT_REPROBE;
}

static st_driver_result_t cache_failure(st_scd41_t *sensor, uint64_t now_ms,
                                        uint32_t failure_flag,
                                        st_driver_result_t failure_result)
{
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = failure_flag;
    sensor->current_failure_result = failure_result;
    sensor->current_has_sample = 0U;
    prepare_reprobe(sensor);

    if (sensor->has_last_valid != 0U) {
        sensor->current_co2_ppm = sensor->last_valid_co2_ppm;
        sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
        sensor->current_quality_flags |= ST_QUALITY_STALE;
        sensor->current_has_sample = 1U;
        sensor->reprobe_pending = 1U;
        return ST_DRIVER_READY;
    }
    return failure_result;
}

static st_driver_result_t begin_operating_mode(st_scd41_t *sensor)
{
    uint16_t command = sensor->config.measurement_mode == ST_SCD41_MODE_LOW_POWER_PERIODIC
                           ? ST_SCD41_COMMAND_START_LOW_POWER_PERIODIC
                           : ST_SCD41_COMMAND_START_PERIODIC;
    st_hal_result_t result;

    sensor->state = ST_SCD41_STARTING;
    result = write_command(sensor, command);
    if (result != ST_HAL_OK) {
        prepare_reprobe(sensor);
        return map_hal_result(result);
    }

    ++sensor->start_commands;
    sensor->probed = 1U;
    sensor->state = ST_SCD41_WARMING_UP;
    sensor->phase = ST_SCD41_PHASE_STATUS_COMMAND;
    sensor->next_action_at_ms = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    return ST_DRIVER_READY;
}

static st_driver_result_t probe_send_serial(st_scd41_t *sensor)
{
    st_hal_result_t result;

    sensor->state = ST_SCD41_PROBING;
    result = write_command(sensor, ST_SCD41_COMMAND_GET_SERIAL);
    if (result == ST_HAL_OK) {
        sensor->phase = ST_SCD41_PHASE_PROBE_READ_SERIAL;
        return ST_DRIVER_RETRY;
    }

    if (result == ST_HAL_NOT_PRESENT || result == ST_HAL_IO_ERROR) {
        st_hal_result_t stop_result = write_command(sensor, ST_SCD41_COMMAND_STOP_PERIODIC);

        if (stop_result == ST_HAL_OK) {
            sensor->phase = ST_SCD41_PHASE_PROBE_AFTER_STOP;
            return ST_DRIVER_RETRY;
        }
        if (result == ST_HAL_NOT_PRESENT && stop_result == ST_HAL_NOT_PRESENT) {
            return ST_DRIVER_NOT_PRESENT;
        }
        return map_hal_result(stop_result);
    }
    return map_hal_result(result);
}

static st_driver_result_t scd41_probe(void *context,
                                      st_physical_module_metadata_t *metadata)
{
    st_scd41_t *sensor = (st_scd41_t *)context;
    uint8_t response[9];
    st_hal_result_t result;
    st_driver_result_t start_result;
    uint16_t serial[3];

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->probed != 0U) {
        *metadata = sensor->metadata;
        return ST_DRIVER_READY;
    }

    if (sensor->phase == ST_SCD41_PHASE_PROBE_AFTER_STOP) {
        sensor->phase = ST_SCD41_PHASE_PROBE_SEND_SERIAL;
    }
    if (sensor->phase == ST_SCD41_PHASE_PROBE_SEND_SERIAL) {
        return probe_send_serial(sensor);
    }
    if (sensor->phase != ST_SCD41_PHASE_PROBE_READ_SERIAL) {
        prepare_reprobe(sensor);
        return ST_DRIVER_ERROR;
    }

    result = sensor->config.bus.read(sensor->config.bus.context,
                                     sensor->config.address,
                                     response, sizeof(response));
    if (result != ST_HAL_OK) {
        sensor->phase = ST_SCD41_PHASE_PROBE_SEND_SERIAL;
        return map_hal_result(result);
    }
    if (response_crc_valid(response, 3U) == 0) {
        prepare_reprobe(sensor);
        return ST_DRIVER_ERROR;
    }

    serial[0] = decode_word(&response[0]);
    serial[1] = decode_word(&response[3]);
    serial[2] = decode_word(&response[6]);
    (void)snprintf(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid),
                   "scd41-%04x%04x%04x", serial[0], serial[1], serial[2]);
    start_result = begin_operating_mode(sensor);
    if (start_result != ST_DRIVER_READY) {
        return start_result;
    }
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t finish_data_ready_status(st_scd41_t *sensor,
                                                    uint64_t now_ms)
{
    uint8_t response[3];
    st_hal_result_t result;
    uint16_t status;

    result = sensor->config.bus.read(sensor->config.bus.context,
                                     sensor->config.address,
                                     response, sizeof(response));
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t failure = result == ST_HAL_NOT_PRESENT
                                         ? ST_DRIVER_NOT_PRESENT
                                         : ST_DRIVER_ERROR;

        return cache_failure(sensor, now_ms, flag, failure);
    }
    if (response_crc_valid(response, 1U) == 0) {
        return cache_failure(sensor, now_ms, ST_QUALITY_CRC_FAILED, ST_DRIVER_ERROR);
    }

    status = decode_word(response);
    if ((status & 0x07FFU) == 0U) {
        sensor->phase = ST_SCD41_PHASE_STATUS_COMMAND;
        sensor->next_action_at_ms = now_ms + sensor->config.poll_interval_ms;
        sensor->state = sensor->has_last_valid != 0U ? ST_SCD41_WAITING_DATA_READY
                                                      : ST_SCD41_WARMING_UP;
        return ST_DRIVER_RETRY;
    }

    result = write_command(sensor, ST_SCD41_COMMAND_READ_MEASUREMENT);
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t failure = result == ST_HAL_NOT_PRESENT
                                         ? ST_DRIVER_NOT_PRESENT
                                         : map_hal_result(result);

        if (failure == ST_DRIVER_RETRY) {
            /* The data-ready response was consumed, so restart that transaction. */
            sensor->phase = ST_SCD41_PHASE_STATUS_COMMAND;
            sensor->next_action_at_ms = now_ms + sensor->config.poll_interval_ms;
            sensor->state = sensor->has_last_valid != 0U
                                ? ST_SCD41_WAITING_DATA_READY
                                : ST_SCD41_WARMING_UP;
            return ST_DRIVER_RETRY;
        }
        return cache_failure(sensor, now_ms, flag, failure);
    }
    sensor->phase = ST_SCD41_PHASE_MEASUREMENT_RESPONSE;
    sensor->next_action_at_ms = now_ms + ST_SCD41_COMMAND_RESPONSE_DELAY_MS;
    sensor->state = ST_SCD41_READING;
    return ST_DRIVER_RETRY;
}

static st_driver_result_t finish_measurement(st_scd41_t *sensor, uint64_t now_ms)
{
    uint8_t response[9];
    st_hal_result_t result;
    uint16_t raw_co2;

    result = sensor->config.bus.read(sensor->config.bus.context,
                                     sensor->config.address,
                                     response, sizeof(response));
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t failure = result == ST_HAL_NOT_PRESENT
                                         ? ST_DRIVER_NOT_PRESENT
                                         : ST_DRIVER_ERROR;

        return cache_failure(sensor, now_ms, flag, failure);
    }
    if (response_crc_valid(response, 3U) == 0) {
        return cache_failure(sensor, now_ms, ST_QUALITY_CRC_FAILED, ST_DRIVER_ERROR);
    }

    raw_co2 = decode_word(&response[0]);
    if (raw_co2 < ST_SCD41_MIN_CO2_PPM || raw_co2 > ST_SCD41_MAX_CO2_PPM) {
        return cache_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE, ST_DRIVER_ERROR);
    }

    sensor->current_co2_ppm = st_scd41_raw_co2_ppm(raw_co2);
    sensor->last_valid_co2_ppm = sensor->current_co2_ppm;
    sensor->current_acquired_at_ms = now_ms;
    sensor->last_valid_at_ms = now_ms;
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_READY;
    sensor->current_has_sample = 1U;
    sensor->has_last_valid = 1U;
    sensor->reprobe_pending = 0U;
    sensor->state = ST_SCD41_READY;
    sensor->phase = ST_SCD41_PHASE_STATUS_COMMAND;
    sensor->next_action_at_ms = now_ms + sensor->config.poll_interval_ms;
    return ST_DRIVER_READY;
}

static st_driver_result_t scd41_acquire(void *context, uint64_t now_ms)
{
    st_scd41_t *sensor = (st_scd41_t *)context;
    st_hal_result_t result;

    if (sensor == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->reprobe_pending != 0U) {
        st_driver_result_t failure = sensor->current_failure_result;

        sensor->reprobe_pending = 0U;
        sensor->current_has_sample = 0U;
        return failure;
    }
    if (sensor->probed == 0U) {
        return ST_DRIVER_NOT_PRESENT;
    }
    if (now_ms < sensor->next_action_at_ms) {
        return ST_DRIVER_RETRY;
    }

    if (sensor->phase == ST_SCD41_PHASE_STATUS_RESPONSE) {
        return finish_data_ready_status(sensor, now_ms);
    }
    if (sensor->phase == ST_SCD41_PHASE_MEASUREMENT_RESPONSE) {
        return finish_measurement(sensor, now_ms);
    }
    if (sensor->phase != ST_SCD41_PHASE_STATUS_COMMAND) {
        return cache_failure(sensor, now_ms, 0U, ST_DRIVER_ERROR);
    }

    sensor->last_attempt_at_ms = now_ms;
    result = write_command(sensor, ST_SCD41_COMMAND_GET_DATA_READY);
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t failure = result == ST_HAL_NOT_PRESENT
                                         ? ST_DRIVER_NOT_PRESENT
                                         : ST_DRIVER_ERROR;

        return cache_failure(sensor, now_ms, flag, failure);
    }

    sensor->phase = ST_SCD41_PHASE_STATUS_RESPONSE;
    sensor->next_action_at_ms = now_ms + ST_SCD41_COMMAND_RESPONSE_DELAY_MS;
    sensor->state = sensor->has_last_valid != 0U ? ST_SCD41_WAITING_DATA_READY
                                                  : ST_SCD41_WARMING_UP;
    return ST_DRIVER_RETRY;
}

static st_driver_result_t scd41_read_channel(void *context, uint8_t channel_index,
                                              st_driver_sample_t *sample)
{
    st_scd41_t *sensor = (st_scd41_t *)context;

    if (sensor == NULL || sample == NULL || channel_index != ST_SCD41_CO2_CHANNEL ||
        sensor->current_has_sample == 0U) {
        return ST_DRIVER_ERROR;
    }

    memset(sample, 0, sizeof(*sample));
    sample->value = sensor->current_co2_ppm;
    sample->unit = ST_UNIT_PPM;
    sample->quality_flags = sensor->current_quality_flags;
    sample->acquired_at_ms = sensor->current_acquired_at_ms;
    sample->acquired_at_valid = 1U;
    return ST_DRIVER_READY;
}

static void scd41_reset(void *context)
{
    st_scd41_t *sensor = (st_scd41_t *)context;

    if (sensor == NULL) {
        return;
    }
    sensor->state = ST_SCD41_DETACHED;
    sensor->phase = ST_SCD41_PHASE_PROBE_SEND_SERIAL;
    sensor->next_action_at_ms = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->last_valid_at_ms = 0U;
    sensor->last_attempt_at_ms = 0U;
    sensor->current_co2_ppm = 0.0F;
    sensor->last_valid_co2_ppm = 0.0F;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->has_last_valid = 0U;
    sensor->reprobe_pending = 0U;
}

int st_scd41_init(st_scd41_t *sensor, const st_scd41_config_t *config)
{
    if (sensor == NULL || config == NULL || config->bus.write == NULL ||
        config->bus.read == NULL || config->address > 0x7FU ||
        config->measurement_mode > ST_SCD41_MODE_LOW_POWER_PERIODIC ||
        config->poll_interval_ms == 0U || config->co2_sensor_id == NULL) {
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    sensor->state = ST_SCD41_UNINITIALISED;
    sensor->phase = ST_SCD41_PHASE_PROBE_SEND_SERIAL;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->metadata.sample_interval_ms = config->poll_interval_ms;
    sensor->metadata.channel_count = ST_SCD41_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[ST_SCD41_CO2_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_SCD41_CO2_CHANNEL].sensor_id),
                config->co2_sensor_id);
    sensor->metadata.channels[ST_SCD41_CO2_CHANNEL].sensor_kind = ST_SENSOR_CO2_PPM;
    sensor->metadata.channels[ST_SCD41_CO2_CHANNEL].unit = ST_UNIT_PPM;
    return 0;
}

st_physical_module_driver_t st_scd41_module_driver(st_scd41_t *sensor)
{
    st_physical_module_driver_t driver;

    driver.context = sensor;
    driver.probe = scd41_probe;
    driver.acquire = scd41_acquire;
    driver.read_channel = scd41_read_channel;
    driver.reset = scd41_reset;
    return driver;
}

st_scd41_state_t st_scd41_state(const st_scd41_t *sensor)
{
    return sensor == NULL ? ST_SCD41_UNINITIALISED : sensor->state;
}

uint32_t st_scd41_start_command_count(const st_scd41_t *sensor)
{
    return sensor == NULL ? 0U : sensor->start_commands;
}

uint64_t st_scd41_last_attempt_at_ms(const st_scd41_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_attempt_at_ms;
}
