#ifndef SITETWIN_SHARED_ALARM_INDICATOR_H
#define SITETWIN_SHARED_ALARM_INDICATOR_H

#include <stdint.h>

/*
 * Portable timing state machine for the final PCB's shared GPIO19
 * buzzer/LED branch. Deliberately mirrors the hardware team's own
 * second reference .ino (updateAlarmPattern/serviceAlarmOutput) rather
 * than inventing a new pattern -- that timing is what was actually
 * validated on real hardware (2s on / 1s off, 8-bit PWM duty), and
 * electrical behaviour this board hasn't independently re-characterised
 * is not a place to improvise.
 *
 * Owns only the ON/OFF blink timing. The caller decides *whether* the
 * indicator should be blinking at all (typically any_alarm_active &&
 * !silenced, or a test_output pulse) and supplies that as
 * desired_active on every tick -- this state machine has no opinion
 * about alarm/command semantics.
 */

#define ST_SHARED_ALARM_ON_MS 2000U
#define ST_SHARED_ALARM_OFF_MS 1000U
/* 8-bit PWM duty (0-255), matching the hardware team's own validated
 * ALARM_PWM_ON_DUTY constant -- not independently re-tuned here. */
#define ST_SHARED_ALARM_PWM_ON_DUTY 128U

typedef struct {
    uint8_t running;
    uint64_t started_at_ms;
} st_shared_alarm_indicator_t;

void st_shared_alarm_indicator_init(st_shared_alarm_indicator_t *indicator);

/*
 * Call on every tick (any cadence finer than ST_SHARED_ALARM_ON_MS/
 * OFF_MS is fine -- e.g. the existing 50ms pod_command_task loop).
 * Returns the PWM duty (0-255) that should be written to GPIO19 this
 * tick: 0 whenever desired_active is false, ST_SHARED_ALARM_PWM_ON_DUTY
 * during the ON phase of the blink cycle, 0 during the OFF phase.
 * Reactivating after a false tick always restarts the cycle from its ON
 * phase (matches the .ino's own restart-on-reactivation behaviour) --
 * it never resumes a stale phase from a previous activation.
 */
uint8_t st_shared_alarm_indicator_tick(st_shared_alarm_indicator_t *indicator,
                                       uint8_t desired_active, uint64_t now_ms);

#endif
