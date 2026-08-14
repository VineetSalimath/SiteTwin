#include "sitetwin/capability_config.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define ST_CONFIG_MAX_NUMERIC_MAGNITUDE 1000000.0F
#define ST_CONFIG_MAX_INTERVAL_MS 86400000U
#define ST_CONFIG_MAX_EVENT_DEBOUNCE_MS 60000U
#define ST_CONFIG_MAX_EVENT_RETRIGGER_MS 3600000U

static int capability_is_numeric(st_sensor_kind_t capability)
{
    return (capability >= ST_SENSOR_TEMPERATURE_C &&
            capability <= ST_SENSOR_ILLUMINANCE_LUX) ||
           (capability >= ST_SENSOR_CURRENT_MA &&
            capability <= ST_SENSOR_VIBRATION_RMS_G);
}

static int capability_is_state_or_event(st_sensor_kind_t capability)
{
    return capability == ST_SENSOR_MOTION || capability == ST_SENSOR_CONTACT;
}

static int value_is_integer(float value)
{
    return isfinite(value) && floorf(value) == value;
}

int st_profile_has_capability(st_pod_profile_t profile, st_sensor_kind_t capability)
{
    switch (profile) {
    case ST_POD_ENVIRONMENT:
        return capability == ST_SENSOR_TEMPERATURE_C ||
               capability == ST_SENSOR_RELATIVE_HUMIDITY_PERCENT ||
               capability == ST_SENSOR_CO2_PPM || capability == ST_SENSOR_VOC_INDEX;
    case ST_POD_ACTIVITY_ACCESS:
        return capability == ST_SENSOR_ILLUMINANCE_LUX ||
               capability == ST_SENSOR_MOTION || capability == ST_SENSOR_CONTACT;
    case ST_POD_EQUIPMENT:
        return capability == ST_SENSOR_TEMPERATURE_C ||
               capability == ST_SENSOR_CURRENT_MA || capability == ST_SENSOR_VOLTAGE_V ||
               capability == ST_SENSOR_VIBRATION_RMS_G;
    default:
        return 0;
    }
}

static st_capability_rule_t *find_rule(st_capability_config_t *config,
                                       st_sensor_kind_t capability,
                                       st_config_rule_kind_t rule_kind)
{
    size_t index;

    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        st_capability_rule_t *rule = &config->rules[index];
        if (rule->used != 0U && rule->capability == capability &&
            rule->rule_kind == rule_kind) {
            return rule;
        }
    }
    return NULL;
}

static const st_capability_rule_t *find_const_rule(const st_capability_config_t *config,
                                                    st_sensor_kind_t capability,
                                                    st_config_rule_kind_t rule_kind)
{
    size_t index;

    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        const st_capability_rule_t *rule = &config->rules[index];
        if (rule->used != 0U && rule->capability == capability &&
            rule->rule_kind == rule_kind) {
            return rule;
        }
    }
    return NULL;
}

static st_capability_rule_t *find_free_rule(st_capability_config_t *config)
{
    size_t index;

    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        if (config->rules[index].used == 0U) {
            return &config->rules[index];
        }
    }
    return NULL;
}

static void install_rule(st_capability_config_t *config, st_sensor_kind_t capability,
                         st_config_rule_kind_t rule_kind, float value, uint32_t revision)
{
    st_capability_rule_t *rule = find_rule(config, capability, rule_kind);

    if (rule == NULL) {
        rule = find_free_rule(config);
        if (rule == NULL) {
            return;
        }
        memset(rule, 0, sizeof(*rule));
        rule->used = 1U;
        rule->capability = capability;
        rule->rule_kind = rule_kind;
        ++config->rule_count;
    }
    rule->value = value;
    rule->revision = revision;
}

void st_capability_config_init(st_capability_config_t *config, st_pod_profile_t profile)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->schema_version = ST_CAPABILITY_CONFIG_SCHEMA_VERSION;
    if (profile == ST_POD_ENVIRONMENT) {
        install_rule(config, ST_SENSOR_CO2_PPM, ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD,
                     ST_CAPABILITY_DEFAULT_CO2_THRESHOLD_PPM, 1U);
    }
}

