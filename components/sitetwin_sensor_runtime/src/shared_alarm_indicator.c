#include "sitetwin/shared_alarm_indicator.h"

#include <string.h>

void st_shared_alarm_indicator_init(st_shared_alarm_indicator_t *indicator)
{
    if (indicator != NULL) {
        memset(indicator, 0, sizeof(*indicator));
    }
}

uint8_t st_shared_alarm_indicator_tick(st_shared_alarm_indicator_t *indicator,
                                       uint8_t desired_active, uint64_t now_ms)
{
    uint64_t cycle_ms;
    uint64_t elapsed_ms;

    if (indicator == NULL) {
        return 0U;
    }
    if (desired_active == 0U) {
        indicator->running = 0U;
        return 0U;
    }
    if (indicator->running == 0U) {
        indicator->running = 1U;
        indicator->started_at_ms = now_ms;
    }
    cycle_ms = (uint64_t)ST_SHARED_ALARM_ON_MS + (uint64_t)ST_SHARED_ALARM_OFF_MS;
    elapsed_ms = now_ms - indicator->started_at_ms;
    return (elapsed_ms % cycle_ms) < (uint64_t)ST_SHARED_ALARM_ON_MS
               ? (uint8_t)ST_SHARED_ALARM_PWM_ON_DUTY
               : 0U;
}
