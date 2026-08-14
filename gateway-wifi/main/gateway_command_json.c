#include "gateway_command_json.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#define COMMAND_TOPIC_PREFIX "sitetwin/pods/"
#define COMMAND_TOPIC_SUFFIX "/commands"
#define JSON_EXACT_INTEGER_MAX 9007199254740991.0

static int copy_string(char *destination, size_t capacity, const char *source)
{
    size_t length;

    if (destination == NULL || capacity == 0U || source == NULL) {
        return -1;
    }
    length = strlen(source);
    if (length == 0U || length >= capacity) {
        return -1;
    }
    memcpy(destination, source, length + 1U);
    return 0;
}

static int pod_id_from_topic(const char *topic, char *pod_id, size_t capacity)
{
    const char *start;
    const char *suffix;
    size_t length;
    size_t index;

    if (topic == NULL || strncmp(topic, COMMAND_TOPIC_PREFIX,
                                 strlen(COMMAND_TOPIC_PREFIX)) != 0) {
        return -1;
    }
    start = topic + strlen(COMMAND_TOPIC_PREFIX);
    suffix = strstr(start, COMMAND_TOPIC_SUFFIX);
    if (suffix == NULL || suffix[strlen(COMMAND_TOPIC_SUFFIX)] != '\0') {
        return -1;
    }
    length = (size_t)(suffix - start);
    if (length == 0U || length >= capacity) {
        return -1;
    }
    for (index = 0U; index < length; ++index) {
        char value = start[index];
        if (!((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
              value == '_')) {
            return -1;
        }
    }
    memcpy(pod_id, start, length);
    pod_id[length] = '\0';
    return 0;
}

static int json_u64(const cJSON *item, uint64_t *value, int allow_zero)
{
    double number;

    if (!cJSON_IsNumber(item) || value == NULL) {
        return -1;
    }
    number = item->valuedouble;
    if (!isfinite(number) || floor(number) != number || number < 0.0 ||
        (!allow_zero && number == 0.0) || number > JSON_EXACT_INTEGER_MAX) {
        return -1;
    }
    *value = (uint64_t)number;
    return 0;
}

static int json_u32(const cJSON *item, uint32_t *value, int allow_zero)
{
    uint64_t wide;

    if (json_u64(item, &wide, allow_zero) != 0 || wide > UINT32_MAX) {
        return -1;
    }
    *value = (uint32_t)wide;
    return 0;
}

static int parse_command_type(const char *name, st_command_type_t *type)
{
    st_command_type_t candidate;

    if (name == NULL || type == NULL) {
        return -1;
    }
    for (candidate = ST_COMMAND_SET_THRESHOLD; candidate <= ST_COMMAND_ACK_ALARM;
         candidate = (st_command_type_t)(candidate + 1)) {
        if (strcmp(name, st_command_type_name(candidate)) == 0) {
            *type = candidate;
            return 0;
        }
    }
    return -1;
}

static int parse_target(const char *name, st_command_target_t *target)
{
    st_command_target_t candidate;

    if (name == NULL || target == NULL) {
        return -1;
    }
    for (candidate = ST_COMMAND_TARGET_CO2_THRESHOLD;
         candidate <= ST_COMMAND_TARGET_CAPABILITIES;
         candidate = (st_command_target_t)(candidate + 1)) {
        if (strcmp(name, st_command_target_name(candidate)) == 0) {
            *target = candidate;
            return 0;
        }
    }
    return -1;
}

static int parse_capability(const char *name, st_sensor_kind_t *capability)
{
    st_sensor_kind_t candidate;

    if (name == NULL || capability == NULL) {
        return -1;
    }
    for (candidate = ST_SENSOR_TEMPERATURE_C; candidate < ST_SENSOR_UNKNOWN;
         candidate = (st_sensor_kind_t)(candidate + 1)) {
        if (strcmp(name, st_sensor_kind_name(candidate)) == 0) {
            *capability = candidate;
            return 0;
        }
    }
    return -1;
}

static int parse_rule_kind(const char *name, st_config_rule_kind_t *rule_kind)
{
    st_config_rule_kind_t candidate;

    if (name == NULL || rule_kind == NULL) {
        return -1;
    }
    for (candidate = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
         candidate <= ST_CONFIG_RULE_STATE_ACTIVE_VALUE;
         candidate = (st_config_rule_kind_t)(candidate + 1)) {
        if (strcmp(name, st_config_rule_kind_name(candidate)) == 0) {
            *rule_kind = candidate;
            return 0;
        }
    }
    return -1;
}

int gw_command_json_parse(const char *topic, const char *payload,
                          st_command_t *command)
{
    cJSON *root = NULL;
    const cJSON *item;
    char topic_pod_id[ST_POD_ID_MAX_LEN];
    uint32_t schema_version;
    uint64_t expires_at_ms;
    int result = -1;

    if (payload == NULL || command == NULL ||
        pod_id_from_topic(topic, topic_pod_id, sizeof(topic_pod_id)) != 0) {
        return -1;
    }
    memset(command, 0, sizeof(*command));
    root = cJSON_Parse(payload);
    if (!cJSON_IsObject(root)) {
        goto done;
    }
    if (json_u32(cJSON_GetObjectItemCaseSensitive(root, "schema_version"),
                 &schema_version, 0) != 0 ||
        (schema_version != ST_COMMAND_CONTRACT_VERSION &&
         schema_version != ST_COMMAND_V2_CONTRACT_VERSION) ||
        json_u64(cJSON_GetObjectItemCaseSensitive(root, "command_id"),
                 &command->command_id, 0) != 0 ||
        json_u64(cJSON_GetObjectItemCaseSensitive(root, "issued_at_ms"),
                 &command->issued_at_ms, 1) != 0 ||
        json_u32(cJSON_GetObjectItemCaseSensitive(root, "valid_for_ms"),
                 &command->valid_for_ms, 0) != 0 ||
        command->valid_for_ms > 60000U) {
        goto done;
    }
    if (command->issued_at_ms > UINT64_MAX - command->valid_for_ms) {
        goto done;
    }
    expires_at_ms = command->issued_at_ms + command->valid_for_ms;
    command->expires_at_ms = expires_at_ms;
    if (copy_string(command->target_pod_id, sizeof(command->target_pod_id),
                    topic_pod_id) != 0) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "pod_id");
    if (!cJSON_IsString(item) || strcmp(item->valuestring, topic_pod_id) != 0) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "command_type");
    if (!cJSON_IsString(item) || parse_command_type(item->valuestring,
                                                    &command->command_type) != 0) {
        goto done;
    }
    if (schema_version == ST_COMMAND_V2_CONTRACT_VERSION &&
        command->command_type > ST_COMMAND_GET_RULE) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "target");
    if (!cJSON_IsString(item) || parse_target(item->valuestring, &command->target) != 0) {
        goto done;
    }
    command->source = ST_COMMAND_SOURCE_THINGSBOARD;

    if (command->command_type == ST_COMMAND_SET_THRESHOLD ||
        command->command_type == ST_COMMAND_SET_RULE) {
        item = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
            goto done;
        }
        command->value = (float)item->valuedouble;
        if (json_u32(cJSON_GetObjectItemCaseSensitive(root, "config_revision"),
                     &command->config_revision, 0) != 0) {
            goto done;
        }
    }
    if (command->command_type == ST_COMMAND_SET_RULE ||
        command->command_type == ST_COMMAND_GET_RULE) {
        const cJSON *capability = cJSON_GetObjectItemCaseSensitive(root, "capability");
        const cJSON *rule_kind = cJSON_GetObjectItemCaseSensitive(root, "rule_kind");
        if (!cJSON_IsString(capability) || !cJSON_IsString(rule_kind) ||
            parse_capability(capability->valuestring, &command->capability) != 0 ||
            parse_rule_kind(rule_kind->valuestring, &command->rule_kind) != 0) {
            goto done;
        }
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "alarm_instance_id");
    if ((command->command_type == ST_COMMAND_ACK_ALARM ||
         command->command_type == ST_COMMAND_SILENCE_ALARM) &&
        json_u64(item, &command->alarm_instance_id, 0) != 0) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
    if (item != NULL && json_u32(item, &command->duration_ms, 1) != 0) {
        goto done;
    }
    result = 0;

