#include "sitetwin/zigbee_payload.h"

#include <string.h>

enum {
    ST_ZB_OFFSET_VERSION = 0,
    ST_ZB_OFFSET_RECORD_CLASS = 1,
    ST_ZB_OFFSET_PRIORITY = 2,
    ST_ZB_OFFSET_SENSOR_KIND = 3,
    ST_ZB_OFFSET_UNIT = 4,
    ST_ZB_OFFSET_SENSOR_SLOT = 5,
    ST_ZB_OFFSET_QUALITY_FLAGS = 6,
    ST_ZB_OFFSET_SEQUENCE = 10,
    ST_ZB_OFFSET_BOOT_ID = 14,
    ST_ZB_OFFSET_UPTIME_MS = 18,
    ST_ZB_OFFSET_VALUE = 26
};

_Static_assert(sizeof(float) == sizeof(uint32_t), "SiteTwin requires 32-bit IEEE-754 floats");

static void write_u32_le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)(value & 0xFFU);
    destination[1] = (uint8_t)((value >> 8U) & 0xFFU);
    destination[2] = (uint8_t)((value >> 16U) & 0xFFU);
    destination[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static void write_u64_le(uint8_t *destination, uint64_t value)
{
    size_t index;

    for (index = 0U; index < 8U; ++index) {
        destination[index] = (uint8_t)((value >> (index * 8U)) & 0xFFU);
    }
}

static uint32_t read_u32_le(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) |
           ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

static uint64_t read_u64_le(const uint8_t *source)
{
    size_t index;
    uint64_t value = 0U;

    for (index = 0U; index < 8U; ++index) {
        value |= (uint64_t)source[index] << (index * 8U);
    }
    return value;
}

static void copy_string(char *destination, size_t capacity, const char *source)
{
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

int st_zigbee_telemetry_encode(const st_telemetry_record_t *record, uint8_t sensor_slot,
                               uint8_t *payload, size_t payload_capacity,
                               size_t *payload_length)
{
    uint32_t value_bits;

    if (record == NULL || payload == NULL || payload_length == NULL ||
        payload_capacity < ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        record->record_class > ST_RECORD_HEALTH ||
        record->priority < ST_PRIORITY_ROUTINE || record->priority > ST_PRIORITY_EVENT ||
        record->reading.sensor_kind > ST_SENSOR_UNKNOWN || record->reading.unit > ST_UNIT_NONE) {
        return -1;
    }

    memset(payload, 0, ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE);
    payload[ST_ZB_OFFSET_VERSION] = ST_ZIGBEE_PAYLOAD_VERSION;
    payload[ST_ZB_OFFSET_RECORD_CLASS] = (uint8_t)record->record_class;
    payload[ST_ZB_OFFSET_PRIORITY] = (uint8_t)record->priority;
    payload[ST_ZB_OFFSET_SENSOR_KIND] = (uint8_t)record->reading.sensor_kind;
    payload[ST_ZB_OFFSET_UNIT] = (uint8_t)record->reading.unit;
    payload[ST_ZB_OFFSET_SENSOR_SLOT] = sensor_slot;
    write_u32_le(&payload[ST_ZB_OFFSET_QUALITY_FLAGS], record->reading.quality_flags);
    write_u32_le(&payload[ST_ZB_OFFSET_SEQUENCE], record->reading.sequence);
    write_u32_le(&payload[ST_ZB_OFFSET_BOOT_ID], record->reading.boot_id);
    write_u64_le(&payload[ST_ZB_OFFSET_UPTIME_MS], record->reading.uptime_ms);
    memcpy(&value_bits, &record->reading.value, sizeof(value_bits));
    write_u32_le(&payload[ST_ZB_OFFSET_VALUE], value_bits);
    *payload_length = ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE;
    return 0;
}

int st_zigbee_telemetry_decode(const uint8_t *payload, size_t payload_length,
                               const char *pod_id, const char *sensor_id,
                               st_telemetry_record_t *record, uint8_t *sensor_slot)
{
    uint32_t value_bits;

    if (payload == NULL || pod_id == NULL || sensor_id == NULL || record == NULL ||
        sensor_slot == NULL || payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        payload[ST_ZB_OFFSET_VERSION] != ST_ZIGBEE_PAYLOAD_VERSION ||
        payload[ST_ZB_OFFSET_RECORD_CLASS] > ST_RECORD_HEALTH ||
        payload[ST_ZB_OFFSET_PRIORITY] < ST_PRIORITY_ROUTINE ||
        payload[ST_ZB_OFFSET_PRIORITY] > ST_PRIORITY_EVENT ||
        payload[ST_ZB_OFFSET_SENSOR_KIND] > ST_SENSOR_UNKNOWN ||
        payload[ST_ZB_OFFSET_UNIT] > ST_UNIT_NONE) {
        return -1;
    }

    memset(record, 0, sizeof(*record));
    record->record_class = (st_record_class_t)payload[ST_ZB_OFFSET_RECORD_CLASS];
    record->priority = (st_delivery_priority_t)payload[ST_ZB_OFFSET_PRIORITY];
    record->reading.sensor_kind = (st_sensor_kind_t)payload[ST_ZB_OFFSET_SENSOR_KIND];
    record->reading.unit = (st_unit_t)payload[ST_ZB_OFFSET_UNIT];
    record->reading.quality_flags = read_u32_le(&payload[ST_ZB_OFFSET_QUALITY_FLAGS]);
    record->reading.sequence = read_u32_le(&payload[ST_ZB_OFFSET_SEQUENCE]);
    record->reading.boot_id = read_u32_le(&payload[ST_ZB_OFFSET_BOOT_ID]);
    record->reading.uptime_ms = read_u64_le(&payload[ST_ZB_OFFSET_UPTIME_MS]);
    value_bits = read_u32_le(&payload[ST_ZB_OFFSET_VALUE]);
    memcpy(&record->reading.value, &value_bits, sizeof(value_bits));
    copy_string(record->reading.pod_id, sizeof(record->reading.pod_id), pod_id);
    copy_string(record->reading.sensor_id, sizeof(record->reading.sensor_id), sensor_id);
    *sensor_slot = payload[ST_ZB_OFFSET_SENSOR_SLOT];
    return 0;
}

int st_zigbee_telemetry_sensor_slot(const uint8_t *payload, size_t payload_length,
                                    uint8_t *sensor_slot)
{
    if (payload == NULL || sensor_slot == NULL ||
        payload_length != ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE ||
        payload[ST_ZB_OFFSET_VERSION] != ST_ZIGBEE_PAYLOAD_VERSION) {
        return -1;
    }
    *sensor_slot = payload[ST_ZB_OFFSET_SENSOR_SLOT];
    return 0;
}
