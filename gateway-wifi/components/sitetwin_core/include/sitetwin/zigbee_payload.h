#ifndef SITETWIN_ZIGBEE_PAYLOAD_H
#define SITETWIN_ZIGBEE_PAYLOAD_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/contracts.h"

#define ST_ZIGBEE_PAYLOAD_VERSION 1U
#define ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE 30U

int st_zigbee_telemetry_encode(const st_telemetry_record_t *record, uint8_t sensor_slot,
                               uint8_t *payload, size_t payload_capacity,
                               size_t *payload_length);
int st_zigbee_telemetry_decode(const uint8_t *payload, size_t payload_length,
                               const char *pod_id, const char *sensor_id,
                               st_telemetry_record_t *record, uint8_t *sensor_slot);
int st_zigbee_telemetry_sensor_slot(const uint8_t *payload, size_t payload_length,
                                    uint8_t *sensor_slot);

#endif
