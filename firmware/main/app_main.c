#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_zigbee.h"

#include "sitetwin/adxl345.h"
#include "sitetwin/bh1750.h"
#include "sitetwin/board_port.h"
#include "sitetwin/board_port_manager.h"
#include "sitetwin/command.h"
#include "sitetwin/control_event.h"
#include "sitetwin/contracts.h"
#include "sitetwin/ds18b20.h"
#include "sitetwin/espidf_actuation.h"
#include "sitetwin/espidf_final_pcb_board.h"
#include "sitetwin/espidf_i2c_bus.h"
#include "sitetwin/espidf_muxed_data_common.h"
#include "sitetwin/espidf_onewire_bus.h"
#include "sitetwin/espidf_shared_i2c_bus.h"
#include "sitetwin/final_pcb_board.h"
#include "sitetwin/gateway_frame.h"
#include "sitetwin/gateway_identity.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/gateway_state.h"
#include "sitetwin/hotswap_module_binding.h"
#include "sitetwin/hotswap_zigbee_slot.h"
#include "sitetwin/ina219.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pir.h"
#include "sitetwin/pod_runtime.h"
#include "sitetwin/reed.h"
#include "sitetwin/scd41.h"
#include "sitetwin/sgp40.h"
#include "sitetwin/sht41.h"
#include "sitetwin/zigbee_payload.h"
#include "control_persistence.h"

#define ST_ZIGBEE_CLUSTER_ID 0xFC00U
#define ST_ZIGBEE_ENDPOINT 1U
#define ST_ZIGBEE_TELEMETRY_COMMAND 0x01U
#define ST_ZIGBEE_POD_COMMAND 0x02U
#define ST_ZIGBEE_COMMAND_ACK 0x03U
#define ST_ZIGBEE_CONTROL_EVENT 0x04U
#define ST_ZIGBEE_STORAGE_PARTITION "zb_storage"
#define ST_GATEWAY_ADDRESS 0x0000U
#define ST_GATEWAY_UART_PORT UART_NUM_1
#define ST_GATEWAY_UART_RX_BUFFER_SIZE 256U
#define ST_GATEWAY_UART_TX_BUFFER_SIZE 1024U
#define ST_GATEWAY_UART_MAX_PAYLOAD ST_CONTROL_EVENT_WIRE_SIZE

static const char *TAG = "sitetwin_zigbee";
#if SITETWIN_GATEWAY_ROLE_BUILD
static st_gateway_runtime_t gateway_runtime;
static st_gateway_state_t gateway_state;
#endif
static volatile bool pod_joined;

static uint64_t monotonic_now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

#if !SITETWIN_GATEWAY_ROLE_BUILD
#ifdef CONFIG_SITETWIN_SHT41_ENABLE_INTERNAL_PULLUPS
#define ST_SHT41_INTERNAL_PULLUPS_ENABLED true
#else
#define ST_SHT41_INTERNAL_PULLUPS_ENABLED false
#endif

static st_pod_runtime_t pod_runtime;
static st_command_runtime_t pod_command_runtime;
static QueueHandle_t pod_command_queue;
static QueueHandle_t pod_command_ack_queue;
/* Alert LED/buzzer driver -- shared code path for whichever single Pod
 * profile this binary is built for (the three profiles are mutually
 * exclusive at compile time, so one set of globals covers all of them;
 * only the Kconfig pin numbers picked at init time differ per profile,
 * see app_main()). */
static st_espidf_local_output_t pod_local_output;
static uint8_t pod_local_output_ready;
typedef struct {
    st_command_t command;
    uint64_t received_at_ms;
} st_queued_pod_command_t;
#if SITETWIN_POD_PROFILE_ACTIVITY_BUILD
#ifdef CONFIG_SITETWIN_ACTIVITY_I2C_INTERNAL_PULLUPS
#define ST_ACTIVITY_I2C_INTERNAL_PULLUPS true
#else
#define ST_ACTIVITY_I2C_INTERNAL_PULLUPS false
#endif
#ifdef CONFIG_SITETWIN_ACTIVITY_PIR_ACTIVE_HIGH
#define ST_ACTIVITY_PIR_ACTIVE_LEVEL 1U
#else
#define ST_ACTIVITY_PIR_ACTIVE_LEVEL 0U
#endif
#ifdef CONFIG_SITETWIN_ACTIVITY_REED_OPEN_HIGH
#define ST_ACTIVITY_REED_OPEN_HIGH 1U
#else
#define ST_ACTIVITY_REED_OPEN_HIGH 0U
#endif

typedef struct {
    const char *sensor_id;
    st_sensor_kind_t kind;
    uint64_t timestamp_ms;
    float value;
} st_pending_pod_event_t;

static st_espidf_i2c_master_bus_t activity_i2c_bus;
static st_espidf_i2c_device_t bh1750_i2c_device;
static st_bh1750_t bh1750_sensor;
static st_module_instance_t bh1750_module;
static st_pir_t pir_sensor;
static st_reed_debounce_t reed_sensor;
static QueueHandle_t activity_gpio_queue;
static QueueHandle_t pod_event_queue;
#elif SITETWIN_POD_PROFILE_EQUIPMENT_BUILD
#ifdef CONFIG_SITETWIN_EQUIPMENT_I2C_INTERNAL_PULLUPS
#define ST_EQUIPMENT_I2C_INTERNAL_PULLUPS true
#else
#define ST_EQUIPMENT_I2C_INTERNAL_PULLUPS false
#endif
#ifdef CONFIG_SITETWIN_DS18B20_INTERNAL_PULLUP
#define ST_DS18B20_INTERNAL_PULLUP true
#else
#define ST_DS18B20_INTERNAL_PULLUP false
#endif

static st_espidf_i2c_master_bus_t equipment_i2c_bus;
static st_espidf_i2c_device_t ina219_i2c_device;
static st_espidf_i2c_device_t adxl345_i2c_device;
static st_espidf_onewire_bus_t ds18b20_onewire_bus;
static st_ina219_t ina219_sensor;
static st_adxl345_t adxl345_sensor;
static st_ds18b20_t ds18b20_sensor;
static st_module_instance_t ina219_module;
static st_module_instance_t adxl345_module;
static st_module_instance_t ds18b20_module;
#else
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
#endif
static bool pod_sensor_runtime_ready;
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
             CONFIG_SITETWIN_GATEWAY_UART_RX_PIN,
             CONFIG_SITETWIN_GATEWAY_UART_BAUD_RATE);
    ESP_ERROR_CHECK(xTaskCreate(gateway_uart_rx_task, "st_uart_down", 4096, NULL,
                                5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
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
    uint8_t frame[ST_GATEWAY_FRAME_HEADER_SIZE + ST_GATEWAY_UART_MAX_PAYLOAD +
                  ST_GATEWAY_FRAME_CRC_SIZE];
    size_t frame_length;

    if (st_gateway_frame_encode(&frame_header, payload, frame, sizeof(frame), &frame_length) != 0) {
        return -1;
    }
    int written = uart_write_bytes(ST_GATEWAY_UART_PORT, frame, frame_length);
    return written == (int)frame_length ? 0 : -1;
}

static int gateway_send_command_downlink(const uint8_t *payload,
                                         uint16_t payload_length)
{
    st_command_t decoded;
    ezb_zcl_custom_cluster_cmd_t command;
    uint16_t short_address;
    int result;

    if (st_command_decode(payload, payload_length, &decoded) != 0 ||
        st_gateway_identity_short_address(decoded.target_pod_id,
                                          &short_address) != 0) {
        return -1;
    }
    memset(&command, 0, sizeof(command));
    command.cmd_ctrl.dst_addr = EZB_ADDRESS_SHORT(short_address);
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
    ack.timestamp_ms = monotonic_now_ms();
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
        ESP_LOGW(TAG, "Rejected invalid UART command frame");
        return;
    }
    if (gateway_send_command_downlink(payload, header.payload_length) != 0) {
        ESP_LOGW(TAG, "Unable to queue command %llu for Zigbee downlink",
                 (unsigned long long)command.command_id);
        gateway_report_transport_failure(&command);
    } else {
        ESP_LOGI(TAG, "Queued command %llu for %s Zigbee downlink",
                 (unsigned long long)command.command_id,
                 command.target_pod_id);
    }
}

