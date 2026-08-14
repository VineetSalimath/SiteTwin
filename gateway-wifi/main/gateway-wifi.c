#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"

#include "gateway_pipeline.h"
#include "console_commands.h"
#include "mqtt_publish.h"
#include "uart_link.h"
#include "wifi_config.h"

static const char *TAG = "gateway-wifi";

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
static char s_command_topic[96];
static char s_command_payload[768];
static size_t s_command_expected;
static size_t s_command_received;
static bool s_command_reassembly_active;

/* ---------- Wi-Fi ---------- */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi connected, got IP");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to Wi-Fi SSID: %s", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}

/* ---------- MQTT ---------- */

static void mqtt_command_reassembly_reset(void)
{
    s_command_topic[0] = '\0';
    s_command_payload[0] = '\0';
    s_command_expected = 0U;
    s_command_received = 0U;
    s_command_reassembly_active = false;
}

static void mqtt_process_command_chunk(esp_mqtt_event_handle_t event)
{
    size_t offset;
    size_t chunk_length;

    if (event->current_data_offset < 0 || event->data_len < 0 ||
        event->total_data_len <= 0) {
        mqtt_command_reassembly_reset();
        return;
    }
    offset = (size_t)event->current_data_offset;
    chunk_length = (size_t)event->data_len;
    if (offset == 0U) {
        size_t topic_length;

        mqtt_command_reassembly_reset();
        if (event->topic == NULL || event->topic_len <= 0 ||
            (size_t)event->topic_len >= sizeof(s_command_topic) ||
            (size_t)event->total_data_len >= sizeof(s_command_payload)) {
            ESP_LOGW(TAG, "Rejected oversized command MQTT message");
            return;
        }
        topic_length = (size_t)event->topic_len;
        memcpy(s_command_topic, event->topic, topic_length);
        s_command_topic[topic_length] = '\0';
        s_command_expected = (size_t)event->total_data_len;
        s_command_reassembly_active = true;
    }
    if (!s_command_reassembly_active || offset != s_command_received ||
        chunk_length > s_command_expected - s_command_received) {
        ESP_LOGW(TAG, "Rejected out-of-order command MQTT chunk");
        mqtt_command_reassembly_reset();
        return;
    }
    memcpy(s_command_payload + s_command_received, event->data, chunk_length);
    s_command_received += chunk_length;
    if (s_command_received == s_command_expected) {
        s_command_payload[s_command_received] = '\0';
        (void)gateway_pipeline_process_mqtt_command(s_command_topic,
                                                    s_command_payload);
        mqtt_command_reassembly_reset();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to HiveMQ");
        s_mqtt_connected = true;
        /* Subscribe to downstream commands for all pods. Wildcard '+' mirrors
         * the pattern bridge.py uses for the upstream telemetry topic. */
        esp_mqtt_client_subscribe(s_mqtt_client, "sitetwin/pods/+/commands", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_mqtt_connected = false;
        mqtt_command_reassembly_reset();
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT publish acknowledged, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribe acknowledged, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        mqtt_process_command_chunk(event);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error event");
        break;
    default:
        break;
    }
}

static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = HIVEMQ_HOST,
        .broker.address.port = HIVEMQ_PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = HIVEMQ_USERNAME,
        .credentials.authentication.password = HIVEMQ_PASSWORD,
        .credentials.client_id = "sitetwin-gateway-c6",
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

/* ---------- Public MQTT accessors, used by gateway_pipeline.c ---------- */

int gw_mqtt_publish(const char *topic, const char *payload)
{
    if (!s_mqtt_connected || s_mqtt_client == NULL) {
        return -1;
    }
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
    return msg_id >= 0 ? 0 : -1;
}

int gw_mqtt_is_connected(void)
{
    return s_mqtt_connected ? 1 : 0;
}

/* ---------- Console ---------- */

static void console_init(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "gw>";

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    console_commands_register();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

/* ---------- app_main ---------- */

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_sta();
    mqtt_init();

    gateway_pipeline_init();

    /* Starts the UART receive/parse framework. Payload interpretation is
     * still a stub pending format confirmation with the Zigbee-side owner;
     * see uart_link.c and GATEWAY_TO_SERVER_BRINGUP.md. This does not
     * interfere with the test data source or the console, which use UART0;
     * this uses UART1. */
    uart_link_init();

    console_init();

    ESP_LOGI(TAG, "Ready. Type 'help' at the prompt for available commands.");
}
