#include "sitetwin/fake_sensor.h"

#include <string.h>

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

static st_driver_result_t fake_probe(void *context, st_module_metadata_t *metadata)
{
    st_fake_sensor_t *sensor = (st_fake_sensor_t *)context;

    if (sensor == NULL || metadata == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (!sensor->present) {
        return ST_DRIVER_NOT_PRESENT;
    }

    *metadata = sensor->metadata;
    return ST_DRIVER_READY;
}

static st_driver_result_t fake_sample(void *context, uint64_t now_ms, st_driver_sample_t *sample)
{
    st_fake_sensor_t *sensor = (st_fake_sensor_t *)context;

    if (sensor == NULL || sample == NULL) {
        return ST_DRIVER_ERROR;
    }
    if (!sensor->present) {
        return ST_DRIVER_NOT_PRESENT;
    }
    if (sensor->fail_next_sample) {
        sensor->fail_next_sample = false;
        return ST_DRIVER_ERROR;
    }

    sample->value = sensor->value;
    sample->unit = sensor->metadata.unit;
    sample->quality_flags = sensor->quality_flags;
    if (now_ms < sensor->warm_until_ms) {
        sample->quality_flags |= ST_QUALITY_WARMING_UP;
    }
    sample->acquired_at_ms = now_ms;
    sample->acquired_at_valid = 1U;
    sensor->value += sensor->increment_per_sample;
    return ST_DRIVER_READY;
}

void st_fake_sensor_init(st_fake_sensor_t *sensor, const char *sensor_id, const char *module_uid,
                         st_sensor_kind_t kind, st_unit_t unit, uint32_t sample_interval_ms,
                         float initial_value)
{
    if (sensor == NULL) {
        return;
    }

    memset(sensor, 0, sizeof(*sensor));
    sensor->present = true;
    sensor->value = initial_value;
    sensor->metadata.sensor_kind = kind;
    sensor->metadata.unit = unit;
    sensor->metadata.sample_interval_ms = sample_interval_ms;
    copy_string(sensor->metadata.sensor_id, sizeof(sensor->metadata.sensor_id), sensor_id);
    copy_string(sensor->metadata.module_uid, sizeof(sensor->metadata.module_uid), module_uid);
}

st_sensor_driver_t st_fake_sensor_driver(st_fake_sensor_t *sensor)
{
    st_sensor_driver_t driver;

    driver.context = sensor;
    driver.probe = fake_probe;
    driver.sample = fake_sample;
    return driver;
}
