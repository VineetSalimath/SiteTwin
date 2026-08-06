#ifndef SITETWIN_LOGICAL_CHANNEL_ADAPTER_H
#define SITETWIN_LOGICAL_CHANNEL_ADAPTER_H

#include <stdint.h>

#include "sitetwin/physical_module.h"

typedef struct {
    st_physical_module_driver_t *module;
    uint8_t channel_index;
    uint8_t active;
} st_logical_channel_adapter_t;

int st_logical_channel_adapter_init(st_logical_channel_adapter_t *adapter,
                                    st_physical_module_driver_t *module,
                                    uint8_t channel_index);
void st_logical_channel_adapter_set_active(st_logical_channel_adapter_t *adapter, int active);
st_sensor_driver_t st_logical_channel_adapter_driver(st_logical_channel_adapter_t *adapter);

#endif
