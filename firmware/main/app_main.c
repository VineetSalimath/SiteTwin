#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_zigbee.h"

#include "sitetwin/command.h"
#include "sitetwin/contracts.h"
#include "sitetwin/espidf_actuation.h"
#include "sitetwin/espidf_i2c_bus.h"
#include "sitetwin/gateway_frame.h"
#include "sitetwin/gateway_identity.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pod_runtime.h"
#include "sitetwin/scd41.h"
#include "sitetwin/sgp40.h"
#include "sitetwin/sht41.h"
#include "sitetwin/zigbee_payload.h"

#define ST_ZIGBEE_CLUSTER_ID 0xFC00U
#define ST_ZIGBEE_ENDPOINT 1U
#define ST_ZIGBEE_TELEMETRY_COMMAND 0x01U
#define ST_ZIGBEE_POD_COMMAND 0x02U
#define ST_ZIGBEE_COMMAND_ACK 0x03U
#define ST_ZIGBEE_STORAGE_PARTITION "zb_storage"
#define ST_GATEWAY_ADDRESS 0x0000U
#define ST_GATEWAY_UART_PORT UART_NUM_1
#define ST_GATEWAY_UART_RX_BUFFER_SIZE 256U
#define ST_GATEWAY_UART_TX_BUFFER_SIZE 1024U

static const char *TAG = "sitetwin_zigbee";
#if SITETWIN_GATEWAY_ROLE_BUILD
static st_gateway_runtime_t gateway_runtime;
#endif
static volatile bool pod_joined;

#if SITETWIN_GATEWAY_ROLE_BUILD
static uint16_t environment_pod_short_address = 0xFFFFU;
#else
static QueueHandle_t pod_command_queue;
static QueueHandle_t pod_command_ack_queue;
static QueueHandle_t pod_co2_observation_queue;
typedef struct {
    st_command_t command;
    uint64_t received_at_ms;
} st_queued_pod_command_t;
typedef struct {
    float value_ppm;
    uint32_t quality_flags;
} st_queued_co2_observation_t;
#endif

#if !SITETWIN_GATEWAY_ROLE_BUILD
#ifdef CONFIG_SITETWIN_SHT41_ENABLE_INTERNAL_PULLUPS
#define ST_SHT41_INTERNAL_PULLUPS_ENABLED true
#else
#define ST_SHT41_INTERNAL_PULLUPS_ENABLED false
#endif

static st_pod_runtime_t pod_runtime;
static st_espidf_i2c_master_bus_t environment_i2c_bus;
static st_espidf_i2c_device_t sht41_i2c_device;
static st_espidf_i2c_device_t scd41_i2c_device;
static st_espidf_i2c_device_t sgp40_i2c_device;
static st_sht41_t sht41_sensor;
static st_scd41_t scd41_sensor;
static st_sgp40_t sgp40_sensor;
static st_module_instance_t sht41_module;
static st_module_instance_t scd41_module;
static st_module_instance_t sgp40_module;
static st_command_runtime_t command_runtime;
static st_espidf_actuation_t actuation_service;
static bool pod_sensor_runtime_ready;
static bool pod_control_runtime_ready;
#endif

#if SITETWIN_GATEWAY_ROLE_BUILD
static void gateway_uart_rx_task(void *context);

