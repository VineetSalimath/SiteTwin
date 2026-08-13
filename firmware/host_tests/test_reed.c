#include <stdio.h>

#include "sitetwin/reed.h"

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

static int init_default(st_reed_debounce_t *debounce, uint32_t debounce_ms)
{
    st_reed_config_t config;

    config.debounce_ms = debounce_ms;
    /* Matches the prototype's documented wiring: INPUT_PULLUP, magnet
     * present (closed) reads LOW, absent (open) reads HIGH -- so raw HIGH
     * maps to OPEN. */
    config.open_when_raw_high = 1U;
    return st_reed_debounce_init(debounce, &config);
}

static int test_reed_seed_produces_no_event(void)
{
    st_reed_debounce_t debounce;
    st_reed_event_t event;

    EXPECT(init_default(&debounce, 30U) == 0);
    /* First-ever observation, closed (raw LOW). Must not report a spurious
     * "transition from unknown state". */
    EXPECT(st_reed_debounce_update(&debounce, 0U, 0U, &event) == 0);
    EXPECT(st_reed_debounce_transition_count(&debounce) == 0U);
    return 0;
}

static int test_reed_bounce_rejection(void)
{
    st_reed_debounce_t debounce;
    st_reed_event_t event;
    uint64_t now_ms;

    EXPECT(init_default(&debounce, 30U) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 0U, 0U, &event) == 0); /* seed: closed */

    /* Mechanical bounce: the contact chatters between 0 and 1 for a few
     * milliseconds, never settling long enough at either level to reach
     * the 30 ms debounce window. None of these should confirm. */
    for (now_ms = 1U; now_ms < 20U; now_ms += 2U) {
        uint8_t raw = (uint8_t)((now_ms / 2U) % 2U);

        EXPECT(st_reed_debounce_update(&debounce, now_ms, raw, &event) == 0);
    }
    EXPECT(st_reed_debounce_transition_count(&debounce) == 0U);
    return 0;
}

static int test_reed_open_then_closed_transitions(void)
{
    st_reed_debounce_t debounce;
    st_reed_event_t event;

    EXPECT(init_default(&debounce, 30U) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 0U, 0U, &event) == 0); /* seed: closed */

    /* Raw goes HIGH (door opens) and stays there. Nothing confirms before
     * 30 ms have elapsed since the level change. */
    EXPECT(st_reed_debounce_update(&debounce, 5U, 1U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 20U, 1U, &event) == 0);
    /* At exactly 5 + 30 = 35 ms of continuous HIGH, the transition confirms. */
    EXPECT(st_reed_debounce_update(&debounce, 35U, 1U, &event) == 1);
    EXPECT(event.level == ST_REED_OPEN);
    EXPECT(event.confirmed_at_ms == 35U);
    EXPECT(st_reed_debounce_transition_count(&debounce) == 1U);

    /* Continuing to sample the same (already-confirmed) level must not
     * re-fire -- duplicate suppression. */
    EXPECT(st_reed_debounce_update(&debounce, 40U, 1U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 100U, 1U, &event) == 0);
    EXPECT(st_reed_debounce_transition_count(&debounce) == 1U);

    /* Door closes: raw goes back LOW and stays there past the debounce
     * window -- a second, distinct confirmed transition. */
    EXPECT(st_reed_debounce_update(&debounce, 200U, 0U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 229U, 0U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 230U, 0U, &event) == 1);
    EXPECT(event.level == ST_REED_CLOSED);
    EXPECT(event.confirmed_at_ms == 230U);
    EXPECT(st_reed_debounce_transition_count(&debounce) == 2U);
    EXPECT(st_reed_debounce_last_confirmed_at_ms(&debounce) == 230U);
    return 0;
}

static int test_reed_configurable_polarity(void)
{
    st_reed_debounce_t debounce;
    st_reed_config_t config;
    st_reed_event_t event;

    /* Inverted wiring: raw HIGH means closed, raw LOW means open. */
    config.debounce_ms = 10U;
    config.open_when_raw_high = 0U;
    EXPECT(st_reed_debounce_init(&debounce, &config) == 0);

    EXPECT(st_reed_debounce_update(&debounce, 0U, 1U, &event) == 0); /* seed: closed (raw HIGH) */
    EXPECT(st_reed_debounce_update(&debounce, 5U, 0U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 15U, 0U, &event) == 1);
    EXPECT(event.level == ST_REED_OPEN);
    return 0;
}

static int test_reed_transition_ordering_not_collapsed_to_latest(void)
{
    st_reed_debounce_t debounce;
    st_reed_event_t event;

    EXPECT(init_default(&debounce, 10U) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 0U, 0U, &event) == 0); /* seed: closed */

    /* Three genuine, individually-confirmed transitions in sequence. Each
     * call that crosses its own debounce window must report exactly one
     * event -- the caller (not this state machine) is responsible for
     * queuing/delivering all three; this test only confirms the state
     * machine itself never silently skips or merges a real transition. */
    EXPECT(st_reed_debounce_update(&debounce, 100U, 1U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 111U, 1U, &event) == 1);
    EXPECT(event.level == ST_REED_OPEN);

    EXPECT(st_reed_debounce_update(&debounce, 200U, 0U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 211U, 0U, &event) == 1);
    EXPECT(event.level == ST_REED_CLOSED);

    EXPECT(st_reed_debounce_update(&debounce, 300U, 1U, &event) == 0);
    EXPECT(st_reed_debounce_update(&debounce, 311U, 1U, &event) == 1);
    EXPECT(event.level == ST_REED_OPEN);

    EXPECT(st_reed_debounce_transition_count(&debounce) == 3U);
    return 0;
}

int st_run_reed_tests(void)
{
    int failures = 0;

    failures += test_reed_seed_produces_no_event();
    failures += test_reed_bounce_rejection();
    failures += test_reed_open_then_closed_transitions();
    failures += test_reed_configurable_polarity();
    failures += test_reed_transition_ordering_not_collapsed_to_latest();
    return failures;
}