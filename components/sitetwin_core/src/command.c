#include "sitetwin/command.h"

#include <math.h>
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
    return (uint64_t)read_u32_le(source) | ((uint64_t)read_u32_le(source + 4U) << 32U);
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

static void copy_id(char *destination, size_t capacity, const char *source)
{
    size_t length = source == NULL ? 0U : strlen(source);
    if (length >= capacity) {
        length = capacity - 1U;
    }
    if (length > 0U) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

int st_command_encode(const st_command_t *command, uint8_t *payload,
                      size_t capacity, size_t *length)
{
    if (command == NULL || payload == NULL || length == NULL ||
        capacity < ST_COMMAND_WIRE_SIZE || command->target_pod_id[0] == '\0') {
        return -1;
    }
    memset(payload, 0, ST_COMMAND_WIRE_SIZE);
    payload[0] = ST_COMMAND_CONTRACT_VERSION;
    payload[1] = (uint8_t)command->command_type;
    payload[2] = (uint8_t)command->target;
    payload[3] = (uint8_t)command->source;
    write_u64_le(&payload[4], command->command_id);
    write_u64_le(&payload[12], command->issued_at_ms);
    write_u64_le(&payload[20], command->expires_at_ms);
    write_u32_le(&payload[28], command->config_revision);
    write_float_le(&payload[32], command->value);
    write_u32_le(&payload[36], command->duration_ms);
    memcpy(&payload[40], command->target_pod_id, ST_POD_ID_MAX_LEN);
    write_u32_le(&payload[56], command->valid_for_ms);
    payload[60] = (uint8_t)command->capability;
    payload[61] = (uint8_t)command->rule_kind;
    write_u64_le(&payload[64], command->alarm_instance_id);
    *length = ST_COMMAND_WIRE_SIZE;
    return 0;
}

int st_command_decode(const uint8_t *payload, size_t length, st_command_t *command)
{
    uint8_t version;

    if (payload == NULL || command == NULL || payload[55] != 0U) {
        return -1;
    }
    version = payload[0];
    if ((version == ST_COMMAND_CONTRACT_VERSION && length != ST_COMMAND_WIRE_SIZE) ||
        ((version == ST_COMMAND_V2_CONTRACT_VERSION ||
          version == ST_COMMAND_LEGACY_CONTRACT_VERSION) &&
         length != ST_COMMAND_V2_WIRE_SIZE) ||
        (version != ST_COMMAND_CONTRACT_VERSION &&
         version != ST_COMMAND_V2_CONTRACT_VERSION &&
         version != ST_COMMAND_LEGACY_CONTRACT_VERSION)) {
        return -1;
    }
    memset(command, 0, sizeof(*command));
    command->command_type = (st_command_type_t)payload[1];
    command->target = (st_command_target_t)payload[2];
    command->source = (st_command_source_t)payload[3];
    command->command_id = read_u64_le(&payload[4]);
    command->issued_at_ms = read_u64_le(&payload[12]);
    command->expires_at_ms = read_u64_le(&payload[20]);
    command->config_revision = read_u32_le(&payload[28]);
    command->value = read_float_le(&payload[32]);
    command->duration_ms = read_u32_le(&payload[36]);
    memcpy(command->target_pod_id, &payload[40], ST_POD_ID_MAX_LEN);
    command->target_pod_id[ST_POD_ID_MAX_LEN - 1U] = '\0';
    command->valid_for_ms = read_u32_le(&payload[56]);
    if (version == ST_COMMAND_CONTRACT_VERSION ||
        version == ST_COMMAND_V2_CONTRACT_VERSION) {
        command->capability = (st_sensor_kind_t)payload[60];
        command->rule_kind = (st_config_rule_kind_t)payload[61];
    } else if (command->command_type == ST_COMMAND_SET_THRESHOLD &&
               command->target == ST_COMMAND_TARGET_CO2_THRESHOLD) {
        command->capability = ST_SENSOR_CO2_PPM;
        command->rule_kind = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
    }
    if (version == ST_COMMAND_CONTRACT_VERSION) {
        command->alarm_instance_id = read_u64_le(&payload[64]);
    }
    return command->target_pod_id[0] == '\0' ? -1 : 0;
}

int st_command_ack_encode(const st_command_ack_t *ack, uint8_t *payload,
                          size_t capacity, size_t *length)
{
    if (ack == NULL || payload == NULL || length == NULL ||
        capacity < ST_COMMAND_ACK_WIRE_SIZE || ack->pod_id[0] == '\0') {
        return -1;
    }
    memset(payload, 0, ST_COMMAND_ACK_WIRE_SIZE);
    payload[0] = ST_COMMAND_CONTRACT_VERSION;
    payload[1] = (uint8_t)ack->status;
    payload[2] = (uint8_t)ack->reason;
    write_u64_le(&payload[4], ack->command_id);
    memcpy(&payload[12], ack->pod_id, ST_POD_ID_MAX_LEN);
    write_u32_le(&payload[28], ack->applied_config_revision);
    write_u64_le(&payload[32], ack->timestamp_ms);
    write_float_le(&payload[40], ack->config_value);
    write_u64_le(&payload[44], ack->alarm_instance_id);
    write_u32_le(&payload[52], ack->capability_mask);
    write_u32_le(&payload[56], ack->ruleset_revision);
    *length = ST_COMMAND_ACK_WIRE_SIZE;
    return 0;
}

int st_command_ack_decode(const uint8_t *payload, size_t length, st_command_ack_t *ack)
{
    if (payload == NULL || ack == NULL ||
        ((payload[0] == ST_COMMAND_CONTRACT_VERSION &&
          length != ST_COMMAND_ACK_WIRE_SIZE) ||
         ((payload[0] == ST_COMMAND_V2_CONTRACT_VERSION ||
           payload[0] == ST_COMMAND_LEGACY_CONTRACT_VERSION) &&
          length != ST_COMMAND_V2_ACK_WIRE_SIZE) ||
         (payload[0] != ST_COMMAND_CONTRACT_VERSION &&
          payload[0] != ST_COMMAND_V2_CONTRACT_VERSION &&
          payload[0] != ST_COMMAND_LEGACY_CONTRACT_VERSION)) ||
        payload[27] != 0U) {
        return -1;
    }
    memset(ack, 0, sizeof(*ack));
    ack->status = (st_command_status_t)payload[1];
    ack->reason = (st_command_reason_t)payload[2];
    ack->command_id = read_u64_le(&payload[4]);
    memcpy(ack->pod_id, &payload[12], ST_POD_ID_MAX_LEN);
    ack->pod_id[ST_POD_ID_MAX_LEN - 1U] = '\0';
    ack->applied_config_revision = read_u32_le(&payload[28]);
    ack->timestamp_ms = read_u64_le(&payload[32]);
    ack->config_value = read_float_le(&payload[40]);
    if (payload[0] == ST_COMMAND_CONTRACT_VERSION) {
        ack->alarm_instance_id = read_u64_le(&payload[44]);
        ack->capability_mask = read_u32_le(&payload[52]);
        ack->ruleset_revision = read_u32_le(&payload[56]);
    }
    return ack->pod_id[0] == '\0' ? -1 : 0;
}

st_pod_capabilities_t st_pod_capabilities(st_pod_profile_t profile)
{
    st_pod_capabilities_t capabilities = {0};
    uint32_t kind;

    capabilities.command_mask = (1UL << ST_COMMAND_SET_RULE) |
                                (1UL << ST_COMMAND_GET_RULE) |
                                (1UL << ST_COMMAND_GET_CAPABILITIES) |
                                (1UL << ST_COMMAND_ACK_ALARM);
    capabilities.target_mask = 1UL << ST_COMMAND_TARGET_CAPABILITY_RULE;
    capabilities.target_mask |= (1UL << ST_COMMAND_TARGET_CAPABILITIES) |
                                (1UL << ST_COMMAND_TARGET_ALARM);
    for (kind = 0U; kind < (uint32_t)ST_SENSOR_UNKNOWN; ++kind) {
        if (st_profile_has_capability(profile, (st_sensor_kind_t)kind)) {
            capabilities.capability_mask |= 1UL << kind;
        }
    }
    if (profile == ST_POD_ENVIRONMENT) {
        capabilities.command_mask |= (1UL << ST_COMMAND_SET_THRESHOLD) |
                                     (1UL << ST_COMMAND_GET_CONFIG);
        capabilities.target_mask |= (1UL << ST_COMMAND_TARGET_CO2_THRESHOLD) |
                                    (1UL << ST_COMMAND_TARGET_CONFIG);
    }
    /* The final-board shared indicator remains electrically unverified.  C1
     * therefore advertises no output capability and never enables GPIO19. */
    capabilities.shared_alarm_indicator_verified = 0U;
    capabilities.pending_hardware_verification = 1U;
    return capabilities;
}

static int state_valid(const st_command_persistent_state_t *state,
                       st_pod_profile_t profile)
{
    size_t index;
    uint8_t used_count = 0U;

    if (state->magic != ST_COMMAND_PERSISTENCE_MAGIC ||
        state->version != ST_COMMAND_PERSISTENCE_VERSION ||
        state->history_count > ST_COMMAND_HISTORY_CAPACITY ||
        state->history_next >= ST_COMMAND_HISTORY_CAPACITY ||
        state->capability_config.schema_version != ST_CAPABILITY_CONFIG_SCHEMA_VERSION ||
        state->capability_config.rule_count > ST_CAPABILITY_RULE_CAPACITY ||
        !st_alarm_persistent_state_valid(&state->alarm_state, profile)) {
        return 0;
    }
    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        const st_capability_rule_t *rule = &state->capability_config.rules[index];
        if (rule->used != 0U) {
            size_t other;
            ++used_count;
            if (!isfinite(rule->value) || rule->revision == 0U ||
                st_capability_config_validate(profile, rule->capability,
                                              rule->rule_kind,
                                              rule->value) != ST_CONFIG_RESULT_OK) {
                return 0;
            }
            for (other = index + 1U; other < ST_CAPABILITY_RULE_CAPACITY; ++other) {
                const st_capability_rule_t *candidate =
                    &state->capability_config.rules[other];
                if (candidate->used != 0U &&
                    candidate->capability == rule->capability &&
                    candidate->rule_kind == rule->rule_kind) {
                    return 0;
                }
            }
        }
    }
    return used_count == state->capability_config.rule_count;
}