static void gateway_uart_rx_task(void *context)
{
    uint8_t buffer[ST_GATEWAY_FRAME_HEADER_SIZE + ST_GATEWAY_UART_MAX_PAYLOAD +
                   ST_GATEWAY_FRAME_CRC_SIZE];
    size_t used = 0U;

    (void)context;
    for (;;) {
        int received = uart_read_bytes(ST_GATEWAY_UART_PORT, buffer + used,
                                       sizeof(buffer) - used,
                                       pdMS_TO_TICKS(100U));
        if (received > 0) {
            used += (size_t)received;
        }
        while (used >= 2U &&
               (buffer[0] != (uint8_t)(ST_GATEWAY_FRAME_START & 0xFFU) ||
                buffer[1] != (uint8_t)(ST_GATEWAY_FRAME_START >> 8U))) {
            memmove(buffer, buffer + 1U, --used);
        }
        if (used >= ST_GATEWAY_FRAME_HEADER_SIZE) {
            uint16_t payload_length = (uint16_t)buffer[4] |
                                      ((uint16_t)buffer[5] << 8U);
            size_t expected = ST_GATEWAY_FRAME_HEADER_SIZE + payload_length +
                              ST_GATEWAY_FRAME_CRC_SIZE;

            if (expected > sizeof(buffer)) {
                memmove(buffer, buffer + 1U, --used);
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
#endif

    st_telemetry_record_t unresolved_record;

    if (header == NULL || payload == NULL ||
        header->cluster_id != ST_ZIGBEE_CLUSTER_ID ||
        EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc) ||
        header->cmd_id != ST_ZIGBEE_TELEMETRY_COMMAND ||
        payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        st_zigbee_telemetry_decode(payload, payload_length, "unresolved",
                                   "unresolved", &unresolved_record,
                                   &sensor_slot) != 0 ||
        st_gateway_identity_resolve(header->src_addr.u.short_addr, sensor_slot,
                                    unresolved_record.reading.sensor_kind,
                                    pod_id, sizeof(pod_id), sensor_id,
                                    sizeof(sensor_id)) != 0) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }

    result = st_gateway_runtime_ingest_zigbee(&gateway_runtime, payload, payload_length,
                                              pod_id, sensor_id);
    ESP_LOGI(TAG, "Telemetry from %s/%s: ingress result %d", pod_id, sensor_id, (int)result);
#if SITETWIN_GATEWAY_ROLE_BUILD
    if (result == ST_GATEWAY_INGRESS_ACCEPTED) {
        int decode_result = st_zigbee_telemetry_decode(payload, payload_length, pod_id, sensor_id,
                                                       &record, &sensor_slot);
        if (decode_result == 0) {
            uint64_t coordinator_now_ms = monotonic_now_ms();
            st_gateway_message_type_t type =
                record.record_class == ST_RECORD_HEALTH ? ST_GATEWAY_MESSAGE_HEALTH
                                                        : ST_GATEWAY_MESSAGE_TELEMETRY;
            if (st_gateway_state_ingest_telemetry(&gateway_state, &record,
                                                  coordinator_now_ms) != 0) {
                ESP_LOGW(TAG, "Gateway-state telemetry capacity reached for %s/%s",
                         pod_id, sensor_id);
            }
            if (gateway_uart_forward(type, header->src_addr.u.short_addr,
                                     payload, payload_length,
                                     record.reading.boot_id,
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
#endif

#if SITETWIN_GATEWAY_ROLE_BUILD
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
    if (gateway_uart_forward(ST_GATEWAY_MESSAGE_COMMAND_ACK,
                             header->src_addr.u.short_addr, payload,
                             payload_length, 0U,
                             (uint32_t)ack.command_id) != 0) {
        ESP_LOGW(TAG, "UART result forward failed for command %llu",
                 (unsigned long long)ack.command_id);
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

static ezb_zcl_status_t gateway_control_event_handler(const ezb_zcl_cmd_hdr_t *header,
                                                       const uint8_t *payload,
                                                       uint16_t payload_length)
{
    st_control_event_t event;
    uint16_t expected_address;
    uint64_t now_ms = monotonic_now_ms();

    if (header == NULL || payload == NULL ||
        header->cluster_id != ST_ZIGBEE_CLUSTER_ID ||
        EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(header->fc) ||
        header->cmd_id != ST_ZIGBEE_CONTROL_EVENT ||
        st_control_event_decode(payload, payload_length, &event) != 0 ||
        st_gateway_identity_short_address(event.pod_id, &expected_address) != 0 ||
        expected_address != header->src_addr.u.short_addr) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    if (event.event_kind == ST_CONTROL_EVENT_ALARM_CONDITION &&
        st_gateway_state_ingest_control(&gateway_state, &event, now_ms) != 0) {
        ESP_LOGW(TAG, "Gateway-state alarm evidence rejected for %s",
                 event.pod_id);
    }
    if (gateway_uart_forward(ST_GATEWAY_MESSAGE_CONTROL_EVENT,
                             header->src_addr.u.short_addr, payload,
                             payload_length, event.boot_id,
                             event.sequence) != 0) {
        ESP_LOGW(TAG, "UART control-event forward failed for %s",
                 event.pod_id);
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

static void gateway_state_task(void *context)
{
    st_control_event_t pending_event;
    bool has_pending_event = false;

    (void)context;
    for (;;) {
        uint8_t payload[ST_CONTROL_EVENT_WIRE_SIZE];
        size_t payload_length;
        uint64_t now_ms = monotonic_now_ms();

        st_gateway_state_tick(&gateway_state, now_ms);
        while (has_pending_event ||
               st_gateway_state_next_event(&gateway_state, &pending_event) == 0) {
            has_pending_event = true;
            if (st_control_event_encode(&pending_event, payload, sizeof(payload),
                                        &payload_length) != 0 ||
                gateway_uart_forward(ST_GATEWAY_MESSAGE_CONTROL_EVENT, 0U,
                                     payload, (uint16_t)payload_length, 0U,
                                     pending_event.sequence) != 0) {
                ESP_LOGW(TAG, "Gateway incident UART forward failed");
                break;
            }
            has_pending_event = false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static ezb_zcl_status_t gateway_cluster_handler(const ezb_zcl_cmd_hdr_t *header,
                                                const uint8_t *payload,
                                                uint16_t payload_length)
{
    if (header != NULL && header->cmd_id == ST_ZIGBEE_COMMAND_ACK) {
        return gateway_command_ack_handler(header, payload, payload_length);
    }
    if (header != NULL && header->cmd_id == ST_ZIGBEE_CONTROL_EVENT) {
        return gateway_control_event_handler(header, payload, payload_length);
    }
    return gateway_telemetry_handler(header, payload, payload_length);
}

static uint8_t gateway_command_discovery(bool is_recv, uint8_t **list)
{
    static uint8_t receive_commands[] = {ST_ZIGBEE_TELEMETRY_COMMAND,
                                         ST_ZIGBEE_COMMAND_ACK,
                                         ST_ZIGBEE_CONTROL_EVENT};
    static uint8_t send_commands[] = {ST_ZIGBEE_POD_COMMAND};

    *list = is_recv ? receive_commands : send_commands;
    return is_recv ? 3U : 1U;
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
    queued.received_at_ms = monotonic_now_ms();
    if (pod_command_queue == NULL ||
        xQueueSend(pod_command_queue, &queued, 0U) != pdPASS) {
        st_command_ack_t ack;

        memset(&ack, 0, sizeof(ack));
        ack.command_id = queued.command.command_id;
        memcpy(ack.pod_id, pod_command_runtime.pod_id, sizeof(ack.pod_id));
        ack.status = ST_COMMAND_STATUS_FAILED;
        ack.reason = ST_COMMAND_REASON_QUEUE_FULL;
        ack.timestamp_ms = queued.received_at_ms;
        if (pod_command_ack_queue != NULL) {
            (void)xQueueSend(pod_command_ack_queue, &ack, 0U);
        }
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

static uint8_t pod_command_discovery(bool is_recv, uint8_t **list)
{
    static uint8_t receive_commands[] = {ST_ZIGBEE_POD_COMMAND};
    static uint8_t send_commands[] = {ST_ZIGBEE_TELEMETRY_COMMAND,
                                      ST_ZIGBEE_COMMAND_ACK,
                                      ST_ZIGBEE_CONTROL_EVENT};

    *list = is_recv ? receive_commands : send_commands;
    return is_recv ? 1U : 3U;
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
#if SITETWIN_POD_PROFILE_ACTIVITY_BUILD
static uint64_t pod_now_ms(void)
{
    return (uint64_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void IRAM_ATTR activity_gpio_isr(void *context)
{
    uint32_t gpio = (uint32_t)(uintptr_t)context;
    BaseType_t task_woken = pdFALSE;

    if (activity_gpio_queue != NULL) {
        (void)xQueueSendFromISR(activity_gpio_queue, &gpio, &task_woken);
    }
    if (task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void queue_pod_event(const char *sensor_id, st_sensor_kind_t kind,
                            uint64_t timestamp_ms, float value)
{
    st_pending_pod_event_t event = {
        .sensor_id = sensor_id,
        .kind = kind,
        .timestamp_ms = timestamp_ms,
        .value = value,
    };

    if (xQueueSend(pod_event_queue, &event, 0U) != pdTRUE) {
        ESP_LOGW(TAG, "Dropping %s event: pod event queue full", sensor_id);
    }
}

static void activity_gpio_task(void *context)
{
    uint32_t changed_gpio;

    (void)context;
    for (;;) {
        st_pir_event_t pir_event;
        st_reed_event_t reed_event;
        uint64_t now_ms;
        TickType_t wait_ticks = st_pir_is_stabilized(&pir_sensor) != 0U
                                    ? portMAX_DELAY
                                    : pdMS_TO_TICKS(100U);
        BaseType_t edge_received;

        /* During the five-second PIR warm-up, a short timeout establishes a
         * clean post-warm baseline. Steady-state operation blocks until a
         * GPIO edge, so the ISR remains a wake-only path without continuous
         * polling. */
        edge_received = xQueueReceive(activity_gpio_queue, &changed_gpio, wait_ticks);
        now_ms = pod_now_ms();
        if (st_pir_process_level(&pir_sensor, now_ms,
                                 (uint8_t)gpio_get_level(CONFIG_SITETWIN_ACTIVITY_PIR_GPIO),
                                 &pir_event) == 1) {
            ESP_LOGI(TAG, "PIR motion detected at %llu ms (event %lu)",
                     (unsigned long long)pir_event.detected_at_ms,
                     (unsigned long)pir_event.event_count);
            queue_pod_event("pir_motion", ST_SENSOR_MOTION,
                            pir_event.detected_at_ms, 1.0F);
        }
        if (st_reed_debounce_update(
                &reed_sensor, now_ms,
                (uint8_t)gpio_get_level(CONFIG_SITETWIN_ACTIVITY_REED_GPIO),
                &reed_event) == 1) {
            queue_pod_event("reed_contact", ST_SENSOR_CONTACT,
                            reed_event.confirmed_at_ms,
                            reed_event.level == ST_REED_OPEN ? 1.0F : 0.0F);
        }
        if (edge_received == pdTRUE &&
            changed_gpio == CONFIG_SITETWIN_ACTIVITY_REED_GPIO) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_SITETWIN_ACTIVITY_REED_DEBOUNCE_MS));
            now_ms = pod_now_ms();
            if (st_reed_debounce_update(
                    &reed_sensor, now_ms,
                    (uint8_t)gpio_get_level(CONFIG_SITETWIN_ACTIVITY_REED_GPIO),
                    &reed_event) == 1) {
                queue_pod_event("reed_contact", ST_SENSOR_CONTACT,
                                reed_event.confirmed_at_ms,
                                reed_event.level == ST_REED_OPEN ? 1.0F : 0.0F);
            }
        }
    }
}

static esp_err_t activity_gpio_init(void)
{
    gpio_config_t config;
    esp_err_t result;
    uint64_t now_ms = pod_now_ms();
    const st_pir_config_t pir_config = {
        .stabilization_ms = CONFIG_SITETWIN_ACTIVITY_PIR_STABILIZATION_MS,
        .retrigger_suppression_ms = CONFIG_SITETWIN_ACTIVITY_PIR_RETRIGGER_MS,
        .active_level = ST_ACTIVITY_PIR_ACTIVE_LEVEL,
        .sensor_id = "pir_motion",
    };
    const st_reed_config_t reed_config = {
        .debounce_ms = CONFIG_SITETWIN_ACTIVITY_REED_DEBOUNCE_MS,
        .open_when_raw_high = ST_ACTIVITY_REED_OPEN_HIGH,
    };

    activity_gpio_queue = xQueueCreate(16U, sizeof(uint32_t));
    pod_event_queue = xQueueCreate(16U, sizeof(st_pending_pod_event_t));
    if (activity_gpio_queue == NULL || pod_event_queue == NULL ||
        st_pir_init(&pir_sensor, &pir_config, now_ms) != 0 ||
        st_reed_debounce_init(&reed_sensor, &reed_config) != 0) {
        return ESP_ERR_NO_MEM;
    }

    memset(&config, 0, sizeof(config));
    config.pin_bit_mask = 1ULL << CONFIG_SITETWIN_ACTIVITY_PIR_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_ANYEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "PIR GPIO configuration failed");

    memset(&config, 0, sizeof(config));
    config.pin_bit_mask = 1ULL << CONFIG_SITETWIN_ACTIVITY_REED_GPIO;
    config.mode = GPIO_MODE_INPUT;
#ifdef CONFIG_SITETWIN_ACTIVITY_REED_INTERNAL_PULLUP
    config.pull_up_en = GPIO_PULLUP_ENABLE;
#else
    config.pull_up_en = GPIO_PULLUP_DISABLE;
#endif
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_ANYEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "Reed GPIO configuration failed");

    result = gpio_install_isr_service(0);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(CONFIG_SITETWIN_ACTIVITY_PIR_GPIO, activity_gpio_isr,
                             (void *)(uintptr_t)CONFIG_SITETWIN_ACTIVITY_PIR_GPIO),
        TAG, "PIR ISR registration failed");
    ESP_RETURN_ON_ERROR(
        gpio_isr_handler_add(CONFIG_SITETWIN_ACTIVITY_REED_GPIO, activity_gpio_isr,
                             (void *)(uintptr_t)CONFIG_SITETWIN_ACTIVITY_REED_GPIO),
        TAG, "Reed ISR registration failed");

    (void)st_pir_process_level(
        &pir_sensor, now_ms,
        (uint8_t)gpio_get_level(CONFIG_SITETWIN_ACTIVITY_PIR_GPIO), NULL);
    (void)st_reed_debounce_update(
        &reed_sensor, now_ms,
        (uint8_t)gpio_get_level(CONFIG_SITETWIN_ACTIVITY_REED_GPIO), NULL);
    if (xTaskCreate(activity_gpio_task, "st_activity_gpio", 3072, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif

#if !SITETWIN_POD_PROFILE_ACTIVITY_BUILD && !SITETWIN_POD_PROFILE_EQUIPMENT_BUILD && \
    CONFIG_SITETWIN_SGP40_ENABLED
static int pod_sgp40_compensation(void *context, uint64_t now_ms,
                                  st_sgp40_compensation_t *compensation)
{
    st_sht41_environment_sample_t environment;
    const st_sht41_t *sht41 = (const st_sht41_t *)context;

    if (compensation == NULL ||
        st_sht41_get_valid_environment(sht41, now_ms,
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
#if SITETWIN_POD_PROFILE_ACTIVITY_BUILD
    static const uint8_t registry_slots[ST_BH1750_CHANNEL_COUNT] = {0U};
    const st_espidf_i2c_master_bus_config_t bus_config = {
        .controller = CONFIG_SITETWIN_ACTIVITY_I2C_CONTROLLER,
        .sda_gpio = CONFIG_SITETWIN_ACTIVITY_I2C_SDA_PIN,
        .scl_gpio = CONFIG_SITETWIN_ACTIVITY_I2C_SCL_PIN,
        .enable_internal_pullups = ST_ACTIVITY_I2C_INTERNAL_PULLUPS,
    };
    const st_espidf_i2c_target_config_t target_config = {
        .address = CONFIG_SITETWIN_BH1750_I2C_ADDRESS,
        .clock_hz = CONFIG_SITETWIN_ACTIVITY_I2C_CLOCK_HZ,
        .timeout_ms = CONFIG_SITETWIN_ACTIVITY_I2C_TIMEOUT_MS,
    };
    st_bh1750_config_t sensor_config;
    esp_err_t result;

    st_pod_runtime_init(&pod_runtime, ST_POD_ACTIVITY_ACCESS, "POD_6647", 2U);
    result = st_espidf_i2c_master_bus_init(&activity_i2c_bus, &bus_config);
    if (result != ESP_OK) {
        return result;
    }
    result = st_espidf_i2c_device_init_on_bus(&bh1750_i2c_device,
                                               &activity_i2c_bus, &target_config);
    if (result != ESP_OK) {
        return result;
    }
    memset(&sensor_config, 0, sizeof(sensor_config));
    sensor_config.bus = st_espidf_i2c_bus(&bh1750_i2c_device);
    sensor_config.address = CONFIG_SITETWIN_BH1750_I2C_ADDRESS;
    sensor_config.sample_interval_ms = CONFIG_SITETWIN_BH1750_SAMPLE_INTERVAL_MS;
    sensor_config.cache_validity_ms = 250U;
    sensor_config.illuminance_sensor_id = "bh1750_illuminance";
    if (st_bh1750_init(&bh1750_sensor, &sensor_config) != 0 ||
        st_module_instance_init(&bh1750_module,
                                st_bh1750_module_driver(&bh1750_sensor),
                                ST_BH1750_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&bh1750_module, &pod_runtime.registry,
                                  registry_slots, ST_BH1750_CHANNEL_COUNT) != 0) {
        return ESP_FAIL;
    }
    ESP_RETURN_ON_ERROR(activity_gpio_init(), TAG, "Activity GPIO runtime failed");
    ESP_LOGI(TAG,
             "Activity Pod ready: BH1750 0x%02X, SR505 GPIO%d, reed GPIO%d",
             CONFIG_SITETWIN_BH1750_I2C_ADDRESS,
             CONFIG_SITETWIN_ACTIVITY_PIR_GPIO,
             CONFIG_SITETWIN_ACTIVITY_REED_GPIO);
    return ESP_OK;
#elif SITETWIN_POD_PROFILE_EQUIPMENT_BUILD
    static const uint8_t ina_slots[ST_INA219_CHANNEL_COUNT] = {0U, 1U};
    static const uint8_t adxl_slots[ST_ADXL345_CHANNEL_COUNT] = {2U};
    static const uint8_t ds18b20_slots[ST_DS18B20_CHANNEL_COUNT] = {3U};
    const st_espidf_i2c_master_bus_config_t bus_config = {
        .controller = CONFIG_SITETWIN_EQUIPMENT_I2C_CONTROLLER,
        .sda_gpio = CONFIG_SITETWIN_EQUIPMENT_I2C_SDA_PIN,
        .scl_gpio = CONFIG_SITETWIN_EQUIPMENT_I2C_SCL_PIN,
        .enable_internal_pullups = ST_EQUIPMENT_I2C_INTERNAL_PULLUPS,
    };
    const st_espidf_i2c_target_config_t ina_target = {
        .address = CONFIG_SITETWIN_INA219_I2C_ADDRESS,
        .clock_hz = CONFIG_SITETWIN_EQUIPMENT_I2C_CLOCK_HZ,
        .timeout_ms = CONFIG_SITETWIN_EQUIPMENT_I2C_TIMEOUT_MS,
    };
    const st_espidf_i2c_target_config_t adxl_target = {
        .address = CONFIG_SITETWIN_ADXL345_I2C_ADDRESS,
        .clock_hz = CONFIG_SITETWIN_EQUIPMENT_I2C_CLOCK_HZ,
        .timeout_ms = CONFIG_SITETWIN_EQUIPMENT_I2C_TIMEOUT_MS,
    };
    const st_espidf_onewire_bus_config_t onewire_config = {
        /* Fixed Pod 3 prototype path only. The final Type 5 DS18B20 route is
         * CD74HC4052M96 -> DATA_COMMON/GPIO3 and remains feature-gated. */
        .gpio = CONFIG_SITETWIN_DS18B20_GPIO,
        .enable_internal_pullup = ST_DS18B20_INTERNAL_PULLUP,
        .max_rx_bytes = ST_DS18B20_SCRATCHPAD_SIZE,
    };
    st_ina219_config_t ina_config;
    st_adxl345_config_t adxl_config;
    st_ds18b20_config_t ds18b20_config;
    esp_err_t result;

    st_pod_runtime_init(&pod_runtime, ST_POD_EQUIPMENT, "POD_1FBA", 3U);
    result = st_espidf_i2c_master_bus_init(&equipment_i2c_bus, &bus_config);
    if (result != ESP_OK) {
        return result;
    }
    ESP_RETURN_ON_ERROR(
        st_espidf_i2c_device_init_on_bus(&ina219_i2c_device,
                                         &equipment_i2c_bus, &ina_target),
        TAG, "INA219 I2C registration failed");
    ESP_RETURN_ON_ERROR(
        st_espidf_i2c_device_init_on_bus(&adxl345_i2c_device,
                                         &equipment_i2c_bus, &adxl_target),
        TAG, "ADXL345 I2C registration failed");
    ESP_RETURN_ON_ERROR(
        st_espidf_onewire_bus_init(&ds18b20_onewire_bus, &onewire_config),
        TAG, "DS18B20 1-Wire registration failed");

    memset(&ina_config, 0, sizeof(ina_config));
    ina_config.bus = st_espidf_i2c_bus(&ina219_i2c_device);
    ina_config.address = CONFIG_SITETWIN_INA219_I2C_ADDRESS;
    ina_config.shunt_resistance_ohms =
        (float)CONFIG_SITETWIN_INA219_SHUNT_MILLIOHMS / 1000.0F;
    ina_config.max_expected_current_a =
        (float)CONFIG_SITETWIN_INA219_MAX_CURRENT_MA / 1000.0F;
    ina_config.sample_interval_ms = CONFIG_SITETWIN_INA219_SAMPLE_INTERVAL_MS;
    ina_config.cache_validity_ms = 250U;
    ina_config.bus_voltage_sensor_id = "ina219_voltage";
    ina_config.current_sensor_id = "ina219_current";

    memset(&adxl_config, 0, sizeof(adxl_config));
    adxl_config.bus = st_espidf_i2c_bus(&adxl345_i2c_device);
    adxl_config.address = CONFIG_SITETWIN_ADXL345_I2C_ADDRESS;
    adxl_config.range_g = 16U;
    adxl_config.rate_code = 0x0AU;
    adxl_config.minimum_window_samples =
        CONFIG_SITETWIN_ADXL345_MINIMUM_WINDOW_SAMPLES;
    adxl_config.sample_interval_ms = CONFIG_SITETWIN_ADXL345_SAMPLE_INTERVAL_MS;
    adxl_config.g_per_lsb = ST_ADXL345_DEFAULT_G_PER_LSB;
    adxl_config.vibration_sensor_id = "adxl345_vibration";

    memset(&ds18b20_config, 0, sizeof(ds18b20_config));
    ds18b20_config.bus = st_espidf_onewire_bus(&ds18b20_onewire_bus);
    ds18b20_config.resolution_bits = CONFIG_SITETWIN_DS18B20_RESOLUTION_BITS;
    ds18b20_config.sample_interval_ms = CONFIG_SITETWIN_DS18B20_SAMPLE_INTERVAL_MS;
    ds18b20_config.cache_validity_ms = 250U;
    ds18b20_config.temperature_sensor_id = "ds18b20_temperature";

    if (st_ina219_init(&ina219_sensor, &ina_config) != 0 ||
        st_module_instance_init(&ina219_module,
                                st_ina219_module_driver(&ina219_sensor),
                                ST_INA219_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&ina219_module, &pod_runtime.registry,
                                  ina_slots, ST_INA219_CHANNEL_COUNT) != 0 ||
        st_adxl345_init(&adxl345_sensor, &adxl_config) != 0 ||
        st_module_instance_init(&adxl345_module,
                                st_adxl345_module_driver(&adxl345_sensor),
                                ST_ADXL345_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&adxl345_module, &pod_runtime.registry,
                                  adxl_slots, ST_ADXL345_CHANNEL_COUNT) != 0 ||
        st_ds18b20_init(&ds18b20_sensor, &ds18b20_config) != 0 ||
        st_module_instance_init(&ds18b20_module,
                                st_ds18b20_module_driver(&ds18b20_sensor),
                                ST_DS18B20_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&ds18b20_module, &pod_runtime.registry,
                                  ds18b20_slots, ST_DS18B20_CHANNEL_COUNT) != 0) {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG,
             "Equipment Pod ready: INA219 0x%02X and ADXL345 0x%02X on I2C%d, "
             "DS18B20 on GPIO%d (%d-bit)",
             CONFIG_SITETWIN_INA219_I2C_ADDRESS,
             CONFIG_SITETWIN_ADXL345_I2C_ADDRESS,
             CONFIG_SITETWIN_EQUIPMENT_I2C_CONTROLLER,
             CONFIG_SITETWIN_DS18B20_GPIO,
             CONFIG_SITETWIN_DS18B20_RESOLUTION_BITS);
    return ESP_OK;
#elif CONFIG_SITETWIN_SHT41_ENABLED || CONFIG_SITETWIN_SCD41_ENABLED || CONFIG_SITETWIN_SGP40_ENABLED
    const st_espidf_i2c_master_bus_config_t bus_config = {
        .controller = CONFIG_SITETWIN_SHT41_I2C_CONTROLLER,
        .sda_gpio = CONFIG_SITETWIN_SHT41_I2C_SDA_PIN,
        .scl_gpio = CONFIG_SITETWIN_SHT41_I2C_SCL_PIN,
        .enable_internal_pullups = ST_SHT41_INTERNAL_PULLUPS_ENABLED,
    };
    esp_err_t result;

    st_pod_runtime_init(&pod_runtime, ST_POD_ENVIRONMENT, "POD_67C3", 1U);
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
        sensor_config.voc_index_sensor_id = "sgp40_voc";
        if (st_sgp40_init(&sgp40_sensor, &sensor_config) != 0 ||
            st_module_instance_init(&sgp40_module,
                                    st_sgp40_module_driver(&sgp40_sensor),
                                    ST_SGP40_CHANNEL_COUNT) != 0 ||
            st_module_instance_attach(&sgp40_module, &pod_runtime.registry,
                                      registry_slots, ST_SGP40_CHANNEL_COUNT) != 0) {
            st_espidf_i2c_device_deinit(&sgp40_i2c_device);
            st_espidf_i2c_master_bus_deinit(&environment_i2c_bus);
            return ESP_FAIL;
        }
    }
#endif

    ESP_LOGI(TAG, "Environment I2C ready on controller %d SDA GPIO%d SCL GPIO%d",
             CONFIG_SITETWIN_SHT41_I2C_CONTROLLER,
             CONFIG_SITETWIN_SHT41_I2C_SDA_PIN,
             CONFIG_SITETWIN_SHT41_I2C_SCL_PIN);
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if SITETWIN_POD_PROFILE_FINAL_PCB_BUILD
#ifdef CONFIG_SITETWIN_FINAL_PCB_I2C_INTERNAL_PULLUPS
#define ST_FINAL_PCB_I2C_INTERNAL_PULLUPS true
#else
#define ST_FINAL_PCB_I2C_INTERNAL_PULLUPS false
#endif
#ifdef CONFIG_SITETWIN_FINAL_PCB_DATA_COMMON_INTERNAL_PULLUP
#define ST_FINAL_PCB_DATA_COMMON_INTERNAL_PULLUP true
#else
#define ST_FINAL_PCB_DATA_COMMON_INTERNAL_PULLUP false
#endif

static st_espidf_final_pcb_board_t final_pcb_board;
static st_espidf_i2c_master_bus_t final_pcb_i2c_master_bus;
static st_espidf_shared_i2c_bus_t final_pcb_shared_i2c_bus;
static st_espidf_onewire_bus_t final_pcb_onewire_bus;
static st_espidf_muxed_data_common_t final_pcb_muxed_data_common[ST_FINAL_PCB_PORT_COUNT];
static st_board_port_manager_t final_pcb_port_manager;
static st_hotswap_module_binding_t final_pcb_binding;
static uint64_t final_pcb_last_scan_at_ms;
/* Temporary diagnostics: logs only on a lifecycle/fault transition per
 * port, so we can see whether a silent port is unclassified, failed its
 * bus cross-check, failed attach, or genuinely attached and just has
 * nothing to report yet -- these all look identical from the outside
 * (zero telemetry, zero error) without this. Remove once the hot-swap
 * path is fully trusted. */
static st_module_lifecycle_state_t final_pcb_last_lifecycle[ST_FINAL_PCB_PORT_COUNT];
static st_port_fault_reason_t final_pcb_last_fault_reason[ST_FINAL_PCB_PORT_COUNT];

static st_onewire_bus_t final_pcb_data_common_bus_for_port(void *context, size_t port_index)
{
    st_espidf_muxed_data_common_t *muxed = (st_espidf_muxed_data_common_t *)context;

    return st_espidf_muxed_onewire_bus(&muxed[port_index]);
}

/*
 * Mirrors pod_sensor_runtime_init()'s job for the three fixed profiles,
 * but composes the hot-swap stack instead: final_pcb_board (Satvik's
 * mux/ADC identification layer) -> board_port_manager (debounce/stable
 * commit/bus cross-check) -> hotswap_module_binding (type -> driver
 * mapping, registry attach or PIR/REED event state). Nothing here
 * decides *when* to attach anything -- that is entirely
 * board_port_manager's job, driven by pod_telemetry_task calling
 * st_board_port_manager_poll() on every tick.
 *
 * Deliberately does not touch pod_local_output/GPIO19 -- the shared
 * buzzer/LED actuation rework for the final PCB is separate, later work
 * (see the project notes); pod_local_output_ready simply stays 0 for
 * this profile for now, so the alarm/command state machine still runs
 * correctly, it just has no physical LED/buzzer to drive yet.
 */
static esp_err_t pod_sensor_runtime_init_final_pcb(void)
{
    const st_espidf_i2c_master_bus_config_t bus_config = {
        .controller = CONFIG_SITETWIN_FINAL_PCB_I2C_CONTROLLER,
        .sda_gpio = CONFIG_SITETWIN_FINAL_PCB_I2C_SDA_GPIO,
        .scl_gpio = CONFIG_SITETWIN_FINAL_PCB_I2C_SCL_GPIO,
        .enable_internal_pullups = ST_FINAL_PCB_I2C_INTERNAL_PULLUPS,
    };
    const st_espidf_onewire_bus_config_t onewire_config = {
        .gpio = CONFIG_SITETWIN_FINAL_PCB_DATA_COMMON_GPIO,
        .enable_internal_pullup = ST_FINAL_PCB_DATA_COMMON_INTERNAL_PULLUP,
        .max_rx_bytes = ST_DS18B20_SCRATCHPAD_SIZE,
    };
    st_hotswap_binding_io_t io;
    st_board_port_manager_callbacks_t callbacks;
    st_board_port_manager_config_t manager_config;
    const st_board_port_ops_t *port_ops;
    esp_err_t result;
    size_t port_index;

    st_pod_runtime_init(&pod_runtime, ST_POD_UNIVERSAL, "POD_UNIVERSAL_1", 4U);

    result = st_espidf_final_pcb_board_init(&final_pcb_board);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Final PCB board-port init failed: %d", (int)result);
        return result;
    }
    port_ops = st_espidf_final_pcb_board_ops(&final_pcb_board);
    if (port_ops == NULL) {
        return ESP_FAIL;
    }

    result = st_espidf_i2c_master_bus_init(&final_pcb_i2c_master_bus, &bus_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Final PCB shared I2C master bus init failed: %d", (int)result);
        return result;
    }
    if (st_espidf_shared_i2c_bus_init(&final_pcb_shared_i2c_bus, &final_pcb_i2c_master_bus,
                                      CONFIG_SITETWIN_FINAL_PCB_I2C_CLOCK_HZ,
                                      CONFIG_SITETWIN_FINAL_PCB_I2C_TIMEOUT_MS) != ESP_OK) {
        st_espidf_i2c_master_bus_deinit(&final_pcb_i2c_master_bus);
        return ESP_FAIL;
    }

    result = st_espidf_onewire_bus_init(&final_pcb_onewire_bus, &onewire_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Final PCB DATA_COMMON OneWire init failed: %d", (int)result);
        return result;
    }
    for (port_index = 0U; port_index < ST_FINAL_PCB_PORT_COUNT; ++port_index) {
        if (st_espidf_muxed_data_common_init(&final_pcb_muxed_data_common[port_index], port_ops,
                                             st_espidf_onewire_bus(&final_pcb_onewire_bus),
                                             CONFIG_SITETWIN_FINAL_PCB_DATA_COMMON_GPIO,
                                             port_index) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    memset(&io, 0, sizeof(io));
    io.i2c_bus = st_espidf_shared_i2c_bus(&final_pcb_shared_i2c_bus);
    io.i2c_probe = st_espidf_shared_i2c_probe;
    io.i2c_probe_context = &final_pcb_shared_i2c_bus;
    io.data_common_bus_for_port = final_pcb_data_common_bus_for_port;
    io.data_common_context = final_pcb_muxed_data_common;
    if (st_hotswap_module_binding_init(&final_pcb_binding, &io, &pod_runtime.registry,
                                       ST_FINAL_PCB_PORT_COUNT) != 0) {
        return ESP_FAIL;
    }

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.context = &final_pcb_binding;
    callbacks.attach = st_hotswap_module_binding_attach;
    callbacks.detach = st_hotswap_module_binding_detach;
    callbacks.module_uses_bus_probe = st_hotswap_module_binding_uses_bus_probe;
    callbacks.bus_probe = st_hotswap_module_binding_bus_probe;
    manager_config.stable_scan_count = CONFIG_SITETWIN_FINAL_PCB_STABLE_SCAN_COUNT;
    if (st_board_port_manager_init(&final_pcb_port_manager, port_ops, &callbacks,
                                   &manager_config) != 0) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Final PCB hot-swap runtime ready (%u ports, GPIO18 pull-up gate re-armed)",
             (unsigned int)ST_FINAL_PCB_PORT_COUNT);
    return ESP_OK;
}
#endif

static uint8_t pod_sensor_slot(const st_telemetry_record_t *record)
{
#if SITETWIN_POD_PROFILE_FINAL_PCB_BUILD
    uint8_t slot = 0U;

    /* Slot is chosen by sensor_id, not sensor_kind, because a hot-swap
     * port's type is not known at compile time and because two of our
     * nine module types (SHT41, DS18B20) share ST_SENSOR_TEMPERATURE_C --
     * see hotswap_zigbee_slot.h for why this reuses the exact slot
     * numbers gateway_identity.c's existing table already associates
     * with each sensor_id, requiring no changes to that shared table. */
    if (st_hotswap_zigbee_sensor_slot(record->reading.sensor_id, &slot) != 0) {
        ESP_LOGW(TAG, "No known Zigbee slot for sensor_id '%s'; gateway will label it unknown",
                 record->reading.sensor_id);
        return 0U;
    }
    return slot;
#elif SITETWIN_POD_PROFILE_ACTIVITY_BUILD
    switch (record->reading.sensor_kind) {
    case ST_SENSOR_MOTION:
        return 1U;
    case ST_SENSOR_CONTACT:
        return 2U;
    default:
        return 0U;
    }
#elif SITETWIN_POD_PROFILE_EQUIPMENT_BUILD
    switch (record->reading.sensor_kind) {
    case ST_SENSOR_CURRENT_MA:
        return 1U;
    case ST_SENSOR_VIBRATION_RMS_G:
        return 2U;
    case ST_SENSOR_TEMPERATURE_C:
        return 3U;
    default:
        return 0U;
    }
#else
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
#endif
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

static int pod_send_control_event(const st_control_event_t *event)
{
    uint8_t payload[ST_CONTROL_EVENT_WIRE_SIZE];
    size_t payload_length;
    ezb_zcl_custom_cluster_cmd_t command;
    int result;

    if (st_control_event_encode(event, payload, sizeof(payload),
                                &payload_length) != 0) {
        return -1;
    }
    memset(&command, 0, sizeof(command));
    command.cmd_ctrl.dst_addr = EZB_ADDRESS_SHORT(ST_GATEWAY_ADDRESS);
    command.cmd_ctrl.dst_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.src_ep = ST_ZIGBEE_ENDPOINT;
    command.cmd_ctrl.cluster_id = ST_ZIGBEE_CLUSTER_ID;
    command.cmd_ctrl.fc.direction = EZB_ZCL_CMD_DIRECTION_TO_SRV;
    command.cmd_ctrl.fc.dis_default_rsp = true;
    command.cmd_id = ST_ZIGBEE_CONTROL_EVENT;
    command.data_length = (uint16_t)payload_length;
    command.data = payload;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    result = ezb_zcl_custom_cluster_cmd_req(&command);
    esp_zigbee_lock_release();
    return result;
}

static int pod_alarm_reading_observer(void *context,
                                      const st_sensor_reading_t *reading,
                                      uint64_t now_ms)
{
    int result = st_command_runtime_ingest_reading(
        (st_command_runtime_t *)context, reading, now_ms);
    if (result < 0) {
        ESP_LOGW(TAG, "Alarm evaluation/persistence failed for %s (%d)",
                 reading->sensor_id, result);
    }
    return result;
}

static void pod_command_task(void *context)
{
    st_control_event_t pending_event;
    bool has_pending_event = false;

    (void)context;
    for (;;) {
        st_command_ack_t queued_ack;
        st_queued_pod_command_t queued;
        uint64_t now_ms = monotonic_now_ms();

        while (xQueueReceive(pod_command_ack_queue, &queued_ack, 0U) == pdPASS) {
            if (pod_joined && pod_send_command_ack(&queued_ack) != 0) {
                ESP_LOGW(TAG, "Queued command result send failed for %llu",
                         (unsigned long long)queued_ack.command_id);
            }
        }
        while (xQueueReceive(pod_command_queue, &queued, 0U) == pdPASS) {
            st_command_t local_command = queued.command;
            st_command_ack_t ack;

            local_command.issued_at_ms = queued.received_at_ms;
            local_command.expires_at_ms = queued.received_at_ms +
                                          local_command.valid_for_ms;
            ESP_LOGI(TAG, "DEBUG command dump: id=%llu type=%d target=%d source=%d "
                     "issued=%llu expires=%llu valid_for=%lu duration_ms=%lu",
                     (unsigned long long)local_command.command_id,
                     (int)local_command.command_type, (int)local_command.target,
                     (int)local_command.source,
                     (unsigned long long)local_command.issued_at_ms,
                     (unsigned long long)local_command.expires_at_ms,
                     (unsigned long)local_command.valid_for_ms,
                     (unsigned long)local_command.duration_ms);
            if (st_command_runtime_handle(&pod_command_runtime, &local_command,
                                          now_ms, &ack) == 0) {
                if (ack.status == ST_COMMAND_STATUS_EXECUTED &&
                    st_command_runtime_apply_reporting_rules(&pod_command_runtime,
                                                             &pod_runtime.reporting) != 0) {
                    ESP_LOGW(TAG, "Reporting-rule application failed for %llu",
                             (unsigned long long)ack.command_id);
                }
                if (pod_joined && pod_send_command_ack(&ack) != 0) {
                    ESP_LOGW(TAG, "Command result send failed for %llu",
                             (unsigned long long)ack.command_id);
                }
            }
        }
        st_command_runtime_tick(&pod_command_runtime, now_ms);
        if (pod_local_output_ready) {
            /* Desired state, recomputed every cycle -- the driver's own
             * submit() only actually touches the GPIO/PWM when the value
             * changes, so no edge-detection needed here.
             * LED: on for any active condition, regardless of silence --
             *   stays lit as a visual reminder even after the buzzer is
             *   silenced, since silence must never make the condition
             *   look resolved.
             * Buzzer: on for any active condition AND not currently
             *   silenced. */
            uint8_t any_active =
                st_command_runtime_any_alarm_active(&pod_command_runtime) != 0;
            uint8_t silenced = pod_command_runtime.alarm.silence_active != 0U;
            /* test_output is a separate, independent trigger source (see
             * command.h) -- ORed in here, not merged into the alarm state
             * machine itself. A TB-triggered (or future ML-triggered) test
             * pulse works on a Pod with zero real alarm conditions, and
             * conversely a real alarm still lights/sounds normally even if
             * nobody has ever sent a test_output command. */
            uint8_t led_on = any_active || pod_command_runtime.test_output_led_active != 0U;
            uint8_t buzzer_on = (any_active && !silenced) ||
                                pod_command_runtime.test_output_buzzer_active != 0U;
            st_local_output_service_t output_service =
                st_espidf_local_output_service(&pod_local_output);
            st_local_output_request_t led_request = {
                .kind = ST_LOCAL_OUTPUT_LED,
                .active = led_on,
                .frequency_hz = 0U,
                .duration_ms = 0U,
            };
            st_local_output_request_t buzzer_request = {
                .kind = ST_LOCAL_OUTPUT_BUZZER,
                .active = buzzer_on,
                .frequency_hz = 0U,
                .duration_ms = 0U,
            };
            (void)output_service.submit(output_service.context, &led_request);
            (void)output_service.submit(output_service.context, &buzzer_request);
        }
        if (pod_joined) {
            while (has_pending_event ||
                   st_command_runtime_next_control_event(&pod_command_runtime,
                                                         &pending_event) == 0) {
                has_pending_event = true;
                if (pod_send_control_event(&pending_event) != 0) {
                    ESP_LOGW(TAG, "Control-event send failed for %s/%s",
                             pending_event.pod_id, pending_event.sensor_id);
                    break;
                }
                has_pending_event = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}

static void pod_telemetry_task(void *context)
{
#if SITETWIN_POD_PROFILE_EQUIPMENT_BUILD
    uint32_t logged_adxl_windows = 0U;
#endif

    (void)context;
    for (;;) {
        uint64_t now_ms = monotonic_now_ms();

        if (pod_sensor_runtime_ready) {
            st_telemetry_record_t record;

#if SITETWIN_POD_PROFILE_ACTIVITY_BUILD
            st_pending_pod_event_t event;
            while (xQueueReceive(pod_event_queue, &event, 0U) == pdTRUE) {
                if (st_pod_runtime_emit_event(&pod_runtime, event.sensor_id,
                                              event.kind, event.timestamp_ms,
                                              event.value) != 0) {
                    ESP_LOGW(TAG, "Dropping %s event: telemetry queue full",
                             event.sensor_id);
                }
            }
#elif SITETWIN_POD_PROFILE_FINAL_PCB_BUILD
            /* Re-scan all four ports for insertion/removal/type changes
             * on its own cadence -- independent of this task's 50ms
             * tick and of each attached sensor's own sample_interval_ms.
             * Attach/detach (and therefore registry membership) happens
             * entirely inside this call via board_port_manager's
             * callbacks into hotswap_module_binding. */
            if (now_ms - final_pcb_last_scan_at_ms >= CONFIG_SITETWIN_FINAL_PCB_SCAN_INTERVAL_MS) {
                final_pcb_last_scan_at_ms = now_ms;
                st_board_port_manager_poll(&final_pcb_port_manager, now_ms);

                {
                    static const char *const kPortStatusSensorId[ST_FINAL_PCB_PORT_COUNT] = {
                        "port0_status", "port1_status", "port2_status", "port3_status"};
                    size_t diag_port;

                    for (diag_port = 0U; diag_port < ST_FINAL_PCB_PORT_COUNT; ++diag_port) {
                        const st_board_port_state_t *diag_state =
                            st_board_port_manager_get_state(&final_pcb_port_manager, diag_port);

                        if (diag_state != NULL &&
                            (diag_state->lifecycle != final_pcb_last_lifecycle[diag_port] ||
                             diag_state->fault_reason !=
                                 final_pcb_last_fault_reason[diag_port])) {
                            ESP_LOGI(TAG,
                                     "Port %u: lifecycle=%d fault=%d committed_type=%d "
                                     "raw_mv=%u calibrated=%u status=%d",
                                     (unsigned int)diag_port, (int)diag_state->lifecycle,
                                     (int)diag_state->fault_reason,
                                     (int)diag_state->committed_type,
                                     (unsigned int)diag_state->last_identity.millivolts,
                                     (unsigned int)diag_state->last_identity.voltage_calibrated,
                                     (int)diag_state->last_identity.status);
                            /* value encoding: lifecycle*10 + fault_reason
                             * (fault_reason is always < 10, so this is
                             * unambiguous) -- e.g. ACTIVE/no-fault = 60,
                             * FAULTED/UNCLASSIFIED_ID = 71. Kept
                             * deliberately simple rather than a second
                             * wire-format contract; decode by reversing
                             * the same arithmetic downstream. */
                            if (st_pod_runtime_emit_health(
                                    &pod_runtime, kPortStatusSensorId[diag_port], now_ms,
                                    (float)((int)diag_state->lifecycle * 10 +
                                           (int)diag_state->fault_reason)) != 0) {
                                ESP_LOGW(TAG, "Dropping port %u status event: queue full",
                                         (unsigned int)diag_port);
                            }
                            final_pcb_last_lifecycle[diag_port] = diag_state->lifecycle;
                            final_pcb_last_fault_reason[diag_port] = diag_state->fault_reason;
                        }
                    }
                }
            }
            /* PIR/REED are event-driven and deliberately outside the
             * registry (see hotswap_module_binding.h) -- service
             * whichever ports currently hold one every tick, same
             * cadence the fixed Activity profile already polls its own
             * PIR/reed at. */
            {
                size_t port_index;

                for (port_index = 0U; port_index < ST_FINAL_PCB_PORT_COUNT; ++port_index) {
                    st_pir_t *pir = NULL;
                    st_reed_debounce_t *reed = NULL;
                    const char *sensor_id = NULL;
                    uint8_t raw_level;

                    if (st_hotswap_binding_get_pir(&final_pcb_binding, port_index, &pir,
                                                   &sensor_id) == 1) {
                        st_pir_event_t pir_event;

                        if (st_espidf_muxed_data_common_read_level(
                                &final_pcb_muxed_data_common[port_index], &raw_level) ==
                                ST_HAL_OK &&
                            st_pir_process_level(pir, now_ms, raw_level, &pir_event) == 1) {
                            if (st_pod_runtime_emit_event(&pod_runtime, sensor_id,
                                                          ST_SENSOR_MOTION,
                                                          pir_event.detected_at_ms,
                                                          1.0F) != 0) {
                                ESP_LOGW(TAG, "Dropping %s event: telemetry queue full",
                                         sensor_id);
                            }
                        }
                    } else if (st_hotswap_binding_get_reed(&final_pcb_binding, port_index, &reed,
                                                           &sensor_id) == 1) {
                        st_reed_event_t reed_event;

                        if (st_espidf_muxed_data_common_read_level(
                                &final_pcb_muxed_data_common[port_index], &raw_level) ==
                                ST_HAL_OK &&
                            st_reed_debounce_update(reed, now_ms, raw_level, &reed_event) == 1) {
                            if (st_pod_runtime_emit_event(
                                    &pod_runtime, sensor_id, ST_SENSOR_CONTACT,
                                    reed_event.confirmed_at_ms,
                                    reed_event.level == ST_REED_OPEN ? 1.0F : 0.0F) != 0) {
                                ESP_LOGW(TAG, "Dropping %s event: telemetry queue full",
                                         sensor_id);
                            }
                        }
                    }
                }
            }
#endif
            st_pod_runtime_tick(&pod_runtime, now_ms);
#if SITETWIN_POD_PROFILE_EQUIPMENT_BUILD
            if (st_adxl345_windows_completed(&adxl345_sensor) != logged_adxl_windows) {
                logged_adxl_windows = st_adxl345_windows_completed(&adxl345_sensor);
                ESP_LOGI(TAG,
                         "ADXL345 window %lu: %.4f g RMS from %u samples, quality 0x%08lX",
                         (unsigned long)logged_adxl_windows,
                         (double)adxl345_sensor.current_vibration_rms_g,
                         (unsigned int)adxl345_sensor.current_sample_count,
                         (unsigned long)adxl345_sensor.current_quality_flags);
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
    uint64_t gateway_start_ms = monotonic_now_ms();

    st_gateway_runtime_init(&gateway_runtime);
    ESP_ERROR_CHECK(st_gateway_state_init(
                        &gateway_state,
                        st_espidf_gateway_state_persistence(),
                        gateway_start_ms) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    ESP_ERROR_CHECK(st_gateway_state_register_pod(&gateway_state, ST_POD_1_ID,
                                                  gateway_start_ms) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    ESP_ERROR_CHECK(st_gateway_state_register_pod(&gateway_state, ST_POD_2_ID,
                                                  gateway_start_ms) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    ESP_ERROR_CHECK(st_gateway_state_register_pod(&gateway_state, ST_POD_3_ID,
                                                  gateway_start_ms) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    gateway_uart_init();
    ESP_ERROR_CHECK(xTaskCreate(gateway_state_task, "st_gw_state", 4096, NULL,
                                5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_LOGI(TAG, "Starting SiteTwin gateway/coordinator image");
#else
    ESP_LOGI(TAG, "Starting SiteTwin pod/end-device image");
    pod_command_queue = xQueueCreate(8U, sizeof(st_queued_pod_command_t));
    pod_command_ack_queue = xQueueCreate(4U, sizeof(st_command_ack_t));
    ESP_ERROR_CHECK(pod_command_queue != NULL && pod_command_ack_queue != NULL
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
#if SITETWIN_POD_PROFILE_FINAL_PCB_BUILD
    pod_sensor_runtime_ready = pod_sensor_runtime_init_final_pcb() == ESP_OK;
#else
    pod_sensor_runtime_ready = pod_sensor_runtime_init() == ESP_OK;
#endif
    if (!pod_sensor_runtime_ready) {
        ESP_LOGE(TAG, "Pod sensor runtime initialization failed");
    }
#if SITETWIN_POD_PROFILE_FINAL_PCB_BUILD
    ESP_ERROR_CHECK(st_command_runtime_init_with_boot(
                                             &pod_command_runtime,
                                             ST_POD_UNIVERSAL,
                                             "POD_UNIVERSAL_1",
                                             st_espidf_pod_command_persistence(),
                                             pod_runtime.registry.boot_id,
                                             monotonic_now_ms()) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    /* Shared GPIO19 buzzer/LED actuation for the final PCB is separate,
     * later work -- pod_local_output_ready deliberately stays 0 here, so
     * the alarm/command state machine still runs correctly, it just has
     * no physical indicator to drive yet. */
    ESP_LOGI(TAG, "Final PCB profile: shared alarm indicator not yet wired, staying silent");
#elif SITETWIN_POD_PROFILE_ACTIVITY_BUILD
    ESP_ERROR_CHECK(st_command_runtime_init_with_boot(
                                             &pod_command_runtime,
                                             ST_POD_ACTIVITY_ACCESS,
                                             "POD_6647",
                                             st_espidf_pod_command_persistence(),
                                             pod_runtime.registry.boot_id,
                                             monotonic_now_ms()) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    pod_local_output_ready = st_espidf_local_output_init(
                                  &pod_local_output,
                                  CONFIG_SITETWIN_ACTIVITY_ALERT_LED_GPIO,
                                  CONFIG_SITETWIN_ACTIVITY_ALERT_BUZZER_GPIO,
                                  CONFIG_SITETWIN_ACTIVITY_ALERT_BUZZER_HZ) == ESP_OK;
    if (!pod_local_output_ready) {
        ESP_LOGW(TAG, "Alert LED/buzzer driver init failed -- indicator will stay silent");
    }
#elif SITETWIN_POD_PROFILE_EQUIPMENT_BUILD
    ESP_ERROR_CHECK(st_command_runtime_init_with_boot(
                                             &pod_command_runtime,
                                             ST_POD_EQUIPMENT,
                                             "POD_1FBA",
                                             st_espidf_pod_command_persistence(),
                                             pod_runtime.registry.boot_id,
                                             monotonic_now_ms()) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    pod_local_output_ready = st_espidf_local_output_init(
                                  &pod_local_output,
                                  CONFIG_SITETWIN_EQUIPMENT_ALERT_LED_GPIO,
                                  CONFIG_SITETWIN_EQUIPMENT_ALERT_BUZZER_GPIO,
                                  CONFIG_SITETWIN_EQUIPMENT_ALERT_BUZZER_HZ) == ESP_OK;
    if (!pod_local_output_ready) {
        ESP_LOGW(TAG, "Alert LED/buzzer driver init failed -- indicator will stay silent");
    }
#else
    ESP_ERROR_CHECK(st_command_runtime_init_with_boot(
                                             &pod_command_runtime,
                                             ST_POD_ENVIRONMENT,
                                             "POD_67C3",
                                             st_espidf_pod_command_persistence(),
                                             pod_runtime.registry.boot_id,
                                             monotonic_now_ms()) == 0
                        ? ESP_OK
                        : ESP_FAIL);
    pod_local_output_ready = st_espidf_local_output_init(
                                  &pod_local_output,
                                  CONFIG_SITETWIN_ENVIRONMENT_ALERT_LED_GPIO,
                                  CONFIG_SITETWIN_ENVIRONMENT_ALERT_BUZZER_GPIO,
                                  CONFIG_SITETWIN_ENVIRONMENT_ALERT_BUZZER_HZ) == ESP_OK;
    if (!pod_local_output_ready) {
        ESP_LOGW(TAG, "Alert LED/buzzer driver init failed -- indicator will stay silent");
    }
#endif
    st_pod_runtime_set_reading_observer(&pod_runtime,
                                        pod_alarm_reading_observer,
                                        &pod_command_runtime);
    ESP_ERROR_CHECK(xTaskCreate(pod_command_task, "st_pod_cmd", 4096, NULL,
                                5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(xTaskCreate(pod_telemetry_task, "st_pod_tx", 4096, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
#endif
    ESP_ERROR_CHECK(xTaskCreate(zigbee_task, "st_zigbee", 6144, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_FAIL);
}