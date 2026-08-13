#ifndef SITETWIN_PIR_H
#define SITETWIN_PIR_H

#include <stdint.h>

#include "sitetwin/contracts.h"

#define ST_PIR_DEFAULT_STABILIZATION_MS 5000U

typedef struct {
    uint32_t stabilization_ms;
    uint32_t retrigger_suppression_ms;
    uint8_t active_level;
    const char *sensor_id;
} st_pir_config_t;

typedef struct {
    uint64_t detected_at_ms;
    uint32_t event_count;
} st_pir_event_t;

/* Portable task-context state machine for the prototype SR505 PIR. The
 * ESP-IDF GPIO ISR must only wake/notify its owner task; GPIO reads and this
 * function belong in task context. A return value of 1 means one new motion
 * event should be passed to st_pod_runtime_emit_event(). */
typedef struct {
    st_pir_config_t config;
    uint64_t started_at_ms;
    uint64_t last_event_at_ms;
    uint32_t event_count;
    uint8_t last_active;
    uint8_t seeded;
    uint8_t stabilized;
} st_pir_t;

int st_pir_init(st_pir_t *sensor, const st_pir_config_t *config,
                uint64_t started_at_ms);
int st_pir_process_level(st_pir_t *sensor, uint64_t now_ms, uint8_t raw_level,
                         st_pir_event_t *event);
uint8_t st_pir_is_stabilized(const st_pir_t *sensor);
uint32_t st_pir_event_count(const st_pir_t *sensor);
uint64_t st_pir_last_event_at_ms(const st_pir_t *sensor);

#endif
