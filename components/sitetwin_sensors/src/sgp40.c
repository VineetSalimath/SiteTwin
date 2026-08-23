#include "sitetwin/sgp40.h"

#include <stdio.h>
#include <string.h>

#define ST_SGP40_COMMAND_MEASURE_RAW 0x260FU
#define ST_SGP40_COMMAND_HEATER_OFF 0x3615U
#define ST_SGP40_COMMAND_GET_SERIAL 0x3682U
#define ST_SGP40_MEASUREMENT_DURATION_MS 30U
#define ST_SGP40_REGISTRY_RETRY_MS 100U
#define ST_SGP40_MIN_VOC_INDEX 1
#define ST_SGP40_MAX_VOC_INDEX 500

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

uint8_t st_sgp40_crc8(const uint8_t data[2])
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

int st_sgp40_humidity_ticks(float humidity_percent, uint16_t *ticks)
{
    if (ticks == NULL || humidity_percent < 0.0F || humidity_percent > 100.0F) {
        return -1;
    }
    *ticks = (uint16_t)((humidity_percent * 65535.0F / 100.0F) + 0.5F);
    return 0;
}

int st_sgp40_temperature_ticks(float temperature_c, uint16_t *ticks)
{
    if (ticks == NULL || temperature_c < -45.0F || temperature_c > 130.0F) {
        return -1;
    }
    *ticks = (uint16_t)((((temperature_c + 45.0F) * 65535.0F / 175.0F)) + 0.5F);
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

static st_hal_result_t write_command(st_sgp40_t *sensor, uint16_t command)
{
    uint8_t bytes[2];

    encode_command(command, bytes);
    return sensor->config.bus.write(sensor->config.bus.context,
                                    sensor->config.address,
                                    bytes,
                                    sizeof(bytes));
}

static void reset_algorithm(st_sgp40_t *sensor)
{
    st_voc_index_algorithm_reset(&sensor->algorithm);
    sensor->last_process_at_ms = 0U;
}

static void prepare_reprobe(st_sgp40_t *sensor)
{
    sensor->probed = 0U;
    sensor->phase = ST_SGP40_PHASE_PROBE_SEND_SERIAL;
    sensor->state = ST_SGP40_FAULT_REPROBE;
}

static st_driver_result_t stale_or_failure(st_sgp40_t *sensor,
                                           uint64_t now_ms,
                                           uint32_t failure_flag,
                                           st_driver_result_t failure_result,
                                           int reprobe)
{
    sensor->last_attempt_at_ms = now_ms;
    sensor->current_quality_flags = failure_flag;
    sensor->current_failure_result = failure_result;
    sensor->current_has_sample = 0U;
    reset_algorithm(sensor);
    if (reprobe != 0) {
        prepare_reprobe(sensor);
    }

    if (sensor->has_last_valid != 0U) {
        sensor->current_voc_index = sensor->last_valid_voc_index;
        sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
        sensor->current_quality_flags |= ST_QUALITY_STALE;
        sensor->current_has_sample = 1U;
        sensor->reprobe_pending = reprobe != 0 ? 1U : 0U;
        return ST_DRIVER_READY;
    }
    return failure_result;
}

static st_driver_result_t compensation_unavailable(st_sgp40_t *sensor,
                                                    uint64_t now_ms)
{
    if (sensor->state != ST_SGP40_COMPENSATION_UNAVAILABLE) {
        reset_algorithm(sensor);
    }
    sensor->state = ST_SGP40_COMPENSATION_UNAVAILABLE;
    sensor->phase = ST_SGP40_PHASE_WAITING_SAMPLE;
    sensor->next_measurement_at_ms = now_ms + sensor->config.algorithm_interval_ms;
    sensor->current_has_sample = 0U;
    sensor->current_quality_flags = ST_QUALITY_COMPENSATION_UNAVAILABLE;
    if (sensor->has_last_valid == 0U) {
        return ST_DRIVER_RETRY;
    }

    sensor->current_voc_index = sensor->last_valid_voc_index;
    sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
    sensor->current_quality_flags |= ST_QUALITY_STALE;
    sensor->current_has_sample = 1U;
    return ST_DRIVER_READY;
}

static st_driver_result_t sgp40_probe(void *context,
                                      st_physical_module_metadata_t *metadata)
{
    st_sgp40_t *sensor = (st_sgp40_t *)context;
    uint8_t response[9];
    st_hal_result_t result;
    uint16_t serial[3];

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->probed != 0U) {
        *metadata = sensor->metadata;
        return ST_DRIVER_READY;
    }

    sensor->state = ST_SGP40_PROBING;
    if (sensor->phase == ST_SGP40_PHASE_PROBE_SEND_SERIAL) {
        result = write_command(sensor, ST_SGP40_COMMAND_GET_SERIAL);
        if (result == ST_HAL_OK) {
            sensor->phase = ST_SGP40_PHASE_PROBE_READ_SERIAL;
            return ST_DRIVER_RETRY;
        }
        return map_hal_result(result);
    }
    if (sensor->phase != ST_SGP40_PHASE_PROBE_READ_SERIAL) {
        prepare_reprobe(sensor);
        return ST_DRIVER_ERROR;
    }

    result = sensor->config.bus.read(sensor->config.bus.context,
                                     sensor->config.address,
                                     response,
                                     sizeof(response));
    if (result != ST_HAL_OK) {
        if (result != ST_HAL_BUSY && result != ST_HAL_TIMEOUT) {
            sensor->phase = ST_SGP40_PHASE_PROBE_SEND_SERIAL;
        }
        return map_hal_result(result);
    }
    if (st_sgp40_crc8(&response[0]) != response[2] ||
        st_sgp40_crc8(&response[3]) != response[5] ||
        st_sgp40_crc8(&response[6]) != response[8]) {
        prepare_reprobe(sensor);
        return ST_DRIVER_ERROR;
    }

    serial[0] = decode_word(&response[0]);
    serial[1] = decode_word(&response[3]);
    serial[2] = decode_word(&response[6]);
    (void)snprintf(sensor->metadata.module_uid,
                   sizeof(sensor->metadata.module_uid),
                   "sgp40-%04x%04x%04x",
                   serial[0], serial[1], serial[2]);
    sensor->probed = 1U;
    sensor->phase = ST_SGP40_PHASE_WAITING_SAMPLE;
    sensor->state = ST_SGP40_WAITING_COMPENSATION;
    sensor->next_measurement_at_ms = 0U;
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t warming_result(st_sgp40_t *sensor)
{
    sensor->state = ST_SGP40_ALGORITHM_WARMING;
    sensor->current_has_sample = 0U;
    sensor->current_quality_flags = ST_QUALITY_WARMING_UP;
    if (sensor->has_last_valid == 0U) {
        return ST_DRIVER_RETRY;
    }

    sensor->current_voc_index = sensor->last_valid_voc_index;
    sensor->current_acquired_at_ms = sensor->last_valid_at_ms;
    sensor->current_quality_flags |= ST_QUALITY_STALE;
    sensor->current_has_sample = 1U;
    return ST_DRIVER_READY;
}

static st_driver_result_t finish_measurement(st_sgp40_t *sensor,
                                              uint64_t now_ms)
{
    uint8_t response[3];
    st_hal_result_t result;
    uint16_t raw_signal;
    int32_t voc_index;
    st_voc_algorithm_result_t algorithm_result;

    result = sensor->config.bus.read(sensor->config.bus.context,
                                     sensor->config.address,
                                     response,
                                     sizeof(response));
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return ST_DRIVER_RETRY;
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t failure = result == ST_HAL_NOT_PRESENT
                                         ? ST_DRIVER_NOT_PRESENT
                                         : ST_DRIVER_ERROR;

        return stale_or_failure(sensor, now_ms, flag, failure, 1);
    }
    if (st_sgp40_crc8(response) != response[2]) {
        return stale_or_failure(sensor, now_ms, ST_QUALITY_CRC_FAILED,
                                ST_DRIVER_ERROR, 1);
    }

    raw_signal = decode_word(response);
    sensor->last_raw_signal = raw_signal;
    sensor->last_attempt_at_ms = now_ms;
    if (raw_signal == 0U || raw_signal >= 65000U) {
        return stale_or_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE,
                                ST_DRIVER_ERROR, 1);
    }
    /* Real scheduling never lands on an exact millisecond match (see
     * sensor_registry.c: next_sample_at_ms is a floor, not an exact
     * trigger -- actual elapsed time is always interval_ms plus some
     * scheduling jitter). A strict equality check here means almost
     * every real measurement cycle looks "abnormal" and resets the
     * algorithm before it can ever finish GasIndexAlgorithm's 45-second
     * warm-up -- confirmed on real hardware: SGP40 produced zero
     * telemetry indefinitely, not just during warm-up. Tolerate up to
     * 50% jitter in either direction; still reset on a genuine gap
     * (e.g. a fault/reprobe stall lasting more than 1.5x the interval),
     * which is what this check was presumably guarding against. */
    if (sensor->last_process_at_ms != 0U) {
        uint64_t elapsed_since_last_process = now_ms - sensor->last_process_at_ms;
        uint64_t interval_low = sensor->config.algorithm_interval_ms / 2U;
        uint64_t interval_high =
            sensor->config.algorithm_interval_ms + sensor->config.algorithm_interval_ms / 2U;

        if (elapsed_since_last_process < interval_low ||
            elapsed_since_last_process > interval_high) {
            reset_algorithm(sensor);
        }
    }

    algorithm_result = st_voc_index_algorithm_process(&sensor->algorithm,
                                                       raw_signal,
                                                       &voc_index);
    sensor->last_process_at_ms = now_ms;
    sensor->phase = ST_SGP40_PHASE_WAITING_SAMPLE;
    if (algorithm_result != ST_VOC_ALGORITHM_READY) {
        return warming_result(sensor);
    }
    if (voc_index < ST_SGP40_MIN_VOC_INDEX || voc_index > ST_SGP40_MAX_VOC_INDEX) {
        return stale_or_failure(sensor, now_ms, ST_QUALITY_OUT_OF_RANGE,
                                ST_DRIVER_ERROR, 1);
    }

    sensor->current_voc_index = (float)voc_index;
    sensor->last_valid_voc_index = sensor->current_voc_index;
    sensor->current_acquired_at_ms = now_ms;
    sensor->last_valid_at_ms = now_ms;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_READY;
    sensor->current_has_sample = 1U;
    sensor->has_last_valid = 1U;
    sensor->reprobe_pending = 0U;
    sensor->state = ST_SGP40_READY;
    return ST_DRIVER_READY;
}

