#ifndef SITETWIN_LOCAL_OUTPUT_H
#define SITETWIN_LOCAL_OUTPUT_H

#include <stdint.h>

/*
 * The real (breadboard) hardware currently in use wires the alert LED and
 * passive buzzer to two independently controllable GPIOs (see
 * sitetwin_espidf_actuation), matching the command protocol's own
 * ST_COMMAND_TARGET_LED / ST_COMMAND_TARGET_BUZZER distinction in
 * command.h. A prior draft of this header assumed a single shared
 * low-side branch on a future PCB revision that is not the board this
 * firmware currently targets; that assumption has been corrected here to
 * match both the real hardware and the existing command protocol.
 */

typedef enum {
    ST_LOCAL_OUTPUT_LED = 0,
    ST_LOCAL_OUTPUT_BUZZER = 1
} st_local_output_kind_t;

typedef struct {
    st_local_output_kind_t kind;
    uint8_t active;
    uint32_t frequency_hz;  // buzzer only; ignored for LED
    uint32_t duration_ms;   // 0 = hold until next submit(); caller is
                             // responsible for timing a follow-up
                             // submit() to turn it back off -- this
                             // service does not run its own timer.
} st_local_output_request_t;

typedef struct {
    void *context;
    int (*submit)(void *context, const st_local_output_request_t *request);
} st_local_output_service_t;

#endif