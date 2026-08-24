#ifndef SITETWIN_GATEWAY_IDENTITY_H
#define SITETWIN_GATEWAY_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/contracts.h"

int st_gateway_identity_resolve(uint16_t source_address, uint8_t sensor_slot,
                                st_sensor_kind_t sensor_kind,
                                char *pod_id, size_t pod_id_capacity,
                                char *sensor_id, size_t sensor_id_capacity);
int st_gateway_identity_short_address(const char *pod_id, uint16_t *short_address);

#endif
