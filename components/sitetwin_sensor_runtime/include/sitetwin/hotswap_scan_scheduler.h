#ifndef SITETWIN_HOTSWAP_SCAN_SCHEDULER_H
#define SITETWIN_HOTSWAP_SCAN_SCHEDULER_H

#include <stdint.h>

/*
 * Decides when board_port_manager's real (ADC-based) identification scan
 * should actually run, using the four TS884 wake pins (empirically
 * confirmed active-low -- see project decision log) as a cheap early
 * signal instead of scanning on a fixed short interval forever.
 *
 * Pure decision logic, no GPIO/HAL access -- the caller reads the wake
 * pins and passes the resulting bitmask in. Fully host-testable.
 *
 * Behaviour: for fast_window_ms after the last observed wake-pin change
 * (bitmask differs from the previous tick), scans run at fast_interval_ms
 * -- matching the existing, already-verified detection latency. Once that
 * window elapses with no further change, scans fall back to the much
 * longer slow_interval_ms. The very first tick after init always scans
 * immediately, regardless of interval, so cold-boot module discovery is
 * never delayed by this scheduler.
 *
 * This never touches board_port_manager, hotswap_module_binding, or any
 * attach/detach/debounce logic -- it only changes how often the existing,
 * unmodified scan is invoked.
 */

typedef struct {
    uint8_t wake_levels;
    uint8_t levels_valid;
    uint64_t last_change_at_ms;
    uint64_t last_scan_at_ms;
    uint8_t last_scan_valid;
    uint32_t fast_interval_ms;
    uint32_t slow_interval_ms;
    uint32_t fast_window_ms;
} st_hotswap_scan_scheduler_t;

/*
 * fast_interval_ms/slow_interval_ms must both be > 0. fast_interval_ms
 * should be <= slow_interval_ms (not enforced, but a scheduler configured
 * the other way around would be nonsensical). Returns 0 on success, -1 on
 * a NULL scheduler or either interval being 0.
 */
int st_hotswap_scan_scheduler_init(st_hotswap_scan_scheduler_t *scheduler,
                                   uint32_t fast_interval_ms,
                                   uint32_t slow_interval_ms,
                                   uint32_t fast_window_ms);

/*
 * Call every tick with the current wake-pin level bitmask (bit N = 1 for
 * port N's wake pin read HIGH, 0 for LOW -- caller's responsibility to
 * pack it; this module treats it as an opaque bitmask, comparison only,
 * so it does not itself need to know the active-low convention) and the
 * current monotonic time. Returns 1 if the caller should run a real
 * board_port_manager_poll() now, 0 otherwise. NULL scheduler returns 0
 * (no scan) rather than treating it as an error, since this is a
 * scheduling hint, not a correctness-critical call the caller must check.
 */
int st_hotswap_scan_scheduler_tick(st_hotswap_scan_scheduler_t *scheduler,
                                   uint8_t wake_levels, uint64_t now_ms);

#endif
