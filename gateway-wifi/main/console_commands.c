#include <stdlib.h>
#include <stdio.h>
#include "esp_console.h"
#include "gateway_pipeline.h"
#include "test_loop.h"
#include "mqtt_publish.h"
#include "uart_link.h"
#include "sitetwin/contracts.h"
#include "sitetwin/gateway_frame.h"
#include "sitetwin/zigbee_payload.h"

static int cmd_send_test(int argc, char **argv)
{
    float value = 21.5f;
    if (argc > 1) {
        value = strtof(argv[1], NULL);
    }
    if (gateway_pipeline_send_test_record(value) == 0) {
        printf("Sent test record, value=%.2f\n", value);
    } else {
        printf("Failed to send test record\n");
    }
    return 0;
}

static int cmd_send_heartbeat(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (gateway_pipeline_send_heartbeat() == 0) {
        printf("Sent heartbeat\n");
    } else {
        printf("Failed to send heartbeat\n");
    }
    return 0;
}

static int cmd_loop_start(int argc, char **argv)
{
    uint32_t interval_ms = 5000;
    if (argc > 1) {
        interval_ms = (uint32_t)atoi(argv[1]);
    }
    test_loop_start(interval_ms);
    printf("Loop started, interval=%lu ms\n", (unsigned long)interval_ms);
    return 0;
}

static int cmd_loop_stop(int argc, char **argv)
{
    (void)argc; (void)argv;
    test_loop_stop();
    printf("Loop stopped\n");
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("MQTT connected: %s\n", gw_mqtt_is_connected() ? "yes" : "no");
    printf("Records sent:   %lu\n", (unsigned long)gateway_pipeline_sent_count());
    printf("Loop running:   %s\n", test_loop_is_running() ? "yes" : "no");
    return 0;
}

/* Self-test for the UART framing/CRC parsing logic in uart_link.c, without
 * requiring a physical connection to the Zigbee-side board. Builds a
 * synthetic 30-byte SiteTwin Zigbee payload, wraps it in a real
 * gateway_frame envelope (same encoder the Zigbee side would use), and
 * feeds the resulting bytes directly into the parser. What this validates:
 * start-marker detection, header parsing, CRC16 checking, and frame
 * reassembly. What this does NOT validate: whether a 30-byte Zigbee
 * payload is actually what will appear inside the real UART frame -- that
 * part is still pending confirmation (see GATEWAY_TO_SERVER_BRINGUP.md). */
static int cmd_uart_test(int argc, char **argv)
{
    (void)argc; (void)argv;

    st_telemetry_record_t record = {0};
    record.record_class = ST_RECORD_STATE;
    record.priority = ST_PRIORITY_ROUTINE;
    record.reading.sensor_kind = ST_SENSOR_TEMPERATURE_C;
    record.reading.unit = ST_UNIT_CELSIUS;
    record.reading.sequence = 1U;
    record.reading.boot_id = 1U;
    record.reading.uptime_ms = 1000U;
    record.reading.value = 19.5f;
    record.reading.quality_flags = ST_QUALITY_VALID;

    uint8_t zb_payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t zb_payload_length = 0;
    if (st_zigbee_telemetry_encode(&record, 0U, zb_payload, sizeof(zb_payload),
                                    &zb_payload_length) != 0) {
        printf("Failed to encode test Zigbee payload\n");
        return 1;
    }

    st_gateway_frame_header_t header = {
        .version = ST_GATEWAY_FRAME_VERSION,
        .message_type = ST_GATEWAY_MESSAGE_TELEMETRY,
        .payload_length = (uint16_t)zb_payload_length,
        .source_address = 0x1234U,
        .boot_id = 1U,
        .sequence = 1U,
    };

    uint8_t frame[ST_GATEWAY_FRAME_HEADER_SIZE + ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE +
                  ST_GATEWAY_FRAME_CRC_SIZE];
    size_t frame_length = 0;
    if (st_gateway_frame_encode(&header, zb_payload, frame, sizeof(frame), &frame_length) != 0) {
        printf("Failed to encode test UART frame\n");
        return 1;
    }

    printf("Injecting synthetic %u-byte UART frame into the parser...\n",
           (unsigned)frame_length);
    uart_link_feed_test_bytes(frame, frame_length);
    printf("Done. Check the log above for \"Frame OK\" from uart_link.\n");
    return 0;
}

void console_commands_register(void)
{
    const esp_console_cmd_t send_test_cmd = {
        .command = "send_test",
        .help = "Send one test telemetry record. Usage: send_test [value]",
        .hint = NULL,
        .func = &cmd_send_test,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&send_test_cmd));

    const esp_console_cmd_t send_heartbeat_cmd = {
        .command = "send_heartbeat",
        .help = "Send one heartbeat/health record.",
        .hint = NULL,
        .func = &cmd_send_heartbeat,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&send_heartbeat_cmd));

    const esp_console_cmd_t loop_start_cmd = {
        .command = "loop_start",
        .help = "Start auto-sending test records. Usage: loop_start [interval_ms]",
        .hint = NULL,
        .func = &cmd_loop_start,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&loop_start_cmd));

    const esp_console_cmd_t loop_stop_cmd = {
        .command = "loop_stop",
        .help = "Stop auto-sending test records.",
        .hint = NULL,
        .func = &cmd_loop_stop,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&loop_stop_cmd));

    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show gateway status.",
        .hint = NULL,
        .func = &cmd_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));

    const esp_console_cmd_t uart_test_cmd = {
        .command = "uart_test",
        .help = "Self-test the UART frame parser with a synthetic frame (no wiring required).",
        .hint = NULL,
        .func = &cmd_uart_test,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&uart_test_cmd));
}