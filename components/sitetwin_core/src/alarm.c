#include "sitetwin/alarm.h"

#include <math.h>
#include <string.h>

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

static int rule_creates_alarm(st_config_rule_kind_t kind)
{
    return kind == ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD ||
           kind == ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD ||
           kind == ST_CONFIG_RULE_STATE_ACTIVE_VALUE;
}

void st_alarm_persistent_state_init(st_alarm_persistent_state_t *state)
{
    if (state == NULL) {
        return;
    }
    memset(state, 0, sizeof(*state));
    state->magic = ST_ALARM_PERSISTENCE_MAGIC;
    state->version = ST_ALARM_PERSISTENCE_VERSION;
    state->ruleset_revision = 1U;
}

int st_alarm_persistent_state_valid(const st_alarm_persistent_state_t *state,
                                    st_pod_profile_t profile)
{
    size_t index;
    uint8_t used = 0U;

    if (state == NULL || state->magic != ST_ALARM_PERSISTENCE_MAGIC ||
        state->version != ST_ALARM_PERSISTENCE_VERSION ||
        state->condition_count > ST_ALARM_CONDITION_CAPACITY ||
        state->ruleset_revision == 0U) {
        return 0;
    }
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        const st_alarm_condition_state_t *condition = &state->conditions[index];
        size_t other;
        if (condition->used == 0U) {
            continue;
        }
        ++used;
        if (!rule_creates_alarm(condition->rule_kind) ||
            !st_profile_has_capability(profile, condition->capability) ||
            !isfinite(condition->threshold) ||
            (condition->active != 0U && condition->instance_id == 0U) ||
            condition->acknowledged > 1U) {
            return 0;
        }
        for (other = index + 1U; other < ST_ALARM_CONDITION_CAPACITY; ++other) {
            const st_alarm_condition_state_t *candidate = &state->conditions[other];
            if (candidate->used != 0U &&
                candidate->capability == condition->capability &&
                candidate->rule_kind == condition->rule_kind) {
                return 0;
            }
        }
    }
    return used == state->condition_count;
}

static st_alarm_condition_state_t *find_condition(st_alarm_runtime_t *runtime,
                                                   st_sensor_kind_t capability,
                                                   st_config_rule_kind_t rule_kind,
                                                   size_t *condition_index)
{
    size_t index;
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        st_alarm_condition_state_t *condition = &runtime->persistent->conditions[index];
        if (condition->used != 0U && condition->capability == capability &&
            condition->rule_kind == rule_kind) {
            if (condition_index != NULL) {
                *condition_index = index;
            }
            return condition;
        }
    }
    return NULL;
}

static st_alarm_condition_state_t *ensure_condition(st_alarm_runtime_t *runtime,
                                                     const st_capability_rule_t *rule,
                                                     size_t *condition_index)
{
    st_alarm_condition_state_t *condition;
    size_t index;

    condition = find_condition(runtime, rule->capability, rule->rule_kind,
                               condition_index);
    if (condition != NULL) {
        condition->threshold = rule->value;
        condition->config_revision = rule->revision;
        return condition;
    }
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        condition = &runtime->persistent->conditions[index];
        if (condition->used == 0U) {
            memset(condition, 0, sizeof(*condition));
            condition->used = 1U;
            condition->capability = rule->capability;
            condition->rule_kind = rule->rule_kind;
            condition->threshold = rule->value;
            condition->config_revision = rule->revision;
            ++runtime->persistent->condition_count;
            if (condition_index != NULL) {
                *condition_index = index;
            }
            return condition;
        }
    }
    return NULL;
}

static void queue_event(st_alarm_runtime_t *runtime, const st_control_event_t *event)
{
    if (runtime->event_count == ST_ALARM_EVENT_CAPACITY) {
        memmove(&runtime->events[0], &runtime->events[1],
                (ST_ALARM_EVENT_CAPACITY - 1U) * sizeof(runtime->events[0]));
        --runtime->event_count;
        ++runtime->event_drops;
    }
    runtime->events[runtime->event_count++] = *event;
}

