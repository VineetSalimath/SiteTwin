#ifndef SITETWIN_CAPABILITY_CONFIG_H
#define SITETWIN_CAPABILITY_CONFIG_H

#include <stdint.h>

#include "sitetwin/contracts.h"
#include "sitetwin/reporting_policy.h"

#define ST_CAPABILITY_CONFIG_SCHEMA_VERSION 1U
#define ST_CAPABILITY_RULE_CAPACITY 24U
#define ST_CAPABILITY_DEFAULT_CO2_THRESHOLD_PPM 1000.0F

typedef enum {
    ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD = 1,
    ST_CONFIG_RULE_NUMERIC_LOW_THRESHOLD,
    ST_CONFIG_RULE_REPORT_DEADBAND,
    ST_CONFIG_RULE_REPORT_MIN_INTERVAL_MS,
    ST_CONFIG_RULE_REPORT_MAX_INTERVAL_MS,
    ST_CONFIG_RULE_EVENT_DEBOUNCE_MS,
    ST_CONFIG_RULE_EVENT_RETRIGGER_MS,
    ST_CONFIG_RULE_STATE_ACTIVE_VALUE
} st_config_rule_kind_t;

typedef enum {
    ST_CONFIG_RESULT_OK = 0,
    ST_CONFIG_RESULT_INVALID,
    ST_CONFIG_RESULT_UNSUPPORTED,
    ST_CONFIG_RESULT_OUT_OF_BOUNDS,
    ST_CONFIG_RESULT_REVISION_CONFLICT,
    ST_CONFIG_RESULT_FULL,
    ST_CONFIG_RESULT_NOT_FOUND
} st_config_result_t;

typedef struct {
    st_sensor_kind_t capability;
    st_config_rule_kind_t rule_kind;
    float value;
    uint32_t revision;
    uint8_t used;
    uint8_t reserved[3];
} st_capability_rule_t;

typedef struct {
    uint8_t schema_version;
    uint8_t rule_count;
    uint8_t reserved[2];
    st_capability_rule_t rules[ST_CAPABILITY_RULE_CAPACITY];
} st_capability_config_t;

void st_capability_config_init(st_capability_config_t *config, st_pod_profile_t profile);
int st_profile_has_capability(st_pod_profile_t profile, st_sensor_kind_t capability);
st_config_result_t st_capability_config_validate(st_pod_profile_t profile,
                                                 st_sensor_kind_t capability,
                                                 st_config_rule_kind_t rule_kind,
                                                 float value);
st_config_result_t st_capability_config_set(st_capability_config_t *config,
                                            st_pod_profile_t profile,
                                            st_sensor_kind_t capability,
                                            st_config_rule_kind_t rule_kind,
                                            float value,
                                            uint32_t next_revision,
                                            st_capability_rule_t *applied_rule);
st_config_result_t st_capability_config_get(const st_capability_config_t *config,
                                            st_sensor_kind_t capability,
                                            st_config_rule_kind_t rule_kind,
                                            st_capability_rule_t *rule);
int st_capability_config_apply_reporting(const st_capability_config_t *config,
                                         st_reporting_policy_t *policy);
const char *st_config_rule_kind_name(st_config_rule_kind_t rule_kind);

#endif
