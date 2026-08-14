#include "sitetwin/control_event.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void write_u64_le(uint8_t *destination, uint64_t value)
{
    write_u32_le(destination, (uint32_t)value);
    write_u32_le(destination + 4U, (uint32_t)(value >> 32U));
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

static uint64_t read_u64_le(const uint8_t *source)
{
    return (uint64_t)read_u32_le(source) |
           ((uint64_t)read_u32_le(source + 4U) << 32U);
}

static void write_float_le(uint8_t *destination, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    write_u32_le(destination, bits);
}

static float read_float_le(const uint8_t *source)
{
    uint32_t bits = read_u32_le(source);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static int safe_identifier(const char *value, size_t capacity)
{
    size_t index;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    for (index = 0U; index < capacity; ++index) {
        unsigned char character = (unsigned char)value[index];
        if (character == '\0') {
            return 1;
        }
        if (!isalnum(character) && character != '_' && character != '-') {
            return 0;
        }
    }
    return 0;
}

int st_control_event_encode(const st_control_event_t *event, uint8_t *payload,
                            size_t capacity, size_t *length)
{
    if (event == NULL || payload == NULL || length == NULL ||
        capacity < ST_CONTROL_EVENT_WIRE_SIZE ||
        !safe_identifier(event->pod_id, sizeof(event->pod_id)) ||
        !safe_identifier(event->sensor_id, sizeof(event->sensor_id)) ||
        event->event_kind < ST_CONTROL_EVENT_ALARM_CONDITION ||
        event->event_kind > ST_CONTROL_EVENT_GATEWAY_INCIDENT ||
        event->transition < ST_CONTROL_TRANSITION_ACTIVE ||
        event->transition > ST_CONTROL_TRANSITION_RECOVERED ||
        event->reason > ST_CONTROL_REASON_CONFIGURATION_CHANGED ||
        event->active > 1U || event->acknowledged > 1U ||
        event->silenced > 1U ||
        event->shared_alarm_indicator_verified > 1U ||
        event->capability > ST_SENSOR_UNKNOWN ||
        event->secondary_capability > ST_SENSOR_UNKNOWN ||
        event->rule_kind > ST_CONFIG_RULE_STATE_ACTIVE_VALUE ||
        !isfinite(event->threshold) || !isfinite(event->observed) ||
        !isfinite(event->secondary_observed) || !isfinite(event->trend)) {
        return -1;
    }
    memset(payload, 0, ST_CONTROL_EVENT_WIRE_SIZE);
    payload[0] = ST_CONTROL_EVENT_SCHEMA_VERSION;
    payload[1] = (uint8_t)event->event_kind;
    payload[2] = (uint8_t)event->transition;
    payload[3] = (uint8_t)event->reason;
    payload[4] = event->active;
    payload[5] = event->acknowledged;
    payload[6] = event->silenced;
    payload[7] = event->shared_alarm_indicator_verified;
    payload[8] = event->evidence_count;
    payload[9] = (uint8_t)event->capability;
    payload[10] = (uint8_t)event->secondary_capability;
    payload[11] = (uint8_t)event->rule_kind;
    write_u32_le(&payload[12], event->config_revision);
    write_u32_le(&payload[16], event->capability_mask);
    write_u32_le(&payload[20], event->quality_flags);
    write_u32_le(&payload[24], event->ruleset_revision);
    write_u64_le(&payload[28], event->instance_id);
    write_u64_le(&payload[36], event->timestamp_ms);
    write_float_le(&payload[44], event->threshold);
    write_float_le(&payload[48], event->observed);
    write_float_le(&payload[52], event->secondary_observed);
    write_float_le(&payload[56], event->trend);
    memcpy(&payload[60], event->pod_id, ST_POD_ID_MAX_LEN);
    memcpy(&payload[76], event->sensor_id, ST_SENSOR_ID_MAX_LEN);
    write_u32_le(&payload[100], event->boot_id);
    write_u32_le(&payload[104], event->sequence);
    *length = ST_CONTROL_EVENT_WIRE_SIZE;
    return 0;
}

int st_control_event_decode(const uint8_t *payload, size_t length,
                            st_control_event_t *event)
{
    if (payload == NULL || event == NULL || length != ST_CONTROL_EVENT_WIRE_SIZE ||
        payload[0] != ST_CONTROL_EVENT_SCHEMA_VERSION || payload[4] > 1U ||
        payload[5] > 1U || payload[6] > 1U || payload[7] > 1U ||
        payload[11] > ST_CONFIG_RULE_STATE_ACTIVE_VALUE ||
        payload[75] != 0U || payload[99] != 0U || payload[108] != 0U ||
        payload[109] != 0U || payload[110] != 0U || payload[111] != 0U) {
        return -1;
    }
    memset(event, 0, sizeof(*event));
    event->event_kind = (st_control_event_kind_t)payload[1];
    event->transition = (st_control_transition_t)payload[2];
    event->reason = (st_control_reason_t)payload[3];
    event->active = payload[4];
    event->acknowledged = payload[5];
    event->silenced = payload[6];
    event->shared_alarm_indicator_verified = payload[7];
    event->evidence_count = payload[8];
    event->capability = (st_sensor_kind_t)payload[9];
    event->secondary_capability = (st_sensor_kind_t)payload[10];
    event->rule_kind = (st_config_rule_kind_t)payload[11];
    event->config_revision = read_u32_le(&payload[12]);
    event->capability_mask = read_u32_le(&payload[16]);
    event->quality_flags = read_u32_le(&payload[20]);
    event->ruleset_revision = read_u32_le(&payload[24]);
    event->instance_id = read_u64_le(&payload[28]);
    event->timestamp_ms = read_u64_le(&payload[36]);
    event->threshold = read_float_le(&payload[44]);
    event->observed = read_float_le(&payload[48]);
    event->secondary_observed = read_float_le(&payload[52]);
    event->trend = read_float_le(&payload[56]);
    memcpy(event->pod_id, &payload[60], ST_POD_ID_MAX_LEN);
    event->pod_id[ST_POD_ID_MAX_LEN - 1U] = '\0';
    memcpy(event->sensor_id, &payload[76], ST_SENSOR_ID_MAX_LEN);
    event->sensor_id[ST_SENSOR_ID_MAX_LEN - 1U] = '\0';
    event->boot_id = read_u32_le(&payload[100]);
    event->sequence = read_u32_le(&payload[104]);
    if (!safe_identifier(event->pod_id, sizeof(event->pod_id)) ||
        !safe_identifier(event->sensor_id, sizeof(event->sensor_id)) ||
        event->event_kind < ST_CONTROL_EVENT_ALARM_CONDITION ||
        event->event_kind > ST_CONTROL_EVENT_GATEWAY_INCIDENT ||
        event->transition < ST_CONTROL_TRANSITION_ACTIVE ||
        event->transition > ST_CONTROL_TRANSITION_RECOVERED ||
        event->reason > ST_CONTROL_REASON_CONFIGURATION_CHANGED ||
        event->capability > ST_SENSOR_UNKNOWN ||
        event->secondary_capability > ST_SENSOR_UNKNOWN ||
        !isfinite(event->threshold) || !isfinite(event->observed) ||
        !isfinite(event->secondary_observed) || !isfinite(event->trend)) {
        return -1;
    }
    return 0;
}

int st_control_event_to_json(const st_control_event_t *event, char *json,
                             size_t json_capacity)
{
    int written;

    if (event == NULL || json == NULL || json_capacity == 0U ||
        !safe_identifier(event->pod_id, sizeof(event->pod_id)) ||
        !safe_identifier(event->sensor_id, sizeof(event->sensor_id))) {
        return -1;
    }
    written = snprintf(
        json, json_capacity,
        "{\"schema_version\":%u,\"event_kind\":\"%s\",\"transition\":\"%s\"," \
        "\"reason\":\"%s\",\"pod_id\":\"%s\",\"sensor_id\":\"%s\"," \
        "\"instance_id\":%llu,\"timestamp_ms\":%llu,\"active\":%s," \
        "\"acknowledged\":%s,\"silenced\":%s," \
        "\"shared_alarm_indicator_verified\":%s,\"capability\":\"%s\"," \
        "\"secondary_capability\":\"%s\",\"rule_kind\":\"%s\"," \
        "\"threshold\":%.3f,\"observed\":%.3f,\"secondary_observed\":%.3f," \
        "\"trend\":%.6f,\"evidence_count\":%u,\"quality_flags\":%lu," \
        "\"config_revision\":%lu,\"ruleset_revision\":%lu," \
        "\"capability_mask\":%lu,\"boot_id\":%lu,\"sequence\":%lu}",
        ST_CONTROL_EVENT_SCHEMA_VERSION, st_control_event_kind_name(event->event_kind),
        st_control_transition_name(event->transition),
        st_control_reason_name(event->reason), event->pod_id, event->sensor_id,
        (unsigned long long)event->instance_id,
        (unsigned long long)event->timestamp_ms, event->active ? "true" : "false",
        event->acknowledged ? "true" : "false", event->silenced ? "true" : "false",
        event->shared_alarm_indicator_verified ? "true" : "false",
        st_sensor_kind_name(event->capability),
        st_sensor_kind_name(event->secondary_capability),
        st_config_rule_kind_name(event->rule_kind), (double)event->threshold,
        (double)event->observed, (double)event->secondary_observed,
        (double)event->trend, (unsigned int)event->evidence_count,
        (unsigned long)event->quality_flags, (unsigned long)event->config_revision,
        (unsigned long)event->ruleset_revision, (unsigned long)event->capability_mask,
        (unsigned long)event->boot_id, (unsigned long)event->sequence);
    return written < 0 || (size_t)written >= json_capacity ? -1 : 0;
}

const char *st_control_event_kind_name(st_control_event_kind_t kind)
{
    switch (kind) {
    case ST_CONTROL_EVENT_ALARM_CONDITION: return "alarm_condition";
    case ST_CONTROL_EVENT_ALARM_ACKNOWLEDGEMENT: return "alarm_acknowledgement";
    case ST_CONTROL_EVENT_ALARM_SILENCE: return "alarm_silence";
    case ST_CONTROL_EVENT_CAPABILITIES: return "capabilities";
    case ST_CONTROL_EVENT_CONFIGURATION: return "configuration";
    case ST_CONTROL_EVENT_GATEWAY_INCIDENT: return "gateway_incident";
    default: return "unknown";
    }
}

const char *st_control_transition_name(st_control_transition_t transition)
{
    switch (transition) {
    case ST_CONTROL_TRANSITION_ACTIVE: return "active";
    case ST_CONTROL_TRANSITION_CLEARED: return "cleared";
    case ST_CONTROL_TRANSITION_ACKNOWLEDGED: return "acknowledged";
    case ST_CONTROL_TRANSITION_SILENCE_ACTIVE: return "silence_active";
    case ST_CONTROL_TRANSITION_SILENCE_EXPIRED: return "silence_expired";
    case ST_CONTROL_TRANSITION_REBOOT_RESET: return "reboot_reset";
    case ST_CONTROL_TRANSITION_UPDATED: return "updated";
    case ST_CONTROL_TRANSITION_SNAPSHOT: return "snapshot";
    case ST_CONTROL_TRANSITION_RECOVERED: return "recovered";
    default: return "unknown";
    }
}

const char *st_control_reason_name(st_control_reason_t reason)
{
    switch (reason) {
    case ST_CONTROL_REASON_NONE: return "none";
    case ST_CONTROL_REASON_RULE_TRIGGERED: return "rule_triggered";
    case ST_CONTROL_REASON_RULE_CLEARED: return "rule_cleared";
    case ST_CONTROL_REASON_COMMAND: return "command";
    case ST_CONTROL_REASON_EXPIRED: return "expired";
    case ST_CONTROL_REASON_REBOOT_RESET: return "reboot_reset";
    case ST_CONTROL_REASON_STALE_DATA: return "stale_data";
    case ST_CONTROL_REASON_MULTI_SENSOR: return "multi_sensor";
    case ST_CONTROL_REASON_COORDINATOR_RESTART: return "coordinator_restart";
    case ST_CONTROL_REASON_CONFIGURATION_CHANGED: return "configuration_changed";
    default: return "unknown";
    }
}