static st_driver_result_t start_measurement(st_sgp40_t *sensor,
                                             uint64_t now_ms)
{
    st_sgp40_compensation_t compensation;
    uint8_t command[8];
    st_hal_result_t result;
    uint16_t humidity_ticks;
    uint16_t temperature_ticks;

    memset(&compensation, 0, sizeof(compensation));
    if (sensor->config.compensation_provider(sensor->config.compensation_context,
                                             now_ms,
                                             &compensation) != 0 ||
        now_ms < compensation.acquired_at_ms ||
        now_ms - compensation.acquired_at_ms >
            sensor->config.compensation_maximum_age_ms ||
        st_sgp40_humidity_ticks(compensation.humidity_percent, &humidity_ticks) != 0 ||
        st_sgp40_temperature_ticks(compensation.temperature_c, &temperature_ticks) != 0) {
        return compensation_unavailable(sensor, now_ms);
    }

    encode_command(ST_SGP40_COMMAND_MEASURE_RAW, command);
    command[2] = (uint8_t)(humidity_ticks >> 8U);
    command[3] = (uint8_t)humidity_ticks;
    command[4] = st_sgp40_crc8(&command[2]);
    command[5] = (uint8_t)(temperature_ticks >> 8U);
    command[6] = (uint8_t)temperature_ticks;
    command[7] = st_sgp40_crc8(&command[5]);
    sensor->last_attempt_at_ms = now_ms;
    result = sensor->config.bus.write(sensor->config.bus.context,
                                      sensor->config.address,
                                      command,
                                      sizeof(command));
    if (result == ST_HAL_BUSY || result == ST_HAL_TIMEOUT) {
        return stale_or_failure(sensor, now_ms, 0U, ST_DRIVER_RETRY, 0);
    }
    if (result != ST_HAL_OK) {
        uint32_t flag = result == ST_HAL_NOT_PRESENT ? ST_QUALITY_SENSOR_MISSING : 0U;
        st_driver_result_t failure = result == ST_HAL_NOT_PRESENT
                                         ? ST_DRIVER_NOT_PRESENT
                                         : ST_DRIVER_ERROR;

        return stale_or_failure(sensor, now_ms, flag, failure, 1);
    }

    sensor->last_humidity_ticks = humidity_ticks;
    sensor->last_temperature_ticks = temperature_ticks;
    ++sensor->measurement_commands;
    sensor->measurement_ready_at_ms = now_ms + ST_SGP40_MEASUREMENT_DURATION_MS;
    sensor->next_measurement_at_ms = now_ms + sensor->config.algorithm_interval_ms;
    sensor->phase = ST_SGP40_PHASE_MEASUREMENT_RESPONSE;
    sensor->state = ST_SGP40_MEASURING;
    return ST_DRIVER_RETRY;
}

