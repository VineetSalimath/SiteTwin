#ifndef SITETWIN_GATEWAY_RUNTIME_H
#define SITETWIN_GATEWAY_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/contracts.h"
#include "sitetwin/gateway_processor.h"
#include "sitetwin/gateway_registry.h"

#define ST_GATEWAY_DELIVERY_CAPACITY 64U

typedef enum {
    ST_GATEWAY_INGRESS_ACCEPTED = 0,
    ST_GATEWAY_INGRESS_DUPLICATE,
    ST_GATEWAY_INGRESS_STALE,
    ST_GATEWAY_INGRESS_INVALID,
    ST_GATEWAY_INGRESS_DROPPED
} st_gateway_ingress_result_t;

typedef struct {
    st_gateway_processor_t processor;
    st_gateway_registry_t registry;
    st_telemetry_record_t delivery_records[ST_GATEWAY_DELIVERY_CAPACITY];
    size_t delivery_count;
    uint32_t delivery_drops;
} st_gateway_runtime_t;

void st_gateway_runtime_init(st_gateway_runtime_t *runtime);
st_gateway_ingress_result_t st_gateway_runtime_ingest_zigbee(
    st_gateway_runtime_t *runtime, const uint8_t *payload, size_t payload_length,
    const char *pod_id, const char *sensor_id);
st_gateway_ingress_result_t st_gateway_runtime_ingest_zigbee_source(
    st_gateway_runtime_t *runtime, uint16_t source_address,
    const uint8_t *payload, size_t payload_length);
int st_gateway_runtime_next_record(st_gateway_runtime_t *runtime,
                                   st_telemetry_record_t *record);
int st_gateway_runtime_next_json(st_gateway_runtime_t *runtime, char *json,
                                 size_t json_capacity);
size_t st_gateway_runtime_pending(const st_gateway_runtime_t *runtime);

#endif
