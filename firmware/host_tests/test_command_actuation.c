#include <stdio.h>
#include <string.h>

#include "sitetwin/command.h"
#include "sitetwin/gateway_frame.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Command expectation failed: %s (%s:%d)\n", #condition, __FILE__,   \
                    __LINE__);                                                                   \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    st_command_persistent_state_t state;
    int has_state;
    int saves;
} fake_persistence_t;

static int fake_load(void *context, st_command_persistent_state_t *state)
{
    fake_persistence_t *fake = (fake_persistence_t *)context;
    if (fake->has_state == 0) {
        return -1;
    }
    *state = fake->state;
    return 0;
}

static int fake_save(void *context, const st_command_persistent_state_t *state)
{
    fake_persistence_t *fake = (fake_persistence_t *)context;
    fake->state = *state;
    fake->has_state = 1;
    fake->saves++;
    return 0;
}

static st_command_t make_command(uint64_t id, st_command_type_t type,
                                 st_command_target_t target)
{
    st_command_t command;
    memset(&command, 0, sizeof(command));
    command.command_id = id;
    strcpy(command.target_pod_id, "ENV_01");
    command.command_type = type;
    command.target = target;
    command.issued_at_ms = 1000U;
    command.expires_at_ms = 10000U;
    command.valid_for_ms = 9000U;
    command.source = ST_COMMAND_SOURCE_THINGSBOARD;
    return command;
}

static int test_command_and_ack_codecs(void)
{
    st_command_t input = make_command(0x0102030405060708ULL,
                                      ST_COMMAND_SET_THRESHOLD,
                                      ST_COMMAND_TARGET_CO2_THRESHOLD);
    st_command_t output;
    st_command_ack_t ack_input;
    st_command_ack_t ack_output;
    uint8_t payload[ST_COMMAND_WIRE_SIZE];
    uint8_t ack_payload[ST_COMMAND_ACK_WIRE_SIZE];
    uint8_t frame[ST_GATEWAY_FRAME_HEADER_SIZE + ST_COMMAND_WIRE_SIZE +
                  ST_GATEWAY_FRAME_CRC_SIZE];
    size_t length = 0U;
    size_t frame_length = 0U;
    const uint8_t *framed_payload;
    st_gateway_frame_header_t frame_header = {0};
    st_gateway_frame_header_t decoded_frame_header;

    input.value = 1200.0F;
    input.config_revision = 2U;
    EXPECT(st_command_encode(&input, payload, sizeof(payload), &length) == 0);
    EXPECT(length == ST_COMMAND_WIRE_SIZE);
    EXPECT(st_command_decode(payload, length, &output) == 0);
    EXPECT(output.command_id == input.command_id);
    EXPECT(strcmp(output.target_pod_id, input.target_pod_id) == 0);
    EXPECT(output.command_type == input.command_type);
    EXPECT(output.target == input.target);
    EXPECT(output.value == input.value);
    EXPECT(output.config_revision == input.config_revision);
    EXPECT(st_command_decode(payload, length - 1U, &output) != 0);
    frame_header.version = ST_GATEWAY_FRAME_VERSION;
    frame_header.message_type = ST_GATEWAY_MESSAGE_COMMAND;
    frame_header.payload_length = (uint16_t)length;
    frame_header.sequence = (uint32_t)input.command_id;
    EXPECT(st_gateway_frame_encode(&frame_header, payload, frame, sizeof(frame),
                                   &frame_length) == 0);
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_frame_header,
                                   &framed_payload) == 0);
    EXPECT(decoded_frame_header.message_type == ST_GATEWAY_MESSAGE_COMMAND);
    EXPECT(st_command_decode(framed_payload, decoded_frame_header.payload_length,
                             &output) == 0);

    memset(&ack_input, 0, sizeof(ack_input));
    ack_input.command_id = input.command_id;
    strcpy(ack_input.pod_id, "ENV_01");
    ack_input.status = ST_COMMAND_STATUS_EXECUTED;
    ack_input.reason = ST_COMMAND_REASON_NONE;
    ack_input.applied_config_revision = 2U;
    ack_input.timestamp_ms = 5000U;
    EXPECT(st_command_ack_encode(&ack_input, ack_payload, sizeof(ack_payload), &length) == 0);
    EXPECT(st_command_ack_decode(ack_payload, length, &ack_output) == 0);
    EXPECT(ack_output.command_id == ack_input.command_id);
    EXPECT(ack_output.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(ack_output.applied_config_revision == 2U);
    frame_header.message_type = ST_GATEWAY_MESSAGE_COMMAND_ACK;
    frame_header.payload_length = (uint16_t)length;
    EXPECT(st_gateway_frame_encode(&frame_header, ack_payload, frame, sizeof(frame),
                                   &frame_length) == 0);
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_frame_header,
                                   &framed_payload) == 0);
    EXPECT(decoded_frame_header.message_type == ST_GATEWAY_MESSAGE_COMMAND_ACK);
    EXPECT(st_command_ack_decode(framed_payload, decoded_frame_header.payload_length,
                                 &ack_output) == 0);
    return 0;
}

