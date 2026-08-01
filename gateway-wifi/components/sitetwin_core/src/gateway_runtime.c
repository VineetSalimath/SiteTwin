#include "sitetwin/gateway_runtime.h"

#include <string.h>

#include "sitetwin/gateway_json.h"
#include "sitetwin/zigbee_payload.h"

static int is_replaceable_state(const st_telemetry_record_t *existing,
                                const st_telemetry_record_t *incoming)
{
    return existing->record_class == ST_RECORD_STATE && incoming->record_class == ST_RECORD_STATE &&
           strcmp(existing->reading.pod_id, incoming->reading.pod_id) == 0 &&
           strcmp(existing->reading.sensor_id, incoming->reading.sensor_id) == 0;
}

static int queue_record(st_gateway_runtime_t *runtime, const st_telemetry_record_t *record)
{
    size_t index;
    size_t replacement_index = ST_GATEWAY_DELIVERY_CAPACITY;

    for (index = 0U; index < runtime->delivery_count; ++index) {
        if (is_replaceable_state(&runtime->delivery_records[index], record)) {
            runtime->delivery_records[index] = *record;
            return 0;
        }
    }
    if (runtime->delivery_count < ST_GATEWAY_DELIVERY_CAPACITY) {
        runtime->delivery_records[runtime->delivery_count++] = *record;
        return 0;
    }

    for (index = 0U; index < runtime->delivery_count; ++index) {
        const st_telemetry_record_t *candidate = &runtime->delivery_records[index];

        if (candidate->record_class == ST_RECORD_EVENT) {
            continue;
        }
        if (replacement_index == ST_GATEWAY_DELIVERY_CAPACITY ||
            candidate->priority < runtime->delivery_records[replacement_index].priority) {
            replacement_index = index;
        }
    }
    if (replacement_index == ST_GATEWAY_DELIVERY_CAPACITY ||
        record->priority <= runtime->delivery_records[replacement_index].priority) {
        return -1;
    }
    runtime->delivery_records[replacement_index] = *record;
    return 0;
}

static size_t highest_priority_index(const st_gateway_runtime_t *runtime)
{
    size_t index;
    size_t best_index = 0U;

    for (index = 1U; index < runtime->delivery_count; ++index) {
        if (runtime->delivery_records[index].priority >
            runtime->delivery_records[best_index].priority) {
            best_index = index;
        }
    }
    return best_index;
}

static void remove_record(st_gateway_runtime_t *runtime, size_t record_index)
{
    size_t index;

    for (index = record_index + 1U; index < runtime->delivery_count; ++index) {
        runtime->delivery_records[index - 1U] = runtime->delivery_records[index];
    }
    --runtime->delivery_count;
}

void st_gateway_runtime_init(st_gateway_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    memset(runtime, 0, sizeof(*runtime));
    st_gateway_processor_init(&runtime->processor);
    st_gateway_registry_init(&runtime->registry);
}

st_gateway_ingress_result_t st_gateway_runtime_ingest_zigbee(
    st_gateway_runtime_t *runtime, const uint8_t *payload, size_t payload_length,
    const char *pod_id, const char *sensor_id)
{
    st_telemetry_record_t record;
    st_gateway_record_result_t processing_result;
    uint8_t sensor_slot;

    if (runtime == NULL ||
        st_zigbee_telemetry_decode(payload, payload_length, pod_id, sensor_id,
                                    &record, &sensor_slot) != 0) {
        if (runtime != NULL) {
            ++runtime->processor.invalid;
        }
        return ST_GATEWAY_INGRESS_INVALID;
    }
    (void)sensor_slot;

    processing_result = st_gateway_processor_ingest(&runtime->processor, &record);
    if (processing_result == ST_GATEWAY_RECORD_DUPLICATE) {
        return ST_GATEWAY_INGRESS_DUPLICATE;
    }
    if (processing_result == ST_GATEWAY_RECORD_STALE) {
        return ST_GATEWAY_INGRESS_STALE;
    }
    if (processing_result != ST_GATEWAY_RECORD_ACCEPTED) {
        return ST_GATEWAY_INGRESS_INVALID;
    }
    if (queue_record(runtime, &record) != 0) {
        ++runtime->delivery_drops;
        return ST_GATEWAY_INGRESS_DROPPED;
    }
    return ST_GATEWAY_INGRESS_ACCEPTED;
}

st_gateway_ingress_result_t st_gateway_runtime_ingest_zigbee_source(
    st_gateway_runtime_t *runtime, uint16_t source_address,
    const uint8_t *payload, size_t payload_length)
{
    uint8_t sensor_slot;
    const char *pod_id;
    const char *sensor_id;

    if (runtime == NULL ||
        st_zigbee_telemetry_sensor_slot(payload, payload_length, &sensor_slot) != 0 ||
        st_gateway_registry_resolve(&runtime->registry, source_address, sensor_slot,
                                    &pod_id, &sensor_id) != 0) {
        if (runtime != NULL) {
            ++runtime->processor.invalid;
        }
        return ST_GATEWAY_INGRESS_INVALID;
    }
    return st_gateway_runtime_ingest_zigbee(runtime, payload, payload_length, pod_id, sensor_id);
}

int st_gateway_runtime_next_record(st_gateway_runtime_t *runtime,
                                   st_telemetry_record_t *record)
{
    size_t record_index;

    if (runtime == NULL || record == NULL || runtime->delivery_count == 0U) {
        return -1;
    }
    record_index = highest_priority_index(runtime);
    *record = runtime->delivery_records[record_index];
    remove_record(runtime, record_index);
    return 0;
}

int st_gateway_runtime_next_json(st_gateway_runtime_t *runtime, char *json,
                                 size_t json_capacity)
{
    size_t record_index;

    if (runtime == NULL || json == NULL || runtime->delivery_count == 0U) {
        return -1;
    }
    record_index = highest_priority_index(runtime);
    if (st_gateway_telemetry_to_json(&runtime->delivery_records[record_index], json,
                                     json_capacity) != 0) {
        return -1;
    }
    remove_record(runtime, record_index);
    return 0;
}

size_t st_gateway_runtime_pending(const st_gateway_runtime_t *runtime)
{
    return runtime == NULL ? 0U : runtime->delivery_count;
}