static void gateway_uart_init(void)
{
    const uart_config_t config = {
        .baud_rate = CONFIG_SITETWIN_GATEWAY_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ST_GATEWAY_UART_PORT, ST_GATEWAY_UART_RX_BUFFER_SIZE,
                                        ST_GATEWAY_UART_TX_BUFFER_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ST_GATEWAY_UART_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(ST_GATEWAY_UART_PORT, CONFIG_SITETWIN_GATEWAY_UART_TX_PIN,
                                 CONFIG_SITETWIN_GATEWAY_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART bridge ready: UART%d TX GPIO%d RX GPIO%d at %d baud",
             ST_GATEWAY_UART_PORT, CONFIG_SITETWIN_GATEWAY_UART_TX_PIN,
             CONFIG_SITETWIN_GATEWAY_UART_RX_PIN, CONFIG_SITETWIN_GATEWAY_UART_BAUD_RATE);
    ESP_ERROR_CHECK(xTaskCreate(gateway_uart_rx_task, "st_uart_down", 4096, NULL, 5, NULL) ==
                            pdPASS
                        ? ESP_OK
                        : ESP_FAIL);
}

static int gateway_uart_forward(st_gateway_message_type_t message_type,
                                uint16_t source_address, const uint8_t *payload,
                                uint16_t payload_length, uint32_t boot_id,
                                uint32_t sequence)
{
    st_gateway_frame_header_t frame_header = {
        .version = ST_GATEWAY_FRAME_VERSION,
        .message_type = message_type,
        .payload_length = payload_length,
        .source_address = source_address,
        .boot_id = boot_id,
        .sequence = sequence,
    };
    uint8_t frame[ST_GATEWAY_FRAME_HEADER_SIZE + ST_COMMAND_WIRE_SIZE +
                  ST_GATEWAY_FRAME_CRC_SIZE];
    size_t frame_length;

    if (st_gateway_frame_encode(&frame_header, payload, frame, sizeof(frame), &frame_length) != 0) {
        return -1;
    }
    int written = uart_write_bytes(ST_GATEWAY_UART_PORT, frame, frame_length);
    return written == (int)frame_length ? 0 : -1;
}

static int gateway_send_command_downlink(const uint8_t *payload, uint16_t payload_length)
{
    st_command_t decoded;
    ezb_zcl_custom_cluster_cmd_t command;
    int result;
    if (st_command_decode(payload, payload_length, &decoded) != 0 ||
        strcmp(decoded.target_pod_id, ST_POD_1_ID) != 0 ||
        environment_pod_short_address == 0xFFFFU) {
        return -1;
    }
    memset(&command, 0, sizeof(command));
    command.cmd_ctrl.dst_addr = EZB_ADDRESS_SHORT(environment_pod_short_address);
    command.cmd_ctrl.dst_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.src_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.cluster_id = ST_ZIGBEE_CLUSTER_ID;
    command.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_CLI;
    command.cmd_ctrl.fc.dis_default_rsp = true;
    command.cmd_id = ST_ZIGBEE_POD_COMMAND;
    command.data_length = payload_length;
    command.data = (uint8_t *)payload;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    result = ezb_zcl_custom_cluster_cmd_req(&command);
    esp_zigbee_lock_release();
    return result;
}

static void gateway_report_transport_failure(const st_command_t *command)
{
    st_command_ack_t ack;
    uint8_t payload[ST_COMMAND_ACK_WIRE_SIZE];
    size_t length;
    memset(&ack, 0, sizeof(ack));
    ack.command_id = command->command_id;
    memcpy(ack.pod_id, command->target_pod_id, sizeof(ack.pod_id));
    ack.status = ST_COMMAND_STATUS_FAILED;
    ack.reason = ST_COMMAND_REASON_TRANSPORT_FAILED;
    ack.timestamp_ms = (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (st_command_ack_encode(&ack, payload, sizeof(payload), &length) == 0) {
        (void)gateway_uart_forward(ST_GATEWAY_MESSAGE_COMMAND_ACK, 0U, payload,
                                   (uint16_t)length, 0U,
                                   (uint32_t)command->command_id);
    }
}

static void gateway_uart_process_frame(const uint8_t *frame, size_t frame_length)
{
    st_gateway_frame_header_t header;
    const uint8_t *payload;
    st_command_t command;
    if (st_gateway_frame_decode(frame, frame_length, &header, &payload) != 0 ||
        header.version != ST_GATEWAY_FRAME_VERSION ||
        header.message_type != ST_GATEWAY_MESSAGE_COMMAND ||
        st_command_decode(payload, header.payload_length, &command) != 0) {
        ESP_LOGW(TAG, "Rejected invalid UART return frame");
        return;
    }
    if (gateway_send_command_downlink(payload, header.payload_length) != 0) {
        ESP_LOGW(TAG, "Unable to queue command %llu for Environment Pod downlink",
                 (unsigned long long)command.command_id);
        gateway_report_transport_failure(&command);
    } else {
        ESP_LOGI(TAG, "Queued command %llu for Zigbee downlink to 0x%04X",
                 (unsigned long long)command.command_id,
                 environment_pod_short_address);
    }
}

static void gateway_uart_rx_task(void *context)
{
    uint8_t buffer[ST_GATEWAY_FRAME_HEADER_SIZE + ST_COMMAND_WIRE_SIZE +
                   ST_GATEWAY_FRAME_CRC_SIZE];
    size_t used = 0U;
    (void)context;
    for (;;) {
        int received = uart_read_bytes(ST_GATEWAY_UART_PORT, buffer + used,
                                       sizeof(buffer) - used, pdMS_TO_TICKS(100U));
        if (received > 0) {
            used += (size_t)received;
        }
        while (used >= 2U &&
               (buffer[0] != (uint8_t)(ST_GATEWAY_FRAME_START & 0xFFU) ||
                buffer[1] != (uint8_t)(ST_GATEWAY_FRAME_START >> 8U))) {
            memmove(buffer, buffer + 1U, --used);
        }
        if (used >= ST_GATEWAY_FRAME_HEADER_SIZE) {
            uint16_t payload_length = (uint16_t)buffer[4] | ((uint16_t)buffer[5] << 8U);
            size_t expected = ST_GATEWAY_FRAME_HEADER_SIZE + payload_length +
                              ST_GATEWAY_FRAME_CRC_SIZE;
            if (expected > sizeof(buffer)) {
                used = 0U;
            } else if (used >= expected) {
                gateway_uart_process_frame(buffer, expected);
                memmove(buffer, buffer + expected, used - expected);
                used -= expected;
            }
        }
        if (used == sizeof(buffer)) {
            used = 0U;
        }
    }
}
#endif

static void commissioning_retry_task(void *context)
{
    ezb_bdb_comm_mode_mask_t mode = (ezb_bdb_comm_mode_mask_t)(uintptr_t)context;

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(mode);
    esp_zigbee_lock_release();
    vTaskDelete(NULL);
}

static void retry_commissioning(ezb_bdb_comm_mode_mask_t mode)
{
    (void)xTaskCreate(commissioning_retry_task, "st_zb_retry", 3072,
                      (void *)(uintptr_t)mode, 5, NULL);
}

#if SITETWIN_GATEWAY_ROLE_BUILD
static ezb_zcl_status_t gateway_telemetry_handler(const ezb_zcl_cmd_hdr_t *header,
                                                   const uint8_t *payload,
                                                   uint16_t payload_length)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint8_t sensor_slot;
    st_gateway_ingress_result_t result;
#if SITETWIN_GATEWAY_ROLE_BUILD
    st_telemetry_record_t record;
    st_telemetry_record_t unresolved_record;
#endif

    if (header == NULL || payload == NULL ||
        header->cluster_id != ST_ZIGBEE_CLUSTER_ID ||
        EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc) ||
        header->cmd_id != ST_ZIGBEE_TELEMETRY_COMMAND ||
        payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        st_zigbee_telemetry_decode(payload, payload_length,
                                   "unresolved", "unresolved",
                                   &unresolved_record, &sensor_slot) != 0) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }

    if (st_gateway_identity_resolve(header->src_addr.u.short_addr, sensor_slot,
                                    unresolved_record.reading.sensor_kind,
                                    pod_id, sizeof(pod_id),
                                    sensor_id, sizeof(sensor_id)) != 0) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    result = st_gateway_runtime_ingest_zigbee(&gateway_runtime, payload, payload_length,
                                              pod_id, sensor_id);
    ESP_LOGI(TAG, "Telemetry from %s/%s: ingress result %d", pod_id, sensor_id, (int)result);
