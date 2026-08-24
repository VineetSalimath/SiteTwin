#include "sitetwin/hotswap_scan_scheduler.h"

#include <string.h>

int st_hotswap_scan_scheduler_init(st_hotswap_scan_scheduler_t *scheduler,
                                   uint32_t fast_interval_ms,
                                   uint32_t slow_interval_ms,
                                   uint32_t fast_window_ms)
{
    if (scheduler == NULL || fast_interval_ms == 0U || slow_interval_ms == 0U) {
        return -1;
    }
    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->fast_interval_ms = fast_interval_ms;
    scheduler->slow_interval_ms = slow_interval_ms;
    scheduler->fast_window_ms = fast_window_ms;
    return 0;
}

int st_hotswap_scan_scheduler_tick(st_hotswap_scan_scheduler_t *scheduler,
                                   uint8_t wake_levels, uint64_t now_ms)
{
    uint32_t required_interval;
    int in_fast_window;

    if (scheduler == NULL) {
        return 0;
    }

    if (scheduler->levels_valid == 0U) {
        /* First tick since init: nothing to compare against yet. Treat as
         * if a change had just happened (forces the fast interval for one
         * full fast_window_ms), and force an immediate scan regardless of
         * interval -- cold-boot module discovery must never wait on the
         * slow interval. */
        scheduler->wake_levels = wake_levels;
        scheduler->levels_valid = 1U;
        scheduler->last_change_at_ms = now_ms;
        scheduler->last_scan_at_ms = now_ms;
        scheduler->last_scan_valid = 1U;
        return 1;
    }

    if (wake_levels != scheduler->wake_levels) {
        scheduler->wake_levels = wake_levels;
        scheduler->last_change_at_ms = now_ms;
    }

    in_fast_window = (now_ms - scheduler->last_change_at_ms) < scheduler->fast_window_ms;
    required_interval = in_fast_window ? scheduler->fast_interval_ms
                                       : scheduler->slow_interval_ms;

    if (scheduler->last_scan_valid == 0U ||
        now_ms - scheduler->last_scan_at_ms >= required_interval) {
        scheduler->last_scan_at_ms = now_ms;
        scheduler->last_scan_valid = 1U;
        return 1;
    }
    return 0;
}
