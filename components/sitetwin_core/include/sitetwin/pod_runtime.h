#ifndef SITETWIN_POD_RUNTIME_H
#define SITETWIN_POD_RUNTIME_H

#include <stdint.h>

#include "sitetwin/contracts.h"
#include "sitetwin/reporting_policy.h"
#include "sitetwin/sensor_registry.h"
#include "sitetwin/telemetry_queue.h"

typedef struct {
    st_pod_profile_t profile;
    st_sensor_registry_t registry;
    st_reporting_policy_t reporting;
    st_telemetry_queue_t outbound;
    uint32_t event_sequence;
    void *reading_observer_context;
    int (*reading_observer)(void *context, const st_sensor_reading_t *reading,
                            uint64_t now_ms);
} st_pod_runtime_t;

void st_pod_runtime_init(st_pod_runtime_t *runtime, st_pod_profile_t profile,
                         const char *pod_id, uint32_t boot_id);
void st_pod_runtime_tick(st_pod_runtime_t *runtime, uint64_t now_ms);
void st_pod_runtime_set_reading_observer(
    st_pod_runtime_t *runtime,
    int (*observer)(void *context, const st_sensor_reading_t *reading,
                    uint64_t now_ms),
    void *context);
int st_pod_runtime_emit_event(st_pod_runtime_t *runtime, const char *sensor_id,
                              st_sensor_kind_t sensor_kind, uint64_t now_ms, float value);
/*
 * For system/status events that are not a real sensor reading (e.g. a
 * hot-swap port's insertion, removal, or identification/attach fault) --
 * always tagged ST_RECORD_HEALTH/ST_PRIORITY_HEALTH and
 * ST_SENSOR_UNKNOWN/ST_UNIT_NONE, distinguishing it from every real
 * sensor_kind. value's meaning is caller-defined (e.g. an encoded event
 * or fault-reason code); sensor_id should be a synthetic, stable label
 * (e.g. "port2_status"), not a real attached sensor's sensor_id.
 */
int st_pod_runtime_emit_health(st_pod_runtime_t *runtime, const char *sensor_id,
                               uint64_t now_ms, float value);
int st_pod_runtime_next_telemetry(st_pod_runtime_t *runtime, st_telemetry_record_t *record);

#endif