#if SITETWIN_GATEWAY_ROLE_BUILD
    if (result == ST_GATEWAY_INGRESS_ACCEPTED) {
        if (strcmp(pod_id, ST_POD_1_ID) == 0) {
            environment_pod_short_address = header->src_addr.u.short_addr;
        }
        int decode_result = st_zigbee_telemetry_decode(payload, payload_length, pod_id, sensor_id,
                                                       &record, &sensor_slot);
        if (decode_result == 0) {
            st_gateway_message_type_t type =
                record.record_class == ST_RECORD_HEALTH ? ST_GATEWAY_MESSAGE_HEALTH
                                                        : ST_GATEWAY_MESSAGE_TELEMETRY;
            if (gateway_uart_forward(type, header->src_addr.u.short_addr, payload,
                                     payload_length, record.reading.boot_id,
                                     record.reading.sequence) != 0) {
                ESP_LOGW(TAG, "UART forward failed for %s/%s", pod_id, sensor_id);
            } else {
                /* The Wi-Fi ESP owns onward delivery after a successful UART hand-off.
                 * Remove this record so the coordinator's local queue cannot fill. */
                (void)st_gateway_runtime_next_record(&gateway_runtime, &record);
            }
        }
    }
#endif
    return result == ST_GATEWAY_INGRESS_INVALID ? EZB_ZCL_STATUS_INVALID_FIELD : EZB_ZCL_STATUS_SUCCESS;
}

static ezb_zcl_status_t gateway_command_ack_handler(const ezb_zcl_cmd_hdr_t *header,
                                                     const uint8_t *payload,
                                                     uint16_t payload_length)
{
    st_command_ack_t ack;
    if (header == NULL || payload == NULL ||
        header->cluster_id != ST_ZIGBEE_CLUSTER_ID ||
        EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc) ||
        header->cmd_id != ST_ZIGBEE_COMMAND_ACK ||
        st_command_ack_decode(payload, payload_length, &ack) != 0) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    if (strcmp(ack.pod_id, ST_POD_1_ID) == 0) {
        environment_pod_short_address = header->src_addr.u.short_addr;
    }
    if (gateway_uart_forward(ST_GATEWAY_MESSAGE_COMMAND_ACK,
                             header->src_addr.u.short_addr, payload, payload_length,
                             0U, (uint32_t)ack.command_id) != 0) {
        ESP_LOGW(TAG, "UART command acknowledgement forward failed for command %llu",
                 (unsigned long long)ack.command_id);
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

static ezb_zcl_status_t gateway_cluster_handler(const ezb_zcl_cmd_hdr_t *header,
                                                 const uint8_t *payload,
                                                 uint16_t payload_length)
{
    if (header != NULL && header->cmd_id == ST_ZIGBEE_COMMAND_ACK) {
        return gateway_command_ack_handler(header, payload, payload_length);
    }
    return gateway_telemetry_handler(header, payload, payload_length);
}
#else
static ezb_zcl_status_t pod_command_handler(const ezb_zcl_cmd_hdr_t *header,
                                             const uint8_t *payload,
                                             uint16_t payload_length)
{
    st_queued_pod_command_t queued;
    if (header == NULL || payload == NULL ||
        header->cluster_id != ST_ZIGBEE_CLUSTER_ID ||
        !EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc) ||
        header->cmd_id != ST_ZIGBEE_POD_COMMAND ||
        st_command_decode(payload, payload_length, &queued.command) != 0) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    queued.received_at_ms = (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (pod_command_queue == NULL ||
        xQueueSend(pod_command_queue, &queued, 0U) != pdPASS) {
        st_command_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.command_id = queued.command.command_id;
        strcpy(ack.pod_id, ST_POD_1_ID);
        ack.status = ST_COMMAND_STATUS_FAILED;
        ack.reason = ST_COMMAND_REASON_QUEUE_FULL;
        if (pod_control_runtime_ready) {
            ack.applied_config_revision = command_runtime.persistent.config.revision;
            ack.config_value = command_runtime.persistent.config.co2_threshold_ppm;
        }
        ack.timestamp_ms = queued.received_at_ms;
        if (pod_command_ack_queue != NULL) {
            (void)xQueueSend(pod_command_ack_queue, &ack, 0U);
        }
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    return EZB_ZCL_STATUS_SUCCESS;
}
#endif

#if SITETWIN_GATEWAY_ROLE_BUILD
static uint8_t gateway_command_discovery(bool is_recv, uint8_t **list)
{
    static uint8_t receive_commands[] = {ST_ZIGBEE_TELEMETRY_COMMAND,
                                         ST_ZIGBEE_COMMAND_ACK};
    static uint8_t send_commands[] = {ST_ZIGBEE_POD_COMMAND};

    *list = is_recv ? receive_commands : send_commands;
    return is_recv ? 2U : 1U;
}
#else
static uint8_t pod_command_discovery(bool is_recv, uint8_t **list)
{
    static uint8_t receive_commands[] = {ST_ZIGBEE_POD_COMMAND};
    static uint8_t send_commands[] = {ST_ZIGBEE_TELEMETRY_COMMAND,
                                      ST_ZIGBEE_COMMAND_ACK};

    *list = is_recv ? receive_commands : send_commands;
    return is_recv ? 1U : 2U;
}
#endif

#if SITETWIN_GATEWAY_ROLE_BUILD
static void gateway_cluster_init(uint8_t endpoint)
{
    const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = ST_ZIGBEE_CLUSTER_ID,
        .cluster_role = EZB_ZCL_CLUSTER_SERVER,
        .process_cmd_cb = gateway_cluster_handler,
        .cmd_disc_cb = gateway_command_discovery,
    };

    (void)endpoint;
    ESP_ERROR_CHECK(ezb_zcl_custom_cluster_handlers_register(&handlers));
}
#else
static void pod_cluster_init(uint8_t endpoint)
{
    const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = ST_ZIGBEE_CLUSTER_ID,
        .cluster_role = EZB_ZCL_CLUSTER_CLIENT,
        .process_cmd_cb = pod_command_handler,
        .cmd_disc_cb = pod_command_discovery,
    };

    (void)endpoint;
    ESP_ERROR_CHECK(ezb_zcl_custom_cluster_handlers_register(&handlers));
}
#endif