st_config_result_t st_capability_config_validate(st_pod_profile_t profile,
                                                 st_sensor_kind_t capability,
                                                 st_config_rule_kind_t rule_kind,
                                                 float value)
{
    if ((uint32_t)capability >= ST_SENSOR_KIND_COUNT ||
        rule_kind < ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD ||
        rule_kind > ST_CONFIG_RULE_STATE_ACTIVE_VALUE || !isfinite(value)) {
        return ST_CONFIG_RESULT_INVALID;
    }
    if (!st_profile_has_capability(profile, capability)) {
        return ST_CONFIG_RESULT_UNSUPPORTED;
    }
    if (rule_kind == ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD ||
        rule_kind == ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD) {
        if (!capability_is_numeric(capability)) {
            return ST_CONFIG_RESULT_UNSUPPORTED;
        }
        if (capability == ST_SENSOR_CO2_PPM &&
            (value < 400.0F || value > 5000.0F)) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        if (capability == ST_SENSOR_RELATIVE_HUMIDITY_PERCENT &&
            (value < 0.0F || value > 100.0F)) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        if (fabsf(value) > ST_CONFIG_MAX_NUMERIC_MAGNITUDE) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        return ST_CONFIG_RESULT_OK;
    }
    if (rule_kind == ST_CONFIG_RULE_REPORT_DEADBAND) {
        return capability_is_numeric(capability) && value >= 0.0F &&
                       value <= ST_CONFIG_MAX_NUMERIC_MAGNITUDE
                   ? ST_CONFIG_RESULT_OK
                   : ST_CONFIG_RESULT_OUT_OF_BOUNDS;
    }
    if (rule_kind == ST_CONFIG_RULE_REPORT_MIN_INTERVAL_MS ||
        rule_kind == ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS) {
        if (!capability_is_numeric(capability) || !value_is_integer(value) ||
            value < 0.0F || value > (float)ST_CONFIG_MAX_INTERVAL_MS ||
            (rule_kind == ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS && value == 0.0F)) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        return ST_CONFIG_RESULT_OK;
    }
    if (rule_kind == ST_CONFIG_RULE_EVENT_DEBOUNCE_MS) {
        if (!capability_is_state_or_event(capability) || !value_is_integer(value) ||
            value < 0.0F || value > (float)ST_CONFIG_MAX_EVENT_DEBOUNCE_MS) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        return ST_CONFIG_RESULT_OK;
    }
    if (rule_kind == ST_CONFIG_RULE_EVENT_RETRIGGER_MS) {
        if (capability != ST_SENSOR_MOTION || !value_is_integer(value) || value < 0.0F ||
            value > (float)ST_CONFIG_MAX_EVENT_RETRIGGER_MS) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        return ST_CONFIG_RESULT_OK;
    }
    if (rule_kind == ST_CONFIG_RULE_STATE_ACTIVE_VALUE) {
        if (!capability_is_state_or_event(capability) ||
            (value != 0.0F && value != 1.0F)) {
            return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
        }
        return ST_CONFIG_RESULT_OK;
    }
    return ST_CONFIG_RESULT_INVALID;
}

static int thresholds_are_ordered(const st_capability_config_t *config,
                                  st_sensor_kind_t capability,
                                  st_config_rule_kind_t rule_kind, float value)
{
    const st_capability_rule_t *other;

    if (rule_kind == ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD) {
        other = find_const_rule(config, capability, ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD);
        return other == NULL || value > other->value;
    }
    if (rule_kind == ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD) {
        other = find_const_rule(config, capability, ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD);
        return other == NULL || value < other->value;
    }
    return 1;
}

static int reporting_intervals_are_ordered(const st_capability_config_t *config,
                                           st_sensor_kind_t capability,
                                           st_config_rule_kind_t rule_kind, float value)
{
    const st_capability_rule_t *other;

    if (rule_kind == ST_CONFIG_RULE_REPORT_MIN_INTERVAL_MS) {
        other = find_const_rule(config, capability, ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS);
        return other == NULL || value <= other->value;
    }
    if (rule_kind == ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS) {
        other = find_const_rule(config, capability, ST_CONFIG_RULE_REPORT_MIN_INTERVAL_MS);
        return other == NULL || other->value <= value;
    }
    return 1;
}

