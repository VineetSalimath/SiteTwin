#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sitetwin/command.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/zigbee_payload.h"
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
#define ENVIRONMENT_POD_ID  "ENV_01"

static int publish_command_ack(const st_command_ack_t *ack)
{
    char json[320];
    char topic[96];
    int length = snprintf(json, sizeof(json),
                          "{\"schema_version\":1,\"command_id\":%llu,"
                          "\"pod_id\":\"%s\",\"status\":\"%s\","
                          "\"reason\":\"%s\",\"applied_config_revision\":%lu,"
                          "\"timestamp_ms\":%llu,\"config_value\":%.3f}",
                          (unsigned long long)ack->command_id, ack->pod_id,
                          st_command_status_name(ack->status),
                          st_command_reason_name(ack->reason),
                          (unsigned long)ack->applied_config_revision,
                          (unsigned long long)ack->timestamp_ms,
                          (double)ack->config_value);
    if (length < 0 || (size_t)length >= sizeof(json)) {
        return -1;
    }
    snprintf(topic, sizeof(topic), "sitetwin/pods/%s/command_acks", ack->pod_id);
    return gw_mqtt_publish(topic, json);
}

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

    if (header == NULL || payload == NULL || header->version != ST_GATEWAY_FRAME_VERSION) {
        return -1;
    }
    if (header->message_type == ST_GATEWAY_MESSAGE_COMMAND_ACK) {
        st_command_ack_t ack;
        if (st_command_ack_decode(payload, header->payload_length, &ack) != 0) {
            ESP_LOGW(TAG, "Rejected malformed command acknowledgement");
            return -1;
        }
        return publish_command_ack(&ack);
    }
    if (
        (header->message_type != ST_GATEWAY_MESSAGE_TELEMETRY &&
         header->message_type != ST_GATEWAY_MESSAGE_HEALTH) ||
        header->payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        st_zigbee_telemetry_sensor_slot(payload, header->payload_length, &sensor_slot) != 0) {
        ESP_LOGW(TAG, "Rejected unsupported UART frame");
        return -1;
    }

    if (sensor_slot <= 3U) {
        snprintf(pod_id, sizeof(pod_id), "%s", ENVIRONMENT_POD_ID);
    } else {
        snprintf(pod_id, sizeof(pod_id), "POD_%04X", header->source_address);
    }
    snprintf(sensor_id, sizeof(sensor_id), "SLOT_%u", (unsigned int)sensor_slot);
    result = st_gateway_runtime_ingest_zigbee(&s_runtime, payload, header->payload_length,
                                              pod_id, sensor_id);
    if (result != ST_GATEWAY_INGRESS_ACCEPTED) {
        ESP_LOGW(TAG, "UART frame ingress result=%d", (int)result);
        return -1;
    }
    return gateway_pipeline_publish_next(pod_id);
}

static int json_u64(const cJSON *root, const char *name, uint64_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    uint64_t converted;
    if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
        item->valuedouble > 9007199254740991.0) {
        return -1;
    }
    converted = (uint64_t)item->valuedouble;
    if ((double)converted != item->valuedouble) {
        return -1;
    }
    *value = converted;
    return 0;
}

static int json_u32(const cJSON *root, const char *name, uint32_t *value)
{
    uint64_t converted;
    if (json_u64(root, name, &converted) != 0 || converted > UINT32_MAX) {
        return -1;
    }
    *value = (uint32_t)converted;
    return 0;
}

static st_command_type_t parse_command_type(const char *value)
{
    if (value == NULL) return 0;
    if (strcmp(value, "set_threshold") == 0) return ST_COMMAND_SET_THRESHOLD;
    if (strcmp(value, "silence_alarm") == 0) return ST_COMMAND_SILENCE_ALARM;
    if (strcmp(value, "test_output") == 0) return ST_COMMAND_TEST_OUTPUT;
    if (strcmp(value, "get_config") == 0) return ST_COMMAND_GET_CONFIG;
    return 0;
}

static st_command_target_t parse_command_target(const char *value)
{
    if (value == NULL) return 0;
    if (strcmp(value, "co2_threshold_ppm") == 0) return ST_COMMAND_TARGET_CO2_THRESHOLD;
    if (strcmp(value, "alarm") == 0) return ST_COMMAND_TARGET_ALARM;
    if (strcmp(value, "led") == 0) return ST_COMMAND_TARGET_LED;
    if (strcmp(value, "buzzer") == 0) return ST_COMMAND_TARGET_BUZZER;
    if (strcmp(value, "config") == 0) return ST_COMMAND_TARGET_CONFIG;
    return 0;
}

