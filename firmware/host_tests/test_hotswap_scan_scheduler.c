#include <stdio.h>
#include <string.h>

#include "sitetwin/hotswap_scan_scheduler.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                         \
    } while (0)

static int test_init_rejects_invalid_args(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    EXPECT(st_hotswap_scan_scheduler_init(NULL, 200U, 5000U, 2000U) != 0);
    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 0U, 5000U, 2000U) != 0);
    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 0U, 2000U) != 0);
    /* fast_window_ms of 0 is a valid (if degenerate) configuration -- it
     * just means the fast interval never actually applies -- not an
     * error. */
    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 0U) == 0);
    return 0;
}

static int test_first_tick_always_scans_regardless_of_levels(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 2000U) == 0);
    /* Cold-boot discovery must never be delayed -- the very first tick
     * scans immediately even though nothing has "changed" yet (there is
     * no prior state to compare against). */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 0U) == 1);
    return 0;
}

static int test_no_change_falls_back_to_slow_interval_after_fast_window(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 2000U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 0U) == 1); /* first tick */

    /* Still inside the 2000ms fast window from boot, level unchanged:
     * should keep scanning at the 200ms fast interval. */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 199U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 200U) == 1);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 399U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 400U) == 1);

    /* Past the fast window (2000ms since the last real change, which was
     * at t=0) with still no level change: next scan should not happen
     * until the slow interval (5000ms) has elapsed since the last scan
     * (last scan was at t=400). */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 2001U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 5399U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 5400U) == 1);
    return 0;
}

static int test_change_reactivates_fast_window(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 2000U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 0U) == 1);

    /* Let it settle into the slow interval (past the fast window with no
     * change, same as the previous test). */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 2001U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 5000U) == 1); /* slow-interval scan */

    /* Now a real wake-pin change arrives (port 0's bit flips) well after
     * settling into slow mode. Since well over fast_interval_ms has
     * already elapsed since the last real scan (at t=5000), the change
     * triggers an immediate scan rather than waiting for the next
     * fast-interval tick. */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0EU, 10000U) == 1);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0EU, 10199U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0EU, 10200U) == 1); /* fast-interval scan */
    return 0;
}

static int test_multiple_simultaneous_bit_changes_treated_as_one_change(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 2000U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 0U) == 1);

    /* Two ports change in the same tick -- still just "a change happened".
     * Enough time has passed since the last scan (t=0), so it scans
     * immediately; the fast window then restarts from this single tick,
     * not compounded per bit. */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x03U, 3000U) == 1);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x03U, 3199U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x03U, 3200U) == 1);
    return 0;
}

static int test_bitmask_is_opaque_arbitrary_encoding_works(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    /* The scheduler doesn't interpret bit meaning (active-low vs
     * active-high is entirely the caller's concern) -- confirm it reacts
     * identically to a bitmask that happens to use the opposite
     * convention from the other tests. */
    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 2000U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x00U, 0U) == 1);
    /* Change from last scan's t=0 is far enough past that it scans
     * immediately rather than waiting a full fast_interval_ms. */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0xFFU, 2500U) == 1);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0xFFU, 2699U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0xFFU, 2700U) == 1);
    return 0;
}

static int test_zero_fast_window_never_applies_fast_interval(void)
{
    st_hotswap_scan_scheduler_t scheduler;

    EXPECT(st_hotswap_scan_scheduler_init(&scheduler, 200U, 5000U, 0U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 0U) == 1);

    /* fast_window_ms=0 means "in fast window" is only ever true at
     * exactly the instant of a change (now_ms - last_change_at_ms < 0 is
     * never true for unsigned arithmetic at the same instant either,
     * except the boundary now_ms==last_change_at_ms giving 0 < 0 =
     * false) -- so it should behave as permanently in slow mode. */
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 199U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 4999U) == 0);
    EXPECT(st_hotswap_scan_scheduler_tick(&scheduler, 0x0FU, 5000U) == 1);
    return 0;
}

int st_run_hotswap_scan_scheduler_tests(void)
{
    int failures = 0;
    failures += test_init_rejects_invalid_args();
    failures += test_first_tick_always_scans_regardless_of_levels();
    failures += test_no_change_falls_back_to_slow_interval_after_fast_window();
    failures += test_change_reactivates_fast_window();
    failures += test_multiple_simultaneous_bit_changes_treated_as_one_change();
    failures += test_bitmask_is_opaque_arbitrary_encoding_works();
    failures += test_zero_fast_window_never_applies_fast_interval();
    return failures;
}
