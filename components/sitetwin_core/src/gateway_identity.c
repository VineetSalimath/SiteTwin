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
    int pod_length;
    int sensor_length;
    int pod_resolved = 0;
    int sensor_resolved = 0;

    if (pod_id == NULL || pod_id_capacity == 0U ||
        sensor_id == NULL || sensor_id_capacity == 0U ||
        sensor_kind > ST_SENSOR_UNKNOWN) {
        return -1;
    }

    for (index = 0U; index < sizeof(pod_identity_table) / sizeof(pod_identity_table[0]);
         ++index) {
        const st_gateway_pod_identity_t *entry = &pod_identity_table[index];

        if (entry->source_address == source_address) {
            if (copy_name(pod_id, pod_id_capacity, entry->pod_id) != 0) {
                return -1;
            }
            pod_resolved = 1;
            break;
        }
    }
    if (pod_resolved == 0) {
        pod_length = snprintf(pod_id, pod_id_capacity, "POD_%04X", source_address);
        if (pod_length < 0 || (size_t)pod_length >= pod_id_capacity) {
            return -1;
        }
    }

    for (index = 0U;
         index < sizeof(sensor_identity_table) / sizeof(sensor_identity_table[0]);
         ++index) {
        const st_gateway_sensor_identity_t *entry = &sensor_identity_table[index];

        if (entry->sensor_slot == sensor_slot && entry->sensor_kind == sensor_kind) {
            if (copy_name(sensor_id, sensor_id_capacity, entry->sensor_id) != 0) {
                return -1;
            }
            sensor_resolved = 1;
            break;
        }
    }
    if (sensor_resolved == 0) {
        sensor_length = snprintf(sensor_id, sensor_id_capacity, "unknown_slot_%u",
                                 (unsigned int)sensor_slot);
        if (sensor_length < 0 || (size_t)sensor_length >= sensor_id_capacity) {
            return -1;
        }
    }
    return 0;
}
