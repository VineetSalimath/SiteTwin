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

static st_command_t make_command(uint64_t command_id)
{
    st_command_t command;

    memset(&command, 0, sizeof(command));
    command.command_id = command_id;
    strcpy(command.target_pod_id, "POD_1");
    command.command_type = ST_COMMAND_SET_THRESHOLD;
    command.target = ST_COMMAND_TARGET_CO2_THRESHOLD;
    command.capability = ST_SENSOR_CO2_PPM;
    command.rule_kind = ST_CONFIG_RULE_NUMERIC_HIGH_THRESHOLD;
    command.value = 1200.0F;
    command.issued_at_ms = 1000U;
    command.expires_at_ms = 11000U;
    command.valid_for_ms = 10000U;
    command.config_revision = 2U;
    command.source = ST_COMMAND_SOURCE_THINGSBOARD;
    return command;
}

static int test_rpc_wire_zigbee_result_round_trip(void)
{
    st_command_t command = make_command(77U);
    st_command_t pod_command;
    st_command_ack_t ack;
    st_command_ack_t returned_ack;
    st_command_runtime_t runtime;
    st_gateway_frame_header_t header;
    st_gateway_frame_header_t decoded_header;
    const uint8_t *decoded_payload;
    uint8_t command_payload[ST_COMMAND_WIRE_SIZE];
    uint8_t ack_payload[ST_COMMAND_ACK_WIRE_SIZE];
    uint8_t frame[ST_GATEWAY_FRAME_HEADER_SIZE + ST_COMMAND_WIRE_SIZE +
                  ST_GATEWAY_FRAME_CRC_SIZE];
    size_t payload_length;
    size_t frame_length;

    EXPECT(st_command_encode(&command, command_payload, sizeof(command_payload),
                             &payload_length) == 0);
    header = (st_gateway_frame_header_t){ST_GATEWAY_FRAME_VERSION,
                                         ST_GATEWAY_MESSAGE_COMMAND,
                                         (uint16_t)payload_length, 0U, 0U, 77U};
    EXPECT(st_gateway_frame_encode(&header, command_payload, frame, sizeof(frame),
                                   &frame_length) == 0);
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_header,
                                   &decoded_payload) == 0);
    EXPECT(decoded_header.message_type == ST_GATEWAY_MESSAGE_COMMAND);
    EXPECT(st_command_decode(decoded_payload, decoded_header.payload_length,
                             &pod_command) == 0);

    pod_command.issued_at_ms = 500U;
    pod_command.expires_at_ms = 10500U;
    EXPECT(st_command_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1",
                                   (st_command_persistence_t){0}) == 0);
    EXPECT(st_command_runtime_handle(&runtime, &pod_command, 600U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXECUTED);
    EXPECT(ack.command_id == command.command_id);
    EXPECT(ack.applied_config_revision == 2U);

    EXPECT(st_command_ack_encode(&ack, ack_payload, sizeof(ack_payload),
                                 &payload_length) == 0);
    header.message_type = ST_GATEWAY_MESSAGE_COMMAND_ACK;
    header.payload_length = (uint16_t)payload_length;
    header.source_address = 0x60D1U;
    EXPECT(st_gateway_frame_encode(&header, ack_payload, frame, sizeof(frame),
                                   &frame_length) == 0);
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_header,
                                   &decoded_payload) == 0);
    EXPECT(decoded_header.message_type == ST_GATEWAY_MESSAGE_COMMAND_ACK);
    EXPECT(st_command_ack_decode(decoded_payload, decoded_header.payload_length,
                                 &returned_ack) == 0);
    EXPECT(returned_ack.command_id == 77U);
    EXPECT(returned_ack.status == ST_COMMAND_STATUS_EXECUTED);

    EXPECT(st_command_runtime_handle(&runtime, &pod_command, 700U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_DUPLICATE);
    return 0;
}

static int test_expired_command_result(void)
{
    st_command_runtime_t runtime;
    st_command_t command = make_command(78U);
    st_command_ack_t ack;

    command.issued_at_ms = 100U;
    command.expires_at_ms = 200U;
    command.valid_for_ms = 100U;
    EXPECT(st_command_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1",
                                   (st_command_persistence_t){0}) == 0);
    EXPECT(st_command_runtime_handle(&runtime, &command, 201U, &ack) == 0);
    EXPECT(ack.status == ST_COMMAND_STATUS_EXPIRED);
    EXPECT(ack.reason == ST_COMMAND_REASON_EXPIRED);
    return 0;
}

int st_run_command_transport_tests(void)
{
    int failures = 0;
    failures += test_rpc_wire_zigbee_result_round_trip();
    failures += test_expired_command_result();
    return failures;
}
