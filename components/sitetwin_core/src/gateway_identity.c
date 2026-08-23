#include "sitetwin/gateway_identity.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t source_address;
    const char *pod_id;
} st_gateway_pod_identity_t;

typedef struct {
    uint8_t sensor_slot;
    st_sensor_kind_t sensor_kind;
    const char *sensor_id;
} st_gateway_sensor_identity_t;

static const st_gateway_pod_identity_t pod_identity_table[] = {
    {ST_POD_1_SHORT_ADDRESS, ST_POD_1_ID},
    {ST_POD_2_SHORT_ADDRESS, ST_POD_2_ID},
    {ST_POD_3_SHORT_ADDRESS, ST_POD_3_ID},
    /* Whitelisted test-bench identities: real short addresses assigned during
     * ad-hoc Zigbee commissioning for the currently provisioned dev boards.
     * These are NOT a substitute for the planned IEEE-address provisioning
     * (see backlog item "Identity provisioning") and will need updating if
     * the coordinator's NVS is erased and pods re-join with new addresses. */
    {0x67C3U, "POD_67C3"},
    {0x6647U, "POD_6647"},
    {0x1FBAU, "POD_1FBA"},
    /* POD_3C60: the final-PCB hot-swap dev unit re-commissioned under a new
     * short address after an accidental gateway-wifi firmware flash
     * overwrote its zb_storage/zb_fct NVS partitions (that image's factory
     * partition spans 0x10000-0x210000, covering the Pod image's
     * zb_storage/zb_fct region at 0x10b000/0x10f000) -- exactly the
     * maintenance case the comment above already warned about. */
    {0x3C60U, "POD_3C60"},
};

static const st_gateway_sensor_identity_t sensor_identity_table[] = {
    {0U, ST_SENSOR_TEMPERATURE_C, "sht41_temperature"},
    {1U, ST_SENSOR_RELATIVE_HUMIDITY_PERCENT, "sht41_humidity"},
    {2U, ST_SENSOR_CO2_PPM, "scd41_co2"},
    {3U, ST_SENSOR_VOC_INDEX, "sgp40_voc"},
    {0U, ST_SENSOR_ILLUMINANCE_LUX, "bh1750_illuminance"},
    {1U, ST_SENSOR_MOTION, "pir_motion"},
    {2U, ST_SENSOR_CONTACT, "reed_contact"},
    {0U, ST_SENSOR_VOLTAGE_V, "ina219_voltage"},
    {1U, ST_SENSOR_CURRENT_MA, "ina219_current"},
    {2U, ST_SENSOR_VIBRATION_RMS_G, "adxl345_vibration"},
    {3U, ST_SENSOR_TEMPERATURE_C, "ds18b20_temperature"},
    /* Final-PCB hot-swap port health/status events (insertion, removal,
     * identification/attach faults) -- not a real sensor reading, so
     * ST_SENSOR_UNKNOWN is the deliberate marker kind. sensor_slot is
     * the only field that can distinguish which physical port an event
     * came from, since sensor_id itself is never transmitted over
     * Zigbee (it's reconstructed here). See
     * hotswap_zigbee_slot.h/st_pod_runtime_emit_health for the pod side
     * of this contract; keep the slot numbers in sync if either changes. */
    {10U, ST_SENSOR_UNKNOWN, "port0_status"},
    {11U, ST_SENSOR_UNKNOWN, "port1_status"},
    {12U, ST_SENSOR_UNKNOWN, "port2_status"},
    {13U, ST_SENSOR_UNKNOWN, "port3_status"},
};

static int copy_name(char *destination, size_t capacity, const char *source)
{
    size_t length;

    if (destination == NULL || capacity == 0U || source == NULL) {
        return -1;
    }
    length = strlen(source);
    if (length >= capacity) {
        return -1;
    }
    memcpy(destination, source, length + 1U);
    return 0;
}

int st_gateway_identity_resolve(uint16_t source_address, uint8_t sensor_slot,
                                st_sensor_kind_t sensor_kind,
                                char *pod_id, size_t pod_id_capacity,
                                char *sensor_id, size_t sensor_id_capacity)
{
    size_t index;
    int written;
    int pod_resolved = 0;
    int sensor_resolved = 0;

    if (pod_id == NULL || pod_id_capacity == 0U || sensor_id == NULL ||
        sensor_id_capacity == 0U || sensor_kind > ST_SENSOR_UNKNOWN) {
        return -1;
    }
    for (index = 0U; index < sizeof(pod_identity_table) / sizeof(pod_identity_table[0]);
         ++index) {
        if (pod_identity_table[index].source_address == source_address) {
            if (copy_name(pod_id, pod_id_capacity, pod_identity_table[index].pod_id) != 0) {
                return -1;
            }
            pod_resolved = 1;
            break;
        }
    }
    if (pod_resolved == 0) {
        written = snprintf(pod_id, pod_id_capacity, "POD_%04X", source_address);
        if (written < 0 || (size_t)written >= pod_id_capacity) {
            return -1;
        }
    }
    for (index = 0U;
         index < sizeof(sensor_identity_table) / sizeof(sensor_identity_table[0]); ++index) {
        if (sensor_identity_table[index].sensor_slot == sensor_slot &&
            sensor_identity_table[index].sensor_kind == sensor_kind) {
            if (copy_name(sensor_id, sensor_id_capacity,
                          sensor_identity_table[index].sensor_id) != 0) {
                return -1;
            }
            sensor_resolved = 1;
            break;
        }
    }
    if (sensor_resolved == 0) {
        written = snprintf(sensor_id, sensor_id_capacity, "unknown_slot_%u",
                           (unsigned int)sensor_slot);
        if (written < 0 || (size_t)written >= sensor_id_capacity) {
            return -1;
        }
    }
    return 0;
}

int st_gateway_identity_short_address(const char *pod_id, uint16_t *short_address)
{
    size_t index;

    if (pod_id == NULL || short_address == NULL) {
        return -1;
    }
    for (index = 0U; index < sizeof(pod_identity_table) / sizeof(pod_identity_table[0]);
         ++index) {
        if (strcmp(pod_identity_table[index].pod_id, pod_id) == 0) {
            *short_address = pod_identity_table[index].source_address;
            return 0;
        }
    }
    return -1;
}
