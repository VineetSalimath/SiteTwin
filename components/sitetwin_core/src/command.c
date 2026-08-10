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
    *length = ST_COMMAND_WIRE_SIZE;
    return 0;
}

int st_command_decode(const uint8_t *payload, size_t length, st_command_t *command)
{
    if (payload == NULL || command == NULL || length != ST_COMMAND_WIRE_SIZE ||
        payload[0] != ST_COMMAND_CONTRACT_VERSION || payload[55] != 0U) {
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
    *length = ST_COMMAND_ACK_WIRE_SIZE;
    return 0;
}

int st_command_ack_decode(const uint8_t *payload, size_t length, st_command_ack_t *ack)
{
    if (payload == NULL || ack == NULL || length != ST_COMMAND_ACK_WIRE_SIZE ||
        payload[0] != ST_COMMAND_CONTRACT_VERSION || payload[27] != 0U) {
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
    return ack->pod_id[0] == '\0' ? -1 : 0;
}

st_pod_capabilities_t st_pod_capabilities(st_pod_profile_t profile)
{
    st_pod_capabilities_t capabilities = {0};
    if (profile == ST_POD_ENVIRONMENT) {
        capabilities.command_mask = (1UL << ST_COMMAND_SET_THRESHOLD) |
                                    (1UL << ST_COMMAND_SILENCE_ALARM) |
                                    (1UL << ST_COMMAND_TEST_OUTPUT) |
                                    (1UL << ST_COMMAND_GET_CONFIG);
        capabilities.target_mask = (1UL << ST_COMMAND_TARGET_CO2_THRESHOLD) |
                                   (1UL << ST_COMMAND_TARGET_ALARM) |
                                   (1UL << ST_COMMAND_TARGET_LED) |
                                   (1UL << ST_COMMAND_TARGET_BUZZER) |
                                   (1UL << ST_COMMAND_TARGET_CONFIG);
    } else {
        capabilities.pending_hardware_verification = 1U;
    }
    return capabilities;
}

static int state_valid(const st_command_persistent_state_t *state)
{
    return state->magic == ST_COMMAND_PERSISTENCE_MAGIC &&
           state->version == ST_COMMAND_CONTRACT_VERSION &&
           state->history_count <= ST_COMMAND_HISTORY_CAPACITY &&
           state->history_next < ST_COMMAND_HISTORY_CAPACITY &&
           isfinite(state->config.co2_threshold_ppm) &&
           state->config.co2_threshold_ppm >= ST_COMMAND_MIN_CO2_THRESHOLD_PPM &&
           state->config.co2_threshold_ppm <= ST_COMMAND_MAX_CO2_THRESHOLD_PPM;
}

int st_command_runtime_init(st_command_runtime_t *runtime, st_pod_profile_t profile,
                            const char *pod_id, st_command_persistence_t persistence)
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
    runtime->persistent.version = ST_COMMAND_CONTRACT_VERSION;
    runtime->persistent.config.co2_threshold_ppm = ST_COMMAND_DEFAULT_CO2_THRESHOLD_PPM;
    runtime->persistent.config.revision = 1U;
    if (persistence.load != NULL) {
        st_command_persistent_state_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        if (persistence.load(persistence.context, &loaded) == 0 && state_valid(&loaded)) {
            runtime->persistent = loaded;
        }
    }
    return 0;
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
    ack->applied_config_revision = runtime->persistent.config.revision;
    ack->timestamp_ms = now_ms;
    ack->config_value = runtime->persistent.config.co2_threshold_ppm;
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

static st_command_reason_t validate_command(const st_command_runtime_t *runtime,
                                             const st_command_t *command,
                                             uint64_t now_ms,
                                             st_command_status_t *status)
{
    if (command->command_id == 0U || command->command_type < ST_COMMAND_SET_THRESHOLD ||
        command->command_type > ST_COMMAND_GET_CONFIG ||
        command->target < ST_COMMAND_TARGET_CO2_THRESHOLD ||
        command->target > ST_COMMAND_TARGET_CONFIG ||
        command->source < ST_COMMAND_SOURCE_THINGSBOARD ||
        command->source > ST_COMMAND_SOURCE_LOCAL_MAINTENANCE ||
        command->expires_at_ms <= command->issued_at_ms || command->valid_for_ms == 0U ||
        command->valid_for_ms > 60000U) {
        return ST_COMMAND_REASON_INVALID_SYNTAX;
    }
    if (strcmp(command->target_pod_id, runtime->pod_id) != 0) {
        return ST_COMMAND_REASON_WRONG_TARGET;
    }
    if ((runtime->capabilities.command_mask & (1UL << command->command_type)) == 0U ||
        (runtime->capabilities.target_mask & (1UL << command->target)) == 0U) {
        return ST_COMMAND_REASON_UNSUPPORTED;
    }
    if (now_ms > command->expires_at_ms) {
        *status = ST_COMMAND_STATUS_EXPIRED;
        return ST_COMMAND_REASON_EXPIRED;
    }
    if (command->command_type == ST_COMMAND_SET_THRESHOLD) {
        if (command->target != ST_COMMAND_TARGET_CO2_THRESHOLD || !isfinite(command->value) ||
            command->value < ST_COMMAND_MIN_CO2_THRESHOLD_PPM ||
            command->value > ST_COMMAND_MAX_CO2_THRESHOLD_PPM) {
            return ST_COMMAND_REASON_OUT_OF_BOUNDS;
        }
        if (command->config_revision != runtime->persistent.config.revision + 1U) {
            return ST_COMMAND_REASON_REVISION_CONFLICT;
        }
    } else if (command->config_revision != 0U &&
               command->config_revision != runtime->persistent.config.revision) {
        return ST_COMMAND_REASON_REVISION_CONFLICT;
    }
    if (command->command_type == ST_COMMAND_SILENCE_ALARM &&
        (command->target != ST_COMMAND_TARGET_ALARM || command->duration_ms == 0U ||
         command->duration_ms > ST_COMMAND_MAX_SILENCE_MS)) {
        return ST_COMMAND_REASON_OUT_OF_BOUNDS;
    }
    if (command->command_type == ST_COMMAND_TEST_OUTPUT &&
        ((command->target != ST_COMMAND_TARGET_LED &&
          command->target != ST_COMMAND_TARGET_BUZZER &&
          command->target != ST_COMMAND_TARGET_ALARM) ||
         command->duration_ms == 0U || command->duration_ms > ST_COMMAND_MAX_TEST_OUTPUT_MS)) {
        return ST_COMMAND_REASON_OUT_OF_BOUNDS;
    }
    if (command->command_type == ST_COMMAND_GET_CONFIG &&
        command->target != ST_COMMAND_TARGET_CONFIG) {
        return ST_COMMAND_REASON_INVALID_SYNTAX;
    }
    return ST_COMMAND_REASON_NONE;
}

int st_command_runtime_handle(st_command_runtime_t *runtime, const st_command_t *command,
                              uint64_t now_ms, st_command_ack_t *ack)
{
    st_command_status_t status = ST_COMMAND_STATUS_REJECTED;
    st_command_reason_t reason;
    const st_command_history_entry_t *duplicate;
    st_command_persistent_state_t before;

    if (runtime == NULL || command == NULL || ack == NULL) {
        return -1;
    }
    duplicate = find_history(runtime, command->command_id);
    if (duplicate != NULL) {
        set_ack(runtime, command, now_ms, ST_COMMAND_STATUS_DUPLICATE,
                duplicate->reason, ack);
        ack->applied_config_revision = duplicate->applied_config_revision;
        return 0;
    }
    reason = validate_command(runtime, command, now_ms, &status);
    if (reason != ST_COMMAND_REASON_NONE) {
        set_ack(runtime, command, now_ms, status, reason, ack);
        before = runtime->persistent;
        if (persist_ack(runtime, ack) != 0) {
            runtime->persistent = before;
        }
        return 0;
    }

    before = runtime->persistent;
    if (command->command_type == ST_COMMAND_SET_THRESHOLD) {
        runtime->persistent.config.co2_threshold_ppm = command->value;
        runtime->persistent.config.revision = command->config_revision;
    }
    set_ack(runtime, command, now_ms, ST_COMMAND_STATUS_EXECUTED,
            ST_COMMAND_REASON_NONE, ack);
    if (persist_ack(runtime, ack) != 0) {
        runtime->persistent = before;
        set_ack(runtime, command, now_ms, ST_COMMAND_STATUS_FAILED,
                ST_COMMAND_REASON_PERSISTENCE_FAILED, ack);
        return 0;
    }

    if (command->command_type == ST_COMMAND_SILENCE_ALARM) {
        runtime->silence_until_ms = now_ms + command->duration_ms;
    } else if (command->command_type == ST_COMMAND_TEST_OUTPUT) {
        runtime->test_target = command->target;
        runtime->test_until_ms = now_ms + command->duration_ms;
    } else if (command->command_type == ST_COMMAND_SET_THRESHOLD &&
               runtime->has_valid_co2 != 0U &&
               runtime->last_valid_co2_ppm >
                   runtime->persistent.config.co2_threshold_ppm) {
        /* A stricter remote threshold may assert the alarm immediately, but a
         * remote configuration change must never clear an existing condition.
         * Only the next valid local sensor observation may clear it. */
        runtime->alarm_condition_active = 1U;
    }
    return 0;
}

void st_command_runtime_observe_co2(st_command_runtime_t *runtime, float co2_ppm,
                                    uint32_t quality_flags)
{
    const uint32_t invalid_flags = ST_QUALITY_WARMING_UP | ST_QUALITY_STALE |
                                   ST_QUALITY_CRC_FAILED | ST_QUALITY_OUT_OF_RANGE |
                                   ST_QUALITY_SENSOR_MISSING;
    if (runtime == NULL || !isfinite(co2_ppm) ||
        (quality_flags & ST_QUALITY_VALID) == 0U ||
        (quality_flags & invalid_flags) != 0U) {
        return;
    }
    runtime->has_valid_co2 = 1U;
    runtime->last_valid_co2_ppm = co2_ppm;
    runtime->alarm_condition_active = co2_ppm > runtime->persistent.config.co2_threshold_ppm;
}

st_local_actuation_state_t st_command_runtime_tick(st_command_runtime_t *runtime,
                                                   uint64_t now_ms)
{
    st_local_actuation_state_t output = {0};
    uint8_t alarm_phase;
    if (runtime == NULL) {
        return output;
    }
    if (runtime->silence_until_ms != 0U && now_ms >= runtime->silence_until_ms) {
        runtime->silence_until_ms = 0U;
    }
    if (runtime->test_until_ms != 0U && now_ms >= runtime->test_until_ms) {
        runtime->test_until_ms = 0U;
    }
    alarm_phase = (uint8_t)(((now_ms / 1000U) % 2U) == 0U);
    if (runtime->alarm_condition_active != 0U && alarm_phase != 0U) {
        output.led_active = 1U;
        if (runtime->silence_until_ms == 0U) {
            output.buzzer_active = 1U;
        }
    }
    if (runtime->test_until_ms > now_ms) {
        if (runtime->test_target == ST_COMMAND_TARGET_LED ||
            runtime->test_target == ST_COMMAND_TARGET_ALARM) {
            output.led_active = 1U;
        }
        if (runtime->test_target == ST_COMMAND_TARGET_BUZZER ||
            runtime->test_target == ST_COMMAND_TARGET_ALARM) {
            output.buzzer_active = 1U;
        }
    }
    output.buzzer_frequency_hz = output.buzzer_active != 0U ? 1000U : 0U;
    return output;
}

const char *st_command_type_name(st_command_type_t type)
{
    switch (type) {
    case ST_COMMAND_SET_THRESHOLD: return "set_threshold";
    case ST_COMMAND_SILENCE_ALARM: return "silence_alarm";
    case ST_COMMAND_TEST_OUTPUT: return "test_output";
    case ST_COMMAND_GET_CONFIG: return "get_config";
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
    default: return "unknown";
    }
}