static st_control_event_t base_event(st_alarm_runtime_t *runtime,
                                     st_control_event_kind_t kind,
                                     st_control_transition_t transition,
                                     st_control_reason_t reason,
                                     uint64_t now_ms)
{
    st_control_event_t event;
    memset(&event, 0, sizeof(event));
    event.event_kind = kind;
    event.transition = transition;
    event.reason = reason;
    event.timestamp_ms = now_ms;
    event.capability = ST_SENSOR_UNKNOWN;
    event.secondary_capability = ST_SENSOR_UNKNOWN;
    event.shared_alarm_indicator_verified =
        runtime->shared_alarm_indicator_verified;
    event.capability_mask = runtime->capability_mask;
    event.ruleset_revision = runtime->persistent->ruleset_revision;
    event.boot_id = runtime->boot_id;
    event.sequence = ++runtime->event_sequence;
    copy_id(event.pod_id, sizeof(event.pod_id), runtime->pod_id);
    copy_id(event.sensor_id, sizeof(event.sensor_id), "control_state");
    return event;
}

static void emit_capabilities(st_alarm_runtime_t *runtime, uint64_t now_ms)
{
    st_control_event_t event = base_event(runtime, ST_CONTROL_EVENT_CAPABILITIES,
                                          ST_CONTROL_TRANSITION_SNAPSHOT,
                                          ST_CONTROL_REASON_NONE, now_ms);
    copy_id(event.sensor_id, sizeof(event.sensor_id), "capabilities");
    queue_event(runtime, &event);
}

static void emit_configuration(st_alarm_runtime_t *runtime,
                               const st_capability_rule_t *rule,
                               st_control_transition_t transition,
                               uint64_t now_ms)
{
    st_control_event_t event = base_event(runtime, ST_CONTROL_EVENT_CONFIGURATION,
                                          transition,
                                          transition == ST_CONTROL_TRANSITION_UPDATED
                                              ? ST_CONTROL_REASON_CONFIGURATION_CHANGED
                                              : ST_CONTROL_REASON_NONE,
                                          now_ms);
    copy_id(event.sensor_id, sizeof(event.sensor_id), "configuration");
    event.capability = rule->capability;
    event.rule_kind = rule->rule_kind;
    event.threshold = rule->value;
    event.config_revision = rule->revision;
    queue_event(runtime, &event);
}

static uint64_t make_instance_id(st_alarm_runtime_t *runtime)
{
    uint32_t hash = 2166136261U;
    size_t index;
    uint64_t prefix;

    for (index = 0U; runtime->pod_id[index] != '\0'; ++index) {
        hash ^= (uint8_t)runtime->pod_id[index];
        hash *= 16777619U;
    }
    ++runtime->persistent->next_instance_counter;
    if (runtime->persistent->next_instance_counter == 0U) {
        runtime->persistent->next_instance_counter = 1U;
    }
    prefix = (uint64_t)(hash & 0xFFFFU) << 32U;
    return prefix | runtime->persistent->next_instance_counter;
}

static int persist(st_alarm_runtime_t *runtime)
{
    return runtime->persist == NULL ? 0
                                    : runtime->persist(runtime->persist_context);
}

static void emit_condition(st_alarm_runtime_t *runtime,
                           const st_alarm_condition_state_t *condition,
                           const st_sensor_reading_t *reading,
                           st_control_transition_t transition,
                           st_control_reason_t reason,
                           uint64_t now_ms)
{
    st_control_event_t event = base_event(runtime, ST_CONTROL_EVENT_ALARM_CONDITION,
                                          transition, reason, now_ms);
    event.instance_id = condition->instance_id;
    event.active = condition->active;
    event.acknowledged = condition->acknowledged;
    event.silenced = runtime->silence_active;
    event.capability = condition->capability;
    event.rule_kind = condition->rule_kind;
    event.threshold = condition->threshold;
    event.observed = reading == NULL ? condition->last_observed : reading->value;
    event.quality_flags = reading == NULL ? condition->quality_flags
                                          : reading->quality_flags;
    event.config_revision = condition->config_revision;
    event.evidence_count = 1U;
    if (reading != NULL) {
        copy_id(event.sensor_id, sizeof(event.sensor_id), reading->sensor_id);
        event.boot_id = reading->boot_id;
        event.sequence = reading->sequence;
    } else {
        copy_id(event.sensor_id, sizeof(event.sensor_id), "alarm_condition");
    }
    queue_event(runtime, &event);
}