static int state_v2_valid(const st_command_persistent_state_t *state,
                          st_pod_profile_t profile)
{
    st_command_persistent_state_t candidate = *state;
    candidate.version = ST_COMMAND_PERSISTENCE_VERSION;
    st_alarm_persistent_state_init(&candidate.alarm_state);
    return state->magic == ST_COMMAND_PERSISTENCE_MAGIC &&
           state->version == ST_COMMAND_V2_CONTRACT_VERSION &&
           state_valid(&candidate, profile);
}

static int state_v1_valid(const st_command_persistent_state_t *state)
{
    return state->magic == ST_COMMAND_PERSISTENCE_MAGIC &&
           state->version == ST_COMMAND_LEGACY_CONTRACT_VERSION &&
           state->history_count <= ST_COMMAND_HISTORY_CAPACITY &&
           state->history_next < ST_COMMAND_HISTORY_CAPACITY &&
           isfinite(state->config.co2_threshold_ppm) &&
           state->config.co2_threshold_ppm >= ST_COMMAND_MIN_CO2_THRESHOLD_PPM &&
           state->config.co2_threshold_ppm <= ST_COMMAND_MAX_CO2_THRESHOLD_PPM &&
           state->config.revision != 0U && state->config.revision <= 1000000U;
}

static void sync_legacy_co2_mirror(st_command_persistent_state_t *state)
{
    st_capability_rule_t rule;

    if (st_capability_config_get(&state->capability_config, ST_SENSOR_CO2_PPM,
                                 ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD,
                                 &rule) == ST_CONFIG_RESULT_OK) {
        state->config.co2_threshold_ppm = rule.value;
        state->config.revision = rule.revision;
    }
}