done:
    cJSON_Delete(root);
    return result;
}

int gw_command_result_json(const st_command_ack_t *ack, char *json,
                           size_t json_capacity)
{
    int written;

    if (ack == NULL || json == NULL || json_capacity == 0U || ack->command_id == 0U ||
        ack->pod_id[0] == '\0') {
        return -1;
    }
    written = snprintf(
        json, json_capacity,
        "{\"schema_version\":%u,\"command_id\":%llu,\"pod_id\":\"%s\"," \
        "\"status\":\"%s\",\"reason\":\"%s\",\"applied_config_revision\":%lu," \
        "\"timestamp_ms\":%llu,\"config_value\":%.3f," \
        "\"alarm_instance_id\":%llu,\"capability_mask\":%lu," \
        "\"ruleset_revision\":%lu}",
        ST_COMMAND_CONTRACT_VERSION, (unsigned long long)ack->command_id, ack->pod_id,
        st_command_status_name(ack->status), st_command_reason_name(ack->reason),
        (unsigned long)ack->applied_config_revision,
        (unsigned long long)ack->timestamp_ms, (double)ack->config_value,
        (unsigned long long)ack->alarm_instance_id,
        (unsigned long)ack->capability_mask,
        (unsigned long)ack->ruleset_revision);
    return written < 0 || (size_t)written >= json_capacity ? -1 : 0;
}
