#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/zigbee_payload.h"
#include "gateway_pipeline.h"
#include "mqtt_publish.h"

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

int gateway_pipeline_process_uart_frame(const st_gateway_frame_header_t *header,
                                        const uint8_t *payload)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint8_t sensor_slot;
    st_gateway_ingress_result_t result;

    if (header == NULL || payload == NULL || header->version != ST_GATEWAY_FRAME_VERSION ||
        (header->message_type != ST_GATEWAY_MESSAGE_TELEMETRY &&
         header->message_type != ST_GATEWAY_MESSAGE_HEALTH) ||
        header->payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        st_zigbee_telemetry_sensor_slot(payload, header->payload_length, &sensor_slot) != 0) {
        ESP_LOGW(TAG, "Rejected unsupported UART frame");
        return -1;
    }

    snprintf(pod_id, sizeof(pod_id), "POD_%04X", header->source_address);
    snprintf(sensor_id, sizeof(sensor_id), "SLOT_%u", (unsigned int)sensor_slot);
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
