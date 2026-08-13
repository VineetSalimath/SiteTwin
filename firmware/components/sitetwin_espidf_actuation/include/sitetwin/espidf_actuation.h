#ifndef SITETWIN_ESP_IDF_ACTUATION_H
#define SITETWIN_ESP_IDF_ACTUATION_H

#include "esp_err.h"
#include "sitetwin/command.h"

typedef struct {
    int led_gpio;
    int buzzer_gpio;
    uint32_t buzzer_frequency_hz;
    uint8_t initialised;
    st_local_actuation_state_t applied;
} st_espidf_actuation_t;

esp_err_t st_espidf_actuation_init(st_espidf_actuation_t *service, int led_gpio,
                                  int buzzer_gpio, uint32_t buzzer_frequency_hz);
esp_err_t st_espidf_actuation_apply(st_espidf_actuation_t *service,
                                   st_local_actuation_state_t requested);
st_command_persistence_t st_espidf_command_persistence(void);

#endif