static void cluster_deinit(uint8_t endpoint)
{
    (void)endpoint;
}

static void register_site_twin_endpoint(void)
{
    ezb_af_device_desc_t device = ezb_af_create_device_desc();
    ezb_af_ep_desc_t endpoint;
    ezb_zcl_cluster_desc_t basic;
    ezb_zcl_cluster_desc_t custom;
    const ezb_zcl_basic_cluster_server_config_t basic_config = {
        .zcl_version = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    const ezb_zcl_custom_cluster_config_t custom_config = {
        .cluster_id = ST_ZIGBEE_CLUSTER_ID,
#if SITETWIN_GATEWAY_ROLE_BUILD
        .init_func = gateway_cluster_init,
#else
        .init_func = pod_cluster_init,
#endif
        .deinit_func = cluster_deinit,
    };
    const ezb_af_ep_config_t endpoint_config = {
        .ep_id = ST_ZIGBEE_ENDPOINT,
        .app_profile_id = EZB_AF_HA_PROFILE_ID,
        .app_device_id = 0xFF00U,
        .app_device_version = 1U,
    };

    basic = ezb_zcl_basic_create_cluster_desc(&basic_config, EZB_ZCL_CLUSTER_SERVER);
    ESP_ERROR_CHECK(ezb_zcl_basic_cluster_desc_add_attr(
        basic, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)"\x08" "SITETWIN"));
    ESP_ERROR_CHECK(ezb_zcl_basic_cluster_desc_add_attr(
        basic, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
#if SITETWIN_GATEWAY_ROLE_BUILD
        (void *)"\x10" "SITETWIN_GATEWAY"));
#else
        (void *)"\x0C" "SITETWIN_POD"));
#endif
    custom = ezb_zcl_custom_create_cluster_desc(&custom_config,
#if SITETWIN_GATEWAY_ROLE_BUILD
                                                 EZB_ZCL_CLUSTER_SERVER);
#else
                                                 EZB_ZCL_CLUSTER_CLIENT);
#endif
    endpoint = ezb_af_create_endpoint_desc(&endpoint_config);
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(endpoint, basic));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(endpoint, custom));
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(device, endpoint));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(device));
}

static bool zigbee_signal_handler(const ezb_app_signal_t *signal)
{
    ezb_app_signal_type_t type = ezb_app_signal_get_type(signal);

    ESP_LOGI(TAG, "Zigbee signal %d", (int)type);
    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        (void)ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal);
        ESP_LOGI(TAG, "Zigbee startup status %d, factory new=%d", (int)status,
                 ezb_bdb_is_factory_new());
        if (status != EZB_BDB_STATUS_SUCCESS) {
            retry_commissioning(EZB_BDB_MODE_INITIALIZATION);
            break;
        }
#if SITETWIN_GATEWAY_ROLE_BUILD
        if (ezb_bdb_is_factory_new()) {
            (void)ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
        } else {
            ezb_err_t open_result = ezb_bdb_open_network(240);
            ESP_LOGI(TAG, "Gateway restored network; opening joining result %d", (int)open_result);
        }
#else
        if (ezb_bdb_is_factory_new()) {
            (void)ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        } else {
            pod_joined = true;
            ESP_LOGI(TAG, "Pod restored network as 0x%04X", ezb_nwk_get_short_address());
        }
