#ifndef UART_LINK_H
#define UART_LINK_H

#include <stddef.h>
#include <stdint.h>

/* Initializes the UART peripheral and starts the background receive task.
 *
 * NOTE: baud rate and GPIO pins are placeholders pending confirmation with
 * the Zigbee-side owner. See "Open decisions" in GATEWAY_TO_SERVER_BRINGUP.md.
 * The outer frame format itself (start marker, header layout, CRC16) is not
 * a placeholder -- it comes from the shared, already-tested gateway_frame.c.
 */
void uart_link_init(void);

/* Feeds a byte sequence directly into the frame parser, bypassing the UART
 * peripheral. Used to self-test the framing/CRC parsing logic without a
 * physical connection to the Zigbee-side board (see the "uart_test" console
 * command).
 */
void uart_link_feed_test_bytes(const uint8_t *data, size_t length);

#endif