static void migrate_v1_state(st_command_persistent_state_t *state,
                             st_pod_profile_t profile)
{
    float threshold = state->config.co2_threshold_ppm;
    uint32_t revision = state->config.revision;

    state->version = ST_COMMAND_PERSISTENCE_VERSION;
    st_capability_config_init(&state->capability_config, profile);
    st_alarm_persistent_state_init(&state->alarm_state);
    if (profile == ST_POD_ENVIRONMENT && revision >= 1U) {
        /* The environment defaults install this CO2 rule in slot zero. Preserve
         * the legacy revision directly so migration time is constant even for
         * long-lived pods with a high stored revision. */
        state->capability_config.rules[0].value = threshold;
        state->capability_config.rules[0].revision = revision;
    }
    sync_legacy_co2_mirror(state);
}

static void migrate_v2_state(st_command_persistent_state_t *state)
{
    state->version = ST_COMMAND_PERSISTENCE_VERSION;
    st_alarm_persistent_state_init(&state->alarm_state);
}

static int persist_alarm_state(void *context)
{
    st_command_runtime_t *runtime = (st_command_runtime_t *)context;
    return runtime == NULL || runtime->persistence.save == NULL
               ? 0
               : runtime->persistence.save(runtime->persistence.context,
                                           &runtime->persistent);
}

