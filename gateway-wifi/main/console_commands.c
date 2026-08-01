#include <stdlib.h>
#include <stdio.h>
#include "esp_console.h"
#include "gateway_pipeline.h"
#include "test_loop.h"
#include "mqtt_publish.h"

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
}