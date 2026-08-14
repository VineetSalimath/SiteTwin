#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sitetwin/command.h"
#include "sitetwin/gateway_frame.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    st_command_persistent_state_t stored;
    int has_state;
    int saves;
} fake_persistence_t;

static int fake_load(void *context, st_command_persistent_state_t *state)
{
    fake_persistence_t *fake = (fake_persistence_t *)context;
    if (fake == NULL || state == NULL || fake->has_state == 0) {
        return -1;
    }
    *state = fake->stored;
    return 0;
}

static int fake_save(void *context, const st_command_persistent_state_t *state)
{
    fake_persistence_t *fake = (fake_persistence_t *)context;
    if (fake == NULL || state == NULL) {
        return -1;
    }
    fake->stored = *state;
    fake->has_state = 1;
    ++fake->saves;
    return 0;
}

static st_command_t make_command(uint64_t id, st_command_type_t type,
                                 st_command_target_t target)
{
    st_command_t command;
    memset(&command, 0, sizeof(command));
    command.command_id = id;
    strcpy(command.target_pod_id, "POD_1");
    command.command_type = type;
    command.target = target;
    command.issued_at_ms = 1000U;
    command.expires_at_ms = 11000U;
    command.valid_for_ms = 10000U;
    command.source = ST_COMMAND_SOURCE_THINGSBOARD;
    return command;
}

static int test_v2_and_legacy_codecs(void)
{
    st_command_t input = make_command(101U, ST_COMMAND_SET_RULE,
                                      ST_COMMAND_TARGET_CAPABILITY_RULE);
    st_command_t output;
    uint8_t payload[ST_COMMAND_WIRE_SIZE];
    size_t length = 0U;

    input.capability = ST_SENSOR_VOC_INDEX;
    input.rule_kind = ST_CONFIG_RULE_REPORT_DEADBAND;
    input.value = 7.0F;
    input.config_revision = 1U;
    EXPECT(st_command_encode(&input, payload, sizeof(payload), &length) == 0);
    EXPECT(payload[0] == ST_COMMAND_CONTRACT_VERSION);
    EXPECT(st_command_decode(payload, length, &output) == 0);
    EXPECT(output.capability == ST_SENSOR_VOC_INDEX);
    EXPECT(output.rule_kind == ST_CONFIG_RULE_REPORT_DEADBAND);

    payload[0] = ST_COMMAND_LEGACY_CONTRACT_VERSION;
    payload[1] = ST_COMMAND_SET_THRESHOLD;
    payload[2] = ST_COMMAND_TARGET_CO2_THRESHOLD;
    payload[60] = 0U;
    payload[61] = 0U;
    EXPECT(st_command_decode(payload, length, &output) == 0);
    EXPECT(output.capability == ST_SENSOR_CO2_PPM);
    EXPECT(output.rule_kind == ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD);
    return 0;
}

static int test_capability_rules_and_legacy_threshold(void)
{
    st_command_runtime_t runtime;
    st_command_ack_t ack;
    st_capability_rule_t rule;
    st_command_t command;
    st_reporting_policy_t reporting;

    EXPECT(st_command_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1",
                                   (st_command_persistence_t){0}) == 0);
    EXPECT(st_command_runtime_get_rule(&runtime, ST_SENSOR_CO2_PPM,
                                       ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD,
                                       &rule) == 0);
    EXPECT(rule.value == 1000.0F && rule.revision == 1U);

    command = make_command(1U, ST_COMMAND_SET_THRESHOLD,
                           ST_COMMAND_TARGET_CO2_THRESHOLD);
    command.value = 1200.0F;
    command.config_revision = 2U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(ack.applied_config_revision == 2U);
    EXPECT(runtime.persistent.config.co2_threshold_ppm == 1200.0F);

    command = make_command(2U, ST_COMMAND_SET_RULE,
                           ST_COMMAND_TARGET_CAPABILITY_RULE);
    command.capability = ST_SENSOR_VOC_INDEX;
    command.rule_kind = ST_CONFIG_RULE_REPORT_DEADBAND;
    command.value = 8.0F;
    command.config_revision = 1U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2100U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(st_command_runtime_get_rule(&runtime, ST_SENSOR_VOC_INDEX,
                                       ST_CONFIG_RULE_REPORT_DEADBAND, &rule) == 0);
    EXPECT(rule.value == 8.0F && rule.revision == 1U);

    st_reporting_policy_init(&reporting);
    EXPECT(st_command_runtime_apply_reporting_rules(&runtime, &reporting) == 0);
    EXPECT(reporting.rules[ST_SENSOR_VOC_INDEX].deadband == 8.0F);

    command = make_command(3U, ST_COMMAND_SET_RULE,
                           ST_COMMAND_TARGET_CAPABILITY_RULE);
    command.capability = ST_SENSOR_CONTACT;
    command.rule_kind = ST_CONFIG_RULE_EVENT_DEBOUNCE_MS;
    command.value = 50.0F;
    command.config_revision = 1U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2200U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_UNSUPPORTED);

    command = make_command(4U, ST_COMMAND_TEST_OUTPUT, ST_COMMAND_TARGET_ALARM);
    command.duration_ms = 1000U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2300U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_UNSUPPORTED);
    return 0;
}

