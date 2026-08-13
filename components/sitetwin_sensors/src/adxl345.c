#include "sitetwin/adxl345.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ST_ADXL345_REG_DEVID 0x00U
#define ST_ADXL345_REG_BW_RATE 0x2CU
#define ST_ADXL345_REG_POWER_CTL 0x2DU
#define ST_ADXL345_REG_DATA_FORMAT 0x31U
#define ST_ADXL345_REG_DATAX0 0x32U
#define ST_ADXL345_REG_FIFO_CTL 0x38U
#define ST_ADXL345_REG_FIFO_STATUS 0x39U

#define ST_ADXL345_POWER_MEASURE 0x08U
#define ST_ADXL345_FULL_RESOLUTION 0x08U
#define ST_ADXL345_FIFO_STREAM_MODE 0x80U
#define ST_ADXL345_FIFO_ENTRIES_MASK 0x3FU
#define ST_ADXL345_CLIP_POSITIVE 4095
#define ST_ADXL345_CLIP_NEGATIVE (-4096)

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

static st_driver_result_t map_hal_result(st_hal_result_t result)
{
    switch (result) {
    case ST_HAL_OK: return ST_DRIVER_READY;
    case ST_HAL_NOT_PRESENT: return ST_DRIVER_NOT_PRESENT;
    case ST_HAL_BUSY:
    case ST_HAL_TIMEOUT: return ST_DRIVER_RETRY;
    default: return ST_DRIVER_ERROR;
    }
}

static st_hal_result_t read_registers(st_adxl345_t *sensor, uint8_t reg,
                                      uint8_t *data, size_t length)
{
    st_hal_result_t result = sensor->config.bus.write(sensor->config.bus.context,
                                                       sensor->config.address, &reg, 1U);
    if (result != ST_HAL_OK) {
        return result;
    }
    return sensor->config.bus.read(sensor->config.bus.context,
                                   sensor->config.address, data, length);
}

static st_hal_result_t write_register(st_adxl345_t *sensor, uint8_t reg, uint8_t value)
{
    uint8_t payload[2] = {reg, value};
    return sensor->config.bus.write(sensor->config.bus.context,
                                    sensor->config.address, payload, sizeof(payload));
}

static uint8_t range_code(uint8_t range_g)
{
    switch (range_g) {
    case 2U: return 0U;
    case 4U: return 1U;
    case 8U: return 2U;
    case 16U: return 3U;
    default: return 0xFFU;
    }
}

float st_adxl345_window_rms_g(const int16_t *xyz, uint8_t sample_count,
                              float g_per_lsb)
{
    double sums[3] = {0.0, 0.0, 0.0};
    double sum_squares = 0.0;
    double mean_square;
    uint8_t index;

    if (xyz == NULL || sample_count < 2U || g_per_lsb <= 0.0F) {
        return 0.0F;
    }
    for (index = 0U; index < sample_count; ++index) {
        double x = (double)xyz[(size_t)index * 3U] * g_per_lsb;
        double y = (double)xyz[(size_t)index * 3U + 1U] * g_per_lsb;
        double z = (double)xyz[(size_t)index * 3U + 2U] * g_per_lsb;
        sums[0] += x;
        sums[1] += y;
        sums[2] += z;
        sum_squares += x * x + y * y + z * z;
    }
    mean_square = sum_squares / sample_count;
    mean_square -= (sums[0] / sample_count) * (sums[0] / sample_count);
    mean_square -= (sums[1] / sample_count) * (sums[1] / sample_count);
    mean_square -= (sums[2] / sample_count) * (sums[2] / sample_count);
    if (mean_square < 0.0 && mean_square > -1e-9) {
        mean_square = 0.0;
    }
    return mean_square <= 0.0 ? 0.0F : (float)sqrt(mean_square);
}