#endif
        break;
    }
    case EZB_BDB_SIGNAL_FORMATION:
#if SITETWIN_GATEWAY_ROLE_BUILD
        if (*(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal) == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Gateway formed network; opening joining for 240 seconds");
            (void)ezb_bdb_open_network(240);
        } else {
            ESP_LOGW(TAG, "Gateway network formation failed with status %d; retrying",
                     (int)*(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal));
            retry_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
        }
#endif
        break;
    case EZB_BDB_SIGNAL_STEERING:
#if !SITETWIN_GATEWAY_ROLE_BUILD
        if (*(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal) == EZB_BDB_STATUS_SUCCESS) {
            pod_joined = true;
            ESP_LOGI(TAG, "Pod joined network as 0x%04X", ezb_nwk_get_short_address());
        } else {
            retry_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
        }
#endif
        break;
    default:
        break;
    }
    return true;
}

#if !SITETWIN_GATEWAY_ROLE_BUILD
#if CONFIG_SITETWIN_SGP40_ENABLED
static int pod_sgp40_compensation(void *context,
                                  uint64_t now_ms,
                                  st_sgp40_compensation_t *compensation)
{
    st_sht41_environment_sample_t environment;
    const st_sht41_t *sht41 = (const st_sht41_t *)context;

    if (compensation == NULL ||
        st_sht41_get_valid_environment(
            sht41, now_ms,
            CONFIG_SITETWIN_SGP40_COMPENSATION_MAXIMUM_AGE_MS,
            &environment) != 0) {
        return -1;
    }
    compensation->temperature_c = environment.temperature_c;
    compensation->humidity_percent = environment.humidity_percent;
    compensation->acquired_at_ms = environment.acquired_at_ms;
    return 0;
}
#endif

