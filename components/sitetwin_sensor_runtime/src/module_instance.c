#include "sitetwin/module_instance.h"

#include <string.h>

int st_module_instance_init(st_module_instance_t *instance,
                            st_physical_module_driver_t driver,
                            uint8_t channel_count)
{
    uint8_t channel_index;

    if (instance == NULL || driver.probe == NULL || driver.acquire == NULL ||
        driver.read_channel == NULL || channel_count == 0U ||
        channel_count > ST_MODULE_MAX_CHANNELS) {
        return -1;
    }

    memset(instance, 0, sizeof(*instance));
    instance->driver = driver;
    instance->channel_count = channel_count;
    for (channel_index = 0U; channel_index < channel_count; ++channel_index) {
        if (st_logical_channel_adapter_init(&instance->adapters[channel_index],
                                            &instance->driver, channel_index) != 0) {
            memset(instance, 0, sizeof(*instance));
            return -1;
        }
    }
    return 0;
}

int st_module_instance_attach(st_module_instance_t *instance,
                              st_sensor_registry_t *registry,
                              const uint8_t *registry_slots,
                              size_t slot_count)
{
    size_t channel_index;

    if (instance == NULL || registry == NULL || registry_slots == NULL ||
        instance->attached != 0U || slot_count != instance->channel_count) {
        return -1;
    }

    for (channel_index = 0U; channel_index < slot_count; ++channel_index) {
        size_t earlier_index;

        if (registry_slots[channel_index] >= ST_MAX_SENSOR_PORTS) {
            return -1;
        }
        if (registry->ports[registry_slots[channel_index]].attached != 0U) {
            return -1;
        }
        for (earlier_index = 0U; earlier_index < channel_index; ++earlier_index) {
            if (registry_slots[earlier_index] == registry_slots[channel_index]) {
                return -1;
            }
        }
    }

    for (channel_index = 0U; channel_index < slot_count; ++channel_index) {
        st_logical_channel_adapter_set_active(&instance->adapters[channel_index], 1);
        if (st_sensor_registry_attach(
                registry, registry_slots[channel_index],
                st_logical_channel_adapter_driver(&instance->adapters[channel_index])) != 0) {
            size_t rollback_index;

            st_logical_channel_adapter_set_active(&instance->adapters[channel_index], 0);
            for (rollback_index = 0U; rollback_index < channel_index; ++rollback_index) {
                st_logical_channel_adapter_set_active(&instance->adapters[rollback_index], 0);
                st_sensor_registry_detach(registry, registry_slots[rollback_index], 0U);
            }
            return -1;
        }
        instance->registry_slots[channel_index] = registry_slots[channel_index];
    }

    instance->attached = 1U;
    return 0;
}

void st_module_instance_detach(st_module_instance_t *instance,
                               st_sensor_registry_t *registry,
                               uint64_t now_ms)
{
    uint8_t channel_index;

    if (instance == NULL || registry == NULL || instance->attached == 0U) {
        return;
    }

    for (channel_index = 0U; channel_index < instance->channel_count; ++channel_index) {
        st_logical_channel_adapter_set_active(&instance->adapters[channel_index], 0);
    }
    if (instance->driver.reset != NULL) {
        instance->driver.reset(instance->driver.context);
    }
    for (channel_index = 0U; channel_index < instance->channel_count; ++channel_index) {
        st_sensor_registry_detach(registry, instance->registry_slots[channel_index], now_ms);
        instance->registry_slots[channel_index] = 0U;
    }
    instance->attached = 0U;
}
