#include "sitetwin/telemetry_queue.h"

#include <string.h>

static int is_replaceable_state(const st_telemetry_record_t *existing,
                                const st_telemetry_record_t *incoming)
{
    return existing->record_class == ST_RECORD_STATE && incoming->record_class == ST_RECORD_STATE &&
           strcmp(existing->reading.pod_id, incoming->reading.pod_id) == 0 &&
           strcmp(existing->reading.sensor_id, incoming->reading.sensor_id) == 0;
}

void st_telemetry_queue_init(st_telemetry_queue_t *queue)
{
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
    }
}

int st_telemetry_queue_push(st_telemetry_queue_t *queue, const st_telemetry_record_t *record)
{
    size_t index;
    size_t replacement_index = ST_TELEMETRY_QUEUE_CAPACITY;

    if (queue == NULL || record == NULL) {
        return -1;
    }

    for (index = 0U; index < queue->count; ++index) {
        if (is_replaceable_state(&queue->records[index], record)) {
            queue->records[index] = *record;
            return 0;
        }
    }

    if (queue->count < ST_TELEMETRY_QUEUE_CAPACITY) {
        queue->records[queue->count++] = *record;
        return 0;
    }

    for (index = 0U; index < queue->count; ++index) {
        const st_telemetry_record_t *candidate = &queue->records[index];
        if (candidate->record_class == ST_RECORD_EVENT) {
            continue;
        }
        if (replacement_index == ST_TELEMETRY_QUEUE_CAPACITY ||
            candidate->priority < queue->records[replacement_index].priority) {
            replacement_index = index;
        }
    }

    if (replacement_index == ST_TELEMETRY_QUEUE_CAPACITY ||
        record->priority <= queue->records[replacement_index].priority) {
        return -1;
    }

    queue->records[replacement_index] = *record;
    return 0;
}

int st_telemetry_queue_pop(st_telemetry_queue_t *queue, st_telemetry_record_t *record)
{
    size_t index;
    size_t best_index = 0U;

    if (queue == NULL || record == NULL || queue->count == 0U) {
        return -1;
    }

    for (index = 1U; index < queue->count; ++index) {
        if (queue->records[index].priority > queue->records[best_index].priority) {
            best_index = index;
        }
    }

    *record = queue->records[best_index];
    for (index = best_index + 1U; index < queue->count; ++index) {
        queue->records[index - 1U] = queue->records[index];
    }
    --queue->count;
    return 0;
}

size_t st_telemetry_queue_count(const st_telemetry_queue_t *queue)
{
    return queue == NULL ? 0U : queue->count;
}