static esp_err_t pod_sensor_runtime_init(void)
{
#if CONFIG_SITETWIN_SHT41_ENABLED || CONFIG_SITETWIN_SCD41_ENABLED || CONFIG_SITETWIN_SGP40_ENABLED
    const st_espidf_i2c_master_bus_config_t bus_config = {
        .controller = CONFIG_SITETWIN_SHT41_I2C_CONTROLLER,
        .sda_gpio = CONFIG_SITETWIN_SHT41_I2C_SDA_PIN,
        .scl_gpio = CONFIG_SITETWIN_SHT41_I2C_SCL_PIN,
        .enable_internal_pullups = ST_SHT41_INTERNAL_PULLUPS_ENABLED,
    };
    esp_err_t result;

    st_pod_runtime_init(&pod_runtime, ST_POD_ENVIRONMENT, ST_POD_1_ID, 1U);
    if (st_command_runtime_init(&command_runtime, ST_POD_ENVIRONMENT, ST_POD_1_ID,
                                st_espidf_command_persistence()) != 0) {
        return ESP_FAIL;
    }
    result = st_espidf_actuation_init(&actuation_service,
                                      CONFIG_SITETWIN_ENVIRONMENT_ALERT_LED_GPIO,
                                      CONFIG_SITETWIN_ENVIRONMENT_ALERT_BUZZER_GPIO,
                                      CONFIG_SITETWIN_ENVIRONMENT_ALERT_BUZZER_HZ);
    if (result != ESP_OK) {
        return result;
    }
    pod_control_runtime_ready = true;
    result = st_espidf_i2c_master_bus_init(&environment_i2c_bus, &bus_config);
    if (result != ESP_OK) {
        return result;
    }

#if CONFIG_SITETWIN_SHT41_ENABLED
    {
        static const uint8_t registry_slots[ST_SHT41_CHANNEL_COUNT] = {0U, 1U};
        const st_espidf_i2c_target_config_t target_config = {
            .address = CONFIG_SITETWIN_SHT41_I2C_ADDRESS,
            .clock_hz = CONFIG_SITETWIN_SHT41_I2C_CLOCK_HZ,
            .timeout_ms = CONFIG_SITETWIN_SHT41_I2C_TIMEOUT_MS,
        };
        st_sht41_config_t sensor_config;

        result = st_espidf_i2c_device_init_on_bus(&sht41_i2c_device,
                                                  &environment_i2c_bus,
                                                  &target_config);
        if (result != ESP_OK) {
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return result;
        }

        memset(&sensor_config, 0, sizeof(sensor_config));
        sensor_config.bus = st_espidf_i2c_bus(&sht41_i2c_device);
        sensor_config.address = CONFIG_SITETWIN_SHT41_I2C_ADDRESS;
        sensor_config.sample_interval_ms = CONFIG_SITETWIN_SHT41_SAMPLE_INTERVAL_MS;
        sensor_config.cache_validity_ms = CONFIG_SITETWIN_SHT41_CACHE_VALIDITY_MS;
        sensor_config.temperature_sensor_id = "sht41_temperature";
        sensor_config.humidity_sensor_id = "sht41_humidity";
        if (st_sht41_init(&sht41_sensor, &sensor_config) != 0 ||
            st_module_instance_init(&sht41_module,
                                    st_sht41_module_driver(&sht41_sensor),
                                    ST_SHT41_CHANNEL_COUNT) != 0 ||
            st_module_instance_attach(&sht41_module, &pod_runtime.registry,
                                      registry_slots, ST_SHT41_CHANNEL_COUNT) != 0) {
            st_espidf_i2c_device_deinit(&sht41_i2c_device);
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return ESP_FAIL;
        }
    }
#endif

#if CONFIG_SITETWIN_SCD41_ENABLED
    {
        static const uint8_t registry_slots[ST_SCD41_CHANNEL_COUNT] = {2U};
        const st_espidf_i2c_target_config_t target_config = {
            .address = CONFIG_SITETWIN_SCD41_I2C_ADDRESS,
            .clock_hz = CONFIG_SITETWIN_SHT41_I2C_CLOCK_HZ,
            .timeout_ms = CONFIG_SITETWIN_SHT41_I2C_TIMEOUT_MS,
        };
        st_scd41_config_t sensor_config;

        result = st_espidf_i2c_device_init_on_bus(&scd41_i2c_device,
                                                  &environment_i2c_bus,
                                                  &target_config);
        if (result != ESP_OK) {
#if CONFIG_SITETWIN_SHT41_ENABLED
            st_module_instance_detach(&sht41_module, &pod_runtime.registry, 0U);
            st_espidf_i2c_device_deinit(&sht41_i2c_device);
#endif
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return result;
        }

        memset(&sensor_config, 0, sizeof(sensor_config));
        sensor_config.bus = st_espidf_i2c_bus(&scd41_i2c_device);
        sensor_config.address = CONFIG_SITETWIN_SCD41_I2C_ADDRESS;
#ifdef CONFIG_SITETWIN_SCD41_MODE_LOW_POWER_PERIODIC
        sensor_config.measurement_mode = ST_SCD41_MODE_LOW_POWER_PERIODIC;
#else
        sensor_config.measurement_mode = ST_SCD41_MODE_PERIODIC;
#endif
        sensor_config.poll_interval_ms = CONFIG_SITETWIN_SCD41_POLL_INTERVAL_MS;
        sensor_config.co2_sensor_id = "scd41_co2";
        if (st_scd41_init(&scd41_sensor, &sensor_config) != 0 ||
            st_module_instance_init(&scd41_module,
                                    st_scd41_module_driver(&scd41_sensor),
                                    ST_SCD41_CHANNEL_COUNT) != 0 ||
            st_module_instance_attach(&scd41_module, &pod_runtime.registry,
                                      registry_slots, ST_SCD41_CHANNEL_COUNT) != 0) {
            st_espidf_i2c_device_deinit(&scd41_i2c_device);
#if CONFIG_SITETWIN_SHT41_ENABLED
            st_module_instance_detach(&sht41_module, &pod_runtime.registry, 0U);
            st_espidf_i2c_device_deinit(&sht41_i2c_device);
#endif
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return ESP_FAIL;
        }
    }
#endif

#if CONFIG_SITETWIN_SGP40_ENABLED
    {
        static const uint8_t registry_slots[ST_SGP40_CHANNEL_COUNT] = {3U};
        const st_espidf_i2c_target_config_t target_config = {
            .address = CONFIG_SITETWIN_SGP40_I2C_ADDRESS,
            .clock_hz = CONFIG_SITETWIN_SHT41_I2C_CLOCK_HZ,
            .timeout_ms = CONFIG_SITETWIN_SHT41_I2C_TIMEOUT_MS,
        };
        st_sgp40_config_t sensor_config;

        result = st_espidf_i2c_device_init_on_bus(&sgp40_i2c_device,
                                                  &environment_i2c_bus,
                                                  &target_config);
        if (result != ESP_OK) {
#if CONFIG_SITETWIN_SCD41_ENABLED
            st_module_instance_detach(&scd41_module, &pod_runtime.registry, 0U);
            st_espidf_i2c_device_deinit(&scd41_i2c_device);
#endif
#if CONFIG_SITETWIN_SHT41_ENABLED
            st_module_instance_detach(&sht41_module, &pod_runtime.registry, 0U);
            st_espidf_i2c_device_deinit(&sht41_i2c_device);
#endif
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return result;
        }

        memset(&sensor_config, 0, sizeof(sensor_config));
        sensor_config.bus = st_espidf_i2c_bus(&sgp40_i2c_device);
        sensor_config.address = CONFIG_SITETWIN_SGP40_I2C_ADDRESS;
#ifdef CONFIG_SITETWIN_SGP40_ALGORITHM_INTERVAL_10S
        sensor_config.algorithm_interval_ms = 10000U;
#else
        sensor_config.algorithm_interval_ms = 1000U;
#endif
        sensor_config.compensation_maximum_age_ms =
            CONFIG_SITETWIN_SGP40_COMPENSATION_MAXIMUM_AGE_MS;
        sensor_config.compensation_provider = pod_sgp40_compensation;
        sensor_config.compensation_context = &sht41_sensor;
        sensor_config.voc_index_sensor_id = "sgp40_voc_index";
        if (st_sgp40_init(&sgp40_sensor, &sensor_config) != 0 ||
            st_module_instance_init(&sgp40_module,
                                    st_sgp40_module_driver(&sgp40_sensor),
                                    ST_SGP40_CHANNEL_COUNT) != 0 ||
            st_module_instance_attach(&sgp40_module, &pod_runtime.registry,
                                      registry_slots, ST_SGP40_CHANNEL_COUNT) != 0) {
            st_espidf_i2c_device_deinit(&sgp40_i2c_device);
#if CONFIG_SITETWIN_SCD41_ENABLED
            st_module_instance_detach(&scd41_module, &pod_runtime.registry, 0U);
            st_espidf_i2c_device_deinit(&scd41_i2c_device);
#endif
#if CONFIG_SITETWIN_SHT41_ENABLED
            st_module_instance_detach(&sht41_module, &pod_runtime.registry, 0U);
            st_espidf_i2c_device_deinit(&sht41_i2c_device);
#endif
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return ESP_FAIL;
        }
    }
#endif

    ESP_LOGI(TAG, "Environment I2C ready on controller %d SDA GPIO%d SCL GPIO%d",
             CONFIG_SITETWIN_SHT41_I2C_CONTROLLER,
             CONFIG_SITETWIN_SHT41_I2C_SDA_PIN,
             CONFIG_SITETWIN_SHT41_I2C_SCL_PIN);
#if CONFIG_SITETWIN_SHT41_ENABLED
    ESP_LOGI(TAG, "SHT41 runtime attached at address 0x%02X",
             CONFIG_SITETWIN_SHT41_I2C_ADDRESS);
#endif
#if CONFIG_SITETWIN_SCD41_ENABLED
    ESP_LOGI(TAG, "SCD41 runtime attached at address 0x%02X in %s periodic mode",
             CONFIG_SITETWIN_SCD41_I2C_ADDRESS,
#ifdef CONFIG_SITETWIN_SCD41_MODE_LOW_POWER_PERIODIC
             "low-power");
#else
             "standard");
