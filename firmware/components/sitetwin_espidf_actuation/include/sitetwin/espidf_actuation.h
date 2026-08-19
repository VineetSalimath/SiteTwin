#ifndef SITETWIN_ESP_IDF_ACTUATION_H
#define SITETWIN_ESP_IDF_ACTUATION_H

#include "esp_err.h"
#include "sitetwin/local_output.h"

/*
 * Low-level ESP-IDF driver for the alert LED (plain GPIO) and passive
 * buzzer (LEDC/PWM), on the real breadboard hardware -- two independently
 * controllable pins, not the single shared branch a prior local_output.h
 * draft assumed for a future PCB revision. This is the physical layer
 * only; it holds no opinion about WHEN the LED/buzzer should be on --
 * that decision is made elsewhere (see the alarm-output glue) and
 * expressed here purely as submit() calls.
 */

typedef struct {
    int led_gpio;
    int buzzer_gpio;
    uint32_t buzzer_frequency_hz;
    uint8_t initialised;
    uint8_t led_active;
    uint8_t buzzer_active;
} st_espidf_local_output_t;

esp_err_t st_espidf_local_output_init(st_espidf_local_output_t *driver, int led_gpio,
                                       int buzzer_gpio, uint32_t default_buzzer_frequency_hz);

/* Wraps `driver` as a generic st_local_output_service_t so callers that only
 * know about the local_output.h contract (not this driver's concrete type)
 * can submit() requests without depending on ESP-IDF headers directly. */
st_local_output_service_t st_espidf_local_output_service(st_espidf_local_output_t *driver);

#endif