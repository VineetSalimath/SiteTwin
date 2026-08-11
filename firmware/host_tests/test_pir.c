#include <stdio.h>
#include <string.h>

#include "sitetwin/pir.h"
#include "sitetwin/pod_runtime.h"

#define EXPECT(condition)                                                                      \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            fprintf(stderr, "PIR expectation failed: %s (%s:%d)\n", #condition, __FILE__,    \
                    __LINE__);                                                                 \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static int test_pir_stabilization_edges_and_retrigger(void)
{
    const st_pir_config_t config = {
        .stabilization_ms = ST_PIR_DEFAULT_STABILIZATION_MS,
        .retrigger_suppression_ms = 1000U,
        .active_level = 1U,
        .sensor_id = "pir_motion",
    };
    st_pir_t sensor;
    st_pir_event_t event;

    EXPECT(st_pir_init(&sensor, &config, 100U) == 0);
    memset(&event, 0, sizeof(event));
    EXPECT(st_pir_process_level(&sensor, 100U, 1U, &event) == 0);
    EXPECT(st_pir_process_level(&sensor, 5099U, 0U, &event) == 0);
    EXPECT(st_pir_is_stabilized(&sensor) == 0U);
    EXPECT(st_pir_process_level(&sensor, 5100U, 0U, &event) == 0);
    EXPECT(st_pir_is_stabilized(&sensor) == 1U);

    EXPECT(st_pir_process_level(&sensor, 5200U, 1U, &event) == 1);
    EXPECT(event.detected_at_ms == 5200U);
    EXPECT(event.event_count == 1U);
    EXPECT(st_pir_process_level(&sensor, 5300U, 1U, &event) == 0);
    EXPECT(st_pir_process_level(&sensor, 5400U, 0U, &event) == 0);
    EXPECT(st_pir_process_level(&sensor, 5500U, 1U, &event) == 0);
    EXPECT(st_pir_process_level(&sensor, 5600U, 0U, &event) == 0);
    EXPECT(st_pir_process_level(&sensor, 6200U, 1U, &event) == 1);
    EXPECT(event.event_count == 2U);
    EXPECT(st_pir_event_count(&sensor) == 2U);
    EXPECT(st_pir_last_event_at_ms(&sensor) == 6200U);
    return 0;
}

static int test_pir_active_low_and_runtime_event(void)
{
    const st_pir_config_t config = {
        .stabilization_ms = 0U,
        .retrigger_suppression_ms = 0U,
        .active_level = 0U,
        .sensor_id = "pir_motion",
    };
    st_pir_t sensor;
    st_pir_event_t event;
    st_pod_runtime_t runtime;
    st_telemetry_record_t record;

    EXPECT(st_pir_init(&sensor, &config, 0U) == 0);
    EXPECT(st_pir_process_level(&sensor, 0U, 1U, &event) == 0);
    EXPECT(st_pir_process_level(&sensor, 1U, 0U, &event) == 1);
    st_pod_runtime_init(&runtime, ST_POD_ACTIVITY_ACCESS, "ACT_01", 7U);
    EXPECT(st_pod_runtime_emit_event(&runtime, config.sensor_id, ST_SENSOR_MOTION,
                                     event.detected_at_ms, 1.0F) == 0);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT(record.record_class == ST_RECORD_EVENT);
    EXPECT(record.priority == ST_PRIORITY_EVENT);
    EXPECT(record.reading.sensor_kind == ST_SENSOR_MOTION);
    EXPECT(record.reading.value == 1.0F);
    EXPECT(record.reading.uptime_ms == 1U);
    return 0;
}

static int test_pir_validation(void)
{
    st_pir_t sensor;
    st_pir_config_t config = {
        .stabilization_ms = 5000U,
        .active_level = 2U,
        .sensor_id = "pir_motion",
    };
    EXPECT(st_pir_init(&sensor, &config, 0U) != 0);
    config.active_level = 1U;
    config.sensor_id = "";
    EXPECT(st_pir_init(&sensor, &config, 0U) != 0);
    return 0;
}

int st_run_pir_tests(void)
{
    int failures = 0;
    failures += test_pir_stabilization_edges_and_retrigger();
    failures += test_pir_active_low_and_runtime_event();
    failures += test_pir_validation();
    return failures;
}
