#include <stdio.h>
#include <string.h>

#include "sitetwin/gateway_identity.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

static int test_numbered_identity_and_reverse_route(void)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint16_t address = 0U;

    EXPECT(st_gateway_identity_resolve(ST_POD_1_SHORT_ADDRESS, 2U, ST_SENSOR_CO2_PPM,
                                       pod_id, sizeof(pod_id), sensor_id,
                                       sizeof(sensor_id)) == 0);
    EXPECT(strcmp(pod_id, ST_POD_1_ID) == 0);
    EXPECT(strcmp(sensor_id, "scd41_co2") == 0);
    EXPECT(st_gateway_identity_short_address(pod_id, &address) == 0);
    EXPECT(address == ST_POD_1_SHORT_ADDRESS);

    EXPECT(st_gateway_identity_resolve(ST_POD_2_SHORT_ADDRESS, 1U, ST_SENSOR_MOTION,
                                       pod_id, sizeof(pod_id), sensor_id,
                                       sizeof(sensor_id)) == 0);
    EXPECT(strcmp(pod_id, ST_POD_2_ID) == 0);
    EXPECT(strcmp(sensor_id, "pir_motion") == 0);

    EXPECT(st_gateway_identity_resolve(ST_POD_3_SHORT_ADDRESS, 3U,
                                       ST_SENSOR_TEMPERATURE_C, pod_id,
                                       sizeof(pod_id), sensor_id, sizeof(sensor_id)) == 0);
    EXPECT(strcmp(pod_id, ST_POD_3_ID) == 0);
    EXPECT(strcmp(sensor_id, "ds18b20_temperature") == 0);
    return 0;
}

static int test_unknown_identity_is_not_promoted(void)
{
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint16_t address;

    EXPECT(st_gateway_identity_resolve(0xBEEFU, 7U, ST_SENSOR_UNKNOWN,
                                       pod_id, sizeof(pod_id), sensor_id,
                                       sizeof(sensor_id)) == 0);
    EXPECT(strcmp(pod_id, "POD_BEEF") == 0);
    EXPECT(strcmp(sensor_id, "unknown_slot_7") == 0);
    EXPECT(st_gateway_identity_short_address(pod_id, &address) != 0);
    return 0;
}

int st_run_gateway_identity_tests(void)
{
    int failures = 0;
    failures += test_numbered_identity_and_reverse_route();
    failures += test_unknown_identity_is_not_promoted();
    return failures;
}
