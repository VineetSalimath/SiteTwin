#include "sitetwin/reed.h"

#include <string.h>

int st_reed_debounce_init(st_reed_debounce_t *debounce, const st_reed_config_t *config)
{
    if (debounce == NULL || config == NULL) {
        return -1;
    }
    memset(debounce, 0, sizeof(*debounce));
    debounce->config = *config;
    return 0;
}

static st_reed_level_t raw_to_level(const st_reed_debounce_t *debounce, uint8_t raw_level)
{
    uint8_t is_high = raw_level != 0U;
    uint8_t is_open = debounce->config.open_when_raw_high != 0U ? is_high : (is_high == 0U);

    return is_open != 0U ? ST_REED_OPEN : ST_REED_CLOSED;
}

int st_reed_debounce_update(st_reed_debounce_t *debounce, uint64_t now_ms,
                            uint8_t raw_level, st_reed_event_t *event)
{
    uint8_t level = raw_level != 0U ? 1U : 0U;

    if (debounce == NULL) {
        return 0;
    }

    if (debounce->seeded == 0U) {
        /* First observation: establish the starting state without emitting
         * a transition -- there is nothing to have transitioned FROM yet. */
        debounce->seeded = 1U;
        debounce->candidate_raw_level = level;
        debounce->candidate_since_ms = now_ms;
        debounce->confirmed_raw_level = level;
        return 0;
    }

    if (level != debounce->candidate_raw_level) {
        /* Level changed since the last sample -- restart the debounce
         * timer. This is what collapses bounce: a burst of raw edges
         * within the debounce window never accumulates enough continuous
         * time at any one level to be confirmed. */
        debounce->candidate_raw_level = level;
        debounce->candidate_since_ms = now_ms;
        return 0;
    }

    if (level == debounce->confirmed_raw_level) {
        /* Stable at the level we already reported -- duplicate
         * suppression, no event storm from repeated sampling. */
        return 0;
    }

    if ((now_ms - debounce->candidate_since_ms) < (uint64_t)debounce->config.debounce_ms) {
        /* Steady, but not yet held long enough to trust it. */
        return 0;
    }

    debounce->confirmed_raw_level = level;
    ++debounce->transition_count;
    debounce->last_confirmed_at_ms = now_ms;

    if (event != NULL) {
        event->level = raw_to_level(debounce, level);
        event->confirmed_at_ms = now_ms;
    }
    return 1;
}

uint32_t st_reed_debounce_transition_count(const st_reed_debounce_t *debounce)
{
    return debounce == NULL ? 0U : debounce->transition_count;
}

uint64_t st_reed_debounce_last_confirmed_at_ms(const st_reed_debounce_t *debounce)
{
    return debounce == NULL ? 0U : debounce->last_confirmed_at_ms;
}