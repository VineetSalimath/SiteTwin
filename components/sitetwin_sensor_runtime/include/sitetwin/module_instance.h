#ifndef SITETWIN_MODULE_INSTANCE_H
#define SITETWIN_MODULE_INSTANCE_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/logical_channel_adapter.h"
#include "sitetwin/sensor_registry.h"

typedef struct {
    st_physical_module_driver_t driver;
    st_logical_channel_adapter_t adapters[ST_MODULE_MAX_CHANNELS];
    uint8_t registry_slots[ST_MODULE_MAX_CHANNELS];
    uint8_t channel_count;
    uint8_t attached;
} st_module_instance_t;

int st_module_instance_init(st_module_instance_t *instance,
                            st_physical_module_driver_t driver,
                            uint8_t channel_count);
int st_module_instance_attach(st_module_instance_t *instance,
                              st_sensor_registry_t *registry,
                              const uint8_t *registry_slots,
                              size_t slot_count);
void st_module_instance_detach(st_module_instance_t *instance,
                               st_sensor_registry_t *registry,
                               uint64_t now_ms);

#endif
