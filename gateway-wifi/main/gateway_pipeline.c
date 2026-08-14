#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sitetwin/command.h"
#include "sitetwin/gateway_identity.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/zigbee_payload.h"
#include "gateway_command_json.h"
#include "gateway_pipeline.h"
#include "mqtt_publish.h"
#include "uart_link.h"

static const char *TAG = "gw_pipeline";

static st_gateway_runtime_t s_runtime;
static uint32_t s_sequence = 0;
static uint32_t s_sent_count = 0;

#define TEST_SOURCE_ADDRESS 0x1234U
#define TEST_IEEE_ADDRESS   0x00124B0012345678ULL
#define TEST_SENSOR_SLOT    0U
#define TEST_POD_ID         "POD_1234"
#define TEST_SENSOR_ID      "SLOT_0"

static int gateway_pipeline_publish_next(const char *pod_id)
{
    char json[256];
    char topic[96];

    if (st_gateway_runtime_next_json(&s_runtime, json, sizeof(json)) != 0) {
        ESP_LOGE(TAG, "Failed to convert record to JSON");
        return -1;
    }
    snprintf(topic, sizeof(topic), "sitetwin/pods/%s/telemetry", pod_id);
    if (gw_mqtt_publish(topic, json) != 0) {
        ESP_LOGW(TAG, "MQTT publish skipped or failed (not connected?)");
        return -1;
    }
    s_sent_count++;
    ESP_LOGI(TAG, "Sent #%lu: %s", (unsigned long)s_sent_count, json);
    return 0;
}

static int gateway_pipeline_publish_command_result(const st_command_ack_t *ack)
{
    char json[320];
    char topic[96];

    if (gw_command_result_json(ack, json, sizeof(json)) != 0) {
        ESP_LOGW(TAG, "Rejected invalid command result");
        return -1;
    }
    snprintf(topic, sizeof(topic), "sitetwin/pods/%s/command_results", ack->pod_id);
    if (gw_mqtt_publish(topic, json) != 0) {
        ESP_LOGW(TAG, "Command result publish failed for %llu",
                 (unsigned long long)ack->command_id);
        return -1;
    }
    ESP_LOGI(TAG, "Published command result %llu: %s",
             (unsigned long long)ack->command_id, json);
    return 0;
}

static void gateway_pipeline_transport_failure(const st_command_t *command,
                                               st_command_reason_t reason)
{
    st_command_ack_t ack;

    memset(&ack, 0, sizeof(ack));
    ack.command_id = command->command_id;
    memcpy(ack.pod_id, command->target_pod_id, sizeof(ack.pod_id));
    ack.status = ST_COMMAND_STATUS_FAILED;
    ack.reason = reason;
    ack.timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000);
    (void)gateway_pipeline_publish_command_result(&ack);
}

int gateway_pipeline_process_mqtt_command(const char *topic, const char *payload)
{
    st_command_t command;
    uint8_t wire_payload[ST_COMMAND_WIRE_SIZE];
    size_t wire_length;

    if (gw_command_json_parse(topic, payload, &command) != 0) {
        ESP_LOGW(TAG, "Rejected invalid downstream command JSON");
        return -1;
    }
    if (st_command_encode(&command, wire_payload, sizeof(wire_payload),
                          &wire_length) != 0 ||
        uart_link_send_payload(ST_GATEWAY_MESSAGE_COMMAND, wire_payload,
                               (uint16_t)wire_length,
                               (uint32_t)command.command_id) != 0) {
        ESP_LOGW(TAG, "UART command transport failed for %llu",
                 (unsigned long long)command.command_id);
        gateway_pipeline_transport_failure(&command, ST_COMMAND_REASON_TRANSPORT_FAILED);
        return -1;
    }
    ESP_LOGI(TAG, "Forwarded command %llu for %s to Zigbee gateway",
             (unsigned long long)command.command_id, command.target_pod_id);
    return 0;
}

int gateway_pipeline_process_uart_frame(const st_gateway_frame_header_t *header,
                                        const uint8_t *payload)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint8_t sensor_slot;
    st_gateway_ingress_result_t result;
    st_telemetry_record_t decoded;

    if (header == NULL || payload == NULL ||
        header->version != ST_GATEWAY_FRAME_VERSION) {
        ESP_LOGW(TAG, "Rejected invalid UART frame header");
        return -1;
    }
    if (header->message_type == ST_GATEWAY_MESSAGE_COMMAND_ACK) {
        st_command_ack_t ack;
        if (header->payload_length != ST_COMMAND_ACK_WIRE_SIZE ||
            st_command_ack_decode(payload, header->payload_length, &ack) != 0) {
            ESP_LOGW(TAG, "Rejected invalid UART command result");
            return -1;
        }
        return gateway_pipeline_publish_command_result(&ack);
    }

    if ((header->message_type != ST_GATEWAY_MESSAGE_TELEMETRY &&
         header->message_type != ST_GATEWAY_MESSAGE_HEALTH) ||
        header->payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        st_zigbee_telemetry_decode(payload, header->payload_length,
                                   "unresolved", "unresolved", &decoded,
                                   &sensor_slot) != 0 ||
        st_gateway_identity_resolve(header->source_address, sensor_slot,
                                    decoded.reading.sensor_kind, pod_id,
                                    sizeof(pod_id), sensor_id,
                                    sizeof(sensor_id)) != 0) {
        ESP_LOGW(TAG, "Rejected unsupported UART frame");
        return -1;
    }

    result = st_gateway_runtime_ingest_zigbee(&s_runtime, payload, header->payload_length,
                                              pod_id, sensor_id);
    if (result != ST_GATEWAY_INGRESS_ACCEPTED) {
        ESP_LOGW(TAG, "UART frame ingress result=%d", (int)result);
        return -1;
    }
    return gateway_pipeline_publish_next(pod_id);
}