static st_driver_result_t adxl345_probe(void *context,
                                        st_physical_module_metadata_t *metadata)
{
    st_adxl345_t *sensor = (st_adxl345_t *)context;
    uint8_t device_id;
    st_hal_result_t result;
    uint8_t format;

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (sensor->probed != 0U) {
        *metadata = sensor->metadata;
        return ST_DRIVER_READY;
    }
    result = read_registers(sensor, ST_ADXL345_REG_DEVID, &device_id, 1U);
    if (result != ST_HAL_OK) {
        return map_hal_result(result);
    }
    if (device_id != ST_ADXL345_DEVICE_ID) {
        return ST_DRIVER_NOT_PRESENT;
    }
    format = (uint8_t)(ST_ADXL345_FULL_RESOLUTION | range_code(sensor->config.range_g));
    result = write_register(sensor, ST_ADXL345_REG_POWER_CTL, 0U);
    if (result == ST_HAL_OK) result = write_register(sensor, ST_ADXL345_REG_BW_RATE,
                                                      sensor->config.rate_code);
    if (result == ST_HAL_OK) result = write_register(sensor, ST_ADXL345_REG_DATA_FORMAT, format);
    if (result == ST_HAL_OK) result = write_register(sensor, ST_ADXL345_REG_FIFO_CTL,
                                                      (uint8_t)(ST_ADXL345_FIFO_STREAM_MODE |
                                                                sensor->config.minimum_window_samples));
    if (result == ST_HAL_OK) result = write_register(sensor, ST_ADXL345_REG_POWER_CTL,
                                                      ST_ADXL345_POWER_MEASURE);
    if (result != ST_HAL_OK) {
        return map_hal_result(result);
    }
    (void)snprintf(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid),
                   "adxl345-%02x", sensor->config.address);
    sensor->probed = 1U;
    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t adxl345_acquire(void *context, uint64_t now_ms)
{
    st_adxl345_t *sensor = (st_adxl345_t *)context;
    int16_t xyz[ST_ADXL345_FIFO_CAPACITY * 3U];
    uint8_t status;
    uint8_t entries;
    uint8_t index;
    uint8_t clipped = 0U;
    st_hal_result_t result;

    if (sensor == NULL) {
        return ST_DRIVER_ERROR;
    }
    result = read_registers(sensor, ST_ADXL345_REG_FIFO_STATUS, &status, 1U);
    if (result != ST_HAL_OK) {
        sensor->current_has_sample = 0U;
        return map_hal_result(result);
    }
    entries = (uint8_t)(status & ST_ADXL345_FIFO_ENTRIES_MASK);
    if (entries > ST_ADXL345_FIFO_CAPACITY) {
        entries = ST_ADXL345_FIFO_CAPACITY;
    }
    if (entries < sensor->config.minimum_window_samples) {
        return ST_DRIVER_RETRY;
    }
    for (index = 0U; index < entries; ++index) {
        uint8_t data[6];
        uint8_t axis;
        result = read_registers(sensor, ST_ADXL345_REG_DATAX0, data, sizeof(data));
        if (result != ST_HAL_OK) {
            sensor->current_has_sample = 0U;
            return map_hal_result(result);
        }
        for (axis = 0U; axis < 3U; ++axis) {
            int16_t raw = (int16_t)((uint16_t)data[axis * 2U] |
                                    ((uint16_t)data[axis * 2U + 1U] << 8U));
            xyz[(size_t)index * 3U + axis] = raw;
            if (raw >= ST_ADXL345_CLIP_POSITIVE || raw <= ST_ADXL345_CLIP_NEGATIVE) {
                clipped = 1U;
            }
        }
    }
    sensor->current_vibration_rms_g =
        st_adxl345_window_rms_g(xyz, entries, sensor->config.g_per_lsb);
    sensor->current_quality_flags = clipped != 0U ? ST_QUALITY_CLIPPED : 0U;
    sensor->current_acquired_at_ms = now_ms;
    sensor->current_sample_count = entries;
    sensor->current_has_sample = 1U;
    ++sensor->windows_completed;
    return ST_DRIVER_READY;
}

static st_driver_result_t adxl345_read_channel(void *context, uint8_t channel_index,
                                               st_driver_sample_t *sample)
{
    st_adxl345_t *sensor = (st_adxl345_t *)context;
    if (sensor == NULL || sample == NULL || channel_index != ST_ADXL345_VIBRATION_CHANNEL ||
        sensor->current_has_sample == 0U) {
        return ST_DRIVER_ERROR;
    }
    memset(sample, 0, sizeof(*sample));
    sample->value = sensor->current_vibration_rms_g;
    sample->unit = ST_UNIT_G;
    sample->quality_flags = sensor->current_quality_flags;
    sample->acquired_at_ms = sensor->current_acquired_at_ms;
    sample->acquired_at_valid = 1U;
    return ST_DRIVER_READY;
}

static void adxl345_reset(void *context)
{
    st_adxl345_t *sensor = (st_adxl345_t *)context;
    if (sensor == NULL) {
        return;
    }
    sensor->current_vibration_rms_g = 0.0F;
    sensor->current_quality_flags = 0U;
    sensor->current_acquired_at_ms = 0U;
    sensor->windows_completed = 0U;
    sensor->probed = 0U;
    sensor->current_has_sample = 0U;
    sensor->current_sample_count = 0U;
}

int st_adxl345_init(st_adxl345_t *sensor, const st_adxl345_config_t *config)
{
    if (sensor == NULL || config == NULL || config->bus.write == NULL ||
        config->bus.read == NULL ||
        (config->address != ST_ADXL345_DEFAULT_ADDRESS &&
         config->address != ST_ADXL345_ALT_ADDRESS) ||
        range_code(config->range_g) == 0xFFU || config->rate_code < 0x06U ||
        config->rate_code > 0x0FU || config->minimum_window_samples < 2U ||
        config->minimum_window_samples > 31U || config->sample_interval_ms == 0U ||
        config->vibration_sensor_id == NULL || config->vibration_sensor_id[0] == '\0') {
        return -1;
    }
    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    if (sensor->config.g_per_lsb <= 0.0F) {
        sensor->config.g_per_lsb = ST_ADXL345_DEFAULT_G_PER_LSB;
    }
    sensor->metadata.sample_interval_ms = sensor->config.sample_interval_ms;
    sensor->metadata.channel_count = ST_ADXL345_CHANNEL_COUNT;
    copy_string(sensor->metadata.channels[0].sensor_id,
                sizeof(sensor->metadata.channels[0].sensor_id),
                sensor->config.vibration_sensor_id);
    sensor->metadata.channels[0].sensor_kind = ST_SENSOR_VIBRATION_RMS_G;
    sensor->metadata.channels[0].unit = ST_UNIT_G;
    return 0;
}

st_physical_module_driver_t st_adxl345_module_driver(st_adxl345_t *sensor)
{
    st_physical_module_driver_t driver;
    driver.context = sensor;
    driver.probe = adxl345_probe;
    driver.acquire = adxl345_acquire;
    driver.read_channel = adxl345_read_channel;
    driver.reset = adxl345_reset;
    return driver;
}

uint32_t st_adxl345_windows_completed(const st_adxl345_t *sensor)
{
    return sensor == NULL ? 0U : sensor->windows_completed;
}