int st_command_runtime_init_with_boot(st_command_runtime_t *runtime,
                                     st_pod_profile_t profile,
                                     const char *pod_id,
                                     st_command_persistence_t persistence,
                                     uint32_t boot_id, uint64_t now_ms)
{
    if (runtime == NULL || pod_id == NULL || pod_id[0] == '\0') {
        return -1;
    }
    memset(runtime, 0, sizeof(*runtime));
    runtime->profile = profile;
    runtime->capabilities = st_pod_capabilities(profile);
    runtime->persistence = persistence;
    copy_id(runtime->pod_id, sizeof(runtime->pod_id), pod_id);
    runtime->persistent.magic = ST_COMMAND_PERSISTENCE_MAGIC;
    runtime->persistent.version = ST_COMMAND_PERSISTENCE_VERSION;
    st_capability_config_init(&runtime->persistent.capability_config, profile);
    st_alarm_persistent_state_init(&runtime->persistent.alarm_state);
    sync_legacy_co2_mirror(&runtime->persistent);
    if (persistence.load != NULL) {
        st_command_persistent_state_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        if (persistence.load(persistence.context, &loaded) == 0) {
            if (state_valid(&loaded, profile)) {
                runtime->persistent = loaded;
            } else if (state_v2_valid(&loaded, profile)) {
                migrate_v2_state(&loaded);
                runtime->persistent = loaded;
            } else if (state_v1_valid(&loaded)) {
                migrate_v1_state(&loaded, profile);
                runtime->persistent = loaded;
            }
        }
    }
    sync_legacy_co2_mirror(&runtime->persistent);
    return st_alarm_runtime_init(
        &runtime->alarm, runtime->pod_id,
        &runtime->persistent.capability_config,
        &runtime->persistent.alarm_state, runtime->capabilities.capability_mask,
        boot_id, runtime->capabilities.shared_alarm_indicator_verified,
        persist_alarm_state, runtime, now_ms);
}

int st_command_runtime_init(st_command_runtime_t *runtime, st_pod_profile_t profile,
                            const char *pod_id, st_command_persistence_t persistence)
{
    return st_command_runtime_init_with_boot(runtime, profile, pod_id, persistence,
                                             1U, 0U);
}

static const st_command_history_entry_t *find_history(const st_command_runtime_t *runtime,
                                                       uint64_t command_id)
{
    size_t index;
    for (index = 0U; index < runtime->persistent.history_count; ++index) {
        if (runtime->persistent.history[index].command_id == command_id) {
            return &runtime->persistent.history[index];
        }
    }
    return NULL;
}

