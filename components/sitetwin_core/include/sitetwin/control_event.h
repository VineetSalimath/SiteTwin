#ifndef SITETWIN_CONTROL_EVENT_H
#define SITETWIN_CONTROL_EVENT_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/capability_config.h"
#include "sitetwin/contracts.h"

#define ST_CONTROL_EVENT_SCHEMA_VERSION 1U
#define ST_CONTROL_EVENT_WIRE_SIZE 112U

typedef enum {
    ST_CONTROL_EVENT_ALARM_CONDITION = 1,
    ST_CONTROL_EVENT_ALARM_ACKNOWLEDGEMENT,
    ST_CONTROL_EVENT_ALARM_SILENCE,
    ST_CONTROL_EVENT_CAPABILITIES,
    ST_CONTROL_EVENT_CONFIGURATION,
    ST_CONTROL_EVENT_GATEWAY_INCIDENT
} st_control_event_kind_t;

typedef enum {
    ST_CONTROL_TRANSITION_ACTIVE = 1,
    ST_CONTROL_TRANSITION_CLEARED,
    ST_CONTROL_TRANSITION_ACKNOWLEDGED,
    ST_CONTROL_TRANSITION_SILENCE_ACTIVE,
    ST_CONTROL_TRANSITION_SILENCE_EXPIRED,
    ST_CONTROL_TRANSITION_REBOOT_RESET,
    ST_CONTROL_TRANSITION_UPDATED,
    ST_CONTROL_TRANSITION_SNAPSHOT,
    ST_CONTROL_TRANSITION_RECOVERED
} st_control_transition_t;

typedef enum {
    ST_CONTROL_REASON_NONE = 0,
    ST_CONTROL_REASON_RULE_TRIGGERED,
    ST_CONTROL_REASON_RULE_CLEARED,
    ST_CONTROL_REASON_COMMAND,
    ST_CONTROL_REASON_EXPIRED,
    ST_CONTROL_REASON_REBOOT_RESET,
    ST_CONTROL_REASON_STALE_DATA,
    ST_CONTROL_REASON_MULTI_SENSOR,
    ST_CONTROL_REASON_COORDINATOR_RESTART,
    ST_CONTROL_REASON_CONFIGURATION_CHANGED,
    /* Appended, not inserted -- keeps existing wire-encoded reason values
     * stable. Distinguishes "this condition cleared because its sensor
     * was physically detached" from a real ST_CONTROL_REASON_RULE_CLEARED
     * (the underlying condition itself resolving), which bridge/TB can
     * use to avoid presenting the two the same way. */
    ST_CONTROL_REASON_SENSOR_DETACHED
} st_control_reason_t;

typedef struct {
    st_control_event_kind_t event_kind;
    st_control_transition_t transition;
    st_control_reason_t reason;
    uint8_t active;
    uint8_t acknowledged;
    uint8_t silenced;
    uint8_t shared_alarm_indicator_verified;
    uint8_t evidence_count;
    st_sensor_kind_t capability;
    st_sensor_kind_t secondary_capability;
    st_config_rule_kind_t rule_kind;
    uint32_t config_revision;
    uint32_t capability_mask;
    uint32_t quality_flags;
    uint32_t ruleset_revision;
    uint64_t instance_id;
    uint64_t timestamp_ms;
    float threshold;
    float observed;
    float secondary_observed;
    float trend;
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint32_t boot_id;
    uint32_t sequence;
} st_control_event_t;

int st_control_event_encode(const st_control_event_t *event, uint8_t *payload,
                            size_t capacity, size_t *length);
int st_control_event_decode(const uint8_t *payload, size_t length,
                            st_control_event_t *event);
int st_control_event_to_json(const st_control_event_t *event, char *json,
                             size_t json_capacity);
const char *st_control_event_kind_name(st_control_event_kind_t kind);
const char *st_control_transition_name(st_control_transition_t transition);
const char *st_control_reason_name(st_control_reason_t reason);

#endif
