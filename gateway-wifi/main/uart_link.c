#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "sitetwin/gateway_frame.h"
#include "gateway_pipeline.h"
#include "uart_link.h"

static const char *TAG = "uart_link";

/* UART1 is used instead of UART0, which is claimed by the esp_console REPL.
 * The matching Zigbee gateway defaults to TX GPIO4 at 115200 baud. */
#define UART_LINK_PORT         UART_NUM_1
#define UART_LINK_BAUD_RATE    115200
#define UART_LINK_TXD_PIN      4
#define UART_LINK_RXD_PIN      5
#define UART_LINK_RX_BUF_SIZE  1024

/* Frame reassembly buffer. Sized generously above the largest frame
 * currently expected (16-byte header + 30-byte Zigbee payload + 2-byte
 * CRC = 48 bytes), to tolerate a payload format larger than the current
 * assumption without needing a code change here. */
#define PARSE_BUFFER_SIZE 256

static uint8_t s_parse_buffer[PARSE_BUFFER_SIZE];
static size_t s_parse_buffer_len = 0;

static void uart_link_handle_frame(const st_gateway_frame_header_t *header,
                                    const uint8_t *payload)
{
    if (gateway_pipeline_process_uart_frame(header, payload) == 0) {
        ESP_LOGI(TAG, "Forwarded frame from 0x%04X sequence %lu", header->source_address,
                 (unsigned long)header->sequence);
    }
}

/* Scans the reassembly buffer for a complete, CRC-valid frame starting at
 * the ST_GATEWAY_FRAME_START marker, consumes it if found, and discards
 * leading bytes that cannot be the start of a valid frame. Shared by both
 * the real UART receive task and the test-injection path, so the parsing
 * logic itself only needs to be validated once. */
static void uart_link_try_parse(void)
{
    while (s_parse_buffer_len >= 2U) {
        if (s_parse_buffer[0] != (uint8_t)(ST_GATEWAY_FRAME_START & 0xFFU) ||
            s_parse_buffer[1] != (uint8_t)(ST_GATEWAY_FRAME_START >> 8U)) {
            memmove(s_parse_buffer, s_parse_buffer + 1, --s_parse_buffer_len);
            continue;
        }

        if (s_parse_buffer_len < ST_GATEWAY_FRAME_HEADER_SIZE) {
            return; /* wait for more bytes */
        }

        uint16_t payload_length =
            (uint16_t)s_parse_buffer[4] | ((uint16_t)s_parse_buffer[5] << 8U);
        size_t frame_length = ST_GATEWAY_FRAME_HEADER_SIZE + (size_t)payload_length +
                              ST_GATEWAY_FRAME_CRC_SIZE;

        if (frame_length > PARSE_BUFFER_SIZE) {
            ESP_LOGW(TAG, "Declared payload length implausible (%u), resyncing",
                     (unsigned)payload_length);
            memmove(s_parse_buffer, s_parse_buffer + 1, --s_parse_buffer_len);
            continue;
        }

        if (s_parse_buffer_len < frame_length) {
            return; /* wait for the rest of the frame */
        }

        st_gateway_frame_header_t header;
        const uint8_t *payload = NULL;
        if (st_gateway_frame_decode(s_parse_buffer, frame_length, &header, &payload) == 0) {
            uart_link_handle_frame(&header, payload);
        } else {
            ESP_LOGW(TAG, "CRC or framing check failed, discarding frame");
        }

        memmove(s_parse_buffer, s_parse_buffer + frame_length,
                s_parse_buffer_len - frame_length);
        s_parse_buffer_len -= frame_length;
    }
}

static void uart_link_append_bytes(const uint8_t *data, size_t length)
{
    if (length > PARSE_BUFFER_SIZE - s_parse_buffer_len) {
        ESP_LOGW(TAG, "Parse buffer overflow, resetting");
        s_parse_buffer_len = 0;
    }
    memcpy(s_parse_buffer + s_parse_buffer_len, data, length);
    s_parse_buffer_len += length;
    uart_link_try_parse();
}

void uart_link_feed_test_bytes(const uint8_t *data, size_t length)
{
    uart_link_append_bytes(data, length);
}

static void uart_link_rx_task(void *arg)
{
    (void)arg;
    uint8_t chunk[64];
    for (;;) {
        int read = uart_read_bytes(UART_LINK_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
        if (read > 0) {
            uart_link_append_bytes(chunk, (size_t)read);
        }
    }
}

void uart_link_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = UART_LINK_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_LINK_PORT, UART_LINK_RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_LINK_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_LINK_PORT, UART_LINK_TXD_PIN, UART_LINK_RXD_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(uart_link_rx_task, "uart_link_rx", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "UART link initialised: port=%d baud=%d txd=%d rxd=%d",
             (int)UART_LINK_PORT, UART_LINK_BAUD_RATE, UART_LINK_TXD_PIN, UART_LINK_RXD_PIN);
}
