#include "sitetwin/gateway_processor.h"

#include <float.h>
#include <string.h>

static int is_safe_identifier(const char *value, size_t capacity)
{
    size_t index;

    for (index = 0U; index < capacity; ++index) {
        char character = value[index];

        if (character == '\0') {
            return index > 0U;
        }
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '_' || character == '-')) {
            return 0;
        }
    }
    return 0;
}

static int is_record_valid(const st_telemetry_record_t *record)
{
    return is_safe_identifier(record->reading.pod_id, sizeof(record->reading.pod_id)) &&
           is_safe_identifier(record->reading.sensor_id, sizeof(record->reading.sensor_id)) &&
           record->reading.sensor_kind <= ST_SENSOR_UNKNOWN && record->reading.unit <= ST_UNIT_NONE &&
           record->record_class <= ST_RECORD_HEALTH &&
           record->priority >= ST_PRIORITY_ROUTINE && record->priority <= ST_PRIORITY_EVENT &&
           record->reading.sequence != 0U && record->reading.boot_id != 0U &&
           record->reading.value <= FLT_MAX && record->reading.value >= -FLT_MAX;
}

void st_gateway_processor_init(st_gateway_processor_t *processor)
{
    if (processor != NULL) {
        memset(processor, 0, sizeof(*processor));
    }
}

st_gateway_record_result_t st_gateway_processor_ingest(st_gateway_processor_t *processor,
                                                       const st_telemetry_record_t *record)
{
    size_t index;
    st_gateway_source_state_t *free_source = NULL;

    if (processor == NULL || record == NULL || !is_record_valid(record)) {
        if (processor != NULL) {
            ++processor->invalid;
        }
        return ST_GATEWAY_RECORD_INVALID;
    }

    for (index = 0U; index < ST_GATEWAY_SOURCE_CAPACITY; ++index) {
        st_gateway_source_state_t *source = &processor->sources[index];

        if (source->used == 0U) {
            if (free_source == NULL) {
                free_source = source;
            }
            continue;
        }
        if (strcmp(source->pod_id, record->reading.pod_id) != 0 ||
            strcmp(source->sensor_id, record->reading.sensor_id) != 0) {
            continue;
        }

        if (source->boot_id != record->reading.boot_id) {
            if (source->previous_boot_id == record->reading.boot_id) {
                ++processor->stale;
                return ST_GATEWAY_RECORD_STALE;
            }
            source->previous_boot_id = source->boot_id;
            source->boot_id = record->reading.boot_id;
            source->last_sequence = record->reading.sequence;
            ++processor->accepted;
            return ST_GATEWAY_RECORD_ACCEPTED;
        }
        if (record->reading.sequence == source->last_sequence) {
            ++processor->duplicates;
            return ST_GATEWAY_RECORD_DUPLICATE;
        }
        if (record->reading.sequence < source->last_sequence) {
            ++processor->stale;
            return ST_GATEWAY_RECORD_STALE;
        }

        source->last_sequence = record->reading.sequence;
        ++processor->accepted;
        return ST_GATEWAY_RECORD_ACCEPTED;
    }

    if (free_source == NULL) {
        return ST_GATEWAY_RECORD_CAPACITY_EXCEEDED;
    }
    strncpy(free_source->pod_id, record->reading.pod_id, sizeof(free_source->pod_id) - 1U);
    strncpy(free_source->sensor_id, record->reading.sensor_id, sizeof(free_source->sensor_id) - 1U);
    free_source->boot_id = record->reading.boot_id;
    free_source->last_sequence = record->reading.sequence;
    free_source->used = 1U;
    ++processor->accepted;
    return ST_GATEWAY_RECORD_ACCEPTED;
}
