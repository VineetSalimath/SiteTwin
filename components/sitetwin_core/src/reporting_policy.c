#include "sitetwin/reporting_policy.h"

#include <string.h>

static st_reporting_rule_t make_rule(float deadband, uint32_t minimum_interval_ms,
                                     uint32_t maximum_interval_ms)
{
    st_reporting_rule_t rule;

    rule.deadband = deadband;
    rule.minimum_interval_ms = minimum_interval_ms;
    rule.maximum_interval_ms = maximum_interval_ms;
    return rule;
}

static void set_default_rules(st_reporting_policy_t *policy)
{
    policy->rules[ST_SENSOR_TEMPERATURE_C] = make_rule(0.2F, 30000U, 900000U);
    policy->rules[ST_SENSOR_RELATIVE_HUMIDITY_PERCENT] = make_rule(1.0F, 30000U, 900000U);
    policy->rules[ST_SENSOR_CO2_PPM] = make_rule(50.0F, 30000U, 300000U);
    policy->rules[ST_SENSOR_VOC_INDEX] = make_rule(5.0F, 30000U, 300000U);
    policy->rules[ST_SENSOR_ILLUMINANCE_LUX] = make_rule(10.0F, 30000U, 900000U);
    policy->rules[ST_SENSOR_CURRENT_MA] = make_rule(50.0F, 10000U, 300000U);
    policy->rules[ST_SENSOR_VOLTAGE_V] = make_rule(0.1F, 10000U, 300000U);
    policy->rules[ST_SENSOR_VIBRATION_RMS_G] = make_rule(0.02F, 5000U, 60000U);
    policy->rules[ST_SENSOR_UNKNOWN] = make_rule(0.0F, 30000U, 300000U);
}

static st_reporting_state_t *find_state(st_reporting_policy_t *policy, const char *sensor_id)
{
    size_t index;
    st_reporting_state_t *free_state = NULL;

    for (index = 0U; index < ST_REPORTING_STATE_CAPACITY; ++index) {
        st_reporting_state_t *state = &policy->states[index];

        if (state->used == 0U) {
            if (free_state == NULL) {
                free_state = state;
            }
        } else if (strcmp(state->sensor_id, sensor_id) == 0) {
            return state;
        }
    }
    return free_state;
}

static void remember_report(st_reporting_state_t *state, const st_sensor_reading_t *reading)
{
    if (state->used == 0U) {
        strncpy(state->sensor_id, reading->sensor_id, sizeof(state->sensor_id) - 1U);
        state->sensor_id[sizeof(state->sensor_id) - 1U] = '\0';
        state->used = 1U;
    }
    state->last_reported_value = reading->value;
    state->last_quality_flags = reading->quality_flags;
    state->last_reported_at_ms = reading->uptime_ms;
}

void st_reporting_policy_init(st_reporting_policy_t *policy)
{
    if (policy == NULL) {
        return;
    }
    memset(policy, 0, sizeof(*policy));
    set_default_rules(policy);
}

int st_reporting_policy_set_rule(st_reporting_policy_t *policy, st_sensor_kind_t kind,
                                 st_reporting_rule_t rule)
{
    if (policy == NULL || (uint32_t)kind >= ST_SENSOR_KIND_COUNT || rule.deadband < 0.0F ||
        rule.maximum_interval_ms == 0U ||
        rule.minimum_interval_ms > rule.maximum_interval_ms) {
        return -1;
    }
    policy->rules[kind] = rule;
    return 0;
}

int st_reporting_policy_should_report(st_reporting_policy_t *policy,
                                      const st_telemetry_record_t *record)
{
    st_reporting_state_t *state;
    st_reporting_rule_t rule;
    uint64_t elapsed_ms;
    float difference;
    int changed;

    if (policy == NULL || record == NULL) {
        return 0;
    }

    ++policy->readings_seen;
    if (record->record_class == ST_RECORD_EVENT || record->record_class == ST_RECORD_HEALTH) {
        ++policy->readings_reported;
        return 1;
    }
    if ((uint32_t)record->reading.sensor_kind >= ST_SENSOR_KIND_COUNT) {
        ++policy->readings_suppressed;
        return 0;
    }

    state = find_state(policy, record->reading.sensor_id);
    if (state == NULL) {
        ++policy->readings_reported;
        return 1;
    }
    if (state->used == 0U) {
        remember_report(state, &record->reading);
        ++policy->readings_reported;
        return 1;
    }

    rule = policy->rules[record->reading.sensor_kind];
    elapsed_ms = record->reading.uptime_ms >= state->last_reported_at_ms
                     ? record->reading.uptime_ms - state->last_reported_at_ms
                     : rule.maximum_interval_ms;
    difference = record->reading.value - state->last_reported_value;
    if (difference < 0.0F) {
        difference = -difference;
    }
    changed = rule.deadband == 0.0F ? difference > 0.0F : difference >= rule.deadband;

    if (record->reading.quality_flags != state->last_quality_flags ||
        elapsed_ms >= rule.maximum_interval_ms ||
        (changed && elapsed_ms >= rule.minimum_interval_ms)) {
        remember_report(state, &record->reading);
        ++policy->readings_reported;
        return 1;
    }

    ++policy->readings_suppressed;
    return 0;
}
