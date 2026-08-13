#ifndef SITETWIN_ESPIDF_ONEWIRE_BUS_H
#define SITETWIN_ESPIDF_ONEWIRE_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "onewire_bus.h"

#include "sitetwin/sensor_hal.h"

typedef struct {
    int gpio;
    bool enable_internal_pullup;
    uint8_t max_rx_bytes;
} st_espidf_onewire_bus_config_t;

typedef struct {
    st_espidf_onewire_bus_config_t config;
    onewire_bus_handle_t handle;
} st_espidf_onewire_bus_t;

esp_err_t st_espidf_onewire_bus_init(
    st_espidf_onewire_bus_t *bus,
    const st_espidf_onewire_bus_config_t *config);
void st_espidf_onewire_bus_deinit(st_espidf_onewire_bus_t *bus);
st_onewire_bus_t st_espidf_onewire_bus(st_espidf_onewire_bus_t *bus);

#endif