static int sync_conditions(st_alarm_runtime_t *runtime)
{
    size_t index;
    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        const st_capability_rule_t *rule = &runtime->config->rules[index];
        if (rule->used != 0U && rule_creates_alarm(rule->rule_kind) &&
            ensure_condition(runtime, rule, NULL) == NULL) {
            return -1;
        }
    }
    return 0;
}

int st_alarm_runtime_init(st_alarm_runtime_t *runtime, const char *pod_id,
                          const st_capability_config_t *config,
                          st_alarm_persistent_state_t *persistent,
                          uint32_t capability_mask, uint32_t boot_id,
                          uint8_t shared_alarm_indicator_verified,
                          st_alarm_persist_fn persist_fn, void *persist_context,
                          uint64_t now_ms)
{
    size_t index;

    if (runtime == NULL || pod_id == NULL || pod_id[0] == '\0' || config == NULL ||
        persistent == NULL) {
        return -1;
    }
    memset(runtime, 0, sizeof(*runtime));
    copy_id(runtime->pod_id, sizeof(runtime->pod_id), pod_id);
    runtime->config = config;
    runtime->persistent = persistent;
    runtime->capability_mask = capability_mask;
    runtime->boot_id = boot_id;
    runtime->shared_alarm_indicator_verified =
        shared_alarm_indicator_verified != 0U ? 1U : 0U;
    runtime->persist = persist_fn;
    runtime->persist_context = persist_context;
    if (sync_conditions(runtime) != 0) {
        return -1;
    }
    emit_capabilities(runtime, now_ms);
    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        const st_capability_rule_t *rule = &config->rules[index];
        if (rule->used != 0U) {
            emit_configuration(runtime, rule, ST_CONTROL_TRANSITION_SNAPSHOT, now_ms);
        }
    }
    {
        st_control_event_t reboot = base_event(runtime, ST_CONTROL_EVENT_ALARM_SILENCE,
                                               ST_CONTROL_TRANSITION_REBOOT_RESET,
                                               ST_CONTROL_REASON_REBOOT_RESET, now_ms);
        copy_id(reboot.sensor_id, sizeof(reboot.sensor_id), "alarm_silence");
        reboot.active = 0U;
        reboot.silenced = 0U;
        queue_event(runtime, &reboot);
    }
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        const st_alarm_condition_state_t *condition = &persistent->conditions[index];
        if (condition->used != 0U && condition->active != 0U) {
            emit_condition(runtime, condition, NULL, ST_CONTROL_TRANSITION_RECOVERED,
                           ST_CONTROL_REASON_REBOOT_RESET, now_ms);
        }
    }
    return 0;
}

int st_alarm_runtime_rule_changed(st_alarm_runtime_t *runtime,
                                  const st_capability_rule_t *rule,
                                  uint64_t now_ms)
{
    if (runtime == NULL || rule == NULL || rule->used == 0U) {
        return -1;
    }
    if (rule_creates_alarm(rule->rule_kind) && ensure_condition(runtime, rule, NULL) == NULL) {
        return -1;
    }
    ++runtime->persistent->ruleset_revision;
    if (runtime->persistent->ruleset_revision == 0U) {
        runtime->persistent->ruleset_revision = 1U;
    }
    emit_configuration(runtime, rule, ST_CONTROL_TRANSITION_UPDATED, now_ms);
    return 0;
}

static uint32_t debounce_ms(const st_alarm_runtime_t *runtime,
                            st_sensor_kind_t capability)
{
    st_capability_rule_t rule;
    if (st_capability_config_get(runtime->config, capability,
                                 ST_CONFIG_RULE_EVENT_DEBOUNCE_MS,
                                 &rule) == ST_CONFIG_RESULT_OK &&
        rule.value >= 0.0F) {
        return (uint32_t)rule.value;
    }
    return 0U;
}

