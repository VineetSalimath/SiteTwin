#ifndef SITETWIN_REPORTING_POLICY_H
#define SITETWIN_REPORTING_POLICY_H

#include <stdint.h>

#include "sitetwin/contracts.h"

#define ST_REPORTING_STATE_CAPACITY 8U
#define ST_SENSOR_KIND_COUNT ((uint32_t)ST_SENSOR_UNKNOWN + 1U)

typedef struct {
    float deadband;
    uint32_t minimum_interval_ms;
    uint32_t maximum_interval_ms;
} st_reporting_rule_t;

typedef struct {
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    float last_reported_value;
    uint32_t last_quality_flags;
    uint64_t last_reported_at_ms;
    uint8_t used;
} st_reporting_state_t;

typedef struct {
    st_reporting_rule_t rules[ST_SENSOR_KIND_COUNT];
    st_reporting_state_t states[ST_REPORTING_STATE_CAPACITY];
    uint32_t readings_seen;
    uint32_t readings_reported;
    uint32_t readings_suppressed;
} st_reporting_policy_t;

void st_reporting_policy_init(st_reporting_policy_t *policy);
int st_reporting_policy_set_rule(st_reporting_policy_t *policy, st_sensor_kind_t kind,
                                 st_reporting_rule_t rule);
int st_reporting_policy_should_report(st_reporting_policy_t *policy,
                                      const st_telemetry_record_t *record);

#endif
