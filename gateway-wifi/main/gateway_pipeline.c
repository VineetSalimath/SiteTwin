#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/zigbee_payload.h"
#include "gateway_pipeline.h"
#include "mqtt_publish.h"
#include "cJSON.h"

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

void gateway_pipeline_process_command(const char *topic, const char *payload)
{
    /* Extract pod_id from topic: "sitetwin/pods/{pod_id}/commands" */
    char pod_id[ST_POD_ID_MAX_LEN] = {0};
    const char *prefix = "sitetwin/pods/";
    const char *p = strstr(topic, prefix);
    if (p == NULL) {
        ESP_LOGW(TAG, "Command on unexpected topic: %s", topic);
        return;
    }
    p += strlen(prefix);
    const char *slash = strchr(p, '/');
    if (slash == NULL) {
        ESP_LOGW(TAG, "Malformed command topic: %s", topic);
        return;
    }
    size_t id_len = (size_t)(slash - p);
    if (id_len >= sizeof(pod_id)) {
        id_len = sizeof(pod_id) - 1;
    }
    memcpy(pod_id, p, id_len);
    pod_id[id_len] = '\0';

    cJSON *root = cJSON_Parse(payload);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse command JSON: %s", payload);
        return;
    }

    cJSON *command_id_item = cJSON_GetObjectItem(root, "command_id");
    cJSON *actuator_id_item = cJSON_GetObjectItem(root, "actuator_id");

    if (!cJSON_IsNumber(command_id_item) || !cJSON_IsString(actuator_id_item)) {
        ESP_LOGW(TAG, "Command missing command_id/actuator_id, ignoring: %s", payload);
        cJSON_Delete(root);
        return;
    }

    int command_id = command_id_item->valueint;
    const char *actuator_id = actuator_id_item->valuestring;

    ESP_LOGI(TAG, "Received command for pod=%s actuator=%s command_id=%d",
             pod_id, actuator_id, command_id);

    /* Validate command_type against our own defined set (set_state/set_value/
     * pulse/custom -- see the downstream command design discussion). This is
     * the one piece of `params` we DO understand and can meaningfully check,
     * unlike actuator_id (we have no roster of real actuators to check
     * against yet) or the rest of params (intentionally open-ended).
     * An unrecognised command_type gets an honest "rejected" ack instead of
     * being silently treated as "simulated success" -- claiming success on
     * something we don't understand would misrepresent what happened. If
     * command_type is absent entirely, we don't reject -- params is designed
     * to be an open dict, not a strictly required schema at this stage. */
    bool type_known = true;
    char type_buf[32] = {0};
    cJSON *params_item = cJSON_GetObjectItem(root, "params");
    if (cJSON_IsObject(params_item)) {
        cJSON *type_item = cJSON_GetObjectItem(params_item, "command_type");
        if (cJSON_IsString(type_item)) {
            strncpy(type_buf, type_item->valuestring, sizeof(type_buf) - 1);
            if (strcmp(type_buf, "set_state") != 0 &&
                strcmp(type_buf, "set_value") != 0 &&
                strcmp(type_buf, "pulse") != 0 &&
                strcmp(type_buf, "custom") != 0) {
                type_known = false;
            }
        }
    }

    /* No real Zigbee downlink to the pod yet -- this branch is intentionally
     * a stand-in. Once pod-side command delivery exists, this is where the
     * command would be encoded (ST_GATEWAY_MESSAGE_COMMAND, already reserved
     * in gateway_frame.h) and sent over UART instead of immediately faking
     * a response here. */
    char ack_json[320];
    if (type_known) {
        snprintf(ack_json, sizeof(ack_json),
                 "{\"schema_version\":1,\"pod_id\":\"%s\",\"command_id\":%d,"
                 "\"actuator_id\":\"%s\",\"status\":\"simulated\",\"executed_at_ms\":%llu}",
                 pod_id, command_id, actuator_id,
                 (unsigned long long)(esp_timer_get_time() / 1000));
        ESP_LOGI(TAG, "Publishing simulated ack for command_id=%d", command_id);
    } else {
        snprintf(ack_json, sizeof(ack_json),
                 "{\"schema_version\":1,\"pod_id\":\"%s\",\"command_id\":%d,"
                 "\"actuator_id\":\"%s\",\"status\":\"rejected\","
                 "\"reason\":\"unknown command_type: %s\",\"executed_at_ms\":%llu}",
                 pod_id, command_id, actuator_id, type_buf,
                 (unsigned long long)(esp_timer_get_time() / 1000));
        ESP_LOGW(TAG, "Rejecting command_id=%d: unknown command_type '%s'", command_id, type_buf);
    }

    char ack_topic[96];
    snprintf(ack_topic, sizeof(ack_topic), "sitetwin/pods/%s/command_acks", pod_id);

    if (gw_mqtt_publish(ack_topic, ack_json) != 0) {
        ESP_LOGW(TAG, "Failed to publish ack");
    } else {
        ESP_LOGI(TAG, "Published ack: %s", ack_json);
    }

    cJSON_Delete(root);
}