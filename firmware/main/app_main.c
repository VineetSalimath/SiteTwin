#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_zigbee.h"

#include "sitetwin/contracts.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/zigbee_payload.h"

#define ST_ZIGBEE_CLUSTER_ID 0xFC00U
#define ST_ZIGBEE_ENDPOINT 1U
#define ST_ZIGBEE_TELEMETRY_COMMAND 0x01U
#define ST_ZIGBEE_STORAGE_PARTITION "zb_storage"
#define ST_GATEWAY_ADDRESS 0x0000U
#define ST_HEARTBEAT_PERIOD_MS 15000U

static const char *TAG = "sitetwin_zigbee";
static st_gateway_runtime_t gateway_runtime;
static volatile bool pod_joined;
static uint32_t heartbeat_sequence;

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

static ezb_zcl_status_t gateway_telemetry_handler(const ezb_zcl_cmd_hdr_t *header,
                                                   const uint8_t *payload,
                                                   uint16_t payload_length)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint8_t sensor_slot;
    st_gateway_ingress_result_t result;

    if (header == NULL || payload == NULL ||
        header->cluster_id != ST_ZIGBEE_CLUSTER_ID ||
        EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc) ||
        header->cmd_id != ST_ZIGBEE_TELEMETRY_COMMAND ||
        payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        st_zigbee_telemetry_sensor_slot(payload, payload_length, &sensor_slot) != 0) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }

    snprintf(pod_id, sizeof(pod_id), "POD_%04X", header->src_addr.u.short_addr);
    snprintf(sensor_id, sizeof(sensor_id), "SLOT_%u", (unsigned int)sensor_slot);
    result = st_gateway_runtime_ingest_zigbee(&gateway_runtime, payload, payload_length,
                                              pod_id, sensor_id);
    ESP_LOGI(TAG, "Telemetry from %s/%s: ingress result %d", pod_id, sensor_id, (int)result);
    return result == ST_GATEWAY_INGRESS_INVALID ? EZB_ZCL_STATUS_INVALID_FIELD : EZB_ZCL_STATUS_SUCCESS;
}

static uint8_t gateway_command_discovery(bool is_recv, uint8_t **list)
{
    static uint8_t receive_commands[] = {ST_ZIGBEE_TELEMETRY_COMMAND};

    *list = is_recv ? receive_commands : NULL;
    return is_recv ? 1U : 0U;
}

static uint8_t pod_command_discovery(bool is_recv, uint8_t **list)
{
    static uint8_t send_commands[] = {ST_ZIGBEE_TELEMETRY_COMMAND};

    *list = is_recv ? NULL : send_commands;
    return is_recv ? 0U : 1U;
}

static void gateway_cluster_init(uint8_t endpoint)
{
    const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = ST_ZIGBEE_CLUSTER_ID,
        .cluster_role = EZB_ZCL_CLUSTER_SERVER,
        .process_cmd_cb = gateway_telemetry_handler,
        .cmd_disc_cb = gateway_command_discovery,
    };

    (void)endpoint;
    ESP_ERROR_CHECK(ezb_zcl_custom_cluster_handlers_register(&handlers));
}

static void pod_cluster_init(uint8_t endpoint)
{
    const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id = ST_ZIGBEE_CLUSTER_ID,
        .cluster_role = EZB_ZCL_CLUSTER_CLIENT,
        .cmd_disc_cb = pod_command_discovery,
    };

    (void)endpoint;
    ESP_ERROR_CHECK(ezb_zcl_custom_cluster_handlers_register(&handlers));
}

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

    switch (type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        (void)ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;
    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *(ezb_bdb_comm_status_t *)ezb_app_signal_get_params(signal);
        if (status != EZB_BDB_STATUS_SUCCESS) {
            retry_commissioning(EZB_BDB_MODE_INITIALIZATION);
            break;
        }
#if SITETWIN_GATEWAY_ROLE_BUILD
        if (ezb_bdb_is_factory_new()) {
            (void)ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_FORMATION);
        } else {
            (void)ezb_bdb_open_network(240);
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
static void pod_telemetry_task(void *context)
{
    (void)context;
    for (;;) {
        uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
        size_t payload_length;
        st_telemetry_record_t record = {0};

        if (pod_joined) {
            record.record_class = ST_RECORD_HEALTH;
            record.priority = ST_PRIORITY_HEALTH;
            record.reading.sensor_kind = ST_SENSOR_UNKNOWN;
            record.reading.unit = ST_UNIT_NONE;
            record.reading.sequence = ++heartbeat_sequence;
            record.reading.boot_id = 1U;
            record.reading.uptime_ms = (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
            record.reading.value = 1.0F;
            record.reading.quality_flags = ST_QUALITY_VALID;
            if (st_zigbee_telemetry_encode(&record, 0U, payload, sizeof(payload), &payload_length) == 0) {
                ezb_zcl_custom_cluster_cmd_t command = {
                    .cmd_ctrl = {
                        .dst_addr = EZB_ADDRESS_SHORT(ST_GATEWAY_ADDRESS),
                        .dst_ep = ST_ZIGBEE_ENDPOINT,
                        .src_ep = ST_ZIGBEE_ENDPOINT,
                        .cluster_id = ST_ZIGBEE_CLUSTER_ID,
                        .fc = {.direction = EZB_ZCL_CMD_DIRECTION_TO_SRV, .dis_default_rsp = true},
                    },
                    .cmd_id = ST_ZIGBEE_TELEMETRY_COMMAND,
                    .data_length = (uint16_t)payload_length,
                    .data = payload,
                };
                esp_zigbee_lock_acquire(portMAX_DELAY);
                ESP_LOGI(TAG, "Sending SiteTwin health sequence %lu", (unsigned long)record.reading.sequence);
                (void)ezb_zcl_custom_cluster_cmd_req(&command);
                esp_zigbee_lock_release();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(ST_HEARTBEAT_PERIOD_MS));
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
    ESP_ERROR_CHECK(esp_zigbee_init(&config));
    ezb_aps_secur_enable_distributed_security(false);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(1UL << CONFIG_SITETWIN_ZIGBEE_CHANNEL));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(0));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(zigbee_signal_handler));
    register_site_twin_endpoint();
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition(ST_ZIGBEE_STORAGE_PARTITION));
#if SITETWIN_GATEWAY_ROLE_BUILD
    st_gateway_runtime_init(&gateway_runtime);
    ESP_LOGI(TAG, "Starting SiteTwin gateway/coordinator image");
#else
    ESP_LOGI(TAG, "Starting SiteTwin pod/end-device image");
    ESP_ERROR_CHECK(xTaskCreate(pod_telemetry_task, "st_pod_tx", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
#endif
    ESP_ERROR_CHECK(xTaskCreate(zigbee_task, "st_zigbee", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
}
