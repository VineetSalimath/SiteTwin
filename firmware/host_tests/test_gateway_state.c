#include <stdio.h>
#include <string.h>

#include "sitetwin/gateway_state.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    st_gateway_state_persistent_t state;
    int available;
    int saves;
} gateway_store_t;

static int gateway_load(void *context, st_gateway_state_persistent_t *state)
{
    gateway_store_t *store = (gateway_store_t *)context;
    if (store == NULL || state == NULL || store->available == 0) {
        return -1;
    }
    *state = store->state;
    return 0;
}

static int gateway_save(void *context, const st_gateway_state_persistent_t *state)
{
    gateway_store_t *store = (gateway_store_t *)context;
    if (store == NULL || state == NULL) {
        return -1;
    }
    store->state = *state;
    store->available = 1;
    ++store->saves;
    return 0;
}

static st_telemetry_record_t record_for(const char *pod_id, const char *sensor_id,
                                        st_sensor_kind_t capability, float value)
{
    st_telemetry_record_t record;
    memset(&record, 0, sizeof(record));
    record.record_class = ST_RECORD_STATE;
    record.priority = ST_PRIORITY_ROUTINE;
    strcpy(record.reading.pod_id, pod_id);
    strcpy(record.reading.sensor_id, sensor_id);
    record.reading.sensor_kind = capability;
    record.reading.value = value;
    record.reading.quality_flags = ST_QUALITY_VALID;
    record.reading.sequence = 1U;
    record.reading.boot_id = 1U;
    return record;
}

static st_control_event_t alarm_event(const char *pod_id, uint64_t instance_id,
                                      st_sensor_kind_t capability, int active,
                                      float observed)
{
    st_control_event_t event;
    memset(&event, 0, sizeof(event));
    event.event_kind = ST_CONTROL_EVENT_ALARM_CONDITION;
    event.transition = active ? ST_CONTROL_TRANSITION_ACTIVE
                              : ST_CONTROL_TRANSITION_CLEARED;
    event.reason = active ? ST_CONTROL_REASON_RULE_TRIGGERED
                          : ST_CONTROL_REASON_RULE_CLEARED;
    event.active = active ? 1U : 0U;
    event.instance_id = instance_id;
    event.capability = capability;
    event.secondary_capability = ST_SENSOR_UNKNOWN;
    event.rule_kind = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
    event.observed = observed;
    event.threshold = observed - 1.0F;
    strcpy(event.pod_id, pod_id);
    strcpy(event.sensor_id, "alarm_evidence");
    return event;
}

static int test_stale_clear_and_multi_pod(void)
{
    st_gateway_state_t state;
    st_gateway_state_persistence_t persistence = {0};
    st_telemetry_record_t first = record_for("POD_1", "scd41_co2",
                                             ST_SENSOR_CO2_PPM, 700.0F);
    st_telemetry_record_t second = record_for("POD_2", "bh1750_illuminance",
                                              ST_SENSOR_ILLUMINANCE_LUX, 300.0F);
    st_control_event_t event;
    int stale_active = 0;
    int stale_cleared = 0;

    EXPECT(st_gateway_state_init(&state, persistence, 0U) == 0);
    EXPECT(st_gateway_state_register_pod(&state, "POD_3", 100U) == 0);
    EXPECT(st_gateway_state_ingest_telemetry(&state, &first, 100U) == 0);
    EXPECT(st_gateway_state_ingest_telemetry(&state, &second, 100U) == 0);
    st_gateway_state_tick(&state, 90100U);
    st_gateway_state_tick(&state, 95100U);
    while (st_gateway_state_next_event(&state, &event) == 0) {
        if (event.event_kind == ST_CONTROL_EVENT_GATEWAY_INCIDENT &&
            strcmp(event.sensor_id, "gateway_pod_stale") == 0 &&
            event.transition == ST_CONTROL_TRANSITION_ACTIVE) {
            ++stale_active;
        }
    }
    EXPECT(stale_active == 3);

    EXPECT(st_gateway_state_ingest_telemetry(&state, &first, 96000U) == 0);
    st_gateway_state_tick(&state, 96000U);
    st_gateway_state_tick(&state, 101000U);
    while (st_gateway_state_next_event(&state, &event) == 0) {
        if (strcmp(event.pod_id, "POD_1") == 0 &&
            strcmp(event.sensor_id, "gateway_pod_stale") == 0 &&
            event.transition == ST_CONTROL_TRANSITION_CLEARED) {
            stale_cleared = 1;
        }
    }
    EXPECT(stale_cleared == 1);
    return 0;
}