static void set_ack(const st_command_runtime_t *runtime, const st_command_t *command,
                    uint64_t now_ms, st_command_status_t status,
                    st_command_reason_t reason, st_command_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->command_id = command->command_id;
    copy_id(ack->pod_id, sizeof(ack->pod_id), runtime->pod_id);
    ack->status = status;
    ack->reason = reason;
    ack->timestamp_ms = now_ms;
}

static int persist_ack(st_command_runtime_t *runtime, const st_command_ack_t *ack)
{
    uint8_t index = runtime->persistent.history_next;
    st_command_history_entry_t *entry = &runtime->persistent.history[index];
    entry->command_id = ack->command_id;
    entry->status = ack->status;
    entry->reason = ack->reason;
    entry->applied_config_revision = ack->applied_config_revision;
    entry->timestamp_ms = ack->timestamp_ms;
    if (runtime->persistent.history_count < ST_COMMAND_HISTORY_CAPACITY) {
        runtime->persistent.history_count++;
    }
    runtime->persistent.history_next = (uint8_t)((index + 1U) % ST_COMMAND_HISTORY_CAPACITY);
    return runtime->persistence.save == NULL
               ? 0
               : runtime->persistence.save(runtime->persistence.context, &runtime->persistent);
}

static st_command_reason_t config_result_reason(st_config_result_t result)
{
    switch (result) {
    case ST_CONFIG_RESULT_OK: return ST_COMMAND_REASON_NONE;
    case ST_CONFIG_RESULT_UNSUPPORTED: return ST_COMMAND_REASON_UNSUPPORTED;
    case ST_CONFIG_RESULT_OUT_OF_BOUNDS: return ST_COMMAND_REASON_OUT_OF_BOUNDS;
    case ST_CONFIG_RESULT_REVISION_CONFLICT: return ST_COMMAND_REASON_REVISION_CONFLICT;
    case ST_CONFIG_RESULT_FULL: return ST_COMMAND_REASON_CONFIG_FULL;
    case ST_CONFIG_RESULT_NOT_FOUND: return ST_COMMAND_REASON_NOT_CONFIGURED;
    default: return ST_COMMAND_REASON_INVALID_SYNTAX;
    }
}

static st_command_reason_t validate_common(const st_command_runtime_t *runtime,
                                           const st_command_t *command,
                                           uint64_t now_ms,
                                           st_command_status_t *status)
{
    if (command->command_id == 0U || command->command_type < ST_COMMAND_SET_THRESHOLD ||
        command->command_type > ST_COMMAND_ACK_ALARM ||
        command->target < ST_COMMAND_TARGET_CO2_THRESHOLD ||
        command->target > ST_COMMAND_TARGET_CAPABILITIES ||
        command->source < ST_COMMAND_SOURCE_THINGSBOARD ||
        command->source > ST_COMMAND_SOURCE_LOCAL_MAINTENANCE ||
        command->expires_at_ms <= command->issued_at_ms || command->valid_for_ms == 0U ||
        command->valid_for_ms > 60000U) {
        return ST_COMMAND_REASON_INVALID_SYNTAX;
    }
    if (strcmp(command->target_pod_id, runtime->pod_id) != 0) {
        return ST_COMMAND_REASON_WRONG_TARGET;
    }
    if (now_ms > command->expires_at_ms) {
        *status = ST_COMMAND_STATUS_EXPIRED;
        return ST_COMMAND_REASON_EXPIRED;
    }
    if ((runtime->capabilities.command_mask & (1UL << command->command_type)) == 0U ||
        (runtime->capabilities.target_mask & (1UL << command->target)) == 0U) {
        return ST_COMMAND_REASON_UNSUPPORTED;
    }
    return ST_COMMAND_REASON_NONE;
}

