#include "sitetwin/espidf_onewire_bus.h"

#include <string.h>

static st_hal_result_t map_esp_error(esp_err_t error)
{
    switch (error) {
    case ESP_OK:
        return ST_HAL_OK;
    case ESP_ERR_NOT_FOUND:
        return ST_HAL_NOT_PRESENT;
    case ESP_ERR_TIMEOUT:
        return ST_HAL_TIMEOUT;
    case ESP_ERR_INVALID_STATE:
        return ST_HAL_BUSY;
    default:
        return ST_HAL_IO_ERROR;
    }
}

static st_hal_result_t espidf_onewire_reset(void *context)
{
    st_espidf_onewire_bus_t *bus = (st_espidf_onewire_bus_t *)context;

    if (bus == NULL || bus->handle == NULL) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(onewire_bus_reset(bus->handle));
}

static st_hal_result_t espidf_onewire_write(void *context,
                                            const uint8_t *data, size_t length)
{
    st_espidf_onewire_bus_t *bus = (st_espidf_onewire_bus_t *)context;

    if (bus == NULL || bus->handle == NULL || data == NULL ||
        length == 0U || length > UINT8_MAX) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(onewire_bus_write_bytes(bus->handle, data, (uint8_t)length));
}

static st_hal_result_t espidf_onewire_read(void *context,
                                           uint8_t *data, size_t length)
{
    st_espidf_onewire_bus_t *bus = (st_espidf_onewire_bus_t *)context;

    if (bus == NULL || bus->handle == NULL || data == NULL || length == 0U) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(onewire_bus_read_bytes(bus->handle, data, length));
}

esp_err_t st_espidf_onewire_bus_init(
    st_espidf_onewire_bus_t *bus,
    const st_espidf_onewire_bus_config_t *config)
{
    onewire_bus_config_t bus_config;
    onewire_bus_rmt_config_t rmt_config;
    esp_err_t result;

    if (bus == NULL || config == NULL || config->gpio < 0 ||
        config->max_rx_bytes == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(bus, 0, sizeof(*bus));
    memset(&bus_config, 0, sizeof(bus_config));
    memset(&rmt_config, 0, sizeof(rmt_config));
    bus_config.bus_gpio_num = config->gpio;
    bus_config.flags.en_pull_up = config->enable_internal_pullup ? 1U : 0U;
    rmt_config.max_rx_bytes = config->max_rx_bytes;
    result = onewire_new_bus_rmt(&bus_config, &rmt_config, &bus->handle);
    if (result != ESP_OK) {
        memset(bus, 0, sizeof(*bus));
        return result;
    }
    bus->config = *config;
    return ESP_OK;
}

void st_espidf_onewire_bus_deinit(st_espidf_onewire_bus_t *bus)
{
    if (bus == NULL) {
        return;
    }
    if (bus->handle != NULL) {
        (void)onewire_bus_del(bus->handle);
    }
    memset(bus, 0, sizeof(*bus));
}

st_onewire_bus_t st_espidf_onewire_bus(st_espidf_onewire_bus_t *bus)
{
    st_onewire_bus_t interface;

    interface.context = bus;
    interface.reset = espidf_onewire_reset;
    interface.write = espidf_onewire_write;
    interface.read = espidf_onewire_read;
    return interface;
}