void gateway_pipeline_init(void)
{
    st_gateway_runtime_init(&s_runtime);
    st_gateway_registry_register_node(&s_runtime.registry, TEST_SOURCE_ADDRESS,
                                       TEST_IEEE_ADDRESS, TEST_POD_ID);
    st_gateway_registry_bind_sensor(&s_runtime.registry, TEST_SOURCE_ADDRESS,
                                     TEST_SENSOR_SLOT, TEST_SENSOR_ID);
    ESP_LOGI(TAG, "Gateway pipeline initialised (test node %s registered)", TEST_POD_ID);
}

int gateway_pipeline_send_test_record(float value)
{
    st_telemetry_record_t record = {0};
    record.record_class = ST_RECORD_STATE;
    record.priority = ST_PRIORITY_ROUTINE;
    record.reading.sensor_kind = ST_SENSOR_TEMPERATURE_C;
    record.reading.unit = ST_UNIT_CELSIUS;
    record.reading.sequence = ++s_sequence;
    record.reading.boot_id = 1U;
    record.reading.uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
    record.reading.value = value;
    record.reading.quality_flags = ST_QUALITY_VALID;

    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length = 0;
    if (st_zigbee_telemetry_encode(&record, TEST_SENSOR_SLOT, payload, sizeof(payload),
                                    &payload_length) != 0) {
        ESP_LOGE(TAG, "Failed to encode test payload");
        return -1;
    }

    st_gateway_ingress_result_t result = st_gateway_runtime_ingest_zigbee_source(
        &s_runtime, TEST_SOURCE_ADDRESS, payload, payload_length);
    if (result != ST_GATEWAY_INGRESS_ACCEPTED) {
        ESP_LOGW(TAG, "Ingress rejected record, result=%d", (int)result);
        return -1;
    }

    char json[256];
    if (st_gateway_runtime_next_json(&s_runtime, json, sizeof(json)) != 0) {
        ESP_LOGE(TAG, "Failed to convert record to JSON");
        return -1;
    }

    char topic[96];
    snprintf(topic, sizeof(topic), "sitetwin/pods/%s/telemetry", TEST_POD_ID);

    if (gw_mqtt_publish(topic, json) != 0) {
        ESP_LOGW(TAG, "MQTT publish skipped or failed (not connected?)");
        return -1;
    }

    s_sent_count++;
    ESP_LOGI(TAG, "Sent #%lu: %s", (unsigned long)s_sent_count, json);
    return 0;
}

int gateway_pipeline_send_heartbeat(void)
{
    st_telemetry_record_t record = {0};
    record.record_class = ST_RECORD_HEALTH;
    record.priority = ST_PRIORITY_HEALTH;
    record.reading.sensor_kind = ST_SENSOR_UNKNOWN;
    record.reading.unit = ST_UNIT_NONE;
    record.reading.sequence = ++s_sequence;
    record.reading.boot_id = 1U;
    record.reading.uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
    record.reading.value = 1.0f;
    record.reading.quality_flags = ST_QUALITY_VALID;

    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length = 0;
    if (st_zigbee_telemetry_encode(&record, TEST_SENSOR_SLOT, payload, sizeof(payload),
                                    &payload_length) != 0) {
        ESP_LOGE(TAG, "Failed to encode heartbeat payload");
        return -1;
    }

    st_gateway_ingress_result_t result = st_gateway_runtime_ingest_zigbee_source(
        &s_runtime, TEST_SOURCE_ADDRESS, payload, payload_length);
    if (result != ST_GATEWAY_INGRESS_ACCEPTED) {
        ESP_LOGW(TAG, "Ingress rejected heartbeat, result=%d", (int)result);
        return -1;
    }

    char json[256];
    if (st_gateway_runtime_next_json(&s_runtime, json, sizeof(json)) != 0) {
        ESP_LOGE(TAG, "Failed to convert heartbeat to JSON");
        return -1;
    }

    char topic[96];
    snprintf(topic, sizeof(topic), "sitetwin/pods/%s/telemetry", TEST_POD_ID);

    if (gw_mqtt_publish(topic, json) != 0) {
        ESP_LOGW(TAG, "MQTT publish skipped or failed (not connected?)");
        return -1;
    }

    s_sent_count++;
    ESP_LOGI(TAG, "Sent heartbeat #%lu: %s", (unsigned long)s_sent_count, json);
    return 0;
}

uint32_t gateway_pipeline_sent_count(void)
{
    return s_sent_count;
}
