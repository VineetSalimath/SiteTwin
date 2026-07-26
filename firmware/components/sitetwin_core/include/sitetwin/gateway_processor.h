#ifndef SITETWIN_GATEWAY_PROCESSOR_H
#define SITETWIN_GATEWAY_PROCESSOR_H

#include <stdint.h>

#include "sitetwin/contracts.h"

#define ST_GATEWAY_SOURCE_CAPACITY 64U

typedef enum {
    ST_GATEWAY_RECORD_ACCEPTED = 0,
    ST_GATEWAY_RECORD_DUPLICATE,
    ST_GATEWAY_RECORD_STALE,
    ST_GATEWAY_RECORD_INVALID,
    ST_GATEWAY_RECORD_CAPACITY_EXCEEDED
} st_gateway_record_result_t;

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint32_t boot_id;
    uint32_t previous_boot_id;
    uint32_t last_sequence;
    uint8_t used;
} st_gateway_source_state_t;

typedef struct {
    st_gateway_source_state_t sources[ST_GATEWAY_SOURCE_CAPACITY];
    uint32_t accepted;
    uint32_t duplicates;
    uint32_t stale;
    uint32_t invalid;
} st_gateway_processor_t;

void st_gateway_processor_init(st_gateway_processor_t *processor);
st_gateway_record_result_t st_gateway_processor_ingest(st_gateway_processor_t *processor,
                                                       const st_telemetry_record_t *record);

#endif
