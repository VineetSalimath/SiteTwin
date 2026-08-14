#include <stdio.h>
#include <string.h>

#include "sitetwin/alarm.h"
#include "sitetwin/command.h"
#include "sitetwin/control_event.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    st_command_persistent_state_t state;
    int available;
    int saves;
} command_store_t;

static int command_load(void *context, st_command_persistent_state_t *state)
{
    command_store_t *store = (command_store_t *)context;
    if (store == NULL || state == NULL || store->available == 0) {
        return -1;
    }
    *state = store->state;
    return 0;
}

static int command_save(void *context, const st_command_persistent_state_t *state)
{
    command_store_t *store = (command_store_t *)context;
    if (store == NULL || state == NULL) {
        return -1;
    }
    store->state = *state;
    store->available = 1;
    ++store->saves;
    return 0;
}

static st_sensor_reading_t co2_reading(float value, uint32_t sequence,
                                       uint64_t uptime_ms)
{
    st_sensor_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    strcpy(reading.pod_id, "POD_1");
    strcpy(reading.sensor_id, "scd41_co2");
    reading.sensor_kind = ST_SENSOR_CO2_PPM;
    reading.unit = ST_UNIT_PPM;
    reading.sequence = sequence;
    reading.boot_id = 1U;
    reading.uptime_ms = uptime_ms;
    reading.value = value;
    reading.quality_flags = ST_QUALITY_VALID;
    return reading;
}

static st_sensor_reading_t contact_reading(float value, uint32_t sequence,
                                           uint64_t uptime_ms)
{
    st_sensor_reading_t reading;
    memset(&reading, 0, sizeof(reading));
    strcpy(reading.pod_id, "POD_2");
    strcpy(reading.sensor_id, "reed_contact");
    reading.sensor_kind = ST_SENSOR_CONTACT;
    reading.unit = ST_UNIT_BOOLEAN;
    reading.sequence = sequence;
    reading.boot_id = 1U;
    reading.uptime_ms = uptime_ms;
    reading.value = value;
    reading.quality_flags = ST_QUALITY_VALID;
    return reading;
}

static void drain_alarm_events(st_alarm_runtime_t *runtime)
{
    st_control_event_t event;
    while (st_alarm_runtime_next_event(runtime, &event) == 0) {
    }
}

static void drain_command_events(st_command_runtime_t *runtime)
{
    st_control_event_t event;
    while (st_command_runtime_next_control_event(runtime, &event) == 0) {
    }
}

static int test_control_event_codec(void)
{
    st_control_event_t input;
    st_control_event_t output;
    uint8_t payload[ST_CONTROL_EVENT_WIRE_SIZE];
    char json[1024];
    size_t length;

    memset(&input, 0, sizeof(input));
    input.event_kind = ST_CONTROL_EVENT_ALARM_CONDITION;
    input.transition = ST_CONTROL_TRANSITION_ACTIVE;
    input.reason = ST_CONTROL_REASON_RULE_TRIGGERED;
    input.active = 1U;
    input.instance_id = 123456U;
    input.timestamp_ms = 500U;
    input.capability = ST_SENSOR_CO2_PPM;
    input.secondary_capability = ST_SENSOR_UNKNOWN;
    input.rule_kind = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
    input.threshold = 1000.0F;
    input.observed = 1200.0F;
    input.quality_flags = ST_QUALITY_VALID;
    strcpy(input.pod_id, "POD_1");
    strcpy(input.sensor_id, "scd41_co2");
    EXPECT(st_control_event_encode(&input, payload, sizeof(payload), &length) == 0);
    EXPECT(length == ST_CONTROL_EVENT_WIRE_SIZE);
    EXPECT(st_control_event_decode(payload, length, &output) == 0);
    EXPECT(output.instance_id == input.instance_id);
    EXPECT(output.observed == input.observed);
    EXPECT(st_control_event_to_json(&output, json, sizeof(json)) == 0);
    EXPECT(strstr(json, "\"event_kind\":\"alarm_condition\"") != NULL);
    EXPECT(strstr(json, "\"instance_id\":123456") != NULL);
    payload[4] = 2U;
    EXPECT(st_control_event_decode(payload, length, &output) != 0);
    payload[4] = 1U;
    payload[75] = 'X';
    EXPECT(st_control_event_decode(payload, length, &output) != 0);
    return 0;
}