static st_command_reason_t resolve_rule_request(const st_command_t *command,
                                                st_sensor_kind_t *capability,
                                                st_config_rule_kind_t *rule_kind)
{
    if (command->command_type == ST_COMMAND_SET_THRESHOLD &&
        command->target == ST_COMMAND_TARGET_CO2_THRESHOLD) {
        *capability = ST_SENSOR_CO2_PPM;
        *rule_kind = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
        return ST_COMMAND_REASON_NONE;
    }
    if (command->command_type == ST_COMMAND_GET_CONFIG &&
        command->target == ST_COMMAND_TARGET_CONFIG) {
        *capability = ST_SENSOR_CO2_PPM;
        *rule_kind = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
        return ST_COMMAND_REASON_NONE;
    }
    if ((command->command_type != ST_COMMAND_SET_RULE &&
         command->command_type != ST_COMMAND_GET_RULE) ||
        command->target != ST_COMMAND_TARGET_CAPABILITY_RULE ||
        (uint32_t)command->capability >= (uint32_t)ST_SENSOR_UNKNOWN ||
        command->rule_kind < ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD ||
        command->rule_kind > ST_CONFIG_RULE_STATE_ACTIVE_VALUE) {
        return ST_COMMAND_REASON_INVALID_SYNTAX;
    }
    *capability = command->capability;
    *rule_kind = command->rule_kind;
    return ST_COMMAND_REASON_NONE;
}