static int reading_is_usable(const st_sensor_reading_t *reading)
{
    uint32_t rejected = ST_QUALITY_STALE | ST_QUALITY_CRC_FAILED |
                        ST_QUALITY_OUT_OF_RANGE | ST_QUALITY_SENSOR_MISSING;
    return reading != NULL && isfinite(reading->value) &&
           (reading->quality_flags & ST_QUALITY_VALID) != 0U &&
           (reading->quality_flags & rejected) == 0U;
}

static int desired_state(const st_alarm_condition_state_t *condition, float value)
{
    if (condition->rule_kind == ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD) {
        return value >= condition->threshold;
    }
    if (condition->rule_kind == ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD) {
        return value <= condition->threshold;
    }
    if (condition->rule_kind == ST_CONFIG_RULE_STATE_ACTIVE_VALUE) {
        return value == condition->threshold;
    }
    return 0;
}

static int apply_transition(st_alarm_runtime_t *runtime,
                            st_alarm_condition_state_t *condition,
                            const st_sensor_reading_t *reading,
                            int active, uint64_t now_ms)
{
    st_alarm_persistent_state_t before = *runtime->persistent;
    uint64_t cleared_instance = condition->instance_id;

    condition->active = active ? 1U : 0U;
    condition->acknowledged = 0U;
    condition->last_observed = reading->value;
    condition->quality_flags = reading->quality_flags;
    if (active) {
        condition->instance_id = make_instance_id(runtime);
    }
    if (persist(runtime) != 0) {
        *runtime->persistent = before;
        return -2;
    }
    if (!active) {
        condition->instance_id = cleared_instance;
    }
    emit_condition(runtime, condition, reading,
                   active ? ST_CONTROL_TRANSITION_ACTIVE
                          : ST_CONTROL_TRANSITION_CLEARED,
                   active ? ST_CONTROL_REASON_RULE_TRIGGERED
                          : ST_CONTROL_REASON_RULE_CLEARED,
                   now_ms);
    if (!active) {
        condition->instance_id = 0U;
    }
    return 1;
}

int st_alarm_runtime_ingest(st_alarm_runtime_t *runtime,
                            const st_sensor_reading_t *reading,
                            uint64_t now_ms)
{
    size_t index;
    int transitions = 0;

    if (runtime == NULL || !reading_is_usable(reading)) {
        return runtime == NULL ? -1 : 0;
    }
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        st_alarm_condition_state_t *condition = &runtime->persistent->conditions[index];
        int desired;
        uint32_t duration;
        int result;

        if (condition->used == 0U || condition->capability != reading->sensor_kind) {
            continue;
        }
        desired = desired_state(condition, reading->value);
        if (desired == (condition->active != 0U)) {
            runtime->candidate_valid[index] = 0U;
            continue;
        }
        duration = condition->rule_kind == ST_CONFIG_RULE_STATE_ACTIVE_VALUE
                       ? debounce_ms(runtime, condition->capability)
                       : 0U;
        if (duration != 0U) {
            if (runtime->candidate_valid[index] == 0U ||
                runtime->candidate_active[index] != (uint8_t)desired) {
                runtime->candidate_valid[index] = 1U;
                runtime->candidate_active[index] = (uint8_t)desired;
                runtime->candidate_since_ms[index] = now_ms;
                continue;
            }
            if (now_ms - runtime->candidate_since_ms[index] < duration) {
                continue;
            }
        }
        runtime->candidate_valid[index] = 0U;
        result = apply_transition(runtime, condition, reading, desired, now_ms);
        if (result < 0) {
            return result;
        }
        transitions += result;
    }
    return transitions;
}