static int test_condition_clear_retrigger_ack_and_silence(void)
{
    st_capability_config_t config;
    st_alarm_persistent_state_t persistent;
    st_alarm_runtime_t runtime;
    st_alarm_runtime_t restored;
    st_control_event_t event;
    st_sensor_reading_t reading;
    uint64_t first_instance;
    int saw_reboot_reset = 0;
    int saw_recovered_ack = 0;

    st_capability_config_init(&config, ST_POD_ENVIRONMENT);
    st_alarm_persistent_state_init(&persistent);
    EXPECT(st_alarm_runtime_init(&runtime, "POD_1", &config, &persistent,
                                 1UL << ST_SENSOR_CO2_PPM, 1U, 1U,
                                 NULL, NULL, 0U) == 0);
    drain_alarm_events(&runtime);

    reading = co2_reading(1200.0F, 1U, 10U);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 10U) == 1);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.transition == ST_CONTROL_TRANSITION_ACTIVE);
    first_instance = event.instance_id;
    EXPECT(first_instance != 0U);

    EXPECT(st_alarm_runtime_acknowledge(&runtime, first_instance, 20U) == 0);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.event_kind == ST_CONTROL_EVENT_ALARM_ACKNOWLEDGEMENT);
    EXPECT(event.acknowledged == 1U && event.active == 1U);

    EXPECT(st_alarm_runtime_silence(&runtime, first_instance, 100U, 30U) == 0);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.silenced == 1U && event.active == 1U);
    st_alarm_runtime_tick(&runtime, 129U);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) != 0);
    st_alarm_runtime_tick(&runtime, 130U);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.transition == ST_CONTROL_TRANSITION_SILENCE_EXPIRED);
    EXPECT(st_alarm_runtime_condition_active(&runtime, first_instance) == 1);

    reading = co2_reading(800.0F, 2U, 140U);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 140U) == 1);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.transition == ST_CONTROL_TRANSITION_CLEARED);
    EXPECT(event.instance_id == first_instance);

    reading = co2_reading(1300.0F, 3U, 150U);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 150U) == 1);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.transition == ST_CONTROL_TRANSITION_ACTIVE);
    EXPECT(event.instance_id != first_instance);
    EXPECT(st_alarm_runtime_acknowledge(&runtime, event.instance_id, 160U) == 0);
    drain_alarm_events(&runtime);

    EXPECT(st_alarm_runtime_init(&restored, "POD_1", &config, &persistent,
                                 1UL << ST_SENSOR_CO2_PPM, 2U, 1U,
                                 NULL, NULL, 0U) == 0);
    while (st_alarm_runtime_next_event(&restored, &event) == 0) {
        if (event.event_kind == ST_CONTROL_EVENT_ALARM_SILENCE &&
            event.transition == ST_CONTROL_TRANSITION_REBOOT_RESET &&
            event.silenced == 0U) {
            saw_reboot_reset = 1;
        }
        if (event.event_kind == ST_CONTROL_EVENT_ALARM_CONDITION &&
            event.transition == ST_CONTROL_TRANSITION_RECOVERED &&
            event.acknowledged == 1U) {
            saw_recovered_ack = 1;
        }
    }
    EXPECT(saw_reboot_reset == 1);
    EXPECT(saw_recovered_ack == 1);
    EXPECT(restored.silence_active == 0U);
    return 0;
}

static int test_state_alarm_debounce(void)
{
    st_capability_config_t config;
    st_capability_rule_t applied;
    st_alarm_persistent_state_t persistent;
    st_alarm_runtime_t runtime;
    st_control_event_t event;
    st_sensor_reading_t reading;

    st_capability_config_init(&config, ST_POD_ACTIVITY_ACCESS);
    EXPECT(st_capability_config_set(&config, ST_POD_ACTIVITY_ACCESS,
                                    ST_SENSOR_CONTACT,
                                    ST_CONFIG_RULE_STATE_ACTIVE_VALUE,
                                    1.0F, 1U, &applied) == ST_CONFIG_RESULT_OK);
    EXPECT(st_capability_config_set(&config, ST_POD_ACTIVITY_ACCESS,
                                    ST_SENSOR_CONTACT,
                                    ST_CONFIG_RULE_EVENT_DEBOUNCE_MS,
                                    50.0F, 1U, &applied) == ST_CONFIG_RESULT_OK);
    st_alarm_persistent_state_init(&persistent);
    EXPECT(st_alarm_runtime_init(&runtime, "POD_2", &config, &persistent,
                                 1UL << ST_SENSOR_CONTACT, 1U, 0U,
                                 NULL, NULL, 0U) == 0);
    drain_alarm_events(&runtime);

    reading = contact_reading(1.0F, 1U, 10U);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 10U) == 0);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 59U) == 0);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) != 0);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 60U) == 1);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.transition == ST_CONTROL_TRANSITION_ACTIVE);

    reading = contact_reading(0.0F, 2U, 70U);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 70U) == 0);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 119U) == 0);
    EXPECT(st_alarm_runtime_ingest(&runtime, &reading, 120U) == 1);
    EXPECT(st_alarm_runtime_next_event(&runtime, &event) == 0);
    EXPECT(event.transition == ST_CONTROL_TRANSITION_CLEARED);
    return 0;
}