int gateway_pipeline_submit_command(const char *topic, const char *json)
{
    st_command_t command;
    st_gateway_frame_header_t header;
    st_command_ack_t queued_ack;
    uint8_t payload[ST_COMMAND_WIRE_SIZE];
    size_t payload_length;
    cJSON *root;
    const cJSON *item;
    const cJSON *parameters;
    char expected_topic[96];
    int result = -1;

    if (topic == NULL || json == NULL || (root = cJSON_Parse(json)) == NULL) {
        return -1;
    }
    memset(&command, 0, sizeof(command));
    item = cJSON_GetObjectItemCaseSensitive(root, "schema_version");
    if (!cJSON_IsNumber(item) || item->valuedouble != 1.0) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "target_pod_id");
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= sizeof(command.target_pod_id)) {
        goto done;
    }
    strcpy(command.target_pod_id, item->valuestring);
    snprintf(expected_topic, sizeof(expected_topic), "sitetwin/pods/%s/commands",
             command.target_pod_id);
    if (strcmp(topic, expected_topic) != 0 || strcmp(command.target_pod_id, ENVIRONMENT_POD_ID) != 0) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "command_type");
    command.command_type = parse_command_type(cJSON_IsString(item) ? item->valuestring : NULL);
    item = cJSON_GetObjectItemCaseSensitive(root, "target");
    command.target = parse_command_target(cJSON_IsString(item) ? item->valuestring : NULL);
    if (json_u64(root, "command_id", &command.command_id) != 0 ||
        json_u64(root, "issued_at_ms", &command.issued_at_ms) != 0 ||
        json_u64(root, "expires_at_ms", &command.expires_at_ms) != 0 ||
        command.command_type == 0 || command.target == 0) {
        goto done;
    }
    if (json_u32(root, "valid_for_ms", &command.valid_for_ms) != 0 ||
        command.valid_for_ms == 0U || command.valid_for_ms > 60000U) {
        goto done;
    }
    if (json_u32(root, "config_revision", &command.config_revision) != 0) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "source");
    if (!cJSON_IsString(item) || strcmp(item->valuestring, "thingsboard") != 0) {
        goto done;
    }
    command.source = ST_COMMAND_SOURCE_THINGSBOARD;
    parameters = cJSON_GetObjectItemCaseSensitive(root, "parameters");
    if (!cJSON_IsObject(parameters)) {
        goto done;
    }
    item = cJSON_GetObjectItemCaseSensitive(parameters, "value");
    if (cJSON_IsNumber(item)) command.value = (float)item->valuedouble;
    item = cJSON_GetObjectItemCaseSensitive(parameters, "duration_ms");
    if (item != NULL) {
        uint64_t duration_ms;
        if (!cJSON_IsNumber(item) || item->valuedouble < 0.0 ||
            item->valuedouble > (double)UINT32_MAX) {
            goto done;
        }
        duration_ms = (uint64_t)item->valuedouble;
        if ((double)duration_ms != item->valuedouble) {
            goto done;
        }
        command.duration_ms = (uint32_t)duration_ms;
    }
    if (command.expires_at_ms <= command.issued_at_ms ||
        st_command_encode(&command, payload, sizeof(payload), &payload_length) != 0) {
        goto done;
    }
    memset(&header, 0, sizeof(header));
    header.version = ST_GATEWAY_FRAME_VERSION;
    header.message_type = ST_GATEWAY_MESSAGE_COMMAND;
    header.payload_length = (uint16_t)payload_length;
    header.sequence = (uint32_t)command.command_id;
    if (uart_link_send_frame(&header, payload) != 0) {
        goto done;
    }
    memset(&queued_ack, 0, sizeof(queued_ack));
    queued_ack.command_id = command.command_id;
    strcpy(queued_ack.pod_id, command.target_pod_id);
    queued_ack.status = ST_COMMAND_STATUS_QUEUED;
    queued_ack.reason = ST_COMMAND_REASON_NONE;
    queued_ack.timestamp_ms = (uint64_t)(esp_timer_get_time() / 1000);
    (void)publish_command_ack(&queued_ack);
    result = 0;
done:
    cJSON_Delete(root);
    return result;
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