static int test_capabilities_validation_and_idempotency(void)
{
    st_command_runtime_t runtime;
    st_command_runtime_t restored;
    st_command_ack_t ack;
    fake_persistence_t fake = {0};
    st_command_persistence_t persistence = {&fake, fake_load, fake_save};
    st_command_t command = make_command(1U, ST_COMMAND_SET_THRESHOLD,
                                        ST_COMMAND_TARGET_CO2_THRESHOLD);

    EXPECT(st_command_runtime_init(&runtime, ST_POD_ENVIRONMENT, "ENV_01", persistence) == 0);
    EXPECT(runtime.persistent.config.co2_threshold_ppm == 1000.0F);
    command.value = 1200.0F;
    command.config_revision = 2U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(runtime.persistent.config.co2_threshold_ppm == 1200.0F);
    EXPECT(fake.saves == 1);

    command.value = 1400.0F;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2100U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_DUPLICATE);
    EXPECT(runtime.persistent.config.co2_threshold_ppm == 1200.0F);
    EXPECT(fake.saves == 1);

    EXPECT(st_command_runtime_init(&restored, ST_POD_ENVIRONMENT, "ENV_01", persistence) == 0);
    EXPECT(restored.persistent.config.co2_threshold_ppm == 1200.0F);
    EXPECT(st_command_runtime_handle(&restored, &command, 2200U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_DUPLICATE);

    command = make_command(2U, ST_COMMAND_SET_THRESHOLD,
                           ST_COMMAND_TARGET_CO2_THRESHOLD);
    command.value = 1201.0F;
    command.config_revision = 5U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2300U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_REVISION_CONFLICT);

    command = make_command(3U, ST_COMMAND_SET_THRESHOLD,
                           ST_COMMAND_TARGET_CO2_THRESHOLD);
    command.value = 10000.0F;
    command.config_revision = 3U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2400U, &ack) == 0);
    EXPECT(ack.reason == ST_COMMAND_REASON_OUT_OF_BOUNDS);

    command = make_command(4U, ST_COMMAND_TEST_OUTPUT, ST_COMMAND_TARGET_LED);
    command.expires_at_ms = 1500U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2500U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXPIRED);

    command = make_command(5U, ST_COMMAND_TEST_OUTPUT, ST_COMMAND_TARGET_LED);
    strcpy(command.target_pod_id, "ACT_01");
    command.duration_ms = 1000U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 2500U, &ack) == 0);
    EXPECT(ack.reason == ST_COMMAND_REASON_WRONG_TARGET);

    EXPECT(st_pod_capabilities(ST_POD_ACTIVITY_ACCESS).pending_hardware_verification == 1U);
    EXPECT(st_pod_capabilities(ST_POD_ACTIVITY_ACCESS).command_mask == 0U);
    EXPECT(st_command_runtime_init(&restored, ST_POD_ACTIVITY_ACCESS, "ACT_01",
                                   (st_command_persistence_t){0}) == 0);
    command = make_command(6U, ST_COMMAND_TEST_OUTPUT, ST_COMMAND_TARGET_LED);
    strcpy(command.target_pod_id, "ACT_01");
    command.duration_ms = 1000U;
    EXPECT(st_command_runtime_handle(&restored, &command, 2500U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_UNSUPPORTED);
    return 0;
}

