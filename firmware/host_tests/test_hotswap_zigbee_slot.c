#include <stdio.h>
#include <string.h>

#include "sitetwin/gateway_identity.h"
#include "sitetwin/hotswap_zigbee_slot.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                         \
    } while (0)

typedef struct {
    const char *sensor_id;
    st_sensor_kind_t kind;
} st_expected_entry_t;

static const st_expected_entry_t kExpected[] = {
    {"sht41_temperature", ST_SENSOR_TEMPERATURE_C},
    {"sht41_humidity", ST_SENSOR_RELATIVE_HUMIDITY_PERCENT},
    {"scd41_co2", ST_SENSOR_CO2_PPM},
    {"sgp40_voc", ST_SENSOR_VOC_INDEX},
    {"bh1750_illuminance", ST_SENSOR_ILLUMINANCE_LUX},
    {"pir_motion", ST_SENSOR_MOTION},
    {"reed_contact", ST_SENSOR_CONTACT},
    {"ina219_voltage", ST_SENSOR_VOLTAGE_V},
    {"ina219_current", ST_SENSOR_CURRENT_MA},
    {"adxl345_vibration", ST_SENSOR_VIBRATION_RMS_G},
    {"ds18b20_temperature", ST_SENSOR_TEMPERATURE_C},
    {"port0_status", ST_SENSOR_UNKNOWN},
    {"port1_status", ST_SENSOR_UNKNOWN},
    {"port2_status", ST_SENSOR_UNKNOWN},
    {"port3_status", ST_SENSOR_UNKNOWN},
};

/* This is the real, load-bearing test: every hot-swap sensor_id's chosen
 * slot must round-trip through the REAL production gateway_identity.c
 * resolver (not a mock of it) back to that exact same sensor_id -- using
 * a made-up hot-swap short address that is NOT in pod_identity_table, to
 * prove the sensor-side resolution genuinely does not depend on which
 * pod sent it. */
static int test_every_hotswap_sensor_id_round_trips_through_real_gateway_identity(void)
{
    const uint16_t hotswap_pod_address = 0xABCDU; /* deliberately not in pod_identity_table */
    size_t i;

    for (i = 0U; i < sizeof(kExpected) / sizeof(kExpected[0]); ++i) {
        uint8_t slot = 0U;
        char pod_id[32];
        char resolved_sensor_id[ST_SENSOR_ID_MAX_LEN];

        EXPECT(st_hotswap_zigbee_sensor_slot(kExpected[i].sensor_id, &slot) == 0);
        EXPECT(st_gateway_identity_resolve(hotswap_pod_address, slot, kExpected[i].kind, pod_id,
                                           sizeof(pod_id), resolved_sensor_id,
                                           sizeof(resolved_sensor_id)) == 0);
        EXPECT(strcmp(resolved_sensor_id, kExpected[i].sensor_id) == 0);
        /* Unresolved pod address falls back to the auto-generated
         * POD_XXXX form -- confirms this path really did NOT depend on a
         * pod_identity_table entry existing for the hot-swap pod. */
        EXPECT(strcmp(pod_id, "POD_ABCD") == 0);
    }
    return 0;
}

static int test_unknown_sensor_id_returns_error(void)
{
    uint8_t slot = 0U;

    EXPECT(st_hotswap_zigbee_sensor_slot("not_a_real_sensor", &slot) != 0);
    EXPECT(st_hotswap_zigbee_sensor_slot(NULL, &slot) != 0);
    EXPECT(st_hotswap_zigbee_sensor_slot("sht41_temperature", NULL) != 0);
    return 0;
}

static int test_temperature_ambiguity_is_resolved_by_slot(void)
{
    uint8_t sht41_slot = 0U;
    uint8_t ds18b20_slot = 0U;

    EXPECT(st_hotswap_zigbee_sensor_slot("sht41_temperature", &sht41_slot) == 0);
    EXPECT(st_hotswap_zigbee_sensor_slot("ds18b20_temperature", &ds18b20_slot) == 0);
    /* Same sensor_kind (ST_SENSOR_TEMPERATURE_C), so the slots MUST
     * differ or the two would be indistinguishable to the gateway. */
    EXPECT(sht41_slot != ds18b20_slot);

    return 0;
}

int st_run_hotswap_zigbee_slot_tests(void)
{
    int failures = 0;

    failures += test_every_hotswap_sensor_id_round_trips_through_real_gateway_identity();
    failures += test_unknown_sensor_id_returns_error();
    failures += test_temperature_ambiguity_is_resolved_by_slot();

    if (failures == 0) {
        printf("test_hotswap_zigbee_slot: all tests passed\n");
    }
    return failures;
}
