#ifndef SITETWIN_GATEWAY_STATE_H
#define SITETWIN_GATEWAY_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/control_event.h"
#include "sitetwin/contracts.h"

#define ST_GATEWAY_STATE_MAGIC 0x53544753U
#define ST_GATEWAY_STATE_VERSION 1U
#define ST_GATEWAY_STATE_POD_CAPACITY 8U
#define ST_GATEWAY_STATE_SENSOR_CAPACITY 24U
#define ST_GATEWAY_STATE_ALARM_CAPACITY 24U
#define ST_GATEWAY_STATE_INCIDENT_CAPACITY 16U
#define ST_GATEWAY_STATE_TREND_WINDOW 8U
#define ST_GATEWAY_STATE_EVENT_CAPACITY 16U
#define ST_GATEWAY_FRESHNESS_TIMEOUT_MS 90000U
#define ST_GATEWAY_STATE_DEBOUNCE_MS 5000U

typedef enum {
    ST_GATEWAY_INCIDENT_POD_STALE = 1,
    ST_GATEWAY_INCIDENT_MULTI_SENSOR
} st_gateway_incident_kind_t;

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    uint64_t alarm_instance_id;
    st_sensor_kind_t capability;
    float observed;
    float threshold;
    uint8_t used;
    uint8_t active;
    uint8_t reserved[2];
} st_gateway_alarm_evidence_t;

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    uint64_t instance_id;
    st_gateway_incident_kind_t incident_kind;
    st_sensor_kind_t primary_capability;
    st_sensor_kind_t secondary_capability;
    float primary_observed;
    float secondary_observed;
    float trend;
    uint8_t used;
    uint8_t active;
    uint8_t evidence_count;
    uint8_t reserved;
} st_gateway_incident_state_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t alarm_count;
    uint8_t incident_count;
    uint8_t reserved;
    uint32_t next_instance_counter;
    st_gateway_alarm_evidence_t alarms[ST_GATEWAY_STATE_ALARM_CAPACITY];
    st_gateway_incident_state_t incidents[ST_GATEWAY_STATE_INCIDENT_CAPACITY];
} st_gateway_state_persistent_t;

typedef struct {
    void *context;
    int (*load)(void *context, st_gateway_state_persistent_t *state);
    int (*save)(void *context, const st_gateway_state_persistent_t *state);
} st_gateway_state_persistence_t;

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    st_sensor_kind_t capability;
    float values[ST_GATEWAY_STATE_TREND_WINDOW];
    uint64_t last_seen_ms;
    uint32_t quality_flags;
    uint8_t window_count;
    uint8_t window_next;
    uint8_t used;
    uint8_t reserved;
} st_gateway_sensor_track_t;

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    uint64_t tracked_since_ms;
    uint64_t last_seen_ms;
    uint64_t stale_candidate_since_ms;
    uint64_t fresh_candidate_since_ms;
    uint64_t multi_candidate_since_ms;
    uint64_t multi_clear_since_ms;
    uint8_t used;
} st_gateway_pod_track_t;

typedef struct {
    st_gateway_state_persistent_t persistent;
    st_gateway_state_persistence_t persistence;
    st_gateway_pod_track_t pods[ST_GATEWAY_STATE_POD_CAPACITY];
    st_gateway_sensor_track_t sensors[ST_GATEWAY_STATE_SENSOR_CAPACITY];
    st_control_event_t events[ST_GATEWAY_STATE_EVENT_CAPACITY];
    size_t event_count;
    uint32_t event_sequence;
    uint32_t event_drops;
} st_gateway_state_t;

void st_gateway_state_persistent_init(st_gateway_state_persistent_t *state);
int st_gateway_state_persistent_valid(const st_gateway_state_persistent_t *state);
int st_gateway_state_init(st_gateway_state_t *state,
                          st_gateway_state_persistence_t persistence,
                          uint64_t now_ms);
int st_gateway_state_register_pod(st_gateway_state_t *state,
                                  const char *pod_id, uint64_t now_ms);
int st_gateway_state_ingest_telemetry(st_gateway_state_t *state,
                                      const st_telemetry_record_t *record,
                                      uint64_t now_ms);
int st_gateway_state_ingest_control(st_gateway_state_t *state,
                                    const st_control_event_t *event,
                                    uint64_t now_ms);
void st_gateway_state_tick(st_gateway_state_t *state, uint64_t now_ms);
int st_gateway_state_next_event(st_gateway_state_t *state,
                                st_control_event_t *event);

#endif
