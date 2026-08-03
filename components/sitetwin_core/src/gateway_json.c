#include "sitetwin/gateway_json.h"

#include <ctype.h>
#include <stdio.h>

static int is_safe_identifier(const char *value)
{
    size_t index = 0U;

    if (value == NULL || value[0] == '\0') {
        return 0;
    }
    while (value[index] != '\0') {
        unsigned char character = (unsigned char)value[index];
        if (!isalnum(character) && character != '_' && character != '-') {
            return 0;
        }
        ++index;
    }
    return 1;
}

int st_gateway_telemetry_to_json(const st_telemetry_record_t *record, char *json,
                                 size_t json_capacity)
{
    int written;
    const st_sensor_reading_t *reading;

    if (record == NULL || json == NULL || json_capacity == 0U ||
        !is_safe_identifier(record->reading.pod_id) ||
        !is_safe_identifier(record->reading.sensor_id)) {
        return -1;
    }

    reading = &record->reading;
    written = snprintf(
        json, json_capacity,
        "{\"schema_version\":%u,\"pod_id\":\"%s\",\"sensor_id\":\"%s\","
        "\"sensor_kind\":\"%s\",\"record_class\":\"%s\",\"sequence\":%lu,"
        "\"boot_id\":%lu,\"uptime_ms\":%llu,\"value\":%.3f,\"unit\":\"%s\","
        "\"quality_flags\":%lu}",
        ST_CONTRACT_VERSION, reading->pod_id, reading->sensor_id,
        st_sensor_kind_name(reading->sensor_kind), st_record_class_name(record->record_class),
        (unsigned long)reading->sequence, (unsigned long)reading->boot_id,
        (unsigned long long)reading->uptime_ms, (double)reading->value, st_unit_name(reading->unit),
        (unsigned long)reading->quality_flags);

    return written < 0 || (size_t)written >= json_capacity ? -1 : 0;
}
