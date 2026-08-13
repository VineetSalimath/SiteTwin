#ifndef SITETWIN_LOCAL_OUTPUT_H
#define SITETWIN_LOCAL_OUTPUT_H

#include <stdint.h>

typedef enum {
    ST_LOCAL_OUTPUT_SHARED_ALARM_INDICATOR = 0
} st_local_output_kind_t;

/*
 * The final board exposes one shared buzzer/LED low-side branch on GPIO19;
 * LED and buzzer cannot be controlled independently. This service remains a
 * dormant contract in I1: no ESP32-C6 implementation is composed until active
 * polarity, permitted PWM, and load limits have been electrically validated.
 */

typedef struct {
    st_local_output_kind_t kind;
    uint8_t active;
    uint32_t frequency_hz;
    uint32_t duration_ms;
} st_local_output_request_t;

typedef struct {
    void *context;
    int (*submit)(void *context, const st_local_output_request_t *request);
} st_local_output_service_t;

#endif
