#include "sitetwin/hotswap_zigbee_slot.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *sensor_id;
    uint8_t slot;
} st_hotswap_slot_entry_t;

/* Slot numbers copied verbatim from gateway_identity.c's
 * sensor_identity_table -- see this header's comment for why reuse
 * (rather than new slot numbers) is correct here. */
static const st_hotswap_slot_entry_t kSlotTable[] = {
    {"sht41_temperature", 0U},
    {"sht41_humidity", 1U},
    {"scd41_co2", 2U},
    {"sgp40_voc", 3U},
    {"bh1750_illuminance", 0U},
    {"pir_motion", 1U},
    {"reed_contact", 2U},
    {"ina219_voltage", 0U},
    {"ina219_current", 1U},
    {"adxl345_vibration", 2U},
    {"ds18b20_temperature", 3U},
    /* Port health/status events -- see gateway_identity.c's matching
     * table entries and st_pod_runtime_emit_health's doc comment. */
    {"port0_status", 10U},
    {"port1_status", 11U},
    {"port2_status", 12U},
    {"port3_status", 13U},
};

int st_hotswap_zigbee_sensor_slot(const char *sensor_id, uint8_t *out_slot)
{
    size_t index;

    if (sensor_id == NULL || out_slot == NULL) {
        return -1;
    }
    for (index = 0U; index < sizeof(kSlotTable) / sizeof(kSlotTable[0]); ++index) {
        if (strcmp(sensor_id, kSlotTable[index].sensor_id) == 0) {
            *out_slot = kSlotTable[index].slot;
            return 0;
        }
    }
    return -1;
}
