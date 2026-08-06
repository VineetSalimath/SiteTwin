#ifndef SITETWIN_LOCAL_OUTPUT_H
#define SITETWIN_LOCAL_OUTPUT_H

#include <stdint.h>

typedef enum {
    ST_LOCAL_OUTPUT_LED = 0,
    ST_LOCAL_OUTPUT_BUZZER
} st_local_output_kind_t;

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