int st_command_runtime_handle(st_command_runtime_t *runtime, const st_command_t *command,
                              uint64_t now_ms, st_command_ack_t *ack)
{
    st_command_status_t status = ST_COMMAND_STATUS_REJECTED;
    st_command_reason_t reason;
    const st_command_history_entry_t *duplicate;
    st_command_persistent_state_t before;
    st_capability_rule_t rule;
    st_sensor_kind_t capability = ST_SENSOR_UNKNOWN;
    st_config_rule_kind_t rule_kind = 0;
    st_config_result_t config_result;
    size_t alarm_event_count;
    uint8_t silence_active;
    uint64_t silence_until_ms;
    uint64_t silenced_instance_id;

    if (runtime == NULL || command == NULL || ack == NULL) {
        return -1;
    }
    duplicate = find_history(runtime, command->command_id);
    if (duplicate != NULL) {
        set_ack(runtime, command, now_ms, ST_COMMAND_STATUS_DUPLICATE,
                duplicate->reason, ack);
        ack->applied_config_revision = duplicate->applied_config_revision;
        ack->alarm_instance_id = command->alarm_instance_id;
        ack->capability_mask = runtime->capabilities.capability_mask;
        ack->ruleset_revision = runtime->persistent.alarm_state.ruleset_revision;
        return 0;
    }
    before = runtime->persistent;
    alarm_event_count = runtime->alarm.event_count;
    silence_active = runtime->alarm.silence_active;
    silence_until_ms = runtime->alarm.silence_until_ms;
    silenced_instance_id = runtime->alarm.silenced_instance_id;
    reason = validate_common(runtime, command, now_ms, &status);
    if (reason == ST_COMMAND_REASON_NONE &&
        (command->command_type == ST_COMMAND_SET_THRESHOLD ||
         command->command_type == ST_COMMAND_GET_CONFIG ||
         command->command_type == ST_COMMAND_SET_RULE ||
         command->command_type == ST_COMMAND_GET_RULE)) {
        reason = resolve_rule_request(command, &capability, &rule_kind);
    }
    if (reason == ST_COMMAND_REASON_NONE &&
        (command->command_type == ST_COMMAND_GET_RULE ||
         command->command_type == ST_COMMAND_GET_CONFIG)) {
        config_result = st_capability_config_get(&runtime->persistent.capability_config,
                                                 capability, rule_kind, &rule);
        reason = config_result_reason(config_result);
    } else if (reason == ST_COMMAND_REASON_NONE &&
               (command->command_type == ST_COMMAND_SET_THRESHOLD ||
                command->command_type == ST_COMMAND_SET_RULE)) {
        config_result = st_capability_config_set(&runtime->persistent.capability_config,
                                                 runtime->profile, capability, rule_kind,
                                                 command->value,
                                                 command->config_revision, &rule);
        reason = config_result_reason(config_result);
        if (reason == ST_COMMAND_REASON_NONE &&
            st_alarm_runtime_rule_changed(&runtime->alarm, &rule, now_ms) != 0) {
            runtime->persistent = before;
            runtime->alarm.event_count = alarm_event_count;
            reason = ST_COMMAND_REASON_CONFIG_FULL;
        }
    } else if (reason == ST_COMMAND_REASON_NONE &&
               command->command_type == ST_COMMAND_GET_CAPABILITIES &&
               command->target != ST_COMMAND_TARGET_CAPABILITIES) {
        reason = ST_COMMAND_REASON_INVALID_SYNTAX;
    } else if (reason == ST_COMMAND_REASON_NONE &&
               command->command_type == ST_COMMAND_ACK_ALARM) {
        st_alarm_persist_fn saved_persist = runtime->alarm.persist;
        int alarm_result;
        if (command->target != ST_COMMAND_TARGET_ALARM ||
            command->alarm_instance_id == 0U) {
            reason = ST_COMMAND_REASON_INVALID_SYNTAX;
        } else {
            runtime->alarm.persist = NULL;
            alarm_result = st_alarm_runtime_acknowledge(
                &runtime->alarm, command->alarm_instance_id, now_ms);
            runtime->alarm.persist = saved_persist;
            if (alarm_result == -1) {
                reason = ST_COMMAND_REASON_NOT_ACTIVE;
            } else if (alarm_result != 0) {
                reason = ST_COMMAND_REASON_PERSISTENCE_FAILED;
            }
        }
    } else if (reason == ST_COMMAND_REASON_NONE &&
               command->command_type == ST_COMMAND_SILENCE_ALARM) {
        if (st_alarm_runtime_silence(&runtime->alarm,
                                     command->alarm_instance_id,
                                     command->duration_ms, now_ms) != 0) {
            reason = ST_COMMAND_REASON_UNSUPPORTED;
        }
    } else if (reason == ST_COMMAND_REASON_NONE &&
               command->command_type == ST_COMMAND_TEST_OUTPUT) {
        reason = ST_COMMAND_REASON_UNSUPPORTED;
    }
    if (reason == ST_COMMAND_REASON_NONE) {
        sync_legacy_co2_mirror(&runtime->persistent);
        status = ST_COMMAND_STATUS_EXECUTED;
    }
    set_ack(runtime, command, now_ms, status, reason, ack);
    if (reason == ST_COMMAND_REASON_NONE) {
        if (command->command_type == ST_COMMAND_SET_THRESHOLD ||
            command->command_type == ST_COMMAND_GET_CONFIG ||
            command->command_type == ST_COMMAND_SET_RULE ||
            command->command_type == ST_COMMAND_GET_RULE) {
            ack->applied_config_revision = rule.revision;
            ack->config_value = rule.value;
        }
        ack->alarm_instance_id = command->alarm_instance_id;
        ack->capability_mask = runtime->capabilities.capability_mask;
        ack->ruleset_revision = runtime->persistent.alarm_state.ruleset_revision;
    }
    if (persist_ack(runtime, ack) != 0) {
        runtime->persistent = before;
        runtime->alarm.event_count = alarm_event_count;
        runtime->alarm.silence_active = silence_active;
        runtime->alarm.silence_until_ms = silence_until_ms;
        runtime->alarm.silenced_instance_id = silenced_instance_id;
        set_ack(runtime, command, now_ms, ST_COMMAND_STATUS_FAILED,
                ST_COMMAND_REASON_PERSISTENCE_FAILED, ack);
    }
    return 0;
}

int st_command_runtime_get_rule(const st_command_runtime_t *runtime,
                                st_sensor_kind_t capability,
                                st_config_rule_kind_t rule_kind,
                                st_capability_rule_t *rule)
{
    if (runtime == NULL) {
        return -1;
    }
    return st_capability_config_get(&runtime->persistent.capability_config,
                                    capability, rule_kind, rule) == ST_CONFIG_RESULT_OK
               ? 0
               : -1;
}

int st_command_runtime_apply_reporting_rules(const st_command_runtime_t *runtime,
                                             st_reporting_policy_t *policy)
{
    return runtime == NULL ? -1
                           : st_capability_config_apply_reporting(
                                 &runtime->persistent.capability_config, policy);
}

