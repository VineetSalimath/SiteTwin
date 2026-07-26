#include "sitetwin/contracts.h"

const char *st_sensor_kind_name(st_sensor_kind_t kind)
{
    switch (kind) {
    case ST_SENSOR_TEMPERATURE_C:
        return "temperature_c";
    case ST_SENSOR_RELATIVE_HUMIDITY_PERCENT:
        return "relative_humidity_percent";
    case ST_SENSOR_CO2_PPM:
        return "co2_ppm";
    case ST_SENSOR_VOC_INDEX:
        return "voc_index";
    case ST_SENSOR_ILLUMINANCE_LUX:
        return "illuminance_lux";
    case ST_SENSOR_MOTION:
        return "motion";
    case ST_SENSOR_CONTACT:
        return "contact";
    case ST_SENSOR_CURRENT_MA:
        return "current_ma";
    case ST_SENSOR_VOLTAGE_V:
        return "voltage_v";
    case ST_SENSOR_VIBRATION_RMS_G:
        return "vibration_rms_g";
    case ST_SENSOR_UNKNOWN:
    default:
        return "unknown";
    }
}

const char *st_unit_name(st_unit_t unit)
{
    switch (unit) {
    case ST_UNIT_CELSIUS:
        return "celsius";
    case ST_UNIT_PERCENT:
        return "percent";
    case ST_UNIT_PPM:
        return "ppm";
    case ST_UNIT_INDEX:
        return "index";
    case ST_UNIT_LUX:
        return "lux";
    case ST_UNIT_BOOLEAN:
        return "boolean";
    case ST_UNIT_MILLIAMP:
        return "milliamp";
    case ST_UNIT_VOLT:
        return "volt";
    case ST_UNIT_G:
        return "g";
    case ST_UNIT_NONE:
    default:
        return "none";
    }
}

const char *st_record_class_name(st_record_class_t record_class)
{
    switch (record_class) {
    case ST_RECORD_STATE:
        return "state";
    case ST_RECORD_EVENT:
        return "event";
    case ST_RECORD_FEATURE:
        return "feature";
    case ST_RECORD_HEALTH:
        return "health";
    default:
        return "unknown";
    }
}
