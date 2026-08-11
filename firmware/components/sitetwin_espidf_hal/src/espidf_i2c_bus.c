#include "sitetwin/espidf_i2c_bus.h"

#include <string.h>

static st_hal_result_t map_esp_error(st_espidf_i2c_device_t *device, esp_err_t error)
{
    if (error == ESP_ERR_INVALID_STATE && device != NULL && device->master_bus != NULL &&
        device->master_bus->handle != NULL) {
        /*
         * ESP-IDF 5.5 reports a synchronous transaction NACK as
         * ESP_ERR_INVALID_STATE. Probe only on that error to distinguish an
         * absent device from a different controller-state failure.
         */
        esp_err_t probe_result = i2c_master_probe(device->master_bus->handle,
                                                  device->config.address,
                                                  (int)device->config.timeout_ms);

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

static st_hal_result_t espidf_i2c_write(void *context, uint8_t address,
                                        const uint8_t *data, size_t length)
{
    st_espidf_i2c_device_t *device = (st_espidf_i2c_device_t *)context;

    if (device == NULL || device->device_handle == NULL || data == NULL || length == 0U ||
        address != device->config.address) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(device,
                         i2c_master_transmit(device->device_handle, data, length,
                                             (int)device->config.timeout_ms));
}

static st_hal_result_t espidf_i2c_read(void *context, uint8_t address,
                                       uint8_t *data, size_t length)
{
    st_espidf_i2c_device_t *device = (st_espidf_i2c_device_t *)context;

    if (device == NULL || device->device_handle == NULL || data == NULL || length == 0U ||
        address != device->config.address) {
        return ST_HAL_IO_ERROR;
    }
    return map_esp_error(device,
                         i2c_master_receive(device->device_handle, data, length,
                                            (int)device->config.timeout_ms));
}

esp_err_t st_espidf_i2c_master_bus_init(
    st_espidf_i2c_master_bus_t *bus,
    const st_espidf_i2c_master_bus_config_t *config)
{
    i2c_master_bus_config_t bus_config;
    esp_err_t result;

    if (bus == NULL || config == NULL || config->controller < 0 ||
        config->sda_gpio < 0 || config->scl_gpio < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(bus, 0, sizeof(*bus));
    bus->config = *config;
    memset(&bus_config, 0, sizeof(bus_config));
    bus_config.i2c_port = config->controller;
    bus_config.sda_io_num = (gpio_num_t)config->sda_gpio;
    bus_config.scl_io_num = (gpio_num_t)config->scl_gpio;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7U;
    bus_config.flags.enable_internal_pullup = config->enable_internal_pullups ? 1U : 0U;
    result = i2c_new_master_bus(&bus_config, &bus->handle);
    if (result != ESP_OK) {
        memset(bus, 0, sizeof(*bus));
    }
    return result;
}

void st_espidf_i2c_master_bus_deinit(st_espidf_i2c_master_bus_t *bus)
{
    if (bus == NULL) {
        return;
    }
    if (bus->handle != NULL) {
        (void)i2c_del_master_bus(bus->handle);
    }
    memset(bus, 0, sizeof(*bus));
}

static esp_err_t add_device(st_espidf_i2c_device_t *device,
                            const st_espidf_i2c_target_config_t *config)
{
    i2c_device_config_t device_config;
    esp_err_t result;

    memset(&device_config, 0, sizeof(device_config));
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = config->address;
    device_config.scl_speed_hz = config->clock_hz;
    result = i2c_master_bus_add_device(device->master_bus->handle, &device_config,
                                       &device->device_handle);
    return result;
}

esp_err_t st_espidf_i2c_device_init_on_bus(
    st_espidf_i2c_device_t *device,
    st_espidf_i2c_master_bus_t *bus,
    const st_espidf_i2c_target_config_t *config)
{
    esp_err_t result;

    if (device == NULL || bus == NULL || bus->handle == NULL || config == NULL ||
        config->address > 0x7FU || config->clock_hz == 0U || config->timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(device, 0, sizeof(*device));
    device->master_bus = bus;
    device->config.address = config->address;
    device->config.clock_hz = config->clock_hz;
    device->config.timeout_ms = config->timeout_ms;
    result = add_device(device, config);
    if (result != ESP_OK) {
        memset(device, 0, sizeof(*device));
    }
    return result;
}

esp_err_t st_espidf_i2c_device_init(st_espidf_i2c_device_t *device,
                                    const st_espidf_i2c_device_config_t *config)
{
    st_espidf_i2c_master_bus_config_t bus_config;
    st_espidf_i2c_target_config_t target_config;
    esp_err_t result;

    if (device == NULL || config == NULL || config->address > 0x7FU ||
        config->clock_hz == 0U || config->timeout_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(device, 0, sizeof(*device));
    bus_config.controller = config->controller;
    bus_config.sda_gpio = config->sda_gpio;
    bus_config.scl_gpio = config->scl_gpio;
    bus_config.enable_internal_pullups = config->enable_internal_pullups;
    result = st_espidf_i2c_master_bus_init(&device->owned_bus, &bus_config);
    if (result != ESP_OK) {
        return result;
    }

    device->config = *config;
    device->master_bus = &device->owned_bus;
    device->owns_master_bus = 1U;
    target_config.address = config->address;
    target_config.clock_hz = config->clock_hz;
    target_config.timeout_ms = config->timeout_ms;
    result = add_device(device, &target_config);
    if (result != ESP_OK) {
        st_espidf_i2c_master_bus_deinit(&device->owned_bus);
        memset(device, 0, sizeof(*device));
    }
    return result;
}

void st_espidf_i2c_device_deinit(st_espidf_i2c_device_t *device)
{
    if (device == NULL) {
        return;
    }
    if (device->device_handle != NULL) {
        (void)i2c_master_bus_rm_device(device->device_handle);
    }
    if (device->owns_master_bus != 0U) {
        st_espidf_i2c_master_bus_deinit(&device->owned_bus);
    }
    memset(device, 0, sizeof(*device));
}

st_i2c_bus_t st_espidf_i2c_bus(st_espidf_i2c_device_t *device)
{
    st_i2c_bus_t bus;

    bus.context = device;
    bus.write = espidf_i2c_write;
    bus.read = espidf_i2c_read;
    return bus;
}
