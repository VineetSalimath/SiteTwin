#ifndef SITETWIN_CONTRACTS_H
#define SITETWIN_CONTRACTS_H

#include <stdint.h>

#define ST_CONTRACT_VERSION 1U
#define ST_POD_ID_MAX_LEN 16U
#define ST_SENSOR_ID_MAX_LEN 24U
#define ST_MODULE_UID_MAX_LEN 24U

typedef enum {
    ST_POD_ENVIRONMENT = 0,
    ST_POD_ACTIVITY_ACCESS,
    ST_POD_EQUIPMENT,
    /* Final four-port universal hot-swap PCB: any of the nine supported
     * module types may be plugged into any port at runtime, so this
     * profile's capability set is the union of all of them rather than a
     * fixed subset -- see st_profile_has_capability(). */
    ST_POD_UNIVERSAL
} st_pod_profile_t;

typedef enum {
    ST_SENSOR_TEMPERATURE_C = 0,
    ST_SENSOR_RELATIVE_HUMIDITY_PERCENT,
    ST_SENSOR_CO2_PPM,
    ST_SENSOR_VOC_INDEX,
    ST_SENSOR_ILLUMINANCE_LUX,
    ST_SENSOR_MOTION,
    ST_SENSOR_CONTACT,
    ST_SENSOR_CURRENT_MA,
    ST_SENSOR_VOLTAGE_V,
    ST_SENSOR_VIBRATION_RMS_G,
    ST_SENSOR_UNKNOWN
} st_sensor_kind_t;

typedef enum {
    ST_UNIT_CELSIUS = 0,
    ST_UNIT_PERCENT,
    ST_UNIT_PPM,
    ST_UNIT_INDEX,
    ST_UNIT_LUX,
    ST_UNIT_BOOLEAN,
    ST_UNIT_MILLIAMP,
    ST_UNIT_VOLT,
    ST_UNIT_G,
    ST_UNIT_NONE
} st_unit_t;

typedef enum {
    ST_RECORD_STATE = 0,
    ST_RECORD_EVENT,
    ST_RECORD_FEATURE,
    ST_RECORD_HEALTH
} st_record_class_t;

typedef enum {
    ST_PRIORITY_ROUTINE = 1,
    ST_PRIORITY_FEATURE = 2,
    ST_PRIORITY_HEALTH = 3,
    ST_PRIORITY_EVENT = 4
} st_delivery_priority_t;

enum {
    ST_QUALITY_VALID = 1U << 0,
    ST_QUALITY_WARMING_UP = 1U << 1,
    ST_QUALITY_STALE = 1U << 2,
    ST_QUALITY_CRC_FAILED = 1U << 3,
    ST_QUALITY_OUT_OF_RANGE = 1U << 4,
    ST_QUALITY_SENSOR_MISSING = 1U << 5,
    ST_QUALITY_COMPENSATION_UNAVAILABLE = 1U << 6,
    ST_QUALITY_BATTERY_LOW = 1U << 7,
    ST_QUALITY_CLIPPED = 1U << 8,
    ST_QUALITY_MOUNTING_CHANGED = 1U << 9
};

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    st_sensor_kind_t sensor_kind;
    st_unit_t unit;
    uint32_t sequence;
    uint32_t boot_id;
    uint64_t uptime_ms;
    float value;
    uint32_t quality_flags;
} st_sensor_reading_t;

typedef struct {
    st_record_class_t record_class;
    st_delivery_priority_t priority;
    st_sensor_reading_t reading;
} st_telemetry_record_t;

typedef struct {
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    char module_uid[ST_MODULE_UID_MAX_LEN];
    st_sensor_kind_t sensor_kind;
    st_unit_t unit;
    uint32_t sample_interval_ms;
} st_module_metadata_t;

const char *st_sensor_kind_name(st_sensor_kind_t kind);
const char *st_unit_name(st_unit_t unit);
const char *st_record_class_name(st_record_class_t record_class);

#endif