static int test_alarm_silence_and_bounded_output(void)
{
    st_command_runtime_t runtime;
    st_command_ack_t ack;
    st_command_persistence_t persistence = {0};
    st_command_t command;
    st_local_actuation_state_t output;

    EXPECT(st_command_runtime_init(&runtime, ST_POD_ENVIRONMENT, "ENV_01", persistence) == 0);
    st_command_runtime_observe_co2(&runtime, 1500.0F, ST_QUALITY_WARMING_UP);
    output = st_command_runtime_tick(&runtime, 2000U);
    EXPECT(output.led_active == 0U);
    st_command_runtime_observe_co2(&runtime, 1500.0F, ST_QUALITY_VALID | ST_QUALITY_STALE);
    EXPECT(st_command_runtime_tick(&runtime, 2000U).led_active == 0U);
    st_command_runtime_observe_co2(&runtime, 1500.0F, ST_QUALITY_VALID);
    output = st_command_runtime_tick(&runtime, 2000U);
    EXPECT(output.led_active == 1U);
    EXPECT(output.buzzer_active == 1U);
    EXPECT(output.buzzer_frequency_hz == 1000U);
    EXPECT(st_command_runtime_tick(&runtime, 3000U).led_active == 0U);

    command = make_command(10U, ST_COMMAND_SILENCE_ALARM, ST_COMMAND_TARGET_ALARM);
    command.duration_ms = ST_COMMAND_MAX_SILENCE_MS;
    EXPECT(st_command_runtime_handle(&runtime, &command, 4000U, &ack) == 0);
    output = st_command_runtime_tick(&runtime, 4000U);
    EXPECT(output.led_active == 1U);
    EXPECT(output.buzzer_active == 0U);
    output = st_command_runtime_tick(&runtime, 304000U);
    EXPECT(output.led_active == 1U);
    EXPECT(output.buzzer_active == 1U);

    st_command_runtime_observe_co2(&runtime, 500.0F, ST_QUALITY_VALID);
    command = make_command(11U, ST_COMMAND_TEST_OUTPUT, ST_COMMAND_TARGET_BUZZER);
    command.duration_ms = ST_COMMAND_MAX_TEST_OUTPUT_MS;
    command.expires_at_ms = 400000U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 305000U, &ack) == 0);
    EXPECT(st_command_runtime_tick(&runtime, 305000U).buzzer_active == 1U);
    EXPECT(st_command_runtime_tick(&runtime, 310000U).buzzer_active == 0U);

    command = make_command(12U, ST_COMMAND_TEST_OUTPUT, ST_COMMAND_TARGET_LED);
    command.duration_ms = ST_COMMAND_MAX_TEST_OUTPUT_MS + 1U;
    command.expires_at_ms = 400000U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 311000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_REJECTED);
    EXPECT(ack.reason == ST_COMMAND_REASON_OUT_OF_BOUNDS);

    command = make_command(13U, ST_COMMAND_GET_CONFIG, ST_COMMAND_TARGET_CONFIG);
    command.expires_at_ms = 400000U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 312000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(ack.config_value == ST_COMMAND_DEFAULT_CO2_THRESHOLD_PPM);

    /* Invalid data cannot clear a real condition; only a later valid sample can. */
    st_command_runtime_observe_co2(&runtime, 1500.0F, ST_QUALITY_VALID);
    st_command_runtime_observe_co2(&runtime, 500.0F, ST_QUALITY_STALE);
    EXPECT(runtime.alarm_condition_active == 1U);
    command = make_command(14U, ST_COMMAND_SET_THRESHOLD,
                           ST_COMMAND_TARGET_CO2_THRESHOLD);
    command.value = 2000.0F;
    command.config_revision = 2U;
    command.expires_at_ms = 400000U;
    EXPECT(st_command_runtime_handle(&runtime, &command, 313000U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(runtime.alarm_condition_active == 1U);
    st_command_runtime_observe_co2(&runtime, 500.0F, ST_QUALITY_VALID);
    EXPECT(runtime.alarm_condition_active == 0U);
    return 0;
}

int st_run_command_actuation_tests(void)
{
    int failures = 0;
    failures += test_command_and_ack_codecs();
    failures += test_capabilities_validation_and_idempotency();
    failures += test_alarm_silence_and_bounded_output();
    return failures;
}