static st_driver_result_t sgp40_acquire(void *context, uint64_t now_ms)
{
    st_sgp40_t *sensor = (st_sgp40_t *)context;

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
    if (sensor->phase == ST_SGP40_PHASE_MEASUREMENT_RESPONSE) {
        if (now_ms < sensor->measurement_ready_at_ms) {
            return ST_DRIVER_RETRY;
        }
        return finish_measurement(sensor, now_ms);
    }
    if (sensor->phase != ST_SGP40_PHASE_WAITING_SAMPLE) {
        return stale_or_failure(sensor, now_ms, 0U, ST_DRIVER_ERROR, 1);
    }
    if (now_ms < sensor->next_measurement_at_ms) {
        return ST_DRIVER_RETRY;
    }
    return start_measurement(sensor, now_ms);
}

static st_driver_result_t sgp40_read_channel(void *context,
                                              uint8_t channel_index,
                                              st_driver_sample_t *sample)
{
    st_sgp40_t *sensor = (st_sgp40_t *)context;

    if (sensor == NULL || sample == NULL ||
        channel_index != ST_SGP40_VOC_INDEX_CHANNEL ||
        sensor->current_has_sample == 0U) {
        return ST_DRIVER_ERROR;
    }

    memset(sample, 0, sizeof(*sample));
    sample->value = sensor->current_voc_index;
    sample->unit = ST_UNIT_INDEX;
    sample->quality_flags = sensor->current_quality_flags;
    sample->acquired_at_ms = sensor->current_acquired_at_ms;
    sample->acquired_at_valid = 1U;
    return ST_DRIVER_READY;
}