static int test_state_event_rules_and_revision_scope(void)
{
    st_command_runtime_t runtime;
    st_command_ack_t ack;
    st_command_t command;

    EXPECT(st_command_runtime_init(&runtime, ST_POD_ACTIVITY_ACCESS, "POD_2",
                                   (st_command_persistence_t){0}) == 0);
    command = make_command(10U, ST_COMMAND_SET_RULE,
                           ST_COMMAND_TARGET_CAPABILITY_RULE);
    strcpy(command.target_pod_id, "POD_2");
    command.capability = ST_SENSOR_CONTACT;
    command.rule_kind = ST_CONFIG_RULE_EVENT_DEBOUNCE_MS;
    command.value = 40.0F;
    command.config_revision = 1U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);

    command.command_id = 11U;
    command.value = 45.0F;
    command.config_revision = 3U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2100U, &ack) == 0);
    EXPECT(ack.reason == ST_COMMAND_REASON_REVISION_CONFLICT);

    command.command_id = 12U;
    command.capability = ST_SENSOR_MOTION;
    command.rule_kind = ST_CONFIG_RULE_EVENT_RETRIGGER_MS;
    command.value = 1000.0F;
    command.config_revision = 1U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2200U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    return 0;
}

static int test_persistence_migration_and_idempotency(void)
{
    fake_persistence_t fake;
    st_command_runtime_t runtime;
    st_command_runtime_t restored;
    st_command_ack_t ack;
    st_capability_rule_t rule;
    st_command_t command;
    st_command_persistence_t persistence = {&fake, fake_load, fake_save};

    memset(&fake, 0, sizeof(fake));
    fake.has_state = 1;
    fake.stored.magic = ST_COMMAND_PERSISTENCE_MAGIC;
    fake.stored.version = ST_COMMAND_LEGACY_CONTRACT_VERSION;
    fake.stored.config.co2_threshold_ppm = 1300.0F;
    fake.stored.config.revision = 4U;
    EXPECT(st_command_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1", persistence) == 0);
    EXPECT(runtime.persistent.version == ST_COMMAND_PERSISTENCE_VERSION);
    EXPECT(st_command_runtime_get_rule(&runtime, ST_SENSOR_CO2_PPM,
                                       ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD,
                                       &rule) == 0);
    EXPECT(rule.value == 1300.0F && rule.revision == 4U);

    command = make_command(20U, ST_COMMAND_SET_THRESHOLD,
                           ST_COMMAND_TARGET_CO2_THRESHOLD);
    command.value = 1400.0F;
    command.config_revision = 5U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(fake.saves == 1);
    EXPECT(st_command_runtime_init(&restored, ST_POD_ENVIRONMENT, "POD_1", persistence) == 0);
    EXPECT(st_command_runtime_handle(&restored, &command, 2100U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_DUPLICATE);
    return 0;
}

int st_run_command_actuation_tests(void)
{
    int failures = 0;
    failures += test_v2_and_legacy_codecs();
    failures += test_capability_rules_and_legacy_threshold();
    failures += test_state_event_rules_and_revision_scope();
    failures += test_persistence_migration_and_idempotency();
    return failures;
}
