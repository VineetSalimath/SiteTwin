#include <stdio.h>
#include <string.h>

#include "sitetwin/gateway_identity.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/zigbee_payload.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    uint16_t source_address;
    uint8_t slot;
    st_sensor_kind_t kind;
    const char *pod_id;
    const char *sensor_id;
} expected_identity_t;

static int test_canonical_identity_table(void)
{
    static const expected_identity_t expected[] = {
        {ST_POD_1_SHORT_ADDRESS, 0U, ST_SENSOR_TEMPERATURE_C, "POD_1", "sht41_temperature"},
        {ST_POD_1_SHORT_ADDRESS, 1U, ST_SENSOR_RELATIVE_HUMIDITY_PERCENT, "POD_1", "sht41_humidity"},
        {ST_POD_1_SHORT_ADDRESS, 2U, ST_SENSOR_CO2_PPM, "POD_1", "scd41_co2"},
        {ST_POD_1_SHORT_ADDRESS, 3U, ST_SENSOR_VOC_INDEX, "POD_1", "sgp40_voc"},
        {ST_POD_2_SHORT_ADDRESS, 0U, ST_SENSOR_ILLUMINANCE_LUX, "POD_2", "bh1750_illuminance"},
        {ST_POD_2_SHORT_ADDRESS, 1U, ST_SENSOR_MOTION, "POD_2", "pir_motion"},
        {ST_POD_2_SHORT_ADDRESS, 2U, ST_SENSOR_CONTACT, "POD_2", "reed_contact"},
        {ST_POD_3_SHORT_ADDRESS, 0U, ST_SENSOR_VOLTAGE_V, "POD_3", "ina219_voltage"},
        {ST_POD_3_SHORT_ADDRESS, 1U, ST_SENSOR_CURRENT_MA, "POD_3", "ina219_current"},
        {ST_POD_3_SHORT_ADDRESS, 2U, ST_SENSOR_VIBRATION_RMS_G, "POD_3", "adxl345_vibration"},
        {ST_POD_3_SHORT_ADDRESS, 3U, ST_SENSOR_TEMPERATURE_C, "POD_3", "ds18b20_temperature"},
    };
    size_t index;

    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        char pod_id[ST_POD_ID_MAX_LEN];
        char sensor_id[ST_SENSOR_ID_MAX_LEN];

        EXPECT(st_gateway_identity_resolve(expected[index].source_address,
                                            expected[index].slot,
                                            expected[index].kind,
                                            pod_id, sizeof(pod_id),
                                            sensor_id, sizeof(sensor_id)) == 0);
        EXPECT(strcmp(pod_id, expected[index].pod_id) == 0);
        EXPECT(strcmp(sensor_id, expected[index].sensor_id) == 0);
    }
    return 0;
}

static int test_pod_identity_does_not_depend_on_sensor_type(void)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];

    EXPECT(st_gateway_identity_resolve(ST_POD_1_SHORT_ADDRESS, 2U,
                                        ST_SENSOR_CONTACT,
                                        pod_id, sizeof(pod_id),
                                        sensor_id, sizeof(sensor_id)) == 0);
    EXPECT(strcmp(pod_id, "POD_1") == 0);
    EXPECT(strcmp(sensor_id, "reed_contact") == 0);
    return 0;
}

static int test_same_slot_different_pods_do_not_collide(void)
{
    st_gateway_runtime_t runtime;
    st_telemetry_record_t record;
    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length;
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];

    st_gateway_runtime_init(&runtime);
    memset(&record, 0, sizeof(record));
    record.record_class = ST_RECORD_EVENT;
    record.priority = ST_PRIORITY_EVENT;
    record.reading.sensor_kind = ST_SENSOR_CONTACT;
    record.reading.unit = ST_UNIT_BOOLEAN;
    record.reading.sequence = 1U;
    record.reading.boot_id = 2U;
    record.reading.value = 1.0F;
    record.reading.quality_flags = ST_QUALITY_VALID;
    EXPECT(st_zigbee_telemetry_encode(&record, 2U, payload, sizeof(payload),
                                       &payload_length) == 0);
    EXPECT(st_gateway_identity_resolve(0x9393U, 2U, record.reading.sensor_kind,
                                        pod_id, sizeof(pod_id), sensor_id,
                                        sizeof(sensor_id)) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length,
                                             pod_id, sensor_id) ==
           ST_GATEWAY_INGRESS_ACCEPTED);

    record.record_class = ST_RECORD_FEATURE;
    record.priority = ST_PRIORITY_FEATURE;
    record.reading.sensor_kind = ST_SENSOR_VIBRATION_RMS_G;
    record.reading.unit = ST_UNIT_G;
    record.reading.boot_id = 3U;
    record.reading.value = 0.004F;
    EXPECT(st_zigbee_telemetry_encode(&record, 2U, payload, sizeof(payload),
                                       &payload_length) == 0);
    EXPECT(st_gateway_identity_resolve(0x304EU, 2U, record.reading.sensor_kind,
                                        pod_id, sizeof(pod_id), sensor_id,
                                        sizeof(sensor_id)) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length,
                                             pod_id, sensor_id) ==
           ST_GATEWAY_INGRESS_ACCEPTED);
    EXPECT(runtime.processor.accepted == 2U);
    EXPECT(runtime.processor.stale == 0U);
    return 0;
}

static int test_unknown_identity_is_source_scoped(void)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];

    EXPECT(st_gateway_identity_resolve(0xBEEFU, 5U, ST_SENSOR_UNKNOWN,
                                        pod_id, sizeof(pod_id),
                                        sensor_id, sizeof(sensor_id)) == 0);
    EXPECT(strcmp(pod_id, "POD_BEEF") == 0);
    EXPECT(strcmp(sensor_id, "unknown_slot_5") == 0);
    return 0;
}

int st_run_gateway_identity_tests(void)
{
    int failures = 0;

    failures += test_canonical_identity_table();
    failures += test_pod_identity_does_not_depend_on_sensor_type();
    failures += test_same_slot_different_pods_do_not_collide();
    failures += test_unknown_identity_is_source_scoped();
    return failures;
}
