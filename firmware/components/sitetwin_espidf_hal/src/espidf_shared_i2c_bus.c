#include "sitetwin/espidf_shared_i2c_bus.h"

#include <string.h>

static st_hal_result_t map_esp_error(st_espidf_shared_i2c_bus_t *bus, uint8_t address,
                                     esp_err_t error)
{
    if (error == ESP_ERR_INVALID_STATE && bus != NULL && bus->master_bus != NULL &&
        bus->master_bus->handle != NULL) {
        /* ESP-IDF 5.5 reports a synchronous transaction NACK as
         * ESP_ERR_INVALID_STATE (same quirk as espidf_i2c_bus.c) -- probe
         * only on that error to distinguish an absent device from a
         * different controller-state failure. */
        esp_err_t probe_result =
            i2c_master_probe(bus->master_bus->handle, address, (int)bus->timeout_ms);

        if (probe_result == ESP_ERR_NOT_FOUND) {
            return ST_HAL_NOT_PRESENT;
        }
        if (probe_result == ESP_ERR_TIMEOUT) {
            return ST_HAL_TIMEOUT;
        }
        return ST_HAL_IO_ERROR;
    }

    switch (error) {
    case ESP_OK:
        return ST_HAL_OK;
    case ESP_ERR_TIMEOUT:
        return ST_HAL_TIMEOUT;
    case ESP_ERR_NOT_FOUND:
        return ST_HAL_NOT_PRESENT;
    default:
        return ST_HAL_IO_ERROR;
    }
}

esp_err_t st_espidf_shared_i2c_bus_init(st_espidf_shared_i2c_bus_t *bus,
                                        st_espidf_i2c_master_bus_t *master_bus,
                                        uint32_t clock_hz, uint32_t timeout_ms)
{
    if (bus == NULL || master_bus == NULL || master_bus->handle == NULL ||
        clock_hz == 0U || timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(bus, 0, sizeof(*bus));
    bus->master_bus = master_bus;
    bus->clock_hz = clock_hz;
    bus->timeout_ms = timeout_ms;
    return ESP_OK;
}

void st_espidf_shared_i2c_bus_deinit(st_espidf_shared_i2c_bus_t *bus)
{
    size_t i;

    if (bus == NULL) {
        return;
    }
    for (i = 0U; i < ST_ESPIDF_SHARED_I2C_MAX_DEVICES; ++i) {
        if (bus->devices[i].in_use != 0U && bus->devices[i].handle != NULL) {
            (void)i2c_master_bus_rm_device(bus->devices[i].handle);
        }
    }
    /* The master bus itself is not owned here -- the caller initialised
     * it and is responsible for st_espidf_i2c_master_bus_deinit(). */
    memset(bus, 0, sizeof(*bus));
}

static i2c_master_dev_handle_t lookup_or_add_device(st_espidf_shared_i2c_bus_t *bus,
                                                     uint8_t address)
{
    size_t i;
    size_t free_slot = ST_ESPIDF_SHARED_I2C_MAX_DEVICES;
    i2c_device_config_t device_config;
    esp_err_t result;

    for (i = 0U; i < ST_ESPIDF_SHARED_I2C_MAX_DEVICES; ++i) {
        if (bus->devices[i].in_use != 0U && bus->devices[i].address == address) {
            return bus->devices[i].handle;
        }
        if (bus->devices[i].in_use == 0U && free_slot == ST_ESPIDF_SHARED_I2C_MAX_DEVICES) {
            free_slot = i;
        }
    }
    if (free_slot == ST_ESPIDF_SHARED_I2C_MAX_DEVICES) {
        return NULL; /* every cache slot already holds a different address */
    }

    memset(&device_config, 0, sizeof(device_config));
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = bus->clock_hz;
    result = i2c_master_bus_add_device(bus->master_bus->handle, &device_config,
                                       &bus->devices[free_slot].handle);
    if (result != ESP_OK) {
        return NULL;
    }
    bus->devices[free_slot].address = address;
    bus->devices[free_slot].in_use = 1U;
    return bus->devices[free_slot].handle;
}

static st_hal_result_t shared_i2c_write(void *context, uint8_t address, const uint8_t *data,
                                        size_t length)
{
    st_espidf_shared_i2c_bus_t *bus = (st_espidf_shared_i2c_bus_t *)context;
    i2c_master_dev_handle_t device_handle;

    if (bus == NULL || data == NULL || length == 0U) {
        return ST_HAL_IO_ERROR;
    }
    device_handle = lookup_or_add_device(bus, address);
    if (device_handle == NULL) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(bus, address,
                         i2c_master_transmit(device_handle, data, length, (int)bus->timeout_ms));
}

static st_hal_result_t shared_i2c_read(void *context, uint8_t address, uint8_t *data,
                                       size_t length)
{
    st_espidf_shared_i2c_bus_t *bus = (st_espidf_shared_i2c_bus_t *)context;
    i2c_master_dev_handle_t device_handle;

    if (bus == NULL || data == NULL || length == 0U) {
        return ST_HAL_IO_ERROR;
    }
    device_handle = lookup_or_add_device(bus, address);
    if (device_handle == NULL) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(bus, address,
                         i2c_master_receive(device_handle, data, length, (int)bus->timeout_ms));
}

st_i2c_bus_t st_espidf_shared_i2c_bus(st_espidf_shared_i2c_bus_t *bus)
{
    st_i2c_bus_t i2c_bus;

    i2c_bus.context = bus;
    i2c_bus.write = shared_i2c_write;
    i2c_bus.read = shared_i2c_read;
    return i2c_bus;
}

st_hal_result_t st_espidf_shared_i2c_probe(void *context, uint8_t address)
{
    st_espidf_shared_i2c_bus_t *bus = (st_espidf_shared_i2c_bus_t *)context;
    esp_err_t result;

    if (bus == NULL || bus->master_bus == NULL || bus->master_bus->handle == NULL) {
        return ST_HAL_IO_ERROR;
    }
    result = i2c_master_probe(bus->master_bus->handle, address, (int)bus->timeout_ms);
    if (result == ESP_ERR_NOT_FOUND) {
        return ST_HAL_NOT_PRESENT;
    }
    if (result == ESP_ERR_TIMEOUT) {
        return ST_HAL_TIMEOUT;
    }
    if (result != ESP_OK) {
        return ST_HAL_IO_ERROR;
    }
    return ST_HAL_OK;
}
