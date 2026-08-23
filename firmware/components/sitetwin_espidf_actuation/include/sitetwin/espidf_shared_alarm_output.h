#ifndef SITETWIN_ESPIDF_SHARED_ALARM_OUTPUT_H
#define SITETWIN_ESPIDF_SHARED_ALARM_OUTPUT_H

#include "esp_err.h"

#include "sitetwin/shared_alarm_indicator.h"

/*
 * Owns GPIO19 as a single LEDC PWM channel for the final PCB's shared
 * buzzer/LED branch. Unlike st_espidf_local_output_t (espidf_actuation.h,
 * two independent GPIOs for the fixed pod profiles), there is only one
 * physical output here -- LED and buzzer are electrically the same
 * signal, so there is no per-kind (LED vs buzzer) distinction to make at
 * this layer, or anywhere above it for this profile.
 */

typedef struct {
    int gpio;
    uint32_t frequency_hz;
    uint8_t initialised;
    uint8_t last_duty;
    st_shared_alarm_indicator_t indicator;
} st_espidf_shared_alarm_output_t;

esp_err_t st_espidf_shared_alarm_output_init(st_espidf_shared_alarm_output_t *driver, int gpio,
                                             uint32_t frequency_hz);

/*
 * Call on every tick (matching st_shared_alarm_indicator_tick's own
 * cadence expectations, e.g. every 50ms). desired_active: should the
 * indicator be blinking right now -- the caller's decision (typically
 * any_alarm_active && !silenced, or a test_output pulse), not this
 * driver's; it only owns turning that decision into GPIO19's PWM duty
 * on the correct on/off timing.
 */
esp_err_t st_espidf_shared_alarm_output_tick(st_espidf_shared_alarm_output_t *driver,
                                             uint8_t desired_active, uint64_t now_ms);

#endif