int st_command_runtime_ingest_reading(st_command_runtime_t *runtime,
                                      const st_sensor_reading_t *reading,
                                      uint64_t now_ms)
{
    return runtime == NULL ? -1
                           : st_alarm_runtime_ingest(&runtime->alarm, reading,
                                                     now_ms);
}

void st_command_runtime_tick(st_command_runtime_t *runtime, uint64_t now_ms)
{
    if (runtime != NULL) {
        st_alarm_runtime_tick(&runtime->alarm, now_ms);
    }
}

int st_command_runtime_next_control_event(st_command_runtime_t *runtime,
                                          st_control_event_t *event)
{
    return runtime == NULL ? -1
                           : st_alarm_runtime_next_event(&runtime->alarm, event);
}

const char *st_command_type_name(st_command_type_t type)
{
    switch (type) {
    case ST_COMMAND_SET_THRESHOLD: return "set_threshold";
    case ST_COMMAND_SILENCE_ALARM: return "silence_alarm";
    case ST_COMMAND_TEST_OUTPUT: return "test_output";
    case ST_COMMAND_GET_CONFIG: return "get_config";
    case ST_COMMAND_SET_RULE: return "set_rule";
    case ST_COMMAND_GET_RULE: return "get_rule";
    case ST_COMMAND_GET_CAPABILITIES: return "get_capabilities";
    case ST_COMMAND_ACK_ALARM: return "ack_alarm";
    default: return "unknown";
    }
}

const char *st_command_target_name(st_command_target_t target)
{
    switch (target) {
    case ST_COMMAND_TARGET_CO2_THRESHOLD: return "co2_threshold_ppm";
    case ST_COMMAND_TARGET_ALARM: return "alarm";
    case ST_COMMAND_TARGET_LED: return "led";
    case ST_COMMAND_TARGET_BUZZER: return "buzzer";
    case ST_COMMAND_TARGET_CONFIG: return "config";
    case ST_COMMAND_TARGET_CAPABILITY_RULE: return "capability_rule";
    case ST_COMMAND_TARGET_CAPABILITIES: return "capabilities";
    default: return "unknown";
    }
}

const char *st_command_status_name(st_command_status_t status)
{
    switch (status) {
    case ST_COMMAND_STATUS_QUEUED: return "queued";
    case ST_COMMAND_STATUS_DELIVERED: return "delivered";
    case ST_COMMAND_STATUS_EXECUTED: return "executed";
    case ST_COMMAND_STATUS_REJECTED: return "rejected";
    case ST_COMMAND_STATUS_EXPIRED: return "expired";
    case ST_COMMAND_STATUS_DUPLICATE: return "duplicate";
    case ST_COMMAND_STATUS_FAILED: return "failed";
    default: return "unknown";
    }
}

const char *st_command_reason_name(st_command_reason_t reason)
{
    switch (reason) {
    case ST_COMMAND_REASON_NONE: return "none";
    case ST_COMMAND_REASON_INVALID_SYNTAX: return "invalid_syntax";
    case ST_COMMAND_REASON_WRONG_TARGET: return "wrong_target";
    case ST_COMMAND_REASON_UNSUPPORTED: return "unsupported";
    case ST_COMMAND_REASON_EXPIRED: return "expired";
    case ST_COMMAND_REASON_REVISION_CONFLICT: return "revision_conflict";
    case ST_COMMAND_REASON_OUT_OF_BOUNDS: return "out_of_bounds";
    case ST_COMMAND_REASON_PERSISTENCE_FAILED: return "persistence_failed";
    case ST_COMMAND_REASON_QUEUE_FULL: return "queue_full";
    case ST_COMMAND_REASON_TRANSPORT_FAILED: return "transport_failed";
    case ST_COMMAND_REASON_TIMEOUT: return "timeout";
    case ST_COMMAND_REASON_NOT_CONFIGURED: return "not_configured";
    case ST_COMMAND_REASON_CONFIG_FULL: return "config_full";
    case ST_COMMAND_REASON_NOT_ACTIVE: return "not_active";
    default: return "unknown";
    }
}
