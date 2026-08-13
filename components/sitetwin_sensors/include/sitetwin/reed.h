#ifndef SITETWIN_REED_H
#define SITETWIN_REED_H

#include <stdint.h>

/* This is a pure debounce/confirmation state machine -- it has no GPIO
 * abstraction and no dependency on sitetwin_core or sitetwin_sensor_runtime.
 * It is deliberately NOT wired into st_physical_module_driver_t/
 * module_instance/logical_channel_adapter: reed (like PIR) is event-driven
 * and delivered through st_pod_runtime_emit_event() (ST_SENSOR_CONTACT),
 * not through the poll-based acquire()/read_channel() model that
 * BH1750/SHT41 use. Reading the physical GPIO and calling emit_event() at
 * the confirmed-transition callback below is Pod-composition-layer work,
 * not part of this driver.
 *
 * Caller contract: st_reed_debounce_update() must only ever be called from
 * task context with a timestamp and a GPIO level already read in that same
 * task context -- never from an ISR. See the driver plan's Section 6 event
 * injection design for why (32-bit MCU ISR/task data hand-off hazards). */

typedef enum {
    ST_REED_CLOSED = 0,
    ST_REED_OPEN = 1
} st_reed_level_t;

typedef struct {
    uint32_t debounce_ms;
    /* If non-zero, raw GPIO level 1 (logic HIGH) maps to ST_REED_OPEN and 0
     * maps to ST_REED_CLOSED. If zero, the mapping is inverted. Exposed so
     * callers don't have to assume a specific wiring/polarity. */
    uint8_t open_when_raw_high;
} st_reed_config_t;

typedef struct {
    st_reed_level_t level;
    uint64_t confirmed_at_ms;
} st_reed_event_t;

typedef struct {
    st_reed_config_t config;
    uint8_t candidate_raw_level;
    uint64_t candidate_since_ms;
    uint8_t confirmed_raw_level;
    uint8_t seeded;
    uint32_t transition_count;
    uint64_t last_confirmed_at_ms;
} st_reed_debounce_t;

int st_reed_debounce_init(st_reed_debounce_t *debounce, const st_reed_config_t *config);

/* Feed one (timestamp, raw GPIO level) sample. raw_level must be 0 or 1.
 *
 * Returns 1 and fills *event if this call confirmed a NEW debounced
 * transition (the level held steady for at least debounce_ms and differs
 * from the last confirmed level). Returns 0 otherwise -- including on the
 * very first call, which only seeds the baseline state without producing a
 * spurious "transition from unknown" event, and on repeated calls that
 * observe an already-confirmed level (duplicate suppression).
 *
 * Every genuine confirmed transition is reported exactly once, in order --
 * this function does not collapse multiple real transitions into only the
 * latest level. Rapid raw-level bouncing within the debounce window is
 * collapsed before ever reaching a confirmed transition. */
int st_reed_debounce_update(st_reed_debounce_t *debounce, uint64_t now_ms,
                            uint8_t raw_level, st_reed_event_t *event);

uint32_t st_reed_debounce_transition_count(const st_reed_debounce_t *debounce);
uint64_t st_reed_debounce_last_confirmed_at_ms(const st_reed_debounce_t *debounce);

#endif