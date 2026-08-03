#ifndef SITETWIN_TELEMETRY_QUEUE_H
#define SITETWIN_TELEMETRY_QUEUE_H

#include <stddef.h>

#include "sitetwin/contracts.h"

#define ST_TELEMETRY_QUEUE_CAPACITY 32U

typedef struct {
    st_telemetry_record_t records[ST_TELEMETRY_QUEUE_CAPACITY];
    size_t count;
} st_telemetry_queue_t;

void st_telemetry_queue_init(st_telemetry_queue_t *queue);
int st_telemetry_queue_push(st_telemetry_queue_t *queue, const st_telemetry_record_t *record);
int st_telemetry_queue_pop(st_telemetry_queue_t *queue, st_telemetry_record_t *record);
size_t st_telemetry_queue_count(const st_telemetry_queue_t *queue);

#endif
