#ifndef UART_LINK_H
#define UART_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/gateway_frame.h"

/* Initializes the UART peripheral and starts the background receive task.
 *
 * UART1 receives the matching Zigbee gateway's default GPIO4 TX on GPIO5 at
 * 115200 baud. The outer frame format (start marker, header layout, CRC16)
 * comes from the shared, already-tested gateway_frame.c.
 */
void uart_link_init(void);
int uart_link_send_payload(st_gateway_message_type_t message_type,
                           const uint8_t *payload, uint16_t payload_length,
                           uint32_t sequence);

/* Feeds a byte sequence directly into the frame parser, bypassing the UART
 * peripheral. Used to self-test the framing/CRC parsing logic without a
 * physical connection to the Zigbee-side board (see the "uart_test" console
 * command).
 */
void uart_link_feed_test_bytes(const uint8_t *data, size_t length);

#endif