int st_alarm_runtime_acknowledge(st_alarm_runtime_t *runtime,
                                 uint64_t instance_id, uint64_t now_ms)
{
    size_t index;
    st_alarm_persistent_state_t before;

    if (runtime == NULL || instance_id == 0U) {
        return -1;
    }
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        st_alarm_condition_state_t *condition = &runtime->persistent->conditions[index];
        if (condition->used == 0U || condition->active == 0U ||
            condition->instance_id != instance_id) {
            continue;
        }
        if (condition->acknowledged != 0U) {
            return 0;
        }
        before = *runtime->persistent;
        condition->acknowledged = 1U;
        if (persist(runtime) != 0) {
            *runtime->persistent = before;
            return -2;
        }
        {
            st_control_event_t event = base_event(
                runtime, ST_CONTROL_EVENT_ALARM_ACKNOWLEDGEMENT,
                ST_CONTROL_TRANSITION_ACKNOWLEDGED, ST_CONTROL_REASON_COMMAND,
                now_ms);
            event.instance_id = instance_id;
            event.active = 1U;
            event.acknowledged = 1U;
            event.silenced = runtime->silence_active;
            event.capability = condition->capability;
            event.rule_kind = condition->rule_kind;
            event.threshold = condition->threshold;
            event.observed = condition->last_observed;
            event.quality_flags = condition->quality_flags;
            event.config_revision = condition->config_revision;
            copy_id(event.sensor_id, sizeof(event.sensor_id), "alarm_ack");
            queue_event(runtime, &event);
        }
        return 0;
    }
    return -1;
}

int st_alarm_runtime_silence(st_alarm_runtime_t *runtime,
                             uint64_t instance_id, uint32_t duration_ms,
                             uint64_t now_ms)
{
    st_control_event_t event;

    if (runtime == NULL || runtime->shared_alarm_indicator_verified == 0U ||
        duration_ms == 0U || duration_ms > 60000U ||
        !st_alarm_runtime_condition_active(runtime, instance_id)) {
        return -1;
    }
    runtime->silence_active = 1U;
    runtime->silenced_instance_id = instance_id;
    runtime->silence_until_ms = now_ms + duration_ms;
    event = base_event(runtime, ST_CONTROL_EVENT_ALARM_SILENCE,
                       ST_CONTROL_TRANSITION_SILENCE_ACTIVE,
                       ST_CONTROL_REASON_COMMAND, now_ms);
    event.instance_id = instance_id;
    event.active = 1U;
    event.silenced = 1U;
    copy_id(event.sensor_id, sizeof(event.sensor_id), "alarm_silence");
    queue_event(runtime, &event);
    return 0;
}

void st_alarm_runtime_tick(st_alarm_runtime_t *runtime, uint64_t now_ms)
{
    st_control_event_t event;
    if (runtime == NULL || runtime->silence_active == 0U ||
        now_ms < runtime->silence_until_ms) {
        return;
    }
    event = base_event(runtime, ST_CONTROL_EVENT_ALARM_SILENCE,
                       ST_CONTROL_TRANSITION_SILENCE_EXPIRED,
                       ST_CONTROL_REASON_EXPIRED, now_ms);
    event.instance_id = runtime->silenced_instance_id;
    event.active = 0U;
    event.silenced = 0U;
    copy_id(event.sensor_id, sizeof(event.sensor_id), "alarm_silence");
    runtime->silence_active = 0U;
    runtime->silence_until_ms = 0U;
    runtime->silenced_instance_id = 0U;
    queue_event(runtime, &event);
}

int st_alarm_runtime_next_event(st_alarm_runtime_t *runtime,
                                st_control_event_t *event)
{
    if (runtime == NULL || event == NULL || runtime->event_count == 0U) {
        return -1;
    }
    *event = runtime->events[0];
    if (runtime->event_count > 1U) {
        memmove(&runtime->events[0], &runtime->events[1],
                (runtime->event_count - 1U) * sizeof(runtime->events[0]));
    }
    --runtime->event_count;
    return 0;
}

int st_alarm_runtime_condition_active(const st_alarm_runtime_t *runtime,
                                      uint64_t instance_id)
{
    size_t index;
    if (runtime == NULL || instance_id == 0U) {
        return 0;
    }
    for (index = 0U; index < ST_ALARM_CONDITION_CAPACITY; ++index) {
        const st_alarm_condition_state_t *condition =
            &runtime->persistent->conditions[index];
        if (condition->used != 0U && condition->active != 0U &&
            condition->instance_id == instance_id) {
            return 1;
        }
    }
    return 0;
}