static st_command_t alarm_command(uint64_t command_id,
                                  st_command_type_t type,
                                  uint64_t instance_id)
{
    st_command_t command;
    memset(&command, 0, sizeof(command));
    command.command_id = command_id;
    strcpy(command.target_pod_id, "POD_1");
    command.command_type = type;
    command.target = type == ST_COMMAND_GET_CAPABILITIES
                         ? ST_COMMAND_TARGET_CAPABILITIES
                         : ST_COMMAND_TARGET_ALARM;
    command.alarm_instance_id = instance_id;
    command.duration_ms = 1000U;
    command.issued_at_ms = 1U;
    command.expires_at_ms = 10001U;
    command.valid_for_ms = 10000U;
    command.source = ST_COMMAND_SOURCE_THINGSBOARD;
    return command;
}

static int test_command_capabilities_persistent_ack_and_unsupported_output(void)
{
    command_store_t store;
    st_command_persistence_t persistence = {&store, command_load, command_save};
    st_command_runtime_t runtime;
    st_command_runtime_t restored;
    st_sensor_reading_t reading;
    st_control_event_t event;
    st_command_t command;
    st_command_ack_t ack;
    uint64_t instance_id = 0U;
    int saw_recovered_ack = 0;

    memset(&store, 0, sizeof(store));
    EXPECT(st_command_runtime_init_with_boot(&runtime, ST_POD_ENVIRONMENT,
                                             "POD_1", persistence, 1U, 0U) == 0);
    drain_command_events(&runtime);
    reading = co2_reading(1200.0F, 1U, 10U);
    EXPECT(st_command_runtime_ingest_reading(&runtime, &reading, 10U) == 1);
    EXPECT(st_command_runtime_next_control_event(&runtime, &event) == 0);
    instance_id = event.instance_id;
    EXPECT(instance_id != 0U && store.saves == 1);

    command = alarm_command(100U, ST_COMMAND_ACK_ALARM, instance_id);
    EXPECT(st_command_runtime_handle(&runtime, &command, 20U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(ack.alarm_instance_id == instance_id);
    EXPECT(st_command_runtime_next_control_event(&runtime, &event) == 0);
    EXPECT(event.acknowledged == 1U);

    command = alarm_command(101U, ST_COMMAND_GET_CAPABILITIES, 0U);
    EXPECT(st_command_runtime_handle(&runtime, &command, 30U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT((ack.capability_mask & (1UL << ST_SENSOR_CO2_PPM)) != 0U);
    EXPECT(ack.ruleset_revision >= 1U);

    command = alarm_command(102U, ST_COMMAND_SILENCE_ALARM, instance_id);
    EXPECT(st_command_runtime_handle(&runtime, &command, 40U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_UNSUPPORTED);
    EXPECT(runtime.alarm.silence_active == 0U);

    command = alarm_command(103U, ST_COMMAND_TEST_OUTPUT, instance_id);
    EXPECT(st_command_runtime_handle(&runtime, &command, 50U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_UNSUPPORTED);

    command = alarm_command(104U, ST_COMMAND_ACK_ALARM, instance_id + 1U);
    EXPECT(st_command_runtime_handle(&runtime, &command, 60U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_NOT_ACTIVE);

    EXPECT(st_command_runtime_init_with_boot(&restored, ST_POD_ENVIRONMENT,
                                             "POD_1", persistence, 2U, 0U) == 0);
    while (st_command_runtime_next_control_event(&restored, &event) == 0) {
        if (event.event_kind == ST_CONTROL_EVENT_ALARM_CONDITION &&
            event.transition == ST_CONTROL_TRANSITION_RECOVERED &&
            event.instance_id == instance_id && event.acknowledged == 1U) {
            saw_recovered_ack = 1;
        }
    }
    EXPECT(saw_recovered_ack == 1);
    return 0;
}

int st_run_alarm_control_tests(void)
{
    int failures = 0;
    failures += test_control_event_codec();
    failures += test_condition_clear_retrigger_ack_and_silence();
    failures += test_state_alarm_debounce();
    failures += test_command_capabilities_persistent_ack_and_unsupported_output();
    return failures;
}