st_config_result_t st_capability_config_set(st_capability_config_t *config,
                                            st_pod_profile_t profile,
                                            st_sensor_kind_t capability,
                                            st_config_rule_kind_t rule_kind,
                                            float value,
                                            uint32_t next_revision,
                                            st_capability_rule_t *applied_rule)
{
    st_config_result_t result;
    st_capability_rule_t *rule;
    uint32_t expected_revision;

    if (config == NULL || config->schema_version != ST_CAPABILITY_CONFIG_SCHEMA_VERSION) {
        return ST_CONFIG_RESULT_INVALID;
    }
    result = st_capability_config_validate(profile, capability, rule_kind, value);
    if (result != ST_CONFIG_RESULT_OK) {
        return result;
    }
    if (!thresholds_are_ordered(config, capability, rule_kind, value) ||
        !reporting_intervals_are_ordered(config, capability, rule_kind, value)) {
        return ST_CONFIG_RESULT_OUT_OF_BOUNDS;
    }
    rule = find_rule(config, capability, rule_kind);
    expected_revision = rule == NULL ? 1U : rule->revision + 1U;
    if (next_revision != expected_revision) {
        return ST_CONFIG_RESULT_REVISION_CONFLICT;
    }
    if (rule == NULL) {
        rule = find_free_rule(config);
        if (rule == NULL) {
            return ST_CONFIG_RESULT_FULL;
        }
        memset(rule, 0, sizeof(*rule));
        rule->used = 1U;
        rule->capability = capability;
        rule->rule_kind = rule_kind;
        ++config->rule_count;
    }
    rule->value = value;
    rule->revision = next_revision;
    if (applied_rule != NULL) {
        *applied_rule = *rule;
    }
    return ST_CONFIG_RESULT_OK;
}

st_config_result_t st_capability_config_get(const st_capability_config_t *config,
                                            st_sensor_kind_t capability,
                                            st_config_rule_kind_t rule_kind,
                                            st_capability_rule_t *rule)
{
    const st_capability_rule_t *found;

    if (config == NULL || rule == NULL ||
        config->schema_version != ST_CAPABILITY_CONFIG_SCHEMA_VERSION) {
        return ST_CONFIG_RESULT_INVALID;
    }
    found = find_const_rule(config, capability, rule_kind);
    if (found == NULL) {
        return ST_CONFIG_RESULT_NOT_FOUND;
    }
    *rule = *found;
    return ST_CONFIG_RESULT_OK;
}

int st_capability_config_apply_reporting(const st_capability_config_t *config,
                                         st_reporting_policy_t *policy)
{
    st_reporting_rule_t rules[ST_SENSOR_KIND_COUNT];
    uint8_t touched[ST_SENSOR_KIND_COUNT] = {0};
    size_t index;

    if (config == NULL || policy == NULL ||
        config->schema_version != ST_CAPABILITY_CONFIG_SCHEMA_VERSION) {
        return -1;
    }
    memcpy(rules, policy->rules, sizeof(rules));
    for (index = 0U; index < ST_CAPABILITY_RULE_CAPACITY; ++index) {
        const st_capability_rule_t *configured = &config->rules[index];
        st_reporting_rule_t *reporting;

        if (configured->used == 0U ||
            (uint32_t)configured->capability >= ST_SENSOR_KIND_COUNT) {
            continue;
        }
        reporting = &rules[configured->capability];
        if (configured->rule_kind == ST_CONFIG_RULE_REPORT_DEADBAND) {
            reporting->deadband = configured->value;
            touched[configured->capability] = 1U;
        } else if (configured->rule_kind == ST_CONFIG_RULE_REPORT_MIN_INTERVAL_MS) {
            reporting->minimum_interval_ms = (uint32_t)configured->value;
            touched[configured->capability] = 1U;
        } else if (configured->rule_kind == ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS) {
            reporting->maximum_interval_ms = (uint32_t)configured->value;
            touched[configured->capability] = 1U;
        }
    }
    for (index = 0U; index < ST_SENSOR_KIND_COUNT; ++index) {
        if (touched[index] != 0U &&
            st_reporting_policy_set_rule(policy, (st_sensor_kind_t)index, rules[index]) != 0) {
            return -1;
        }
    }
    return 0;
}

const char *st_config_rule_kind_name(st_config_rule_kind_t rule_kind)
{
    switch (rule_kind) {
    case ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD: return "numeric_high_threshold";
    case ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD: return "numeric_low_threshold";
    case ST_CONFIG_RULE_REPORT_DEADBAND: return "report_deadband";
    case ST_CONFIG_RULE_REPORT_MIN_INTERVAL_MS: return "report_min_interval_ms";
    case ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS: return "report_max_interval_ms";
    case ST_CONFIG_RULE_EVENT_DEBOUNCE_MS: return "event_debounce_ms";
    case ST_CONFIG_RULE_EVENT_RETRIGGER_MS: return "event_retrigger_ms";
    case ST_CONFIG_RULE_STATE_ACTIVE_VALUE: return "state_active_value";
    default: return "unknown";
    }
}
