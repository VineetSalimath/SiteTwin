#ifndef SITETWIN_HOTSWAP_ZIGBEE_SLOT_H
#define SITETWIN_HOTSWAP_ZIGBEE_SLOT_H

#include <stdint.h>

/*
 * gateway_identity.c's sensor_identity_table resolves a Zigbee telemetry
 * record's sensor_id purely from the (sensor_slot, sensor_kind) pair --
 * it never checks which pod (source address) sent it. That means the
 * hot-swap pod does not need any new rows in that table: sending a given
 * sensor_id under the exact same slot number an existing fixed profile
 * already uses for that same type resolves correctly, because the
 * resulting sensor_id string is identical and equally accurate no matter
 * which pod reported it -- pod_id is the separate field that tells the
 * two apart downstream.
 *
 * The one type that needs care is temperature: SHT41 and DS18B20 both
 * report ST_SENSOR_TEMPERATURE_C, so the existing table disambiguates
 * them by slot (0 for SHT41, 3 for DS18B20) rather than by kind alone.
 * This table mirrors that slot assignment exactly for every hot-swap
 * sensor_id -- keep the two in sync if gateway_identity.c's table ever
 * changes.
 */

/* Returns 0 and sets *out_slot for a recognised hot-swap sensor_id.
 * Returns -1 for anything else (the caller should not encode a Zigbee
 * telemetry frame for an unrecognised sensor_id -- there is no slot that
 * would resolve correctly for it on the gateway side). */
int st_hotswap_zigbee_sensor_slot(const char *sensor_id, uint8_t *out_slot);

#endif
