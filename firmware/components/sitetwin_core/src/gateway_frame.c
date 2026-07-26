#include "sitetwin/gateway_frame.h"

#include <limits.h>

static void write_u16_le(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFU);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFU);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint16_t read_u16_le(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

uint16_t st_gateway_crc16_ccitt(const uint8_t *data, size_t length)
{
    size_t index;
    uint16_t crc = 0xFFFFU;

    if (data == NULL) {
        return 0U;
    }

    for (index = 0U; index < length; ++index) {
        uint8_t bit;
        crc ^= (uint16_t)data[index] << 8U;
        for (bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U ? (uint16_t)((crc << 1U) ^ 0x1021U)
                                        : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

int st_gateway_frame_encode(const st_gateway_frame_header_t *header, const uint8_t *payload,
                            uint8_t *frame, size_t frame_capacity, size_t *frame_length)
{
    size_t required_length;
    uint16_t crc;

    if (header == NULL || frame == NULL || frame_length == NULL ||
        (header->payload_length > 0U && payload == NULL)) {
        return -1;
    }

    required_length = ST_GATEWAY_FRAME_HEADER_SIZE + (size_t)header->payload_length +
                      ST_GATEWAY_FRAME_CRC_SIZE;
    if (frame_capacity < required_length) {
        return -1;
    }

    write_u16_le(frame, ST_GATEWAY_FRAME_START);
    frame[2] = header->version;
    frame[3] = (uint8_t)header->message_type;
    write_u16_le(&frame[4], header->payload_length);
    write_u16_le(&frame[6], header->source_address);
    write_u32_le(&frame[8], header->boot_id);
    write_u32_le(&frame[12], header->sequence);
    for (size_t index = 0U; index < header->payload_length; ++index) {
        frame[ST_GATEWAY_FRAME_HEADER_SIZE + index] = payload[index];
    }

    crc = st_gateway_crc16_ccitt(frame, required_length - ST_GATEWAY_FRAME_CRC_SIZE);
    write_u16_le(&frame[required_length - ST_GATEWAY_FRAME_CRC_SIZE], crc);
    *frame_length = required_length;
    return 0;
}

int st_gateway_frame_decode(const uint8_t *frame, size_t frame_length,
                            st_gateway_frame_header_t *header, const uint8_t **payload)
{
    uint16_t payload_length;
    size_t expected_length;
    uint16_t expected_crc;
    uint16_t actual_crc;

    if (frame == NULL || header == NULL || payload == NULL ||
        frame_length < ST_GATEWAY_FRAME_HEADER_SIZE + ST_GATEWAY_FRAME_CRC_SIZE) {
        return -1;
    }
    if (read_u16_le(frame) != ST_GATEWAY_FRAME_START) {
        return -1;
    }

    payload_length = read_u16_le(&frame[4]);
    expected_length = ST_GATEWAY_FRAME_HEADER_SIZE + (size_t)payload_length +
                      ST_GATEWAY_FRAME_CRC_SIZE;
    if (expected_length != frame_length) {
        return -1;
    }

    expected_crc = read_u16_le(&frame[expected_length - ST_GATEWAY_FRAME_CRC_SIZE]);
    actual_crc = st_gateway_crc16_ccitt(frame, expected_length - ST_GATEWAY_FRAME_CRC_SIZE);
    if (actual_crc != expected_crc) {
        return -1;
    }

    header->version = frame[2];
    header->message_type = (st_gateway_message_type_t)frame[3];
    header->payload_length = payload_length;
    header->source_address = read_u16_le(&frame[6]);
    header->boot_id = read_u32_le(&frame[8]);
    header->sequence = read_u32_le(&frame[12]);
    *payload = &frame[ST_GATEWAY_FRAME_HEADER_SIZE];
    return 0;
}