static void sgp40_reset(void *context)
{
    st_sgp40_t *sensor = (st_sgp40_t *)context;

    if (sensor == NULL) {
        return;
    }
    if (sensor->probed != 0U) {
        (void)write_command(sensor, ST_SGP40_COMMAND_HEATER_OFF);
    }
    reset_algorithm(sensor);
    sensor->state = ST_SGP40_DETACHED;
    sensor->phase = ST_SGP40_PHASE_PROBE_SEND_SERIAL;
    sensor->measurement_ready_at_ms = 0U;
    sensor->next_measurement_at_ms = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->last_valid_at_ms = 0U;
    sensor->last_attempt_at_ms = 0U;
    sensor->current_voc_index = 0.0F;
    sensor->last_valid_voc_index = 0.0F;
    sensor->last_raw_signal = 0U;
    sensor->last_humidity_ticks = 0U;
    sensor->last_temperature_ticks = 0U;
    sensor->current_quality_flags = 0U;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->has_last_valid = 0U;
    sensor->reprobe_pending = 0U;
}

int st_sgp40_init(st_sgp40_t *sensor, const st_sgp40_config_t *config)
{
    uint32_t registry_interval_ms;

    if (sensor == NULL || config == NULL || config->bus.write == NULL ||
        config->bus.read == NULL || config->address > 0x7FU ||
        (config->algorithm_interval_ms != 1000U &&
         config->algorithm_interval_ms != 10000U) ||
        config->compensation_maximum_age_ms == 0U ||
        config->compensation_provider == NULL ||
        config->voc_index_sensor_id == NULL) {
        return -1;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    if (st_voc_index_algorithm_init(&sensor->algorithm,
                                    config->algorithm_interval_ms) != 0) {
        return -1;
    }
    registry_interval_ms = config->algorithm_interval_ms - ST_SGP40_REGISTRY_RETRY_MS;
    sensor->metadata.sample_interval_ms = registry_interval_ms;
    sensor->metadata.channel_count = ST_SGP40_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[ST_SGP40_VOC_INDEX_CHANNEL].sensor_id,
                sizeof(sensor->metadata.channels[ST_SGP40_VOC_INDEX_CHANNEL].sensor_id),
                config->voc_index_sensor_id);
    sensor->metadata.channels[ST_SGP40_VOC_INDEX_CHANNEL].sensor_kind =
        ST_SENSOR_VOC_INDEX;
    sensor->metadata.channels[ST_SGP40_VOC_INDEX_CHANNEL].unit = ST_UNIT_INDEX;
    sensor->state = ST_SGP40_UNINITIALISED;
    sensor->phase = ST_SGP40_PHASE_PROBE_SEND_SERIAL;
    sensor->current_failure_result = ST_DRIVER_ERROR;
    return 0;
}

st_physical_module_driver_t st_sgp40_module_driver(st_sgp40_t *sensor)
{
    st_physical_module_driver_t driver;

    driver.context = sensor;
    driver.probe = sgp40_probe;
    driver.acquire = sgp40_acquire;
    driver.read_channel = sgp40_read_channel;
    driver.reset = sgp40_reset;
    return driver;
}

st_sgp40_state_t st_sgp40_state(const st_sgp40_t *sensor)
{
    return sensor == NULL ? ST_SGP40_UNINITIALISED : sensor->state;
}

uint16_t st_sgp40_last_raw_signal(const st_sgp40_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_raw_signal;
}

uint32_t st_sgp40_measurement_command_count(const st_sgp40_t *sensor)
{
    return sensor == NULL ? 0U : sensor->measurement_commands;
}

uint64_t st_sgp40_last_attempt_at_ms(const st_sgp40_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_attempt_at_ms;
}