#endif
#endif
#if CONFIG_SITETWIN_SGP40_ENABLED
    ESP_LOGI(TAG, "SGP40 runtime attached at address 0x%02X with %u ms VOC algorithm cadence",
             CONFIG_SITETWIN_SGP40_I2C_ADDRESS,
#ifdef CONFIG_SITETWIN_SGP40_ALGORITHM_INTERVAL_10S
             10000U);
#else
             1000U);
#endif
#endif
    return ESP_OK;
#else
    st_pod_runtime_init(&pod_runtime, ST_POD_ENVIRONMENT, ST_POD_1_ID, 1U);
    if (st_command_runtime_init(&command_runtime, ST_POD_ENVIRONMENT, ST_POD_1_ID,
                                st_espidf_command_persistence()) == 0 &&
        st_espidf_actuation_init(&actuation_service,
                                 CONFIG_SITETWIN_ENVIRONMENT_ALERT_LED_GPIO,
                                 CONFIG_SITETWIN_ENVIRONMENT_ALERT_BUZZER_GPIO,
                                 CONFIG_SITETWIN_ENVIRONMENT_ALERT_BUZZER_HZ) == ESP_OK) {
        pod_control_runtime_ready = true;
    }
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static uint8_t pod_sensor_slot(const st_telemetry_record_t *record)
{
    switch (record->reading.sensor_kind) {
    case ST_SENSOR_RELATIVE_HUMIDITY_PERCENT:
        return 1U;
    case ST_SENSOR_CO2_PPM:
        return 2U;
    case ST_SENSOR_VOC_INDEX:
        return 3U;
    default:
        return 0U;
    }
}

static int pod_send_telemetry(const st_telemetry_record_t *record)
{
    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length;
    ezb_zcl_custom_cluster_cmd_t command;

    if (st_zigbee_telemetry_encode(record, pod_sensor_slot(record), payload,
                                   sizeof(payload), &payload_length) != 0) {
        return -1;
    }

    memset(&command, 0, sizeof(command));
    command.cmd_ctrl.dst_addr = EZB_ADDRESS_SHORT(ST_GATEWAY_ADDRESS);
    command.cmd_ctrl.dst_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.src_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.cluster_id = ST_ZIGBEE_CLUSTER_ID;
    command.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_SRV;
    command.cmd_ctrl.fc.dis_default_rsp = true;
    command.cmd_id = ST_ZIGBEE_TELEMETRY_COMMAND;
    command.data_length = (uint16_t)payload_length;
    command.data = payload;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    int result = ezb_zcl_custom_cluster_cmd_req(&command);
    esp_zigbee_lock_release();
    return result;
}

static int pod_send_command_ack(const st_command_ack_t *ack)
{
    uint8_t payload[ST_COMMAND_ACK_WIRE_SIZE];
    size_t payload_length;
    ezb_zcl_custom_cluster_cmd_t command;
    int result;
    if (st_command_ack_encode(ack, payload, sizeof(payload), &payload_length) != 0) {
        return -1;
    }
    memset(&command, 0, sizeof(command));
    command.cmd_ctrl.dst_addr = EZB_ADDRESS_SHORT(ST_GATEWAY_ADDRESS);
    command.cmd_ctrl.dst_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.src_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.cluster_id = ST_ZIGBEE_CLUSTER_ID;
    command.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_SRV;
    command.cmd_ctrl.fc.dis_default_rsp = true;
    command.cmd_id = ST_ZIGBEE_COMMAND_ACK;
    command.data_length = (uint16_t)payload_length;
    command.data = payload;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    result = ezb_zcl_custom_cluster_cmd_req(&command);
    esp_zigbee_lock_release();
    return result;
}

