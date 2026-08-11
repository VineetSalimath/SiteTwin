#include "sitetwin/pir.h"

#include <string.h>

int st_pir_init(st_pir_t *sensor, const st_pir_config_t *config,
                uint64_t started_at_ms)
{
    if (sensor == NULL || config == NULL || config->sensor_id == NULL ||
        config->sensor_id[0] == '\0' || config->active_level > 1U) {
        return -1;
    }
    memset(sensor, 0, sizeof(*sensor));
    sensor->config = *config;
    sensor->started_at_ms = started_at_ms;
    return 0;
}

int st_pir_process_level(st_pir_t *sensor, uint64_t now_ms, uint8_t raw_level,
                         st_pir_event_t *event)
{
    uint8_t active;

    if (sensor == NULL || raw_level > 1U || now_ms < sensor->started_at_ms) {
        return 0;
    }
    active = raw_level == sensor->config.active_level ? 1U : 0U;

    if (sensor->seeded == 0U) {
        sensor->seeded = 1U;
        sensor->last_active = active;
    }
    if (now_ms - sensor->started_at_ms < sensor->config.stabilization_ms) {
        sensor->last_active = active;
        return 0;
    }
    if (sensor->stabilized == 0U) {
        /* Establish a post-warm-up baseline. Warm-up activity is never
         * emitted as real motion, even if the output was high throughout. */
        sensor->stabilized = 1U;
        sensor->last_active = active;
        return 0;
    }
    if (active == 0U) {
        sensor->last_active = 0U;
        return 0;
    }
    if (sensor->last_active != 0U) {
        return 0;
    }
    sensor->last_active = 1U;
    if (sensor->event_count != 0U &&
        now_ms - sensor->last_event_at_ms < sensor->config.retrigger_suppression_ms) {
        return 0;
    }

    ++sensor->event_count;
    sensor->last_event_at_ms = now_ms;
    if (event != NULL) {
        event->detected_at_ms = now_ms;
        event->event_count = sensor->event_count;
    }
    return 1;
}

uint8_t st_pir_is_stabilized(const st_pir_t *sensor)
{
    return sensor == NULL ? 0U : sensor->stabilized;
}

uint32_t st_pir_event_count(const st_pir_t *sensor)
{
    return sensor == NULL ? 0U : sensor->event_count;
}

uint64_t st_pir_last_event_at_ms(const st_pir_t *sensor)
{
    return sensor == NULL ? 0U : sensor->last_event_at_ms;
}
