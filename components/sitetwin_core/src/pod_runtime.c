#include "sitetwin/pod_runtime.h"

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

static st_telemetry_record_t make_record(st_sensor_reading_t reading)
{
    st_telemetry_record_t record;

    memset(&record, 0, sizeof(record));
    record.reading = reading;
    if (reading.sensor_kind == ST_SENSOR_MOTION || reading.sensor_kind == ST_SENSOR_CONTACT) {
        record.record_class = ST_RECORD_EVENT;
        record.priority = ST_PRIORITY_EVENT;
    } else if (reading.sensor_kind == ST_SENSOR_VIBRATION_RMS_G) {
        record.record_class = ST_RECORD_FEATURE;
        record.priority = ST_PRIORITY_FEATURE;
    } else {
        record.record_class = ST_RECORD_STATE;
        record.priority = ST_PRIORITY_ROUTINE;
    }
    return record;
}

void st_pod_runtime_init(st_pod_runtime_t *runtime, st_pod_profile_t profile,
                         const char *pod_id, uint32_t boot_id)
{
    if (runtime == NULL) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->profile = profile;
    st_sensor_registry_init(&runtime->registry, pod_id, boot_id);
    st_reporting_policy_init(&runtime->reporting);
    st_telemetry_queue_init(&runtime->outbound);
}

void st_pod_runtime_tick(st_pod_runtime_t *runtime, uint64_t now_ms)
{
    st_sensor_reading_t readings[ST_MAX_SENSOR_PORTS];
    size_t reading_count;
    size_t index;

    if (runtime == NULL) {
        return;
    }

    reading_count = st_sensor_registry_tick(&runtime->registry, now_ms, readings, ST_MAX_SENSOR_PORTS);
    for (index = 0U; index < reading_count; ++index) {
        st_telemetry_record_t record = make_record(readings[index]);
        if (runtime->reading_observer != NULL) {
            (void)runtime->reading_observer(runtime->reading_observer_context,
                                            &readings[index], now_ms);
        }
        if (st_reporting_policy_should_report(&runtime->reporting, &record)) {
            (void)st_telemetry_queue_push(&runtime->outbound, &record);
        }
    }
}

void st_pod_runtime_set_reading_observer(
    st_pod_runtime_t *runtime,
    int (*observer)(void *context, const st_sensor_reading_t *reading,
                    uint64_t now_ms),
    void *context)
{
    if (runtime != NULL) {
        runtime->reading_observer = observer;
        runtime->reading_observer_context = context;
    }
}

int st_pod_runtime_emit_event(st_pod_runtime_t *runtime, const char *sensor_id,
                              st_sensor_kind_t sensor_kind, uint64_t now_ms, float value)
{
    st_telemetry_record_t record;

    if (runtime == NULL || sensor_id == NULL ||
        (sensor_kind != ST_SENSOR_MOTION && sensor_kind != ST_SENSOR_CONTACT)) {
        return -1;
    }

    memset(&record, 0, sizeof(record));
    record.record_class = ST_RECORD_EVENT;
    record.priority = ST_PRIORITY_EVENT;
    copy_string(record.reading.pod_id, sizeof(record.reading.pod_id), runtime->registry.pod_id);
    copy_string(record.reading.sensor_id, sizeof(record.reading.sensor_id), sensor_id);
    record.reading.sensor_kind = sensor_kind;
    record.reading.unit = ST_UNIT_BOOLEAN;
    record.reading.sequence = ++runtime->event_sequence;
    record.reading.boot_id = runtime->registry.boot_id;
    record.reading.uptime_ms = now_ms;
    record.reading.value = value;
    record.reading.quality_flags = ST_QUALITY_VALID;
    if (runtime->reading_observer != NULL) {
        (void)runtime->reading_observer(runtime->reading_observer_context,
                                        &record.reading, now_ms);
    }
    return st_telemetry_queue_push(&runtime->outbound, &record);
}

int st_pod_runtime_next_telemetry(st_pod_runtime_t *runtime, st_telemetry_record_t *record)
{
    return runtime == NULL ? -1 : st_telemetry_queue_pop(&runtime->outbound, record);
}
