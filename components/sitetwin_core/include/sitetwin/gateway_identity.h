#ifndef SITETWIN_GATEWAY_IDENTITY_H
#define SITETWIN_GATEWAY_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/contracts.h"

#define ST_POD_1_ID "POD_1"
#define ST_POD_2_ID "POD_2"
#define ST_POD_3_ID "POD_3"

#define ST_POD_1_SHORT_ADDRESS 0x60D1U
#define ST_POD_2_SHORT_ADDRESS 0x9393U
#define ST_POD_3_SHORT_ADDRESS 0x304EU

/*
 * Resolve the current bring-up network's source address independently from
 * its compact slot/kind sensor identity. Unknown values retain address/slot
 * fallbacks so they cannot collide with a known pod or sensor stream.
 * The short-address table is temporary until IEEE identity is carried across
 * the gateway boundary and stored persistently.
 */
int st_gateway_identity_resolve(uint16_t source_address, uint8_t sensor_slot,
                                st_sensor_kind_t sensor_kind,
                                char *pod_id, size_t pod_id_capacity,
                                char *sensor_id, size_t sensor_id_capacity);

#endif