static int test_multi_sensor_clear_trend_and_restart(void)
{
    gateway_store_t store;
    st_gateway_state_persistence_t persistence = {&store, gateway_load, gateway_save};
    st_gateway_state_t state;
    st_gateway_state_t restored;
    st_telemetry_record_t current;
    st_telemetry_record_t vibration;
    st_control_event_t current_alarm;
    st_control_event_t vibration_alarm;
    st_control_event_t event;
    uint64_t incident_id = 0U;
    int saw_clear = 0;
    int saw_recovery = 0;
    unsigned int sample;

    memset(&store, 0, sizeof(store));
    EXPECT(st_gateway_state_init(&state, persistence, 0U) == 0);
    for (sample = 0U; sample < ST_GATEWAY_STATE_TREND_WINDOW; ++sample) {
        current = record_for("POD_3", "ina219_current",
                             ST_SENSOR_CURRENT_MA, 10.0F + sample);
        vibration = record_for("POD_3", "adxl345_vibration",
                               ST_SENSOR_VIBRATION_RMS_G,
                               0.01F + 0.01F * sample);
        EXPECT(st_gateway_state_ingest_telemetry(&state, &current,
                                                 100U + sample) == 0);
        EXPECT(st_gateway_state_ingest_telemetry(&state, &vibration,
                                                 100U + sample) == 0);
    }
    current_alarm = alarm_event("POD_3", 301U, ST_SENSOR_CURRENT_MA, 1, 17.0F);
    vibration_alarm = alarm_event("POD_3", 302U,
                                  ST_SENSOR_VIBRATION_RMS_G, 1, 0.08F);
    EXPECT(st_gateway_state_ingest_control(&state, &current_alarm, 1000U) == 0);
    EXPECT(st_gateway_state_ingest_control(&state, &vibration_alarm, 1000U) == 0);
    st_gateway_state_tick(&state, 1000U);
    st_gateway_state_tick(&state, 6000U);
    while (st_gateway_state_next_event(&state, &event) == 0) {
        if (strcmp(event.sensor_id, "gateway_multi_sensor") == 0 &&
            event.transition == ST_CONTROL_TRANSITION_ACTIVE) {
            incident_id = event.instance_id;
            EXPECT(event.evidence_count == 2U);
            EXPECT(event.trend > 0.0F);
        }
    }
    EXPECT(incident_id != 0U);
    EXPECT(store.saves > 0);

    EXPECT(st_gateway_state_init(&restored, persistence, 0U) == 0);
    while (st_gateway_state_next_event(&restored, &event) == 0) {
        if (event.instance_id == incident_id &&
            event.transition == ST_CONTROL_TRANSITION_RECOVERED &&
            event.reason == ST_CONTROL_REASON_COORDINATOR_RESTART) {
            saw_recovery = 1;
        }
    }
    EXPECT(saw_recovery == 1);

    current_alarm.active = 0U;
    current_alarm.transition = ST_CONTROL_TRANSITION_CLEARED;
    EXPECT(st_gateway_state_ingest_control(&restored, &current_alarm, 7000U) == 0);
    st_gateway_state_tick(&restored, 7000U);
    st_gateway_state_tick(&restored, 12000U);
    while (st_gateway_state_next_event(&restored, &event) == 0) {
        if (strcmp(event.sensor_id, "gateway_multi_sensor") == 0 &&
            event.transition == ST_CONTROL_TRANSITION_CLEARED) {
            saw_clear = 1;
        }
    }
    EXPECT(saw_clear == 1);
    return 0;
}

int st_run_gateway_state_tests(void)
{
    int failures = 0;
    failures += test_stale_clear_and_multi_pod();
    failures += test_multi_sensor_clear_trend_and_restart();
    return failures;
}
