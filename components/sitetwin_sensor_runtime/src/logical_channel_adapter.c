#include "sitetwin/logical_channel_adapter.h"

#include <string.h>

static void copy_string(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static st_driver_result_t adapter_probe(void *context, st_module_metadata_t *metadata)
{
    st_logical_channel_adapter_t *adapter = (st_logical_channel_adapter_t *)context;
    st_physical_module_metadata_t module_metadata;
    st_driver_result_t result;

    if (adapter == NULL || metadata == NULL || adapter->active == 0U ||
        adapter->module == NULL || adapter->module->probe == NULL) {
        return ST_DRIVER_NOT_PRESENT;
    }

    memset(&module_metadata, 0, sizeof(module_metadata));
    result = adapter->module->probe(adapter->module->context, &module_metadata);
    if (result != ST_DRIVER_READY) {
        return result;
    }
    if (module_metadata.channel_count == 0U ||
        module_metadata.channel_count > ST_MODULE_MAX_CHANNELS ||
        adapter->channel_index >= module_metadata.channel_count) {
        return ST_DRIVER_ERROR;
    }

    memset(metadata, 0, sizeof(*metadata));
    copy_string(metadata->sensor_id, sizeof(metadata->sensor_id),
                module_metadata.channels[adapter->channel_index].sensor_id);
    copy_string(metadata->module_uid, sizeof(metadata->module_uid), module_metadata.module_uid);
    metadata->sensor_kind = module_metadata.channels[adapter->channel_index].sensor_kind;
    metadata->unit = module_metadata.channels[adapter->channel_index].unit;
    metadata->sample_interval_ms = module_metadata.sample_interval_ms;
    return ST_DRIVER_READY;
}

static st_driver_result_t adapter_sample(void *context, uint64_t now_ms, st_driver_sample_t *sample)
{
    st_logical_channel_adapter_t *adapter = (st_logical_channel_adapter_t *)context;
    st_driver_result_t result;

    if (adapter == NULL || sample == NULL || adapter->active == 0U ||
        adapter->module == NULL || adapter->module->acquire == NULL ||
        adapter->module->read_channel == NULL) {
        return ST_DRIVER_NOT_PRESENT;
    }

    result = adapter->module->acquire(adapter->module->context, now_ms);
    if (result != ST_DRIVER_READY) {
        return result;
    }
    return adapter->module->read_channel(adapter->module->context,
                                         adapter->channel_index, sample);
}

int st_logical_channel_adapter_init(st_logical_channel_adapter_t *adapter,
                                    st_physical_module_driver_t *module,
                                    uint8_t channel_index)
{
    if (adapter == NULL || module == NULL || module->probe == NULL ||
        module->acquire == NULL || module->read_channel == NULL ||
        channel_index >= ST_MODULE_MAX_CHANNELS) {
        return -1;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->module = module;
    adapter->channel_index = channel_index;
    return 0;
}

void st_logical_channel_adapter_set_active(st_logical_channel_adapter_t *adapter, int active)
{
    if (adapter != NULL) {
        adapter->active = active != 0 ? 1U : 0U;
    }
}

st_sensor_driver_t st_logical_channel_adapter_driver(st_logical_channel_adapter_t *adapter)
{
    st_sensor_driver_t driver;

    driver.context = adapter;
    driver.probe = adapter_probe;
    driver.sample = adapter_sample;
    return driver;
}
