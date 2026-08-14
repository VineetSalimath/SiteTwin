#ifndef SITETWIN_GATEWAY_FRAME_H
#define SITETWIN_GATEWAY_FRAME_H

#include <stddef.h>
#include <stdint.h>

#define ST_GATEWAY_FRAME_START 0xA55AU
#define ST_GATEWAY_FRAME_VERSION 1U
#define ST_GATEWAY_FRAME_HEADER_SIZE 16U
#define ST_GATEWAY_FRAME_CRC_SIZE 2U

typedef enum {
    ST_GATEWAY_MESSAGE_TELEMETRY = 1,
    ST_GATEWAY_MESSAGE_HEALTH = 2,
    ST_GATEWAY_MESSAGE_COMMAND = 3,
    ST_GATEWAY_MESSAGE_COMMAND_ACK = 4,
    ST_GATEWAY_MESSAGE_CONTROL_EVENT = 5
} st_gateway_message_type_t;

typedef struct {
    uint8_t version;
    st_gateway_message_type_t message_type;
    uint16_t payload_length;
    uint16_t source_address;
    uint32_t boot_id;
    uint32_t sequence;
} st_gateway_frame_header_t;

uint16_t st_gateway_crc16_ccitt(const uint8_t *data, size_t length);
int st_gateway_frame_encode(const st_gateway_frame_header_t *header, const uint8_t *payload,
                            uint8_t *frame, size_t frame_capacity, size_t *frame_length);
int st_gateway_frame_decode(const uint8_t *frame, size_t frame_length,
                            st_gateway_frame_header_t *header, const uint8_t **payload);

#endif
