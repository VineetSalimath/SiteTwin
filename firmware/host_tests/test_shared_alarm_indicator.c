#include <stdio.h>

#include "sitetwin/shared_alarm_indicator.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                         \
    } while (0)

static int test_inactive_always_returns_zero(void)
{
    st_shared_alarm_indicator_t indicator;

    st_shared_alarm_indicator_init(&indicator);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 0U, 0U) == 0U);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 0U, 100000U) == 0U);
    return 0;
}

static int test_active_follows_on_off_cycle(void)
{
    st_shared_alarm_indicator_t indicator;
    uint64_t start = 5000U;

    st_shared_alarm_indicator_init(&indicator);

    /* Whole ON phase: [start, start+2000) */
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start) == ST_SHARED_ALARM_PWM_ON_DUTY);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 1U) ==
           ST_SHARED_ALARM_PWM_ON_DUTY);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 1999U) ==
           ST_SHARED_ALARM_PWM_ON_DUTY);

    /* OFF phase: [start+2000, start+3000) */
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 2000U) == 0U);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 2999U) == 0U);

    /* Second cycle's ON phase starts again at start+3000 */
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 3000U) ==
           ST_SHARED_ALARM_PWM_ON_DUTY);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 4999U) ==
           ST_SHARED_ALARM_PWM_ON_DUTY);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, start + 5000U) == 0U);

    return 0;
}

static int test_deactivate_then_reactivate_restarts_cycle(void)
{
    st_shared_alarm_indicator_t indicator;

    st_shared_alarm_indicator_init(&indicator);

    /* Run partway into the OFF phase of a cycle. */
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, 0U) == ST_SHARED_ALARM_PWM_ON_DUTY);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, 2500U) == 0U);

    /* Deactivate, then wait an arbitrary amount of real time before
     * reactivating -- the new activation must start a fresh ON phase
     * immediately, not resume wherever the stale cycle would have been. */
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 0U, 2600U) == 0U);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, 999999U) ==
           ST_SHARED_ALARM_PWM_ON_DUTY);
    EXPECT(st_shared_alarm_indicator_tick(&indicator, 1U, 999999U + 2000U) == 0U);

    return 0;
}

static int test_null_indicator_is_safe(void)
{
    EXPECT(st_shared_alarm_indicator_tick(NULL, 1U, 0U) == 0U);
    st_shared_alarm_indicator_init(NULL); /* must not crash */
    return 0;
}

int st_run_shared_alarm_indicator_tests(void)
{
    int failures = 0;

    failures += test_inactive_always_returns_zero();
    failures += test_active_follows_on_off_cycle();
    failures += test_deactivate_then_reactivate_restarts_cycle();
    failures += test_null_indicator_is_safe();

    if (failures == 0) {
        printf("test_shared_alarm_indicator: all tests passed\n");
    }
    return failures;
}