static void pod_control_task(void *context)
{
    (void)context;
    for (;;) {
        uint64_t now_ms = (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        st_queued_pod_command_t queued;
        st_queued_co2_observation_t observation;

        if (pod_control_runtime_ready) {
            st_command_ack_t transport_ack;
            while (xQueueReceive(pod_command_ack_queue, &transport_ack, 0U) == pdPASS) {
                if (pod_joined && pod_send_command_ack(&transport_ack) != 0) {
                    ESP_LOGW(TAG, "Transport acknowledgement send failed for %llu",
                             (unsigned long long)transport_ack.command_id);
                }
            }
            while (xQueueReceive(pod_command_queue, &queued, 0U) == pdPASS) {
                st_command_ack_t ack;
                st_command_t local_command = queued.command;
                local_command.issued_at_ms = queued.received_at_ms;
                local_command.expires_at_ms = queued.received_at_ms +
                                              queued.command.valid_for_ms;
                if (st_command_runtime_handle(&command_runtime, &local_command,
                                              now_ms, &ack) == 0 && pod_joined) {
                    if (pod_send_command_ack(&ack) != 0) {
                        ESP_LOGW(TAG, "Command acknowledgement send failed for %llu",
                                 (unsigned long long)ack.command_id);
                    }
                }
            }
            while (xQueueReceive(pod_co2_observation_queue, &observation, 0U) == pdPASS) {
                st_command_runtime_observe_co2(&command_runtime,
                                               observation.value_ppm,
                                               observation.quality_flags);
            }
            st_local_actuation_state_t output =
                st_command_runtime_tick(&command_runtime, now_ms);
            if (st_espidf_actuation_apply(&actuation_service, output) != ESP_OK) {
                ESP_LOGW(TAG, "Local output update failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}

static void pod_telemetry_task(void *context)
{
    (void)context;
    for (;;) {
        uint64_t now_ms = (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (pod_sensor_runtime_ready) {
            st_telemetry_record_t record;

            st_pod_runtime_tick(&pod_runtime, now_ms);
#if CONFIG_SITETWIN_SCD41_ENABLED
            if (pod_control_runtime_ready) {
                const st_queued_co2_observation_t observation = {
                    .value_ppm = scd41_sensor.current_co2_ppm,
                    .quality_flags = scd41_sensor.current_has_sample != 0U
                                         ? scd41_sensor.current_quality_flags
                                         : 0U,
                };
                (void)xQueueOverwrite(pod_co2_observation_queue, &observation);
            }
#endif
            if (pod_joined) {
                while (st_pod_runtime_next_telemetry(&pod_runtime, &record) == 0) {
                    ESP_LOGI(TAG, "Sending %s sequence %lu quality 0x%08lX",
                             record.reading.sensor_id,
                             (unsigned long)record.reading.sequence,
                             (unsigned long)record.reading.quality_flags);
                    if (pod_send_telemetry(&record) != 0) {
                        ESP_LOGW(TAG, "Zigbee send request failed for %s",
                                 record.reading.sensor_id);
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}
#endif

static void zigbee_task(void *context)
{
    esp_zigbee_config_t config = {
#if SITETWIN_GATEWAY_ROLE_BUILD
        .device_config = {.device_type = EZB_NWK_DEVICE_TYPE_COORDINATOR, .install_code_policy = false,
                          .zczr_config = {.max_children = 16}},
#else
        .device_config = {.device_type = EZB_NWK_DEVICE_TYPE_END_DEVICE, .install_code_policy = false},
#endif
        .platform_config = {.storage_partition_name = ST_ZIGBEE_STORAGE_PARTITION,
                            .radio_config = {.radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE}},
    };

    (void)context;
    ESP_LOGI(TAG, "Initialising Zigbee stack");
    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    ESP_LOGI(TAG, "Configuring Zigbee commissioning");
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(1UL << CONFIG_SITETWIN_ZIGBEE_CHANNEL));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(0));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zigbee_signal_handler));
    register_site_twin_endpoint();
    ESP_LOGI(TAG, "Starting Zigbee stack on channel %d", CONFIG_SITETWIN_ZIGBEE_CHANNEL);
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition(ST_ZIGBEE_STORAGE_PARTITION));
#if SITETWIN_GATEWAY_ROLE_BUILD
    st_gateway_runtime_init(&gateway_runtime);
    gateway_uart_init();
    ESP_LOGI(TAG, "Starting SiteTwin gateway/coordinator image");
#else
    ESP_LOGI(TAG, "Starting SiteTwin pod/end-device image");
    pod_command_queue = xQueueCreate(8U, sizeof(st_queued_pod_command_t));
    ESP_ERROR_CHECK(pod_command_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    pod_command_ack_queue = xQueueCreate(4U, sizeof(st_command_ack_t));
    ESP_ERROR_CHECK(pod_command_ack_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    pod_co2_observation_queue = xQueueCreate(1U, sizeof(st_queued_co2_observation_t));
    ESP_ERROR_CHECK(pod_co2_observation_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);
    pod_sensor_runtime_ready = pod_sensor_runtime_init() == ESP_OK;
    if (!pod_sensor_runtime_ready) {
        ESP_LOGE(TAG, "Environment sensor runtime initialization failed");
    }
    ESP_ERROR_CHECK(xTaskCreate(pod_control_task, "st_pod_ctl", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(pod_telemetry_task, "st_pod_tx", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
#endif
    ESP_ERROR_CHECK(xTaskCreate(zigbee_task, "st_zigbee", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
}
